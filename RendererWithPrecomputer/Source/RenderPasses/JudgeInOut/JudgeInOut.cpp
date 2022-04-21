/***************************************************************************
 # Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "JudgeInOut.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Scene/HitInfo.h"
#include <sstream>
#include <cmath>

namespace
{
    const char kShaderFile[] = "RenderPasses/JudgeInOut/JudgeInOutTracer.rt.slang";
    const char kParameterBlockName[] = "gData";

    const uint32_t kMaxPayloadSizeBytes = HitInfo::kMaxPackedSizeInBytes;
    const uint32_t kMaxAttributesSizeBytes = 8;
    const uint32_t kMaxRecursionDepth = 1;

    const std::string kColorOutput = "color";
    const std::string kAlbedoOutput = "albedo";
    const std::string kTimeOutput = "time";

    const Falcor::ChannelList kOutputChannels =
    {
        { kColorOutput,     "gOutputColor",               "Output color (linear)", true /* optional */                              },
        { kAlbedoOutput,    "gOutputAlbedo",              "Surface albedo (base color) or background color", true /* optional */    },
        { kTimeOutput,      "gOutputTime",                "Per-pixel execution time", true /* optional */, ResourceFormat::R32Uint  },
    };


}

const char* JudgeInOut::sDesc = "Judge current point in car or out car";

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerClass("JudgeInOut", JudgeInOut::sDesc, JudgeInOut::create);
}

JudgeInOut::SharedPtr JudgeInOut::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new JudgeInOut(dict));
}

JudgeInOut::JudgeInOut(const Dictionary& dict) : PathTracer(dict, kOutputChannels)
{
    RtProgram::Desc progDesc;
    progDesc.addShaderLibrary(kShaderFile).setRayGen("rayGen");
    progDesc.addHitGroup(kRayTypeScatter, "scatterClosestHit", "scatterAnyHit").addMiss(kRayTypeScatter, "scatterMiss");
    progDesc.addHitGroup(kRayTypeShadow, "", "shadowAnyHit").addMiss(kRayTypeShadow, "shadowMiss");
    progDesc.addDefine("MAX_BOUNCES", std::to_string(mSharedParams.maxBounces));
    progDesc.addDefine("SAMPLES_PER_PIXEL", std::to_string(mSharedParams.samplesPerPixel));
    progDesc.setMaxTraceRecursionDepth(kMaxRecursionDepth);
    mTracer.pProgram = RtProgram::create(progDesc, kMaxPayloadSizeBytes, kMaxAttributesSizeBytes);

    mFrameCount = 0;
    first = true;
    batchSum = 0;
}

void JudgeInOut::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    PathTracer::setScene(pRenderContext, pScene);

    if (pScene)
    {
        mTracer.pProgram->addDefines(pScene->getSceneDefines());

#ifdef getClosestCloudPoint

        std::cout << "enter get closest" << std::endl;
        
        SceneBuilder::Flags buildFlags;
        buildFlags |= SceneBuilder::Flags::Force32BitIndices;
        SceneBuilder::SharedPtr pBuilder = SceneBuilder::create(pScene->getFilename(), buildFlags);
        if(!pBuilder) std::cout << "pBuilder falied" << std::endl;
        
        std::fstream posListFile;
        // posListFile.open("D:/LabProject/CarExhibition/Falcor-master/Source/Mogwai/pointsLocation_audiCarUV_1024_visual.txt", std::ios::in);
        posListFile.open("D:/LabProject/CarExhibition/Falcor-master/Source/Mogwai/pointsLocation_audiCarUV_16384_visual.txt", std::ios::in);
        if(!posListFile.is_open()) std::cout << "in open falied" << std::endl;
        
        const uint cubemapNum = 16384;
        
        float posList[cubemapNum * 3];
        for (uint i = 0; i < cubemapNum * 3; i++)
        {
            posListFile >> posList[i];
        }
        
        posListFile.close();
        
        uint mesh_num = pBuilder->getMeshNum();
        std::vector<std::vector<uint>> index_data(mesh_num);
        std::vector<std::vector<StaticVertexData>> vertex_data(mesh_num);
        pBuilder->getData(index_data, vertex_data);
        
        for (uint meshId = 0; meshId < 79; meshId++)
        {
            // uint meshId = 69;
            // if (!(meshId == 8 || meshId == 9 || meshId == 10 || meshId == 11 || meshId == 12 || meshId == 28 || meshId == 74))
            if(meshId != 10)
            {
                continue;
            }
            uint index_num = (uint)index_data[meshId].size();
            if (index_num == 0) std::cout << "index num is 0" << std::endl;
            std::vector<uint> closestId;
        
            for (uint j = 0; j < index_num; j += 3)
            {
                uint minId = 10;
                double minDis = 1000000;
        
                float3 currentBaryPos = vertex_data[meshId][index_data[meshId][j]].position * 0.333f
                    + vertex_data[meshId][index_data[meshId][j + 1]].position * 0.333f
                    + vertex_data[meshId][index_data[meshId][j + 2]].position * 0.333f;
        
                for (uint k = 0; k < cubemapNum; k++)
                {
                    double distanceSquare = pow(currentBaryPos.x - posList[k * 3], 2) + pow(currentBaryPos.y - posList[k * 3 + 1], 2) + pow(currentBaryPos.z - posList[k * 3 + 2], 2);
        
                    if (distanceSquare < minDis)
                    {
                        minDis = distanceSquare;
                        minId = k;
                    }
                }
        
                closestId.push_back(minId);
            }
        
            std::fstream closestIdFile;
            // closestIdFile.open("D:/LabProject/CarExhibition/Falcor-master/Source/Mogwai/closestId_1024.txt", std::ios::out);
            closestIdFile.open("D:/LabProject/CarExhibition/Falcor-master/Source/Mogwai/closestId_16384.txt", std::ios::out | std::ios::app);
            if (!closestIdFile.is_open()) std::cout << "out open falied" << std::endl;
        
            for (uint i = 0; i < index_num / 3; i++)
            {
                closestIdFile << closestId[i] << std::endl;
            }
        
            closestIdFile.close();
        
        }
        
        
        std::cout << "Success get closest!" << std::endl;

#endif
    }

}

void JudgeInOut::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    // Call shared pre-render code.
    if (!beginFrame(pRenderContext, renderData)) return;

    // Set compile-time constants.
    RtProgram::SharedPtr pProgram = mTracer.pProgram;
    setStaticParams(pProgram.get());

    // For optional I/O resources, set 'is_valid_<name>' defines to inform the program of which ones it can access.
    // TODO: This should be moved to a more general mechanism using Slang.
    pProgram->addDefines(getValidResourceDefines(mInputChannels, renderData));
    pProgram->addDefines(getValidResourceDefines(mOutputChannels, renderData));

    if (mUseEmissiveSampler)
    {
        // Specialize program for the current emissive light sampler options.
        assert(mpEmissiveSampler);
        if (pProgram->addDefines(mpEmissiveSampler->getDefines())) mTracer.pVars = nullptr;
    }

    // Prepare program vars. This may trigger shader compilation.
    // The program should have all necessary defines set at this point.
    if (!mTracer.pVars) prepareVars();

    //!mpIncomeRadiance
    if (first)
    {
        mpIfOutsideCar = Buffer::createTyped(Falcor::ResourceFormat::R32Uint, 16384, Falcor::ResourceBindFlags::UnorderedAccess, Falcor::Buffer::CpuAccess::None);

        mpCloudWorldPos = Buffer::createTyped(Falcor::ResourceFormat::RGBA32Float, 16384, Falcor::ResourceBindFlags::UnorderedAccess, Falcor::Buffer::CpuAccess::None);

        mpCloudWorldNormal = Buffer::createTyped(Falcor::ResourceFormat::RGBA32Float, 16384, Falcor::ResourceBindFlags::UnorderedAccess, Falcor::Buffer::CpuAccess::None);

        first = false;
    }

    mTracer.pVars["gIfOutsideCar"] = mpIfOutsideCar;
    mTracer.pVars["gCloudWorldPos"] = mpCloudWorldPos;
    mTracer.pVars["gCloudWorldNormal"] = mpCloudWorldNormal;

    assert(mTracer.pVars);

    // Set shared data into parameter block.
    setTracerData(renderData);

    // Bind I/O buffers. These needs to be done per-frame as the buffers may change anytime.
    auto bind = [&](const ChannelDesc& desc)
    {
        if (!desc.texname.empty())
        {
            auto pGlobalVars = mTracer.pVars->getRootVar();
            pGlobalVars[desc.texname] = renderData[desc.name]->asTexture();
        }
    };
    for (auto channel : mInputChannels) bind(channel);
    for (auto channel : mOutputChannels) bind(channel);

    // Get dimensions of ray dispatch.
    const uint2 targetDim = renderData.getDefaultTextureDims();
    assert(targetDim.x > 0 && targetDim.y > 0);

    mpPixelDebug->prepareProgram(pProgram, mTracer.pVars->getRootVar());
    mpPixelStats->prepareProgram(pProgram, mTracer.pVars->getRootVar());

    // Spawn the rays.
    {
        PROFILE("RadianceToCubemap::execute()_RayTrace");
        mpScene->raytrace(pRenderContext, mTracer.pProgram.get(), mTracer.pVars, uint3(targetDim, 1));
    }

    // Call shared post-render code.
    endFrame(pRenderContext, renderData);

    // can use random rays
    if (mFrameCount == 0)
    {

        void* pMapContent_pos = mpCloudWorldPos->map(Falcor::Buffer::MapType::Read);
        float4* pContent_pos = (float4*)pMapContent_pos;

        void* pMapContent_normal = mpCloudWorldNormal->map(Falcor::Buffer::MapType::Read);
        float4* pContent_normal = (float4*)pMapContent_normal;

        std::fstream f;
        f.open("pointsWorldInfo_HWcarSeatNewPT_12288.txt", std::ios::out | std::ios::app);
        for (uint i = 0; i < 16384 && (16384 * batchSum + i < 12288); i++)
        {
            f << pContent_pos[i].x << " " << pContent_pos[i].y << " " << pContent_pos[i].z
              << " " << pContent_normal[i].x << " " << pContent_normal[i].y << " " << pContent_normal[i].z
              << std::endl;
        }
        f.close();
        
        mFrameCount = 0;

        batchSum++;
        if (batchSum >= 1)
        {
            exit(0);
        }
    }
    else
    {
        mFrameCount++;
    }
}

void JudgeInOut::prepareVars()
{
    assert(mpScene);
    assert(mTracer.pProgram);

    // Configure program.
    mTracer.pProgram->addDefines(mpSampleGenerator->getDefines());

    // Create program variables for the current program/scene.
    // This may trigger shader compilation. If it fails, throw an exception to abort rendering.
    mTracer.pVars = RtProgramVars::create(mTracer.pProgram, mpScene);

    // Bind utility classes into shared data.
    auto pGlobalVars = mTracer.pVars->getRootVar();
    bool success = mpSampleGenerator->setShaderData(pGlobalVars);
    if (!success) throw std::exception("Failed to bind sample generator");

    // Create parameter block for shared data.
    ProgramReflection::SharedConstPtr pReflection = mTracer.pProgram->getReflector();
    ParameterBlockReflection::SharedConstPtr pBlockReflection = pReflection->getParameterBlock(kParameterBlockName);
    assert(pBlockReflection);
    mTracer.pParameterBlock = ParameterBlock::create(pBlockReflection);
    assert(mTracer.pParameterBlock);

    // Bind static resources to the parameter block here. No need to rebind them every frame if they don't change.
    // Bind the light probe if one is loaded.
    if (mpEnvMapSampler) mpEnvMapSampler->setShaderData(mTracer.pParameterBlock["envMapSampler"]);

    // Bind the parameter block to the global program variables.
    mTracer.pVars->setParameterBlock(kParameterBlockName, mTracer.pParameterBlock);
}

void JudgeInOut::setTracerData(const RenderData& renderData)
{
    auto pBlock = mTracer.pParameterBlock;
    assert(pBlock);

    // Upload parameters struct.
    pBlock["params"].setBlob(mSharedParams);

    // Bind emissive light sampler.
    if (mUseEmissiveSampler)
    {
        assert(mpEmissiveSampler);
        bool success = mpEmissiveSampler->setShaderData(pBlock["emissiveSampler"]);
        if (!success) throw std::exception("Failed to bind emissive light sampler");
    }
}
