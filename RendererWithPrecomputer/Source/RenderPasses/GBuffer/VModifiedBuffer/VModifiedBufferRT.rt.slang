import Scene.Scene;
import Scene.Raytracing;
import Scene.HitInfo;
import Utils.Timing.GpuTimer;
import Utils.Sampling.SampleGenerator;
import Utils.Math.MathHelpers;

cbuffer PerFrameCB
{
    uint frameCount;
};

RWTexture2D<PackedHitInfo> gVBuffer;
RWTexture2D<uint> gTime;
RWTexture2D<float4> gPointId;

RWTexture2D<float4> gPointSide;

Buffer<float4> gSamplePoints;

#define isValid(name) (is_valid_##name != 0)

/** ***************************** Ray index 0 ****************************** */

struct RayData
{
    uint dummy; // TODO: Passing in an empty payload struct doesn't work. Declare a dummy variable so that the compiler doesn't remove the declaration.
};

[shader("miss")]
void miss(inout RayData rayData)
{
    // Write invalid hit to output buffer.
    uint2 launchIndex = DispatchRaysIndex().xy;
    gVBuffer[launchIndex] = { HitInfo::kInvalidIndex };
}

[shader("anyhit")]
void anyHit(
    uniform HitShaderParams hitParams,
    inout RayData rayData : SV_RayPayload,
    BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
#if !DISABLE_ALPHA_TEST
    // Alpha test for non-opaque geometry.
    VertexData v = getVertexData(hitParams, PrimitiveIndex(), attribs);
    uint materialID = gScene.getMaterialID(hitParams.getGlobalHitID());
    if (alphaTest(v, gScene.materials[materialID], gScene.materialResources[materialID], 0.f)) IgnoreHit();
#endif
}

[shader("closesthit")]
void closestHit(
    uniform HitShaderParams hitParams,
    inout RayData rayData,
    BuiltInTriangleIntersectionAttributes attribs)
{
    // Store hit information. Note we don't access the materials here.
    HitInfo hit;
    hit.type = InstanceType::TriangleMesh;
    hit.instanceID = hitParams.getGlobalHitID();
    hit.primitiveIndex = PrimitiveIndex();
    hit.barycentrics = attribs.barycentrics;

    // Write hit info to output buffer.
    uint2 launchIndex = DispatchRaysIndex().xy;
    gVBuffer[launchIndex] = hit.encode();
}


/** ******************************** RayGen ******************************** */

[shader("raygeneration")]
void rayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    GpuTimer timer;
    if (isValid(gTime)) timer.start();

    uint id = launchIndex.x * launchDim.y + launchIndex.y;
    
    gPointId[launchIndex] = gSamplePoints[id];
    float4 curSampleId = gPointId[launchIndex];
    float3 curBary = float3(curSampleId.z, curSampleId.w, 1 - curSampleId.z - curSampleId.w);

    VertexData sampleV = gScene.getVertexData(curSampleId.x, curSampleId.y, curBary);

    SampleGenerator sg = SampleGenerator.create(launchIndex, frameCount);

    RayDesc ray;
    // ray.Origin = sampleV.posW;
    // ray.Direction = normalize(sampleV.normalW + sample_sphere(sampleNext2D(sg)));

    ray.Origin = sampleV.posW + 0.01 * sampleV.normalW;
    ray.Direction = normalize(-sampleV.normalW);
    ray.TMin = 0.001;
    ray.TMax = 10000;
    
    // Trace ray
    RayData rayData;
    TraceRay(gRtScene, RAY_FLAG_NONE, 0xff /* instanceInclusionMask */, 0 /* hitIdx */, hitProgramCount, 0 /* missIdx */, ray, rayData);

    // Write time.
    if (isValid(gTime)) gTime[launchIndex] = timer.getElapsed();
}
