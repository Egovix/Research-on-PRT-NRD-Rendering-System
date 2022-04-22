/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SampleBase.h"

#if __linux__
    #include <csignal>
#endif

#include <array>
#include <thread>

#define CLARG_START(argv, argc, arg, condition)     { size_t argLen = helper::GetCountOf(arg) - 1; int32_t j = 0; for (; j < argc && strncmp(argv[j], arg, argLen); j++); const char* _pArg = j == argc ? nullptr : argv[j] + argLen; if (_pArg || (condition)) {
#define CLARG_IF_VALUE(value)                       !strncmp(_pArg, value, strlen(value))
#define CLARG_TO_UINT                               (uint32_t)atoi(_pArg)
#define CLARG_TO_UINT64                             (uint64_t)_atoi64(_pArg)
#define CLARG_TO_FLOAT                              (float)atof(_pArg)
#define CLARG_END                                   }}

template<typename T> constexpr void MaybeUnused([[maybe_unused]] const T& arg)
{}

constexpr std::array<const char*, (size_t)nri::GraphicsAPI::MAX_NUM> g_GraphicsAPI =
{
    "D3D11",
    "D3D12",
    "VULKAN"
};

constexpr uint64_t STREAM_BUFFER_SIZE = 8 * 1024 * 1024;

//==================================================================================================================================================
// MEMORY
//==================================================================================================================================================

#if _WIN32
void* __CRTDECL operator new(size_t size)
{
    return _aligned_malloc(size, DEFAULT_MEMORY_ALIGNMENT);
}

void* __CRTDECL operator new[](size_t size)
{
    return _aligned_malloc(size, DEFAULT_MEMORY_ALIGNMENT);
}

void __CRTDECL operator delete(void* p) noexcept
{
    _aligned_free(p);
}

void __CRTDECL operator delete[](void* p) noexcept
{
    _aligned_free(p);
}
#endif

//==================================================================================================================================================
// GLFW CALLBACKS
//==================================================================================================================================================

static void GLFW_ErrorCallback(int32_t error, const char* message)
{
    printf("GLFW error[%d]: %s\n", error, message);
#if _WIN32
    DebugBreak();
#else
    raise(SIGTRAP);
#endif
}

static void GLFW_KeyCallback(GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods)
{
    MaybeUnused(scancode);
    MaybeUnused(mods);

    SampleBase* p = (SampleBase*)glfwGetWindowUserPointer(window);

    if( key < 0 )
        return;

    p->m_KeyState[key] = action != GLFW_RELEASE;
    if (action != GLFW_RELEASE)
        p->m_KeyToggled[key] = true;

    if (p->HasUserInterface())
    {
        ImGuiIO& io = ImGui::GetIO();
        if (action == GLFW_PRESS)
            io.KeysDown[key] = true;
        if (action == GLFW_RELEASE)
            io.KeysDown[key] = false;
    }
}

static void GLFW_CharCallback(GLFWwindow* window, uint32_t codepoint)
{
    SampleBase* p = (SampleBase*)glfwGetWindowUserPointer(window);

    if (p->HasUserInterface())
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharacter(codepoint);
    }
}

static void GLFW_ButtonCallback(GLFWwindow* window, int32_t button, int32_t action, int32_t mods)
{
    MaybeUnused(mods);

    SampleBase* p = (SampleBase*)glfwGetWindowUserPointer(window);

    p->m_ButtonState[button] = action != GLFW_RELEASE;
    p->m_ButtonJustPressed[button] = action != GLFW_RELEASE;
}

static void GLFW_CursorPosCallback(GLFWwindow* window, double x, double y)
{
    SampleBase* p = (SampleBase*)glfwGetWindowUserPointer(window);

    float2 curPos = float2(float(x), float(y));
    p->m_MouseDelta = curPos - p->m_MousePosPrev;
}

static void GLFW_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    SampleBase* p = (SampleBase*)glfwGetWindowUserPointer(window);

    p->m_MouseWheel = (float)yoffset;

    if (p->HasUserInterface())
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseWheelH += (float)xoffset;
        io.MouseWheel += (float)yoffset;
    }
}

//==================================================================================================================================================
// SAMPLE BASE
//==================================================================================================================================================

SampleBase::~SampleBase()
{
    glfwTerminate();
}

nri::WindowSystemType SampleBase::GetWindowSystemType() const
{
#if _WIN32
    return nri::WindowSystemType::WINDOWS;
#else
    return nri::WindowSystemType::X11;
#endif
}

const nri::Window& SampleBase::GetWindow() const
{
    return m_NRIWindow;
}

void SampleBase::GetCameraDescFromInputDevices(CameraDesc& cameraDesc)
{
    if (!IsButtonPressed(Button::Right))
    {
        CursorMode(GLFW_CURSOR_NORMAL);
        return;
    }

    CursorMode(GLFW_CURSOR_DISABLED);

    if (GetMouseWheel() > 0.0f)
        m_Camera.state.motionScale *= 1.1f;
    else if (GetMouseWheel() < 0.0f)
        m_Camera.state.motionScale /= 1.1f;

    float motionScale = m_Camera.state.motionScale;

    float2 mouseDelta = GetMouseDelta();
    cameraDesc.dYaw = -mouseDelta.x;
    cameraDesc.dPitch = -mouseDelta.y;

    if (IsKeyPressed(Key::Right))
        cameraDesc.dYaw -= motionScale;
    if (IsKeyPressed(Key::Left))
        cameraDesc.dYaw += motionScale;

    if (IsKeyPressed(Key::Up))
        cameraDesc.dPitch += motionScale;
    if (IsKeyPressed(Key::Down))
        cameraDesc.dPitch -= motionScale;

    if (IsKeyPressed(Key::W))
        cameraDesc.dLocal.z += motionScale;
    if (IsKeyPressed(Key::S))
        cameraDesc.dLocal.z -= motionScale;
    if (IsKeyPressed(Key::D))
        cameraDesc.dLocal.x += motionScale;
    if (IsKeyPressed(Key::A))
        cameraDesc.dLocal.x -= motionScale;
    if (IsKeyPressed(Key::E))
        cameraDesc.dLocal.y += motionScale;
    if (IsKeyPressed(Key::Q))
        cameraDesc.dLocal.y -= motionScale;
}

bool SampleBase::CreateUserInterface(nri::Device& device, const nri::CoreInterface& coreInterface, const nri::HelperInterface& helperInterface, uint32_t windowWidth, uint32_t windowHeight, nri::Format renderTargetFormat)
{
    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameBorderSize = 1;
    style.WindowBorderSize = 1;

    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors; // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos; // We can honor io.WantSetMousePos requests (optional, rarely used)
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)windowWidth, (float)windowHeight);

    #if defined(_WIN32)
        io.ImeWindowHandle = glfwGetWin32Window(m_Window);
    #endif

    io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
    io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
    io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
    io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
    io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
    io.KeyMap[ImGuiKey_Insert] = GLFW_KEY_INSERT;
    io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
    io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
    io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
    io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
    io.KeyMap[ImGuiKey_KeyPadEnter] = GLFW_KEY_KP_ENTER;
    io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
    io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
    io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
    io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
    io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
    io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;

    m_MouseCursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    m_MouseCursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    m_MouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);   // FIXME: GLFW doesn't have this.
    m_MouseCursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    m_MouseCursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    m_MouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);  // FIXME: GLFW doesn't have this.
    m_MouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);  // FIXME: GLFW doesn't have this.
    m_MouseCursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

    // Rendering
    m_Device = &device;
    NRI = &coreInterface;
    m_Helper = &helperInterface;

    const nri::DeviceDesc& deviceDesc = NRI->GetDeviceDesc(device);

    // Pipeline
    {
        nri::StaticSamplerDesc staticSamplerDesc = {};
        staticSamplerDesc.samplerDesc.anisotropy = 1;
        staticSamplerDesc.samplerDesc.addressModes = {nri::AddressMode::REPEAT, nri::AddressMode::REPEAT};
        staticSamplerDesc.samplerDesc.magnification = nri::Filter::LINEAR;
        staticSamplerDesc.samplerDesc.minification = nri::Filter::LINEAR;
        staticSamplerDesc.samplerDesc.mip = nri::Filter::LINEAR;
        staticSamplerDesc.registerIndex = 0;
        staticSamplerDesc.visibility = nri::ShaderStage::FRAGMENT;

        nri::DescriptorRangeDesc descriptorRange = {0, 1, nri::DescriptorType::TEXTURE, nri::ShaderStage::FRAGMENT};

        nri::DescriptorSetDesc descriptorSet = {&descriptorRange, 1, &staticSamplerDesc, 1};

        nri::PushConstantDesc pushConstant = {};
        pushConstant.registerIndex = 0;
        pushConstant.size = 8;
        pushConstant.visibility = nri::ShaderStage::VERTEX;

        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = 1;
        pipelineLayoutDesc.descriptorSets = &descriptorSet;
        pipelineLayoutDesc.pushConstantNum = 1;
        pipelineLayoutDesc.pushConstants = &pushConstant;
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::VERTEX | nri::PipelineLayoutShaderStageBits::FRAGMENT;

        if (NRI->CreatePipelineLayout(device, pipelineLayoutDesc, m_PipelineLayout) != nri::Result::SUCCESS)
            return false;

        utils::ShaderCodeStorage shaderCodeStorage;
        nri::ShaderDesc shaderStages[] =
        {
            utils::LoadShader(deviceDesc.graphicsAPI, "ImGUI.vs", shaderCodeStorage),
            utils::LoadShader(deviceDesc.graphicsAPI, "ImGUI.fs", shaderCodeStorage),
        };

        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;
        vertexStreamDesc.stride = sizeof(ImDrawVert);

        nri::VertexAttributeDesc vertexAttributeDesc[3] = {};
        {
            vertexAttributeDesc[0].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[0].streamIndex = 0;
            vertexAttributeDesc[0].offset = helper::GetOffsetOf(&ImDrawVert::pos);
            vertexAttributeDesc[0].d3d = {"POSITION", 0};
            vertexAttributeDesc[0].vk = {0};

            vertexAttributeDesc[1].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[1].streamIndex = 0;
            vertexAttributeDesc[1].offset = helper::GetOffsetOf(&ImDrawVert::uv);
            vertexAttributeDesc[1].d3d = {"TEXCOORD", 0};
            vertexAttributeDesc[1].vk = {1};

            vertexAttributeDesc[2].format = nri::Format::RGBA8_UNORM;
            vertexAttributeDesc[2].streamIndex = 0;
            vertexAttributeDesc[2].offset = helper::GetOffsetOf(&ImDrawVert::col);
            vertexAttributeDesc[2].d3d = {"COLOR", 0};
            vertexAttributeDesc[2].vk = {2};
        }

        nri::InputAssemblyDesc inputAssemblyDesc = {};
        inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;
        inputAssemblyDesc.attributes = vertexAttributeDesc;
        inputAssemblyDesc.attributeNum = (uint8_t)helper::GetCountOf(vertexAttributeDesc);
        inputAssemblyDesc.streams = &vertexStreamDesc;
        inputAssemblyDesc.streamNum = 1;

        nri::RasterizationDesc rasterizationDesc = {};
        rasterizationDesc.viewportNum = 1;
        rasterizationDesc.fillMode = nri::FillMode::SOLID;
        rasterizationDesc.cullMode = nri::CullMode::NONE;
        rasterizationDesc.sampleNum = 1;
        rasterizationDesc.sampleMask = 0xFFFF;

        nri::ColorAttachmentDesc colorAttachmentDesc = {};
        colorAttachmentDesc.format = renderTargetFormat;
        colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;
        colorAttachmentDesc.blendEnabled = true;
        colorAttachmentDesc.colorBlend = {nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendFunc::ADD};
        colorAttachmentDesc.alphaBlend = {nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendFactor::ZERO, nri::BlendFunc::ADD};

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.color = &colorAttachmentDesc;

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
        graphicsPipelineDesc.inputAssembly = &inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = &rasterizationDesc;
        graphicsPipelineDesc.outputMerger = &outputMergerDesc;
        graphicsPipelineDesc.shaderStages = shaderStages;
        graphicsPipelineDesc.shaderStageNum = helper::GetCountOf(shaderStages);

        if (NRI->CreateGraphicsPipeline(device, graphicsPipelineDesc, m_Pipeline) != nri::Result::SUCCESS)
            return false;
    }

    int32_t fontTextureWidth = 0, fontTextureHeight = 0;
    uint8_t* fontPixels = nullptr;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontTextureWidth, &fontTextureHeight);

    // Resources
    constexpr nri::Format format = nri::Format::RGBA8_UNORM;
    {
        // Geometry
        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = STREAM_BUFFER_SIZE;
        bufferDesc.usageMask = nri::BufferUsageBits::VERTEX_BUFFER | nri::BufferUsageBits::INDEX_BUFFER;
        if (NRI->CreateBuffer(device, bufferDesc, m_GeometryBuffer) != nri::Result::SUCCESS)
            return false;

        // Texture
        nri::TextureDesc textureDesc = {};
        textureDesc.type = nri::TextureType::TEXTURE_2D;
        textureDesc.format = format;
        textureDesc.size[0] = (uint16_t)fontTextureWidth;
        textureDesc.size[1] = (uint16_t)fontTextureHeight;
        textureDesc.size[2] = 1;
        textureDesc.mipNum = 1;
        textureDesc.arraySize = 1;
        textureDesc.sampleNum = 1;
        textureDesc.usageMask = nri::TextureUsageBits::SHADER_RESOURCE;
        if (NRI->CreateTexture(device, textureDesc, m_FontTexture) != nri::Result::SUCCESS)
            return false;
    }

    m_MemoryAllocations.resize(2, nullptr);

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_GeometryBuffer;

    nri::Result result = m_Helper->AllocateAndBindMemory(device, resourceGroupDesc, m_MemoryAllocations.data());
    if (result != nri::Result::SUCCESS)
        return false;

    resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = 1;
    resourceGroupDesc.textures = &m_FontTexture;

    result = m_Helper->AllocateAndBindMemory(device, resourceGroupDesc, m_MemoryAllocations.data() + 1);
    if (result != nri::Result::SUCCESS)
        return false;

    // Descriptor
    {
        nri::Texture2DViewDesc texture2DViewDesc = {m_FontTexture, nri::Texture2DViewType::SHADER_RESOURCE_2D, format};
        if (NRI->CreateTexture2DView(texture2DViewDesc, m_FontShaderResource) != nri::Result::SUCCESS)
            return false;
    }

    utils::Texture texture;
    utils::LoadTextureFromMemory(format, fontTextureWidth, fontTextureHeight, fontPixels, texture);

    nri::CommandQueue* commandQueue = nullptr;
    NRI->GetCommandQueue(device, nri::CommandQueueType::GRAPHICS, commandQueue);

    // Upload data
    {

        nri::TextureSubresourceUploadDesc subresource = {};
        texture.GetSubresource(subresource, 0);

        nri::TextureUploadDesc textureData = {};
        textureData.subresources = &subresource;
        textureData.mipNum = 1;
        textureData.arraySize = 1;
        textureData.texture = m_FontTexture;
        textureData.nextLayout = nri::TextureLayout::SHADER_RESOURCE;
        textureData.nextAccess = nri::AccessBits::SHADER_RESOURCE;

        if ( m_Helper->UploadData(*commandQueue, &textureData, 1, nullptr, 0) != nri::Result::SUCCESS)
            return false;
    }

    // Descriptor pool
    {
        nri::DescriptorPoolDesc descriptorPoolDesc = {};
        descriptorPoolDesc.descriptorSetMaxNum = 1;
        descriptorPoolDesc.textureMaxNum = 1;
        descriptorPoolDesc.staticSamplerMaxNum = 1;

        if (NRI->CreateDescriptorPool(device, descriptorPoolDesc, m_DescriptorPool) != nri::Result::SUCCESS)
            return false;
    }

    // Texture & sampler descriptor set
    {
        if (NRI->AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0, &m_DescriptorSet, 1, nri::WHOLE_DEVICE_GROUP, 0) != nri::Result::SUCCESS)
            return false;

        nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDesc = {};
        descriptorRangeUpdateDesc.descriptorNum = 1;
        descriptorRangeUpdateDesc.descriptors = &m_FontShaderResource;

        NRI->UpdateDescriptorRanges(*m_DescriptorSet, nri::WHOLE_DEVICE_GROUP, 0, 1, &descriptorRangeUpdateDesc);
    }

    m_timePrev = glfwGetTime();

    return true;
}

void SampleBase::DestroyUserInterface()
{
    if (!HasUserInterface())
        return;

    ImGui::DestroyContext();

    if (m_DescriptorPool)
        NRI->DestroyDescriptorPool(*m_DescriptorPool);

    if (m_Pipeline)
        NRI->DestroyPipeline(*m_Pipeline);

    if (m_PipelineLayout)
        NRI->DestroyPipelineLayout(*m_PipelineLayout);

    if (m_FontShaderResource)
        NRI->DestroyDescriptor(*m_FontShaderResource);

    if (m_FontTexture)
        NRI->DestroyTexture(*m_FontTexture);

    if (m_GeometryBuffer)
        NRI->DestroyBuffer(*m_GeometryBuffer);

    for (uint32_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI->FreeMemory(*m_MemoryAllocations[i]);
}

void SampleBase::PrepareUserInterface()
{
    if (!HasUserInterface())
        return;

    ImGuiIO& io = ImGui::GetIO();

    // Setup time step
    double timeCur = glfwGetTime();
    io.DeltaTime = (float)(timeCur - m_timePrev);
    m_timePrev = timeCur;

    // Read keyboard modifiers inputs
    io.KeyCtrl = IsKeyPressed(Key::LControl) || IsKeyPressed(Key::RControl);
    io.KeyShift = IsKeyPressed(Key::LShift) || IsKeyPressed(Key::RShift);
    io.KeyAlt = IsKeyPressed(Key::LAlt) || IsKeyPressed(Key::RAlt);
    io.KeySuper = false;

    // Update buttons
    for (int32_t i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++)
    {
        // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
        io.MouseDown[i] = m_ButtonJustPressed[i] || glfwGetMouseButton(m_Window, i) != 0;
        m_ButtonJustPressed[i] = false;
    }

    // Update mouse position
    if (glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != 0)
    {
        if (io.WantSetMousePos)
            glfwSetCursorPos(m_Window, (double)io.MousePos.x, (double)io.MousePos.y);
        else
        {
            double mouse_x, mouse_y;
            glfwGetCursorPos(m_Window, &mouse_x, &mouse_y);
            io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);
        }
    }

    // Update mouse cursor
    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) == 0 && glfwGetInputMode(m_Window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
    {
        ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
        if (cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
        {
            // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
            CursorMode(GLFW_CURSOR_HIDDEN);
        }
        else
        {
            // Show OS mouse cursor
            glfwSetCursor(m_Window, m_MouseCursors[cursor] ? m_MouseCursors[cursor] : m_MouseCursors[ImGuiMouseCursor_Arrow]);
            CursorMode(GLFW_CURSOR_NORMAL);
        }
    }

    // Start the frame. This call will update the io.WantCaptureMouse, io.WantCaptureKeyboard flag that you can use to dispatch inputs (or not) to your application.
    ImGui::NewFrame();
}

void SampleBase::RenderUserInterface(nri::CommandBuffer& commandBuffer)
{
    if (!HasUserInterface())
        return;

    ImGui::Render();
    const ImDrawData& drawData = *ImGui::GetDrawData();

    // Prepare
    uint32_t vertexDataSize = drawData.TotalVtxCount * sizeof(ImDrawVert);
    uint32_t indexDataSize = drawData.TotalIdxCount * sizeof(ImDrawIdx);
    uint32_t vertexDataSizeAligned = helper::GetAlignedSize(vertexDataSize, 16);
    uint32_t indexDataSizeAligned = helper::GetAlignedSize(indexDataSize, 16);
    uint32_t totalDataSizeAligned = vertexDataSizeAligned + indexDataSizeAligned;
    if (!totalDataSizeAligned)
        return;

    assert(totalDataSizeAligned < STREAM_BUFFER_SIZE / BUFFERED_FRAME_MAX_NUM);
    if (m_StreamBufferOffset + totalDataSizeAligned > STREAM_BUFFER_SIZE)
        m_StreamBufferOffset = 0;

    uint64_t indexBufferOffset = m_StreamBufferOffset;
    uint8_t* indexData = (uint8_t*)NRI->MapBuffer(*m_GeometryBuffer, m_StreamBufferOffset, totalDataSizeAligned);
    uint64_t vertexBufferOffset = indexBufferOffset + indexDataSizeAligned;
    uint8_t* vertexData = indexData + indexDataSizeAligned;

    for (int32_t n = 0; n < drawData.CmdListsCount; n++)
    {
        const ImDrawList& drawList = *drawData.CmdLists[n];

        uint32_t size = drawList.VtxBuffer.Size * sizeof(ImDrawVert);
        memcpy(vertexData, drawList.VtxBuffer.Data, size);
        vertexData += size;

        size = drawList.IdxBuffer.Size * sizeof(ImDrawIdx);
        memcpy(indexData, drawList.IdxBuffer.Data, size);
        indexData += size;
    }

    m_StreamBufferOffset += totalDataSizeAligned;

    NRI->UnmapBuffer(*m_GeometryBuffer);

    float invScreenSize[2];
    invScreenSize[0] = 1.0f / ImGui::GetIO().DisplaySize.x;
    invScreenSize[1] = 1.0f / ImGui::GetIO().DisplaySize.y;

    {
        helper::Annotation(*NRI, commandBuffer, "UserInterface");

        NRI->CmdSetDescriptorPool(commandBuffer, *m_DescriptorPool);
        NRI->CmdSetPipelineLayout(commandBuffer, *m_PipelineLayout);
        NRI->CmdSetPipeline(commandBuffer, *m_Pipeline);
        NRI->CmdSetConstants(commandBuffer, 0, invScreenSize, sizeof(invScreenSize));
        NRI->CmdSetIndexBuffer(commandBuffer, *m_GeometryBuffer, indexBufferOffset, sizeof(ImDrawIdx) == 2 ? nri::IndexType::UINT16 : nri::IndexType::UINT32);
        NRI->CmdSetVertexBuffers(commandBuffer, 0, 1, &m_GeometryBuffer, &vertexBufferOffset);
        NRI->CmdSetDescriptorSets(commandBuffer, 0, 1, &m_DescriptorSet, nullptr);

        const nri::Viewport viewport = { 0.0f, 0.0f, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 0.0f, 1.0f };
        NRI->CmdSetViewports(commandBuffer, &viewport, 1);

        int32_t vertexOffset = 0;
        int32_t indexOffset = 0;
        for (int32_t n = 0; n < drawData.CmdListsCount; n++)
        {
            const ImDrawList& drawList = *drawData.CmdLists[n];
            for (int32_t i = 0; i < drawList.CmdBuffer.Size; i++)
            {
                const ImDrawCmd& drawCmd = drawList.CmdBuffer[i];
                if (drawCmd.UserCallback)
                    drawCmd.UserCallback(&drawList, &drawCmd);
                else
                {
                    nri::Rect rect =
                    {
                        (int32_t)drawCmd.ClipRect.x,
                        (int32_t)drawCmd.ClipRect.y,
                        (uint32_t)(drawCmd.ClipRect.z - drawCmd.ClipRect.x),
                        (uint32_t)(drawCmd.ClipRect.w - drawCmd.ClipRect.y)
                    };
                    NRI->CmdSetScissors(commandBuffer, &rect, 1);

                    NRI->CmdDrawIndexed(commandBuffer, drawCmd.ElemCount, 1, indexOffset, vertexOffset, 0);
                }
                indexOffset += drawCmd.ElemCount;
            }
            vertexOffset += drawList.VtxBuffer.Size;
        }
    }
}

bool SampleBase::Create(const char* windowTitle)
{
    glfwSetErrorCallback(GLFW_ErrorCallback);

    if (!glfwInit())
        return false;

    // Screen size
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    const uint32_t screenW = (uint32_t)mode->width;
    const uint32_t screenH = (uint32_t)mode->height;
    if (m_WindowWidth > screenW)
        m_WindowWidth = screenW;
    if (m_WindowHeight > screenH)
        m_WindowHeight = screenH;
    int32_t decorated = (m_WindowWidth == screenW || m_WindowHeight == screenH) ? 0 : 1;

    // Window creation
    printf("Creating %swindow (%u, %u)\n", decorated ? "" : "borderless ", m_WindowWidth, m_WindowHeight);

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, m_IgnoreDPI ? 1 : 0);
    glfwWindowHint(GLFW_VISIBLE, 0);
    glfwWindowHint(GLFW_DECORATED, decorated);

    char windowName[256];
    snprintf(windowName, sizeof(windowName), "%s [%s]", windowTitle, g_GraphicsAPI[(size_t)m_GraphicsAPI]);

    m_Window = glfwCreateWindow(m_WindowWidth, m_WindowHeight, windowName, NULL, NULL);
    if (!m_Window)
    {
        glfwTerminate();
        return false;
    }

    int32_t x = (screenW - m_WindowWidth) >> 1;
    int32_t y = (screenH - m_WindowHeight) >> 1;
    glfwSetWindowPos(m_Window, x, y);

    // Sample loading
    printf("Loading...\n");

#if _WIN32
    m_NRIWindow.windows.hwnd = glfwGetWin32Window(m_Window);
#elif __linux__
    m_NRIWindow.x11.dpy = glfwGetX11Display();
    m_NRIWindow.x11.window = glfwGetX11Window(m_Window);
#endif

    bool result = Initialize(m_GraphicsAPI);

    // Set callback and show window
    glfwSetWindowUserPointer(m_Window, this);
    glfwSetKeyCallback(m_Window, GLFW_KeyCallback);
    glfwSetCharCallback(m_Window, GLFW_CharCallback);
    glfwSetMouseButtonCallback(m_Window, GLFW_ButtonCallback);
    glfwSetCursorPosCallback(m_Window, GLFW_CursorPosCallback);
    glfwSetScrollCallback(m_Window, GLFW_ScrollCallback);
    glfwShowWindow(m_Window);

    printf("Ready!\n");

    return result;
}

void SampleBase::RenderLoop()
{
    for (uint32_t i = 0; i < m_FrameNum; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));

        glfwPollEvents();

        m_IsActive = glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != 0;
        if (!m_IsActive)
        {
            i--;
            continue;
        }

        if (glfwWindowShouldClose(m_Window))
            break;

        PrepareFrame(i);
        RenderFrame(i);

        double cursorPosx, cursorPosy;
        glfwGetCursorPos(m_Window, &cursorPosx, &cursorPosy);
        m_MousePosPrev = float2(float(cursorPosx), float(cursorPosy));
        m_MouseWheel = 0.0f;
        m_MouseDelta = float2(0.0f);
    }

    printf("Shutting down...\n");
}

void SampleBase::CursorMode(int32_t mode)
{
    if (mode == GLFW_CURSOR_NORMAL)
    {
        glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        #if defined(_WIN32)
            // GLFW works with cursor visibility incorrectly
            for (uint32_t n = 0; ::ShowCursor(1) < 0 && n < 256; n++)
                ;
        #endif
    }
    else
    {
        glfwSetInputMode(m_Window, GLFW_CURSOR, mode);
        #if defined(_WIN32)
            // GLFW works with cursor visibility incorrectly
            for (uint32_t n = 0; ::ShowCursor(0) >= 0 && n < 256; n++)
                ;
        #endif
    }
}

void SampleBase::ParseCommandLine(int32_t argc, char** argv)
{
    CLARG_START(argv, argc, "--help", argc == 1)
        printf
        (
            "Usage:\n"
            "    --help - this message\n"
            "    --api=<D3D11/D3D12/VULKAN>\n"
            "    --width=<window width>\n"
            "    --height=<window height>\n"
            "    --frameNum=<num>\n"
            "    --swapInterval=<0/1>\n"
            "    --dlssQuality=<0/1/2>\n"
            "    --scene=<scene path relative to '_Data/Scenes' folder>\n"
            "    --debugAPI\n"
            "    --debugNRI\n"
            "    --ignoreDPI\n"
            "    --testMode\n\n"
         );
    CLARG_END;

    CLARG_START(argv, argc, "--api=", false)
        if (CLARG_IF_VALUE(g_GraphicsAPI[0]))
            m_GraphicsAPI = nri::GraphicsAPI::D3D11;
        else if (CLARG_IF_VALUE(g_GraphicsAPI[1]))
            m_GraphicsAPI = nri::GraphicsAPI::D3D12;
        else if (CLARG_IF_VALUE(g_GraphicsAPI[2]))
            m_GraphicsAPI = nri::GraphicsAPI::VULKAN;
    CLARG_END;

    CLARG_START(argv, argc, "--width=", false)
        m_WindowWidth = CLARG_TO_UINT;
    CLARG_END;

    CLARG_START(argv, argc, "--height=", false)
        m_WindowHeight = CLARG_TO_UINT;
    CLARG_END;

    CLARG_START(argv, argc, "--frameNum=", false)
        m_FrameNum = CLARG_TO_UINT;
    CLARG_END;

    CLARG_START(argv, argc, "--debugAPI", false)
        m_DebugAPI = true;
    CLARG_END;

    CLARG_START(argv, argc, "--debugNRI", false)
        m_DebugNRI = true;
    CLARG_END;

    CLARG_START(argv, argc, "--ignoreDPI", false)
        m_IgnoreDPI = true;
    CLARG_END;

    CLARG_START(argv, argc, "--testMode", false)
        m_TestMode = true;
    CLARG_END;

    CLARG_START(argv, argc, "--swapInterval=", false)
        m_SwapInterval = CLARG_TO_UINT;
    CLARG_END;

    CLARG_START(argv, argc, "--dlssQuality=", false)
        m_DlssQuality = CLARG_TO_UINT;
    CLARG_END;

    CLARG_START(argv, argc, "--scene=", false)
        m_SceneFile = std::string(_pArg);
    CLARG_END;
}

void SampleBase::EnableMemoryLeakDetection(uint32_t breakOnAllocationIndex)
{
#if( defined(_DEBUG) && defined(_WIN32) )
    int32_t flag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    flag |= _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(flag);

    // https://msdn.microsoft.com/en-us/library/x98tx3cf.aspx
    if (breakOnAllocationIndex)
        _crtBreakAlloc = breakOnAllocationIndex;
#endif
}
