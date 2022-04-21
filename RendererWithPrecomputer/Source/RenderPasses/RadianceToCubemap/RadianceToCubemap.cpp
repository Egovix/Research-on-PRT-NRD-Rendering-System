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
#include "RadianceToCubemap.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Scene/HitInfo.h"
#include <sstream>

// #define PARALLEL_OUT
# include <omp.h>

namespace
{
    const char kShaderFile[] = "RenderPasses/RadianceToCubemap/RadianceToCubemapTracer.rt.slang";
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

const char* RadianceToCubemap::sDesc = "Store radiance to cubemap tracer";

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerClass("RadianceToCubemap", RadianceToCubemap::sDesc, RadianceToCubemap::create);
}

RadianceToCubemap::SharedPtr RadianceToCubemap::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new RadianceToCubemap(dict));
}

RadianceToCubemap::RadianceToCubemap(const Dictionary& dict) : PathTracer(dict, kOutputChannels)
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
    batchSum = 0;
    first = true;
    curRes = 128;
    pLightSaver = new LightComputer();

    // for (int i = 0; i < 128; i++)
    // {
    //     for (int j = 0; j < 128; j++)
    //     {
    //         float3 tempDirT = float3(1.f, (i - 63.5) / 64.0, abs(j - 63.5) / 64.0);
    //         cosine[i * 128 + j] = tempDirT.z / glm::length(tempDirT);
    // 
    //         float3 tempDirN = float3((63.5 - j) / 64.0, (i - 63.5) / 64.0, 1.f);
    //         cosine[16384 + i * 128 + j] = tempDirN.z / glm::length(tempDirN);
    // 
    //         float3 tempDirB = float3((j - 63.5) / 64.0, 1.f, abs(63.5 - i) / 64.0);
    //         cosine[32768 + i * 128 + j] = tempDirB.z / glm::length(tempDirB);
    //     }
    // }

#ifdef PARALLEL_OUT
    quadIndex.resize(16);
    quadTree.resize(16);

    textureArrayData.resize(16);
    for (uint i = 0; i < 16; i++)
    {
        textureArrayData[i].resize(1024);
    }
#endif

}

void RadianceToCubemap::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene, std::vector<uint2> res_id)
{
    PathTracer::setScene(pRenderContext, pScene);

    mRes_Id.resize(res_id.size());
    mRes_Id.assign(res_id.begin(), res_id.end());


    if (pScene)
    {
        mTracer.pProgram->addDefines(pScene->getSceneDefines());
    }

}

std::vector<uint8_t> captureTextureToSystemRAM(uint32_t mipLevel, uint32_t arraySlice, const Texture& pTexture)
{
    RenderContext* pContext = gpDevice->getRenderContext();
    std::vector<uint8_t> textureData;
    uint32_t subresource = pTexture.getSubresourceIndex(arraySlice, mipLevel);
    textureData = pContext->readTextureSubresource(&pTexture, subresource);
    return textureData;
}

void recoverImg()
{
    std::ifstream unimpressedLight("LT_Cloud_Local_Color_HuaWeiCar_SeatAll_128_16384.dat", std::ios::binary | std::ios::in);
}

void RadianceToCubemap::execute(RenderContext* pRenderContext, const RenderData& renderData)
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

    if (mFrameCount == 0)
    {
        curRes = mRes_Id[4096 * batchSum].x;

        if (curRes == 128)
        {
            for (uint i = 0; i < 4; i++)
            {
                mpIncomeRadiance128[i].reset();
                mpIncomeRadiance128[i] = Texture::create2D(128, 384, ResourceFormat::RGBA32Float, 1024, 1, nullptr, ResourceBindFlags::UnorderedAccess);
            }
        }
        else if (curRes == 64)
        {
            for (uint i = 0; i < 4; i++)
            {
                mpIncomeRadiance64[i].reset();
                mpIncomeRadiance64[i] = Texture::create2D(64, 192, ResourceFormat::RGBA32Float, 1024, 1, nullptr, ResourceBindFlags::UnorderedAccess);
            }
        }
        else if (curRes == 32)
        {
            for (uint i = 0; i < 4; i++)
            {
                mpIncomeRadiance32[i].reset();
                mpIncomeRadiance32[i] = Texture::create2D(32, 96, ResourceFormat::RGBA32Float, 1024, 1, nullptr, ResourceBindFlags::UnorderedAccess);
            }
        }
        else
        {
            for (uint i = 0; i < 4; i++)
            {
                mpIncomeRadiance16[i].reset();
                mpIncomeRadiance16[i] = Texture::create2D(16, 48, ResourceFormat::RGBA32Float, 1024, 1, nullptr, ResourceBindFlags::UnorderedAccess);
            }
        }
    }

    if(first)
    {
        uint2* pResolutionId = new uint2[mRes_Id.size()];

        for (uint id = 0; id < mRes_Id.size(); id++)
        {
            pResolutionId[id] = mRes_Id[id];
        }
        
        mpResolutionId = Buffer::createTyped(uint32_t(mRes_Id.size()), Falcor::Resource::BindFlags::ShaderResource, Falcor::Buffer::CpuAccess::None, pResolutionId);

        delete[] pResolutionId;

        first = false;
    }

    if (curRes == 128)
    {
        for (uint i = 0; i < 4; i++)
        {
            mTracer.pVars["gIncomeRadiance_128"][i] = mpIncomeRadiance128[i];
        }
    }
    else if (curRes == 64)
    {
        for (uint i = 0; i < 4; i++)
        {
            mTracer.pVars["gIncomeRadiance_64"][i] = mpIncomeRadiance64[i];
        }
    }
    else if (curRes == 32)
    {
        for (uint i = 0; i < 4; i++)
        {
            mTracer.pVars["gIncomeRadiance_32"][i] = mpIncomeRadiance32[i];
        }
    }
    else
    {
        for (uint i = 0; i < 4; i++)
        {
            mTracer.pVars["gIncomeRadiance_16"][i] = mpIncomeRadiance16[i];
        }
    }

    mTracer.pVars["gRes_Id"] = mpResolutionId;
    
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

    // 49151 = 128 * 128 * 3 - 1
    if (mFrameCount == 49151)
    {

#ifndef PARALLEL_OUT

        if (curRes == 128)
        {
            for (uint i = 0; i < 4; i++)
            {
                pLightSaver->StoreColorLT(*mpIncomeRadiance128[i].get(), 4 * batchSum + i);
            }
        }
        else if (curRes == 64)
        {
            for (uint i = 0; i < 4; i++)
            {
                pLightSaver->StoreColorLT(*mpIncomeRadiance64[i].get(), 4 * batchSum + i);
            }
        }
        else if (curRes == 32)
        {
            for (uint i = 0; i < 4; i++)
            {
                pLightSaver->StoreColorLT(*mpIncomeRadiance32[i].get(), 4 * batchSum + i);
            }
        }
        else
        {
            for (uint i = 0; i < 4; i++)
            {
                pLightSaver->StoreColorLT(*mpIncomeRadiance16[i].get(), 4 * batchSum + i);
            }
        }

#endif

#ifdef PARALLEL_OUT
        // Parallel
        for (uint i = 0; i < 16; i++)
        {
            for (uint j = 0; j < 1024; j++)
            {
                Texture tempTex = *mpIncomeRadiance128[i].get();
                textureArrayData[i][j] = captureTextureToSystemRAM(0, uint32_t(j), tempTex);
            }
        }

        std::cout << std::endl << "capture complete!" << std::endl;

        // omp_set_num_threads(16);
#pragma omp parallel for
        for (int i = 0; i < 16; i++)
        {
            pWaveletSaver->calculateLightWaveletCoefficients_QuadTree_Parallel(textureArrayData[i], i + 16 * batchSum, quadIndex[i], quadTree[i]);
        }

        std::cout << "parallel complete!" << std::endl;

        for (uint i = 0; i < 16; i++)
        {
            std::ofstream ofs_tree("GPUTree_ueCarHood_parallel.light", std::ios::binary | std::ios::app | std::ios::out);
            ofs_tree.write((const char*)quadTree[i].data(), sizeof(float) * quadTree[i].size());
            ofs_tree.close();

            std::ofstream ofs_index("StartIndex_ueCarHood_parallel.lightIndex", std::ios::binary | std::ios::app | std::ios::out);
            ofs_index.write((const char*)quadIndex[i].data(), sizeof(uint) * quadIndex[i].size());
            ofs_index.close();
        }

        std::cout << "output complete!" << std::endl;
#endif

        mFrameCount = 0;

        batchSum++;
        // meshSum equals to batchNum
        if (batchSum >= 108)
        {
            exit(0);
        }
        
    }
    else
    {
        mFrameCount++;
    }

}

void RadianceToCubemap::prepareVars()
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

void RadianceToCubemap::setTracerData(const RenderData& renderData)
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
