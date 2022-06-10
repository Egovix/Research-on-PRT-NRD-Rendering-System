#pragma once
#include "Falcor.h"
#include "RenderPasses/Shared/PathTracer/PathTracer.h"

using namespace Falcor;

class BasicPathTracer : public PathTracer
{
public:
    using SharedPtr = std::shared_ptr<BasicPathTracer>;

    static const Info kInfo;

    static SharedPtr create(RenderContext* pRenderContext, const Dictionary& dict);

    virtual void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;

private:
    BasicPathTracer(const Dictionary& dict);

    void recreateVars() override { mTracer.pVars = nullptr; }
    void prepareVars();
    void setTracerData(const RenderData& renderData);

    struct
    {
        RtProgram::SharedPtr pProgram;
        RtBindingTable::SharedPtr pBindingTable;
        RtProgramVars::SharedPtr pVars;
        ParameterBlock::SharedPtr pParameterBlock;      ///< ParameterBlock for all data.
    } mTracer;
};
