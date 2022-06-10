import PathTracer;
import Utils.Timing.GpuTimer;
import RenderPasses.Shared.PathTracer.LoadShadingData;

ParameterBlock<PathTracerData> gData;

RWTexture2D<float4> gOutputColor;
RWTexture2D<float4> gOutputAlbedo;
RWTexture2D<uint> gOutputTime;

#define is_valid(name) (is_valid_##name != 0)


/** ********************* Ray index 0: Scatter ray ************************ */

[shader("miss")]
void scatterMiss(inout ScatterRayData rayData : SV_RayPayload)
{
}

[shader("anyhit")]
void scatterAnyHit(inout ScatterRayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
#if USE_ALPHA_TEST
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(instanceID);
    if (gScene.materials.alphaTest(v, materialID, 0.f)) IgnoreHit();
#endif
}

[shader("closesthit")]
void scatterClosestHit(inout ScatterRayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    rayData.packedHitInfo = triangleHit.pack();
}

/************************** Ray index 1: Shadow ray ************************ */

[shader("miss")]
void shadowMiss(inout ShadowRayData rayData : SV_RayPayload)
{
    rayData.visible = true;
}

[shader("anyhit")]
void shadowAnyHit(inout ShadowRayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
#if USE_ALPHA_TEST
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(instanceID);
    if (gScene.materials.alphaTest(v, materialID, 0.f)) IgnoreHit();
#endif
}


[shader("raygeneration")]
void rayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 frameDim = DispatchRaysDimensions().xy;

    logSetPixel(pixel);
    printSetPixel(pixel);

    GpuTimer timer;
    if (is_valid(gOutputTime)) timer.start();

    float3 outColor = float3(0, 0, 0);
    float3 outAlbedo = float3(0, 0, 0);
    float outAlpha = 0.f;

    HitInfo hit;
    ShadingData sd;
    let lod = createTextureSampler(pixel);

    if (loadShadingData(pixel, frameDim, gScene.camera, lod, sd, hit))
    {
        let bsdf = gScene.materials.getBSDF(sd, lod);
        let bsdfProperties = bsdf.getProperties(sd);

        const float3 rayOrigin = sd.computeNewRayOrigin();

        [loop]
        for (uint sampleIdx = 0; sampleIdx < kSamplesPerPixel; sampleIdx++)
        {
            PathData path = {};
            path.origin = rayOrigin;
            path.thp = float3(1.f);
            path.hit = hit;

            uint frameSeed = gData.params.useFixedSeed ? 0 : gData.params.frameCount;
            path.sg = SampleGenerator(pixel, frameSeed * kSamplesPerPixel + sampleIdx);

            for (uint i = 0; i < gData.params.prngDimension; i++) sampleNext1D(path.sg);

            if (!(kRayFootprintMode == TexLODMode::Mip0))
            {
                path.rayFootprint = RayFootprint::create(hit.getTriangleHit(), pixel, frameDim, rayOrigin, gScene.camera.getPosition(), sd.faceN, sd.N, gData.params.screenSpacePixelSpreadAngle, bsdfProperties.roughness, path.isSpecular());
            }

            tracePath(gData, sd, bsdf, path);

            logPathLength(path.length);

            outColor += gData.params.clampSamples && path.L > gData.params.clampThreshold ? gData.params.clampThreshold : path.L;
        }

        const float invSpp = 1.f / kSamplesPerPixel;
        outColor *= invSpp;
        outAlbedo = bsdfProperties.diffuseReflectionAlbedo + bsdfProperties.specularReflectionAlbedo; // TODO: Include transmission?
        outAlpha = 1.f;
    }
    else
    {
        outColor = evalBackground(-sd.V);
        outAlbedo = outColor.rgb;
        outAlpha = kForceAlphaOne ? 1.f : 0.f;
    }

    assert(!any(isnan(outColor)));

    if (is_valid(gOutputColor)) gOutputColor[pixel] = float4(outColor, outAlpha);
    if (is_valid(gOutputAlbedo)) gOutputAlbedo[pixel] = float4(outAlbedo, 1);
    if (is_valid(gOutputTime)) gOutputTime[pixel] = timer.getElapsed();
}
