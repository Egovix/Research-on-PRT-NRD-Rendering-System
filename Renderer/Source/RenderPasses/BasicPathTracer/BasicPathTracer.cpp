#include "BasicPathTracer.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Scene/HitInfo.h"
#include <sstream>

const RenderPass::Info BasicPathTracer::kInfo{ "BasicPathTracer", "Basic path tracer." };

namespace
{
    const char kShaderFile[] = "RenderPasses/BasicPathTracer/PathTracer.rt.slang";
    const char kParameterBlockName[] = "gData";

    const uint32_t kMaxPayloadSizeBytes = HitInfo::kMaxPackedSizeInBytes;
    const uint32_t kMaxAttributeSizeBytes = 8;
    const uint32_t kMaxRecursionDepth = 1;

    const std::string kColorOutput = "color";
    const std::string kAlbedoOutput = "albedo";
    const std::string kTimeOutput = "time";

    const Falcor::ChannelList kOutputChannels =
    {
        { kColorOutput,     "gOutputColor",               "Output color (linear)", true, ResourceFormat::RGBA32Float },
        { kAlbedoOutput,    "gOutputAlbedo",              "Surface albedo (base color) or background color", true, ResourceFormat::RGBA32Float },
        { kTimeOutput,      "gOutputTime",                "Per-pixel execution time", true, ResourceFormat::R32Uint },
    };
};

extern "C" FALCOR_API_EXPORT const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" FALCOR_API_EXPORT void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerPass(BasicPathTracer::kInfo, BasicPathTracer::create);
}

BasicPathTracer::SharedPtr BasicPathTracer::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new BasicPathTracer(dict));
}

BasicPathTracer::BasicPathTracer(const Dictionary& dict)
    : PathTracer(kInfo, dict, kOutputChannels)
{
}

void BasicPathTracer::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    PathTracer::setScene(pRenderContext, pScene);

    if (mpScene)
    {
        if (mpScene->hasProceduralGeometry())
        {
            logWarning("BasicPathTracer: This render pass only supports triangles. Other types of geometry will be ignored.");
        }

        Program::DefineList defines;
        defines.add(mpScene->getSceneDefines());
        defines.add("MAX_BOUNCES", std::to_string(mSharedParams.maxBounces));
        defines.add("SAMPLES_PER_PIXEL", std::to_string(mSharedParams.samplesPerPixel));

        RtProgram::Desc desc;
        desc.addShaderLibrary(kShaderFile);
        desc.setMaxPayloadSize(kMaxPayloadSizeBytes);
        desc.setMaxAttributeSize(kMaxAttributeSizeBytes);
        desc.setMaxTraceRecursionDepth(kMaxRecursionDepth);

        mTracer.pBindingTable = RtBindingTable::create(2, 2, mpScene->getGeometryCount());
        auto& sbt = mTracer.pBindingTable;
        sbt->setRayGen(desc.addRayGen("rayGen"));
        sbt->setMiss(kRayTypeScatter, desc.addMiss("scatterMiss"));
        sbt->setMiss(kRayTypeShadow, desc.addMiss("shadowMiss"));
        sbt->setHitGroup(kRayTypeScatter, mpScene->getGeometryIDs(Scene::GeometryType::TriangleMesh), desc.addHitGroup("scatterClosestHit", "scatterAnyHit"));
        sbt->setHitGroup(kRayTypeShadow, mpScene->getGeometryIDs(Scene::GeometryType::TriangleMesh), desc.addHitGroup("", "shadowAnyHit"));

        mTracer.pProgram = RtProgram::create(desc, defines);
    }
}

void BasicPathTracer::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!beginFrame(pRenderContext, renderData)) return;

    RtProgram::SharedPtr pProgram = mTracer.pProgram;
    setStaticParams(pProgram.get());

    pProgram->addDefines(getValidResourceDefines(mInputChannels, renderData));
    pProgram->addDefines(getValidResourceDefines(mOutputChannels, renderData));

    if (mUseEmissiveSampler)
    {
        FALCOR_ASSERT(mpEmissiveSampler);
        if (pProgram->addDefines(mpEmissiveSampler->getDefines())) mTracer.pVars = nullptr;
    }

    if (!mTracer.pVars) prepareVars();
    FALCOR_ASSERT(mTracer.pVars);

    setTracerData(renderData);

    auto bind = [&](const ChannelDesc& desc)
    {
        if (!desc.texname.empty())
        {
            auto var = mTracer.pVars->getRootVar();
            var[desc.texname] = renderData[desc.name]->asTexture();
        }
    };
    for (auto channel : mInputChannels) bind(channel);
    for (auto channel : mOutputChannels) bind(channel);

    const uint2 targetDim = renderData.getDefaultTextureDims();
    FALCOR_ASSERT(targetDim.x > 0 && targetDim.y > 0);

    mpPixelDebug->prepareProgram(pProgram, mTracer.pVars->getRootVar());
    mpPixelStats->prepareProgram(pProgram, mTracer.pVars->getRootVar());

    {
        FALCOR_PROFILE("BasicPathTracer::execute()_RayTrace");
        mpScene->raytrace(pRenderContext, mTracer.pProgram.get(), mTracer.pVars, uint3(targetDim, 1));
    }

    endFrame(pRenderContext, renderData);
}

void BasicPathTracer::prepareVars()
{
    FALCOR_ASSERT(mTracer.pProgram);

    mTracer.pProgram->addDefines(mpSampleGenerator->getDefines());

    mTracer.pProgram->setTypeConformances(mpScene->getTypeConformances());

    mTracer.pVars = RtProgramVars::create(mTracer.pProgram, mTracer.pBindingTable);

    auto var = mTracer.pVars->getRootVar();
    mpSampleGenerator->setShaderData(var);

    ProgramReflection::SharedConstPtr pReflection = mTracer.pProgram->getReflector();
    ParameterBlockReflection::SharedConstPtr pBlockReflection = pReflection->getParameterBlock(kParameterBlockName);
    FALCOR_ASSERT(pBlockReflection);
    mTracer.pParameterBlock = ParameterBlock::create(pBlockReflection);
    FALCOR_ASSERT(mTracer.pParameterBlock);

    if (mpEnvMapSampler) mpEnvMapSampler->setShaderData(mTracer.pParameterBlock["envMapSampler"]);

    mTracer.pVars->setParameterBlock(kParameterBlockName, mTracer.pParameterBlock);
}

void BasicPathTracer::setTracerData(const RenderData& renderData)
{
    auto pBlock = mTracer.pParameterBlock;
    FALCOR_ASSERT(pBlock);

    pBlock["params"].setBlob(mSharedParams);

    if (mUseEmissiveSampler)
    {
        FALCOR_ASSERT(mpEmissiveSampler);
        mpEmissiveSampler->setShaderData(pBlock["emissiveSampler"]);
    }
}
