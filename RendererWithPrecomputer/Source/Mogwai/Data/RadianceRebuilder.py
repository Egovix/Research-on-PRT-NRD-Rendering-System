from falcor import *

def render_graph_PathTracerGraph():
    g = RenderGraph('PathTracerGraph')
    loadRenderPassLibrary('ErrorMeasurePass.dll')
    loadRenderPassLibrary('Utils.dll')
    loadRenderPassLibrary('CSM.dll')
    loadRenderPassLibrary('BSDFViewer.dll')
    loadRenderPassLibrary('MinimalPathTracer.dll')
    loadRenderPassLibrary('AccumulatePass.dll')
    loadRenderPassLibrary('Antialiasing.dll')
    loadRenderPassLibrary('BlitPass.dll')
    loadRenderPassLibrary('DebugPasses.dll')
    loadRenderPassLibrary('ForwardLightingPass.dll')
    loadRenderPassLibrary('DepthPass.dll')
    loadRenderPassLibrary('SVGFPass.dll')
    loadRenderPassLibrary('GBuffer.dll')
    loadRenderPassLibrary('GetSampleRadiance.dll')
    loadRenderPassLibrary('PixelInspectorPass.dll')
    loadRenderPassLibrary('ImageLoader.dll')
    loadRenderPassLibrary('JudgeInOut.dll')
    loadRenderPassLibrary('MegakernelPathTracer.dll')
    loadRenderPassLibrary('MeshDifferentiation.dll')
    loadRenderPassLibrary('PassLibraryTemplate.dll')
    loadRenderPassLibrary('PathTracerWithSMS.dll')
    loadRenderPassLibrary('PerfaceRadianceToCubemap.dll')
    loadRenderPassLibrary('RadianceRebuilder.dll')
    loadRenderPassLibrary('RadianceToCubemap.dll')
    loadRenderPassLibrary('SceneDebugger.dll')
    loadRenderPassLibrary('SkyBox.dll')
    loadRenderPassLibrary('SSAO.dll')
    loadRenderPassLibrary('TestBlitPass.dll')
    loadRenderPassLibrary('TemporalDelayPass.dll')
    loadRenderPassLibrary('ToneMapper.dll')
    loadRenderPassLibrary('TryClearcoatRadiance.dll')
    loadRenderPassLibrary('WorldPointCloudRadiance.dll')
    loadRenderPassLibrary('TryPRTPass.dll')
    loadRenderPassLibrary('WhittedRayTracer.dll')
    loadRenderPassLibrary('WireframePass.dll')
    loadRenderPassLibrary('WorldPerfaceRadiance.dll')
    ToneMappingPass = createPass('ToneMapper', {'exposureCompensation': 0.0, 'autoExposure': False, 'filmSpeed': 100.0, 'whiteBalance': False, 'whitePoint': 6500.0, 'operator': ToneMapOp.Aces, 'clamp': True, 'whiteMaxLuminance': 1.0, 'whiteScale': 11.199999809265137, 'fNumber': 1.0, 'shutter': 1.0, 'exposureMode': ExposureMode.AperturePriority})
    g.addPass(ToneMappingPass, 'ToneMappingPass')
    GBufferRT = createPass('GBufferRT', {'samplePattern': SamplePattern.Stratified, 'sampleCount': 16, 'disableAlphaTest': False, 'adjustShadingNormals': True, 'forceCullMode': False, 'cull': CullMode.CullBack, 'texLOD': LODMode.UseMip0})
    g.addPass(GBufferRT, 'GBufferRT')
    RadianceRebuilder = createPass('RadianceRebuilder', {'mSharedParams': PathTracerParams(samplesPerPixel=1, lightSamplesPerVertex=1, maxNonSpecularBounces=3, maxBounces=3, adjustShadingNormals=0, useVBuffer=0, forceAlphaOne=1, useAlphaTest=1, clampSamples=0, useMIS=1, clampThreshold=10.0, useLightsInDielectricVolumes=0, specularRoughnessThreshold=0.25, useBRDFSampling=1, useNestedDielectrics=1, useNEE=1, misHeuristic=1, misPowerExponent=2.0, probabilityAbsorption=0.20000000298023224, useRussianRoulette=0, useFixedSeed=0, useLegacyBSDF=0, disableCaustics=0, rayFootprintMode=0, rayConeMode=2, rayFootprintUseRoughness=0), 'mSelectedSampleGenerator': 1, 'mSelectedEmissiveSampler': EmissiveLightSamplerType.LightBVH, 'mUniformSamplerOptions': EmissiveUniformSamplerOptions(), 'mLightBVHSamplerOptions': LightBVHSamplerOptions(useBoundingCone=True, buildOptions=LightBVHBuilderOptions(splitHeuristicSelection=SplitHeuristic.BinnedSAOH, maxTriangleCountPerLeaf=10, binCount=16, volumeEpsilon=0.0010000000474974513, useLeafCreationCost=True, createLeavesASAP=True, useLightingCones=True, splitAlongLargest=False, useVolumeOverSA=False, allowRefitting=True, usePreintegration=True), useLightingCone=True, disableNodeFlux=False, useUniformTriangleSampling=True, solidAngleBoundMethod=SolidAngleBoundMethod.Sphere)})
    g.addPass(RadianceRebuilder, 'RadianceRebuilder')
    g.addEdge('GBufferRT.vbuffer', 'RadianceRebuilder.vbuffer')
    g.addEdge('GBufferRT.posW', 'RadianceRebuilder.posW')
    g.addEdge('GBufferRT.normW', 'RadianceRebuilder.normalW')
    g.addEdge('GBufferRT.tangentW', 'RadianceRebuilder.tangentW')
    g.addEdge('GBufferRT.faceNormalW', 'RadianceRebuilder.faceNormalW')
    g.addEdge('GBufferRT.viewW', 'RadianceRebuilder.viewW')
    g.addEdge('GBufferRT.diffuseOpacity', 'RadianceRebuilder.mtlDiffOpacity')
    g.addEdge('GBufferRT.specRough', 'RadianceRebuilder.mtlSpecRough')
    g.addEdge('GBufferRT.emissive', 'RadianceRebuilder.mtlEmissive')
    g.addEdge('GBufferRT.matlExtra', 'RadianceRebuilder.mtlParams')
    g.addEdge('RadianceRebuilder.color', 'ToneMappingPass.src')
    g.markOutput('ToneMappingPass.dst')
    return g

PathTracerGraph = render_graph_PathTracerGraph()
try: m.addGraph(PathTracerGraph)
except NameError: None
