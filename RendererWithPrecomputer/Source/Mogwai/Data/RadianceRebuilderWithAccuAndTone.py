from falcor import *

def render_graph_DefaultRenderGraph():
    g = RenderGraph('DefaultRenderGraph')
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
    VBufferRT = createPass('VBufferRT', {'samplePattern': SamplePattern.Center, 'sampleCount': 16, 'disableAlphaTest': False, 'adjustShadingNormals': True})
    g.addPass(VBufferRT, 'VBufferRT')
    RadianceRebuilder = createPass('RadianceRebuilder', {'mSharedParams': PathTracerParams(samplesPerPixel=1, lightSamplesPerVertex=1, maxNonSpecularBounces=0, maxBounces=0, adjustShadingNormals=0, useVBuffer=1, forceAlphaOne=1, useAlphaTest=1, clampSamples=0, useMIS=1, clampThreshold=10.0, useLightsInDielectricVolumes=0, specularRoughnessThreshold=0.25, useBRDFSampling=1, useNestedDielectrics=1, useNEE=1, misHeuristic=1, misPowerExponent=2.0, probabilityAbsorption=0.20000000298023224, useRussianRoulette=0, useFixedSeed=0, useLegacyBSDF=0, disableCaustics=0, rayFootprintMode=0, rayConeMode=2, rayFootprintUseRoughness=0), 'mSelectedSampleGenerator': 1, 'mSelectedEmissiveSampler': EmissiveLightSamplerType.LightBVH, 'mUniformSamplerOptions': EmissiveUniformSamplerOptions(), 'mLightBVHSamplerOptions': LightBVHSamplerOptions(useBoundingCone=True, buildOptions=LightBVHBuilderOptions(splitHeuristicSelection=SplitHeuristic.BinnedSAOH, maxTriangleCountPerLeaf=10, binCount=16, volumeEpsilon=0.0010000000474974513, useLeafCreationCost=True, createLeavesASAP=True, useLightingCones=True, splitAlongLargest=False, useVolumeOverSA=False, allowRefitting=True, usePreintegration=True), useLightingCone=True, disableNodeFlux=False, useUniformTriangleSampling=True, solidAngleBoundMethod=SolidAngleBoundMethod.Sphere)})
    g.addPass(RadianceRebuilder, 'RadianceRebuilder')
    ToneMapper = createPass('ToneMapper', {'exposureCompensation': 0.0, 'autoExposure': False, 'filmSpeed': 100.0, 'whiteBalance': False, 'whitePoint': 6500.0, 'operator': ToneMapOp.Aces, 'clamp': True, 'whiteMaxLuminance': 1.0, 'whiteScale': 11.199999809265137, 'fNumber': 1.0, 'shutter': 1.0, 'exposureMode': ExposureMode.AperturePriority})
    g.addPass(ToneMapper, 'ToneMapper')
    AccumulatePass = createPass('AccumulatePass', {'enableAccumulation': True, 'autoReset': True, 'precisionMode': AccumulatePrecision.Single, 'subFrameCount': 0})
    g.addPass(AccumulatePass, 'AccumulatePass')
    g.addEdge('VBufferRT.vbuffer', 'RadianceRebuilder.vbuffer')
    g.addEdge('RadianceRebuilder.color', 'AccumulatePass.input')
    g.addEdge('AccumulatePass.output', 'ToneMapper.src')
    g.markOutput('ToneMapper.dst')
    return g

DefaultRenderGraph = render_graph_DefaultRenderGraph()
try: m.addGraph(DefaultRenderGraph)
except NameError: None