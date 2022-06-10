#include "Utils/Math/MathConstants.slangh"

__exported import Scene.Raytracing;
__exported import Scene.Material.ShadingUtils;
__exported import Rendering.Materials.InteriorListHelpers;
__exported import Rendering.Utils.PixelStats;
__exported import RenderPasses.Shared.PathTracer.PathTracerHelpers;
__exported import RenderPasses.Shared.PathTracer.RayFootprint;
__exported import Utils.Debug.PixelDebug;

static const bool kForceOpaque = !kUseAlphaTest;

struct PathTracerData
{
    PathTracerParams        params;             ///< PathTracer shared parameters.

    // Samplers
    EnvMapSampler           envMapSampler;      ///< Environment map sampler.
    EmissiveLightSampler    emissiveSampler;    ///< Emissive light sampler.
};


struct ShadowRayData
{
    bool visible;
};


struct ScatterRayData
{
    PackedHitInfo packedHitInfo;        ///< Packed HitInfo data, or kInvalidIndex in the first component if ray missed.
};


bool traceShadowRay(float3 origin, float3 dir, float distance, bool valid = true)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.f;
    ray.TMax = valid ? distance : 0.f; // Set tmax = 0.0 for dummy rays.

    ShadowRayData rayData = { false }; // Set to true by miss shader if ray is not terminated before
    uint rayFlags = RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    if (kForceOpaque) rayFlags |= RAY_FLAG_FORCE_OPAQUE;
    TraceRay(gScene.rtAccel, rayFlags, 0xff /* instanceInclusionMask */, kRayTypeShadow /* hitIdx */, rayTypeCount, kRayTypeShadow /* missIdx */, ray, rayData);

    if (!valid) return false;

    logTraceRay(PixelStatsRayType::Visibility);

    return rayData.visible;
}

bool traceScatterRay(float3 origin, float3 dir, inout InteriorList interiorList, out HitInfo hit)
{
    hit = {};

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.f;
    ray.TMax = kRayTMax;

    ScatterRayData rayData = {};
    uint rejectedHits = 0;

    while (true)
    {
        rayData.packedHitInfo.x = 0;

        uint rayFlags = RAY_FLAG_NONE;
        if (kForceOpaque) rayFlags |= RAY_FLAG_FORCE_OPAQUE;
        TraceRay(gScene.rtAccel, rayFlags, 0xff, kRayTypeScatter, rayTypeCount, kRayTypeScatter, ray, rayData);

        logTraceRay(PixelStatsRayType::ClosestHit);

        if (kUseNestedDielectrics && rayData.packedHitInfo.x != 0)
        {
            const TriangleHit triangleHit = TriangleHit(rayData.packedHitInfo);
            uint materialID = gScene.getMaterialID(triangleHit.instanceID);
            uint nestedPriority = gScene.materials.getMaterialHeader(materialID).getNestedPriority();
            if (!interiorList.isTrueIntersection(nestedPriority))
            {
                if (rejectedHits < kMaxRejectedHits)
                {
                    rejectedHits++;
                    VertexData v = gScene.getVertexData(triangleHit);
                    bool frontFacing = dot(-ray.Direction, v.faceNormalW) >= 0.f;
                    interiorList.handleIntersection(materialID, nestedPriority, frontFacing);
                    ray.Origin = computeRayOrigin(v.posW, frontFacing ? -v.faceNormalW : v.faceNormalW);
                    continue;
                }
                else
                {
                    return false;
                }
            }
        }

        break;
    }

    if (rayData.packedHitInfo.x != 0)
    {
        hit = HitInfo(rayData.packedHitInfo);
    }

    return true;
}

static StaticVertexData triangleVertices[3];
static VertexData v;

IBSDF handleHit(const PathTracerData pt, inout ShadingData sd, inout PathData path)
{
    logPathVertex();

    const TriangleHit triangleHit = path.hit.getTriangleHit();
    const uint materialID = gScene.getMaterialID(triangleHit.instanceID);

    v = gScene.getVertexData(triangleHit, triangleVertices);

    path.rayFootprint.hitSurface(v, triangleVertices, triangleHit, path.origin, path.dir);

    let lod = path.rayFootprint.createTextureSampler(v, triangleVertices, triangleHit, path.origin, path.dir);

    sd = gScene.materials.prepareShadingData(v, materialID, -path.dir, lod);

    if (kAdjustShadingNormals) adjustShadingNormal(sd, v);

    if (kUseNestedDielectrics)
    {
        sd.IoR = computeOutsideIoR(path.interiorList, sd.materialID, sd.frontFacing);

        if (!path.interiorList.isEmpty())
        {
            const uint interiorMaterialID = path.interiorList.getTopMaterialID();

            if (gScene.materials.isBasicMaterial(interiorMaterialID))
            {
                BasicMaterialData md = gScene.materials.getBasicMaterialData(interiorMaterialID);

                float3 sigmaA = md.volumeAbsorption;
                float distance = length(path.origin - v.posW);
                path.thp *= exp(-sigmaA * distance);
            }
        }
    }

    let bsdf = gScene.materials.getBSDF(sd, lod);
    let bsdfProperties = bsdf.getProperties(sd);

    if (kDisableCaustics && path.nonSpecularBounces > 0 && bsdfProperties.roughness <= pt.params.specularRoughnessThreshold)
    {
        sd.mtl.setActiveLobes((uint)LobeType::Diffuse);
    }

    const bool isLightSamplable = path.isLightSamplable();

    const bool computeEmissive = kUseEmissiveLights && (kUseLightsInDielectricVolumes || !path.isInsideVolume()) && (!kUseNEE || kUseMIS || !isLightSamplable);
    const float3 emission = bsdfProperties.emission;

    if (computeEmissive && any(emission > 0.f))
    {
        float misWeight = 1.f;
        if (kUseNEE && kUseMIS && isLightSamplable)
        {
            TriangleLightHit hit;
            hit.triangleIndex = gScene.lightCollection.getTriangleIndex(triangleHit.instanceID, triangleHit.primitiveIndex);
            hit.posW = sd.posW;
            hit.normalW = sd.frontFacing ? sd.faceN : -sd.faceN;

            float lightPdf = pt.emissiveSampler.evalPdf(path.origin, path.normal, true, hit) * getEmissiveLightSelectionPdf();

            misWeight = evalMIS(pt.params, 1, path.pdf, kLightSamplesPerVertex, lightPdf);
        }

        path.L += path.thp * emission * misWeight;
    }

    path.length++;

    path.origin = sd.computeNewRayOrigin();

    return bsdf;
}

void handleMiss(const PathTracerData pt, inout PathData path)
{
    const bool isLightSamplable = path.isLightSamplable();

    if (kUseEnvLight && (kUseLightsInDielectricVolumes || !path.isInsideVolume()) && (!kUseNEE || kUseMIS || !isLightSamplable))
    {
        logPathVertex();

        float misWeight = 1.f;
        if (kUseNEE && kUseMIS && isLightSamplable)
        {
            float lightPdf = pt.envMapSampler.evalPdf(path.dir) * getEnvLightSelectionPdf();

            misWeight = evalMIS(pt.params, 1, path.pdf, kLightSamplesPerVertex, lightPdf);
        }

        float3 Le = pt.envMapSampler.eval(path.dir);

        path.L += path.thp * Le * misWeight;
    }
}

void evalDirect(const PathTracerData pt, const ShadingData sd, const IBSDF bsdf, inout PathData path)
{
    for (uint i = 0; i < kLightSamplesPerVertex; ++i)
    {
        ShadowRay shadowRay = {};
        bool valid = generateShadowRay(pt.params, pt.envMapSampler, pt.emissiveSampler, sd, bsdf, i, path, path.sg, shadowRay);
        bool visible = traceShadowRay(path.origin, shadowRay.rayParams.xyz, shadowRay.rayParams.w, valid);
        path.L += visible ? shadowRay.Lr : float3(0.f);
    }
}

void tracePath(const PathTracerData pt, ShadingData sd, IBSDF bsdf, inout PathData path)
{
    logPathVertex();

    const uint kIterationCount = kMaxBounces + 1;

    [loop]
    for (uint depth = 0; depth < kIterationCount; depth++)
    {
        let bsdfProperties = bsdf.getProperties(sd);

        if (depth == 0)
        {
            path.L += bsdfProperties.emission;
        }

        bool supportsNEE = (bsdf.getLobes(sd) & (uint)LobeType::NonDeltaReflection) != 0;

        if (kUseNEE && supportsNEE && (kUseLightsInDielectricVolumes || !path.isInsideVolume()))
        {
            evalDirect(pt, sd, bsdf, path);
        }

        if (kUseRussianRoulette)
        {
            float u = sampleNext1D(path.sg);
            if (u < pt.params.probabilityAbsorption) break;
            path.thp /= (1.f - pt.params.probabilityAbsorption);
        }

        const float3 rayDirIn = path.dir;

        if (!generateScatterRay(pt.params, sd, bsdf, path, path.sg)) return;

        if (depth > 0)
        {
            path.rayFootprint.bounceOnSurface(v, triangleVertices, path.hit.getTriangleHit(), sd, v.normalW, rayDirIn, path.dir, bsdfProperties.roughness, path.isSpecular(), path.isTransmission());
        }

        if (path.isTransmission()) path.origin = path.transmissionOrigin;

        const bool isLastVertex = path.length == kMaxBounces || path.nonSpecularBounces > kMaxNonSpecularBounces;

        if (isLastVertex && (kUseNEE && !kUseMIS && path.isLightSamplable())) return;

        if (kDisableCaustics && path.isSpecular() && path.nonSpecularBounces > 0) return;

        if (!traceScatterRay(path.origin, path.dir, path.interiorList, path.hit)) return;

        if (path.hit.isValid())
        {
            bsdf = handleHit(pt, sd, path);
        }
        else
        {
            handleMiss(pt, path);
            break;
        }

        if (isLastVertex) return;
    }
}
