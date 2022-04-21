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

RWTexture2D<float4> gPointSide;
RWTexture2D<float4> gPointId;
RWTexture2D<float4> gJitterXY;

Buffer<float4> gSamplePoints;

Buffer<uint> gSamplePointSide;

#define isValid(name) (is_valid_##name != 0)
#define JITTER_TIMES 1

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

    uint resolution = 128;
    uint sqrRes = resolution * resolution;

    uint batchNum = 108;  // how many batch(4096)

    // // For Jitter
    uint curBatch = ((frameCount / (sqrRes * 3)) / JITTER_TIMES) % batchNum;
    uint curJitterTime = (frameCount / (sqrRes * 3)) % JITTER_TIMES;

    if(id >= curBatch * 4096 && id < (curBatch + 1) * 4096 && id < 4096 * 108)
    {

        // gPointSide[launchIndex] = float4(gSamplePointSide[id], 0, 0, 1);
        gPointId[launchIndex] = gSamplePoints[id];
        float4 curSampleId = gPointId[launchIndex];
        float3 curBary = float3(curSampleId.z, curSampleId.w, 1 - curSampleId.z - curSampleId.w);
        
        VertexData sampleV = gScene.getVertexData(curSampleId.x, curSampleId.y, curBary);
        uint faceId = (frameCount * 2 / sqrRes) % 6;
        uint pixelU = frameCount % resolution;
        uint pixelV = (frameCount / resolution) % (uint)(resolution * 0.5);


        float3 tangent = normalize(sampleV.tangentW.xyz);
        float3 normal = normalize(sampleV.normalW);
        float3 bitangent = normalize(cross(normal, tangent)) * sampleV.tangentW.w;

        float NdotT = dot(tangent, normal);
        bool nonParallel = abs(NdotT) < 0.9999f;
        bool nonZero = dot(tangent, tangent) > 0.f;
        
        bool valid = sampleV.tangentW.w != 0.f && nonZero && nonParallel;
        if (valid)
        {
            tangent = normalize(tangent - normal * NdotT);
            bitangent = cross(normal, tangent) * sampleV.tangentW.w;
        }
        else
        {
            tangent = perp_stark(normal);
            bitangent = cross(normal, tangent);
        }
        
        RayDesc ray;
        ray.Origin = sampleV.posW;

        float halfRes = resolution * 0.5;
        float halfResOffset = halfRes - 0.5;

        // // For Jitter
        // SampleGenerator sg = SampleGenerator.create(launchIndex, pixelU + pixelV * resolution + faceId * sqrRes / 2 + curJitterTime * sqrRes * 3);
        // float2 jitter_xy = sampleNext2D(sg) - float2(0.5, 0.5);
        // gJitterXY[launchIndex] = float4(jitter_xy.x, jitter_xy.y, 0, 1);

        float2 jitter_xy = float2(0.0);
        gJitterXY[launchIndex] = float4(jitter_xy.x, jitter_xy.y, 0, 1);

        if(faceId == 0)
        {
            // compact +T
            float2 positiveT_uv = (float2(pixelU - halfResOffset, pixelV + 0.5) + jitter_xy) / halfRes;
            ray.Direction = normalize(tangent + positiveT_uv.x * bitangent + positiveT_uv.y * normal);
        }
        else if(faceId == 1)
        {
            // compact -T
            float2 negativeT_uv = (float2(pixelU - halfResOffset, halfResOffset - pixelV) + jitter_xy) / halfRes;
            ray.Direction = normalize(-tangent + negativeT_uv.x * bitangent + negativeT_uv.y * normal);
        }
        else if(faceId == 2)
        {
            // compact +B
            float2 positiveB_uv = (float2(pixelU - halfResOffset, pixelV + 0.5) + jitter_xy) / halfRes;
            ray.Direction = normalize(bitangent + positiveB_uv.x * tangent + positiveB_uv.y * normal);
        }
        else if(faceId == 3)
        {
            // compact -B
            float2 negativeB_uv = (float2(pixelU - halfResOffset, halfResOffset - pixelV) + jitter_xy) / halfRes;
            ray.Direction = normalize(-bitangent + negativeB_uv.x * tangent + negativeB_uv.y * normal);
        }
        else if(faceId == 4)
        {
            // compact +N
            float2 N_uv = (float2(halfResOffset - pixelU, pixelV - halfResOffset) + jitter_xy) / halfRes;
            ray.Direction = normalize(normal + N_uv.x * tangent + N_uv.y * bitangent);
        }
        else if(faceId == 5)
        {
            // compact +N
            float2 N_uv = (float2(halfResOffset - pixelU, pixelV + 0.5) + jitter_xy) / halfRes;
            ray.Direction = normalize(normal + N_uv.x * tangent + N_uv.y * bitangent);
        }
    }
