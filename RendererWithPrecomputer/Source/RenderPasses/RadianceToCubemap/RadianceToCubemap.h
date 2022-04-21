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
#pragma once
#include "Falcor.h"
#include "FalcorExperimental.h"
#include "RenderPasses/Shared/PathTracer/PathTracer.h"
#include "LightComputer.h"

using namespace Falcor;

class RadianceToCubemap : public PathTracer
{
public:
    using SharedPtr = std::shared_ptr<RadianceToCubemap>;

    static SharedPtr create(RenderContext* pRenderContext, const Dictionary& dict);

    virtual std::string getDesc() override { return sDesc; }
    //virtual void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override;
    virtual void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene, std::vector<uint2> id_res) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;

    static const char* sDesc;

private:
    RadianceToCubemap(const Dictionary& dict);

    void recreateVars() override { mTracer.pVars = nullptr; }
    void prepareVars();
    void setTracerData(const RenderData& renderData);

    // Ray tracing program.
    struct
    {
        RtProgram::SharedPtr pProgram;
        RtProgramVars::SharedPtr pVars;
        ParameterBlock::SharedPtr pParameterBlock;      ///< ParameterBlock for all data.
    } mTracer;

    Texture::SharedPtr          mpIncomeRadiance128[4];   //Incoming radiance to store
    Texture::SharedPtr          mpIncomeRadiance64[4];
    Texture::SharedPtr          mpIncomeRadiance32[4];
    Texture::SharedPtr          mpIncomeRadiance16[4];
    Buffer::SharedPtr           mpResolutionId;
    std::vector<uint2>          mRes_Id;

    bool first;                                         //If first execute
    uint mFrameCount;                                   //Current frame number
    uint curRes;
    LightComputer* pLightSaver;

    uint batchSum;

    float cosine[49152];

    std::vector<std::vector<uint>> quadIndex;
    std::vector<std::vector<float>> quadTree;

    std::vector<std::vector<std::vector<uint8_t>>> textureArrayData;

};
