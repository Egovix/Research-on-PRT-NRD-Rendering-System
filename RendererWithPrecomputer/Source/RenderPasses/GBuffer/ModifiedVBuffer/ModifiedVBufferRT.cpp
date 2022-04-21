#include "ModifiedVBufferRT.h"
#include "Scene/HitInfo.h"
#include "Scene/Scene.h"
#include "Scene/SceneBuilder.h"
#include "RenderGraph/RenderPassStandardFlags.h"
#include "RenderGraph/RenderPassHelpers.h"
#include <algorithm>

const char* ModifiedVBufferRT::kDesc = "Ray traced V-buffer generation pass for sample points only";

namespace
{
    const std::string kProgramFile = "RenderPasses/GBuffer/ModifiedVBuffer/ModifiedVBufferRT.rt.slang";

    // Ray tracing settings that affect the traversal stack size. Set as small as possible.
    const uint32_t kMaxPayloadSizeBytes = 4; // TODO: The shader doesn't need a payload, set this to zero if it's possible to pass a null payload to TraceRay()
    const uint32_t kMaxAttributesSizeBytes = 8;
    const uint32_t kMaxRecursionDepth = 1;

    const std::string kVBufferName = "modifiedVBuffer";
    const std::string kVBufferDesc = "V-buffer in packed format (indices + barycentrics)";

    // Additional output channels.
    const ChannelList kVBufferExtraChannels =
    {
        { "time",           "gTime",            "Per-pixel execution time",         true /* optional */, ResourceFormat::R32Uint     },
        { "pointIndex",     "gPointId",         "Sample points Id(mesh,face,bary)", true,                ResourceFormat::RGBA32Float},
        { "pointSide",      "gPointSide",       "Sample points side(inside or outside)", true,           ResourceFormat::RGBA32Float},
        { "jitterXY",       "gJitterXY",        "Jitter along X and Y(TBN)",             true,                ResourceFormat::RGBA32Float}
    };

    bool myLessSort(uint2 a, uint2 b)
    {
        return (a.x < b.x);
    }
};

RenderPassReflection ModifiedVBufferRT::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    // Arraysize max is 128
    // .texture2D(1920, 1080, 1, 1, 1)
    reflector.addOutput(kVBufferName, kVBufferDesc).bindFlags(Resource::BindFlags::UnorderedAccess).format(mVBufferFormat);
    addRenderPassOutputs(reflector, kVBufferExtraChannels);

    return reflector;
}

ModifiedVBufferRT::SharedPtr ModifiedVBufferRT::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new ModifiedVBufferRT(dict));
}

ModifiedVBufferRT::ModifiedVBufferRT(const Dictionary& dict)
    : GBufferBase()
{
    parseDictionary(dict);

    // Create sample generator
    mpSampleGenerator = SampleGenerator::create(SAMPLE_GENERATOR_DEFAULT);

    // Create ray tracing program
    RtProgram::Desc desc;
    desc.addShaderLibrary(kProgramFile).setRayGen("rayGen");
    desc.addHitGroup(0, "closestHit", "anyHit").addMiss(0, "miss");
    desc.setMaxTraceRecursionDepth(kMaxRecursionDepth);
    desc.addDefines(mpSampleGenerator->getDefines());
    mRaytrace.pProgram = RtProgram::create(desc, kMaxPayloadSizeBytes, kMaxAttributesSizeBytes);
}

void ModifiedVBufferRT::setScene(RenderContext* pRenderContext, const std::shared_ptr<Scene>& pScene, std::vector<std::vector<size_t>> face, std::vector<std::vector<cy::Point3f>> bary)
{
    GBufferBase::setScene(pRenderContext, pScene);

    mPointSize = 0;
    mSamplePointsFaceId.resize(face.size());
    mSamplePointsBary.resize(bary.size());
    for (uint meshId = 0; meshId < face.size(); meshId++)
    {
        mPointSize += face[meshId].size();
        mSamplePointsFaceId[meshId].resize(face[meshId].size());
        mSamplePointsBary[meshId].resize(bary[meshId].size());
    }

    mSamplePointsFaceId.assign(face.begin(), face.end());
    mSamplePointsBary.assign(bary.begin(), bary.end());

    uint* pPointSide = new uint[16384];
    std::fstream f;
    f.open("pointSide.txt", std::ios::in);
    for (uint i = 0; i < 16384; i++)
    {
        f >> pPointSide[i];
    }
    mpSamplePointsSide = Buffer::createTyped(16384, Falcor::Resource::BindFlags::ShaderResource, Falcor::Buffer::CpuAccess::None, pPointSide);
    delete[] pPointSide;

    mFrameCount = 0;
    mRaytrace.pVars = nullptr;

    if (pScene)
    {
        mRaytrace.pProgram->addDefines(pScene->getSceneDefines());
    }

}

void ModifiedVBufferRT::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    GBufferBase::execute(pRenderContext, renderData);

    // If there is no scene, clear the output and return.
    if (mpScene == nullptr)
    {
        auto pOutput = renderData[kVBufferName]->asTexture();
        pRenderContext->clearUAV(pOutput->getUAV().get(), uint4(HitInfo::kInvalidIndex));

        auto clear = [&](const ChannelDesc& channel)
        {
            auto pTex = renderData[channel.name]->asTexture();
            if (pTex) pRenderContext->clearUAV(pTex->getUAV().get(), float4(0.f));
        };
        for (const auto& channel : kVBufferExtraChannels) clear(channel);

        return;
    }

    // Configure depth-of-field.
    // When DOF is enabled, two PRNG dimensions are used. Pass this info to subsequent passes via the dictionary.
    const bool useDOF = mpScene->getCamera()->getApertureRadius() > 0.f;
    if (useDOF) renderData.getDictionary()[Falcor::kRenderPassPRNGDimension] = useDOF ? 2u : 0u;

    // Set program defines.
    mRaytrace.pProgram->addDefine("USE_DEPTH_OF_FIELD", useDOF ? "1" : "0");
    mRaytrace.pProgram->addDefine("DISABLE_ALPHA_TEST", mDisableAlphaTest ? "1" : "0");

    // For optional I/O resources, set 'is_valid_<name>' defines to inform the program of which ones it can access.
    // TODO: This should be moved to a more general mechanism using Slang.
    mRaytrace.pProgram->addDefines(getValidResourceDefines(kVBufferExtraChannels, renderData));

    // Create program vars.
    if (!mRaytrace.pVars)
    {
        mRaytrace.pVars = RtProgramVars::create(mRaytrace.pProgram, mpScene);

        // Bind static resources
        ShaderVar var = mRaytrace.pVars->getRootVar();
        if (!mpSampleGenerator->setShaderData(var)) throw std::exception("Failed to bind sample generator");
    }

    // Bind resources.
    ShaderVar var = mRaytrace.pVars->getRootVar();
    var["PerFrameCB"]["frameCount"] = mFrameCount++;
    
    if (!mpSamplePointsIdBary)
    {
        float4* pSamplePointsIdBary = new float4[mPointSize];

        uint32_t id = 0;

        for (uint i = 0; i < mSamplePointsFaceId.size(); i++)
        {
            for (uint j = 0; j < mSamplePointsFaceId[i].size(); j++)
            {
                pSamplePointsIdBary[id] = float4(i, mSamplePointsFaceId[i][j], mSamplePointsBary[i][j].x, mSamplePointsBary[i][j].y);
                id++;
            }
        }
        
        mpSamplePointsIdBary = Buffer::createTyped(uint32_t(mPointSize), Falcor::ResourceBindFlags::ShaderResource, Falcor::Buffer::CpuAccess::None, pSamplePointsIdBary);

        delete[] pSamplePointsIdBary;

    }
    var["gSamplePoints"] = mpSamplePointsIdBary;
    var["gSamplePointSide"] = mpSamplePointsSide;
    var["gVBuffer"] = renderData[kVBufferName]->asTexture();

    // Bind output channels as UAV buffers.
    auto bind = [&](const ChannelDesc& channel)
    {
        Texture::SharedPtr pTex = renderData[channel.name]->asTexture();
        var[channel.texname] = pTex;
    };
    for (const auto& channel : kVBufferExtraChannels) bind(channel);

    // Dispatch the rays.
    mpScene->raytrace(pRenderContext, mRaytrace.pProgram.get(), mRaytrace.pVars, uint3(mFrameDim, 1));
}
