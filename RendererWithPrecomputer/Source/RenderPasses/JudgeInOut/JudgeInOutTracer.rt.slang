import JudgeInOutTracer;
import Utils.Timing.GpuTimer;
import RenderPasses.Shared.PathTracer.LoadShadingData;

ParameterBlock<PathTracerData> gData;

// Outputs (optional)
RWTexture2D<float4> gOutputColor;
RWTexture2D<float4> gOutputAlbedo;
RWTexture2D<uint> gOutputTime;

RWBuffer<uint> gIfOutsideCar;
RWBuffer<float4> gCloudWorldPos;
RWBuffer<float4> gCloudWorldNormal;

// Static configuration based on which buffers are bound.
#define isValid(name) (is_valid_##name != 0)
#define POINT_MAX_SIZE 12288

/** ********************* Ray index 0: Scatter ray ************************ */

[shader("miss")]
void scatterMiss(inout ScatterRayData rayData : SV_RayPayload)
{
}

[shader("anyhit")]
void scatterAnyHit(
    uniform HitShaderParams hitParams,
    inout ScatterRayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
#if USE_ALPHA_TEST
    // Alpha test for non-opaque geometry.
    VertexData v = getVertexData(hitParams, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(hitParams.getGlobalHitID());
    if (alphaTest(v, gScene.materials[materialID], gScene.materialResources[materialID], 0.f)) IgnoreHit();
#endif
}

[shader("closesthit")]
void scatterClosestHit(
    uniform HitShaderParams hitParams,
    inout ScatterRayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    // Store hit information. Note we don't access the materials here.
    HitInfo hit;
    hit.type = InstanceType::TriangleMesh;
    hit.instanceID = hitParams.getGlobalHitID();
    hit.primitiveIndex = PrimitiveIndex();
    hit.barycentrics = attribs.barycentrics;
    rayData.packedHitInfo = hit.encode();
}

/************************** Ray index 1: Shadow ray ************************ */

[shader("miss")]
void shadowMiss(inout ShadowRayData rayData : SV_RayPayload)
{
    // The miss shader is executed if the ray misses all geometry. Mark as visible.
    rayData.visible = true;
}

[shader("anyhit")]
void shadowAnyHit(
    uniform HitShaderParams hitParams,
    inout ShadowRayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
#if USE_ALPHA_TEST
    // Alpha test for non-opaque geometry.
    VertexData v = getVertexData(hitParams, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(hitParams.getGlobalHitID());
    if (alphaTest(v, gScene.materials[materialID], gScene.materialResources[materialID], 0.f)) IgnoreHit();
#endif
}

/** ******************************** RayGen ******************************** */

/** This is the entry point for the path tracer.

    We generate N paths (= #spp) per pixel, which are traced into the scene.
    The path tracer is written as a for-loop over path segments, where each
    iteration traces a shadow ray for direct illumination and a scatter ray.

    The hit shader for the scatter ray currently generates ray parameters for
    the shadow ray to evaluate direct illumination and generates ray parameters
    for the next scatter ray, which are both returned the raygen shader to be
    traced. This is more efficient than tracing from the hit shader. The max
    recusion depth = 1.
*/
[shader("raygeneration")]
void rayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    logSetPixel(launchIndex);
    printSetPixel(launchIndex);

    GpuTimer timer;
    if (isValid(gOutputTime)) timer.start();

    
    uint curPixelId = launchIndex.x * launchDim.y + launchIndex.y;

    uint batchNum = 1;
    uint curBatch = gData.params.frameCount % batchNum;

    HitInfo hit;
    float3 worldPos;
    float3 worldNormal;

    // load data for ray tracing                                               
    if(curPixelId >= curBatch * 16384 && curPixelId < (curBatch + 1) * 16384 && curPixelId < POINT_MAX_SIZE)
    {
        uint id = curPixelId % 16384;

        loadShadingData(launchIndex, launchDim, worldPos, worldNormal, hit);

        gCloudWorldPos[id] = float4(worldPos, 1.0);
        gCloudWorldNormal[id] = float4(worldNormal, 1.0);

    }

    // Write time.
    if (isValid(gOutputTime)) gOutputTime[launchIndex] = timer.getElapsed();
}
