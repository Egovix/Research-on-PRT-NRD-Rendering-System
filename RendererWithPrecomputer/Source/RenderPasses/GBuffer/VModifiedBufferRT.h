#pragma once
#include "../GBufferBase.h"
#include "Utils/Sampling/SampleGenerator.h"

using namespace Falcor;

/** Ray traced V-buffer pass.

    This pass renders a visibility buffer using ray tracing.
    The visibility buffer encodes the mesh instance ID and primitive index,
    as well as the barycentrics at the hit point.
*/
class VBufferRT : public GBufferBase
{
public:
    using SharedPtr = std::shared_ptr<VBufferRT>;

    static SharedPtr create(RenderContext* pRenderContext, const Dictionary& dict);

    RenderPassReflection reflect(const CompileData& compileData) override;
    void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override;
    void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    std::string getDesc(void) override { return kDesc; }

private:
    VBufferRT(const Dictionary& dict);

    // Internal state
    SampleGenerator::SharedPtr mpSampleGenerator;
    uint32_t mFrameCount = 0;

    struct
    {
        RtProgram::SharedPtr pProgram;
        RtProgramVars::SharedPtr pVars;
    } mRaytrace;

    static const char* kDesc;
    friend void getPasses(Falcor::RenderPassLibrary& lib);
};
