from falcor import *

def render_graph_PathTracer():
    g = RenderGraph('BasicPathTracer')
    loadRenderPassLibrary('AccumulatePass.dll')
    loadRenderPassLibrary('BasicPathTracer.dll')
    loadRenderPassLibrary('GBuffer.dll')
    loadRenderPassLibrary('ToneMapper.dll')
    VBufferRT = createPass('VBufferRT', {'samplePattern': SamplePattern.Stratified, 'sampleCount': 16, 'useAlphaTest': True})
    g.addPass(VBufferRT, 'VBufferRT')
    AccumulatePass = createPass('AccumulatePass', {'enabled': True, 'precisionMode': AccumulatePrecision.Single})
    g.addPass(AccumulatePass, 'AccumulatePass')
    ToneMapper = createPass('ToneMapper', {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMapper, 'ToneMapper')
    BasicPathTracer = createPass('BasicPathTracer', {'samplesPerPixel': 1})
    g.addPass(BasicPathTracer, 'BasicPathTracer')
    g.addEdge('AccumulatePass.output', 'ToneMapper.src')
    g.addEdge('VBufferRT.vbuffer', 'BasicPathTracer.vbuffer')
    g.addEdge('BasicPathTracer.color', 'AccumulatePass.input')
    g.markOutput('ToneMapper.dst')
    return g

BasicPathTracer = render_graph_PathTracer()
try: m.addGraph(BasicPathTracer)
except NameError: None
