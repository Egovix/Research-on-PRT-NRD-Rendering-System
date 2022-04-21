import RadianceToCubemapTracer;
import Utils.Timing.GpuTimer;
import RenderPasses.Shared.PathTracer.LoadShadingData;

ParameterBlock<PathTracerData> gData;

// Outputs (optional)
RWTexture2D<float4> gOutputColor;
RWTexture2D<float4> gOutputAlbedo;

RWTexture2DArray<float4> gIncomeRadiance_128[4];
RWTexture2DArray<float4> gIncomeRadiance_64[4];
RWTexture2DArray<float4> gIncomeRadiance_32[4];
RWTexture2DArray<float4> gIncomeRadiance_16[4];

RWTexture2D<uint> gOutputTime;

Buffer<uint2> gRes_Id;

// Static configuration based on which buffers are bound.
#define isValid(name) (is_valid_##name != 0)
#define OFFSET 1.0
#define BATCH_SIZE 4
#define JITTER_TIMES 1

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

    uint trueRes = gRes_Id[curPixelId].x;
    uint resolution = 128;
    uint sqrRes = resolution * resolution;
    uint halfRes = resolution * 0.5;

    uint batchNum = 108;

    // For Jitter
    uint curBatch = ((gData.params.frameCount / (sqrRes * 3)) / JITTER_TIMES) % batchNum;

    uint batchSize = 1024;  // one texture array size
    uint curCubemapId = gRes_Id[curPixelId].y % batchSize;
    uint batchId = (gRes_Id[curPixelId].y / batchSize) % BATCH_SIZE;

    if(curPixelId >= curBatch * 4096 && curPixelId < (curBatch + 1) * 4096 && curPixelId < 4096 * 108)
    {
        float3 outColor = float3(0, 0, 0);
        float3 outAlbedo = float3(0, 0, 0);
        float outAlpha = 0.f;

        HitInfo hit;
        ShadingData sd;

        // load data for ray tracing
        if (loadShadingData(launchIndex, launchDim, resolution, gData.params.frameCount, sd, hit))
        {
            // Pixel represents a primary hit. Compute its contribution.

            // Compute ray origin for new rays spawned from the G-buffer.
            const float3 rayOrigin = sd.computeNewRayOrigin();

            // Loop over samples in pixel.
            [loop]
            for (uint sampleIdx = 0; sampleIdx < kSamplesPerPixel; sampleIdx++)
            {
                // Setup path data.
                PathData path = {};
                path.origin = rayOrigin;
                path.thp = float3(1.f);

                // Create sample generator.
                uint frameSeed = gData.params.useFixedSeed ? 0 : gData.params.frameCount;
                path.sg = SampleGenerator.create(launchIndex, frameSeed * kSamplesPerPixel + sampleIdx);

                // Advance the generator to the first available dimension.
                for (uint i = 0; i < gData.params.prngDimension; i++)
                {
                    sampleNext1D(path.sg);
                }

                // Trace the path.
                tracePath(gData, sd, path);
                
                logPathLength(path.length);

                // Accumulate after clamping.
                // Note the comparison is written so that NaNs propagate (unless the compiler rewrites it).
                // TODO: Check the generated code that this is the case.

                outColor += gData.params.clampSamples && path.L > gData.params.clampThreshold ? gData.params.clampThreshold : path.L;                
            }

            // We're done accumulating over all samples.
            const float invSpp = 1.f / kSamplesPerPixel;
            outColor *= invSpp;
            outAlbedo = sd.diffuse + sd.specular;
            outAlpha = 1.f;

        }
        else
        {
            outColor = evalBackground(-sd.V);

            outAlbedo = outColor.rgb;
            outAlpha = kForceAlphaOne ? 1.f : 0.f;
        }

        uint faceId = (gData.params.frameCount * 2 / sqrRes) % 6;
        uint pixelU = gData.params.frameCount % resolution;
        uint pixelV = (gData.params.frameCount / resolution) % uint(resolution * 0.5);

        if(trueRes == 128)
        {
            if(faceId == 0)
            {
                // +T
                gIncomeRadiance_128[batchId][uint3(pixelV + halfRes, pixelU, curCubemapId)] = float4(outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 1)
            {
                // -T
                gIncomeRadiance_128[batchId][uint3(pixelV, pixelU, curCubemapId)] = float4(outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 2)
            {
                // +B
                gIncomeRadiance_128[batchId][uint3(pixelU, pixelV + halfRes * 5, curCubemapId)] = float4(outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 3)
            {
                // -B
                gIncomeRadiance_128[batchId][uint3(pixelU, pixelV + halfRes * 4, curCubemapId)] = float4(outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 4)
            {
                // +N
                gIncomeRadiance_128[batchId][uint3(pixelU, pixelV + halfRes * 2, curCubemapId)] = float4(outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 5)
            {
                gIncomeRadiance_128[batchId][uint3(pixelU, pixelV + halfRes * 3, curCubemapId)] = float4(outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
        }
        else if(trueRes == 64)
        {
            if(faceId == 0)
            {
                // +T
                gIncomeRadiance_64[batchId][uint3(pixelV / 2 + 32, pixelU / 2, curCubemapId)] += float4(0.25 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 1)
            {
                // -T
                gIncomeRadiance_64[batchId][uint3(pixelV / 2, pixelU / 2, curCubemapId)] += float4(0.25 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 2)
            {
                // +B
                gIncomeRadiance_64[batchId][uint3(pixelU / 2, pixelV / 2 + 160, curCubemapId)] += float4(0.25 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 3)
            {
                // -B
                gIncomeRadiance_64[batchId][uint3(pixelU / 2, pixelV / 2 + 128, curCubemapId)] += float4(0.25 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 4)
            {
                // +N
                gIncomeRadiance_64[batchId][uint3(pixelU / 2, pixelV / 2 + 64, curCubemapId)] += float4(0.25 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 5)
            {
                gIncomeRadiance_64[batchId][uint3(pixelU / 2, pixelV / 2 + 96, curCubemapId)] += float4(0.25 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
        }
        else if(trueRes == 32)
        {
            if(faceId == 0)
            {
                // +T
                gIncomeRadiance_32[batchId][uint3(pixelV / 4 + 16, pixelU / 4, curCubemapId)] += float4(0.0625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 1)
            {
                // -T
                gIncomeRadiance_32[batchId][uint3(pixelV / 4, pixelU / 4, curCubemapId)] += float4(0.0625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 2)
            {
                // +B
                gIncomeRadiance_32[batchId][uint3(pixelU / 4, pixelV / 4 + 80, curCubemapId)] += float4(0.0625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 3)
            {
                // -B
                gIncomeRadiance_32[batchId][uint3(pixelU / 4, pixelV / 4 + 64, curCubemapId)] += float4(0.0625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 4)
            {
                // +N
                gIncomeRadiance_32[batchId][uint3(pixelU / 4, pixelV / 4 + 32, curCubemapId)] += float4(0.0625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 5)
            {
                gIncomeRadiance_32[batchId][uint3(pixelU / 4, pixelV / 4 + 48, curCubemapId)] += float4(0.0625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
        }
        else if(trueRes == 16)
        {
            if(faceId == 0)
            {
                // +T
                gIncomeRadiance_16[batchId][uint3(pixelV / 8 + 8, pixelU / 8, curCubemapId)] += float4(0.015625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 1)
            {
                // -T
                gIncomeRadiance_16[batchId][uint3(pixelV / 8, pixelU / 8, curCubemapId)] += float4(0.015625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 2)
            {
                // +B
                gIncomeRadiance_16[batchId][uint3(pixelU / 8, pixelV / 8 + 40, curCubemapId)] += float4(0.015625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 3)
            {
                // -B
                gIncomeRadiance_16[batchId][uint3(pixelU / 8, pixelV / 8 + 32, curCubemapId)] += float4(0.015625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 4)
            {
                // +N
                gIncomeRadiance_16[batchId][uint3(pixelU / 8, pixelV / 8 + 16, curCubemapId)] += float4(0.015625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
            else if(faceId == 5)
            {
                gIncomeRadiance_16[batchId][uint3(pixelU / 8, pixelV / 8 + 24, curCubemapId)] += float4(0.015625 * outColor * sqrRes * CompactTexelCoordSolidAngle(faceId, pixelU, pixelV, resolution), 1);
            }
        }
    }

    // Write time.
    if (isValid(gOutputTime)) gOutputTime[launchIndex] = timer.getElapsed();
}
