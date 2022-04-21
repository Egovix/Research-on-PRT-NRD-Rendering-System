#pragma once
#include "../GBufferBase.h"
#include "Utils/Sampling/SampleGenerator.h"

#include "Cy/cySampleElim.h"
#include "Cy/cyPoint.h"

using namespace Falcor;

/** Ray traced V-buffer pass.

    This pass renders a visibility buffer using ray tracing.
    The visibility buffer encodes the mesh instance ID and primitive index,
    as well as the barycentrics at the hit point.
*/
class VModifiedBufferRT : public GBufferBase
{
public:
    using SharedPtr = std::shared_ptr<VModifiedBufferRT>;

    static SharedPtr create(RenderContext* pRenderContext, const Dictionary& dict);

    RenderPassReflection reflect(const CompileData& compileData) override;
    //void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override;
    void setScene(RenderContext* pRenderContext, const std::shared_ptr<Scene>& pScene, std::vector<std::vector<size_t>> face, std::vector<std::vector<cy::Point3f>> bary) override;
    void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    std::string getDesc(void) override { return kDesc; }

private:
    VModifiedBufferRT(const Dictionary& dict);

    // Internal state
    SampleGenerator::SharedPtr mpSampleGenerator;

    std::vector<std::vector<size_t>> mSamplePointsFaceId;
    std::vector<std::vector<cy::Point3f>> mSamplePointsBary;

    Buffer::SharedPtr   mpSamplePointsIdBary;


    struct
    {
        RtProgram::SharedPtr pProgram;
        RtProgramVars::SharedPtr pVars;
    } mRaytrace;

    static const char* kDesc;
    friend void getPasses(Falcor::RenderPassLibrary& lib);

    uint32_t mFrameCount;
    size_t mPointSize;
};
