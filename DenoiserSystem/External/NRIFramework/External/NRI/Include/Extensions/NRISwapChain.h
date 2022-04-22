/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

namespace nri
{
    struct SwapChain;
    struct Display;

    enum class SwapChainFormat : uint16_t
    {
        // BT.709 - LDR, https://en.wikipedia.org/wiki/Rec._709
        // BT.2020 - HDR, https://en.wikipedia.org/wiki/Rec._2020
        // G10 - linear (gamma 1.0)
        // G22 - sRGB (gamma ~2.2)
        // G2084 - SMPTE ST.2084 (Perceptual Quantization)

        BT709_G10_8BIT,
        BT709_G10_16BIT,
        BT709_G22_8BIT,
        BT709_G22_10BIT,
        BT2020_G2084_10BIT,
        MAX_NUM
    };

    enum class WindowSystemType : uint8_t
    {
        WINDOWS,
        X11,
        WAYLAND,
        MAX_NUM
    };

    struct WindowsWindow
    {
        void* hwnd; // HWND
    };

    struct X11Window
    {
        void* dpy; // Display*
        uint64_t window; // Window
    };

    struct WaylandWindow
    {
        void* display; // wl_display*
        void* surface; // wl_surface*
    };

    union Window
    {
        WindowsWindow windows;
        X11Window x11;
        WaylandWindow wayland;
    };

    // SwapChain buffers will be created as "color attachment" resources
    struct SwapChainDesc
    {
        WindowSystemType windowSystemType;
        Window window;
        const CommandQueue* commandQueue;
        uint16_t width;
        uint16_t height;
        uint16_t textureNum;
        SwapChainFormat format;
        uint32_t verticalSyncInterval;
        uint32_t physicalDeviceIndex;
        Display* display;
    };

    struct HdrMetadata
    {
        float displayPrimaryRed[2];
        float displayPrimaryGreen[2];
        float displayPrimaryBlue[2];
        float whitePoint[2];
        float luminanceMax;
        float luminanceMin;
        float contentLightLevelMax;
        float frameAverageLightLevelMax;
    };

    struct SwapChainInterface
    {
        Result (NRI_CALL *CreateSwapChain)(Device& device, const SwapChainDesc& swapChainDesc, SwapChain*& swapChain);
        void (NRI_CALL *DestroySwapChain)(SwapChain& swapChain);
        void (NRI_CALL *SetSwapChainDebugName)(SwapChain& swapChain, const char* name);
        Texture* const* (NRI_CALL *GetSwapChainTextures)(const SwapChain& swapChain, uint32_t& textureNum, Format& format);
        uint32_t (NRI_CALL *AcquireNextSwapChainTexture)(SwapChain& swapChain, QueueSemaphore& textureReadyForRender);
        Result (NRI_CALL *SwapChainPresent)(SwapChain& swapChain, QueueSemaphore& textureReadyForPresent);
        Result (NRI_CALL *SetSwapChainHdrMetadata)(SwapChain& swapChain, const HdrMetadata& hdrMetadata);
        Result (NRI_CALL *GetDisplays)(Device& device, Display** displays, uint32_t& displayNum);
        Result (NRI_CALL *GetDisplaySize)(Device& device, Display& display, uint16_t& width, uint16_t& height);
    };
}
