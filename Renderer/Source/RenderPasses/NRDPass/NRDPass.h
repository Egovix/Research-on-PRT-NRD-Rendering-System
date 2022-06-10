#pragma once

#if FALCOR_ENABLE_NRD
    // frame and NRD
    #include <Falcor.h>
    using namespace Falcor;
    #include <NRD/Include/NRD.h>

    // graphic API
    #include "Core/API/Shared/D3D12DescriptorSet.h"
    #include "Core/API/Shared/D3D12RootSignature.h"

    class NRDPass : public RenderPass
    {
    public:
        // for frame
        using SharedPtr = std::shared_ptr<NRDPass>;
        static const Info kInfo;
        static SharedPtr create(RenderContext* pRenderContext = nullptr, const Dictionary& dict = {});
        virtual Dictionary getScriptingDictionary() override;
        virtual RenderPassReflection reflect(const CompileData& compileData) override;
        virtual void compile(RenderContext* pRenderContext, const CompileData& compileData) override;
        virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
        virtual void renderUI(Gui::Widgets& widget) override;
        virtual void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override;
        enum class DenoisingMethod : uint32_t
        {
            // Method1 : Relax
            RelaxDiffuse,
            RelaxDiffuseSpecular,
            // Method2 : Reblur
            ReblurDiffuseSpecular,
            // Motion Vector
            SpecularReflectionMv,
            SpecularDeltaMv
        };

    private:
        // --------- NRD ---------
        // NRD Denoiser & Method
        nrd::Denoiser* mpDenoiser = nullptr;
        DenoisingMethod mDenoisingMethod = DenoisingMethod::RelaxDiffuseSpecular;

        // NRD Settings
        nrd::CommonSettings mCommonSettings = {};
        nrd::RelaxDiffuseSettings mRelaxDiffuseSettings = {};
        nrd::RelaxDiffuseSpecularSettings mRelaxDiffuseSpecularSettings = {};
        nrd::ReblurSettings mReblurSettings = {};

        bool mEnabled = true;
        bool mRecreateDenoiser = false;
        bool mWorldSpaceMotion = true;
        float mMaxIntensity = 1000.f;
        float mDisocclusionThreshold = 2.f;

        // ----------- Falcor -------------
        // Frame Parameter
        uint2 mScreenSize;
        uint32_t mFrameIndex = 0;
        Scene::SharedPtr mpScene;
        // Falcor Settings
        Falcor::Buffer::SharedPtr mpConstantBuffer;
        Falcor::D3D12DescriptorSet::SharedPtr mpSamplersDescriptorSet;

        std::vector<Falcor::Sampler::SharedPtr> mpSamplers;
        std::vector<Falcor::Texture::SharedPtr> mpPermanentTextures;
        std::vector<Falcor::Texture::SharedPtr> mpTransientTextures;
        std::vector<Falcor::D3D12RootSignature::SharedPtr> mpRootSignatures;
        std::vector<Falcor::D3D12DescriptorSet::Layout> mCBVSRVUAVdescriptorSetLayouts;

        std::vector<ComputePass::SharedPtr> mpPasses;
        std::vector<ComputeStateObject::SharedPtr> mpCSOs;
        std::vector<ProgramKernels::SharedConstPtr> mpCachedProgramKernels;

        // Falcor API
        ComputePass::SharedPtr mpPackRadiancePassRelax;
        ComputePass::SharedPtr mpPackRadiancePassReblur;

        // Math Lib
        glm::mat4x4 mPrevViewMatrix;
        glm::mat4x4 mPrevProjMatrix;

        // ------------- Functions ------------
        NRDPass(const Dictionary& dict);
        void reinit();
        void createPipelines();
        void createResources();
        void executeInternal(RenderContext* pRenderContext, const RenderData& renderData);
        void dispatch(RenderContext* pRenderContext, const RenderData& renderData, const nrd::DispatchDesc& dispatchDesc);

    };

#endif

