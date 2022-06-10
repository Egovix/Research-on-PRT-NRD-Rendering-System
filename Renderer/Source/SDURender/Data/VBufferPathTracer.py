from falcor import *

def render_graph_VBufferPathTracer():
    g = RenderGraph('VBufferPathTracer')
    loadRenderPassLibrary('AccumulatePass.dll')
    loadRenderPassLibrary('GBuffer.dll')
    loadRenderPassLibrary('MegakernelPathTracer.dll')
    loadRenderPassLibrary('ToneMapper.dll')
    VBufferRT = createPass('VBufferRT', {'outputSize': IOSize.Default, 'samplePattern': SamplePattern.Center, 'sampleCount': 16, 'useAlphaTest': True, 'adjustShadingNormals': True, 'forceCullMode': False, 'cull': CullMode.CullBack, 'useTraceRayInline': False, 'useDOF': True})
    g.addPass(VBufferRT, 'VBufferRT')
    MegakernelPathTracer = createPass('MegakernelPathTracer', {'params': PathTracerParams(samplesPerPixel=1, lightSamplesPerVertex=1, maxBounces=3, maxNonSpecularBounces=3, useVBuffer=1, useAlphaTest=1, adjustShadingNormals=0, forceAlphaOne=1, clampSamples=0, clampThreshold=10.0, specularRoughnessThreshold=0.25, useBRDFSampling=1, useNEE=1, useMIS=1, misHeuristic=1, misPowerExponent=2.0, useRussianRoulette=0, probabilityAbsorption=0.20000000298023224, useFixedSeed=0, useNestedDielectrics=1, useLightsInDielectricVolumes=0, disableCaustics=0, rayFootprintMode=0, rayConeMode=0, rayFootprintUseRoughness=0), 'sampleGenerator': 1, 'emissiveSampler': EmissiveLightSamplerType.LightBVH, 'uniformSamplerOptions': LightBVHSamplerOptions(buildOptions=LightBVHBuilderOptions(splitHeuristicSelection=SplitHeuristic.BinnedSAOH, maxTriangleCountPerLeaf=10, binCount=16, volumeEpsilon=0.0010000000474974513, splitAlongLargest=False, useVolumeOverSA=False, useLeafCreationCost=True, createLeavesASAP=True, allowRefitting=True, usePreintegration=True, useLightingCones=True), useBoundingCone=True, useLightingCone=True, disableNodeFlux=False, useUniformTriangleSampling=True, solidAngleBoundMethod=SolidAngleBoundMethod.Sphere)})
    g.addPass(MegakernelPathTracer, 'MegakernelPathTracer')
    AccumulatePass = createPass('AccumulatePass', {'enabled': True, 'outputSize': IOSize.Default, 'autoReset': True, 'precisionMode': AccumulatePrecision.Single, 'subFrameCount': 0, 'maxAccumulatedFrames': 0})
    g.addPass(AccumulatePass, 'AccumulatePass')
    ToneMapper = createPass('ToneMapper', {'outputSize': IOSize.Default, 'useSceneMetadata': True, 'exposureCompensation': 0.0, 'autoExposure': False, 'filmSpeed': 100.0, 'whiteBalance': False, 'whitePoint': 6500.0, 'operator': ToneMapOp.Aces, 'clamp': True, 'whiteMaxLuminance': 1.0, 'whiteScale': 11.199999809265137, 'fNumber': 1.0, 'shutter': 1.0, 'exposureMode': ExposureMode.AperturePriority})
    g.addPass(ToneMapper, 'ToneMapper')
    g.addEdge('VBufferRT.vbuffer', 'MegakernelPathTracer.vbuffer')
    g.addEdge('MegakernelPathTracer.color', 'AccumulatePass.input')
    g.addEdge('AccumulatePass.output', 'ToneMapper.src')
    g.markOutput('ToneMapper.dst')
    return g

VBufferPathTracer = render_graph_VBufferPathTracer()
try: m.addGraph(VBufferPathTracer)
except NameError: None
