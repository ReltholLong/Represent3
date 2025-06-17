#ifndef __D3D_DEVICE_H__
#define __D3D_DEVICE_H__

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// Forward declarations
struct D3DAdapterInfo;
struct D3DDeviceInfo;
struct D3DModeInfo;

class CD3D_Device {
public:
    CD3D_Device() { Reset(); }
    ~CD3D_Device() { FreeAll(); }

    // Creation/Destruction Routines...
    bool CreateDevice(D3DAdapterInfo* pAdapter, D3DDeviceInfo* pDevice, D3DModeInfo* pMode);
    bool SetMode(D3DModeInfo* pMode);
    void FreeDevice();
    bool ReleaseDevObjects();
    bool RestoreDevObjects();
    void Reset();
    void ResetDeviceVars();
    void FreeAll();

    // Accessors
    D3DAdapterInfo* GetAdapterInfo() { return m_pAdapter; }
    D3DDeviceInfo* GetDeviceInfo() { return m_pDevice; }
    D3DModeInfo* GetModeInfo() { return m_pMode; }

    void SetDefaultRenderStates();
    void SetupViewport(uint32 iLeft, uint32 iRight, uint32 iTop, uint32 iBottom, float fMinZ = 0.0f, float fMaxZ = 1.0f);

    // Rendering functions
    static bool Start3D();
    static bool End3D();
    static bool IsIn3D();
    bool ResetDevice();

    // DirectX 12 specific methods
    void WaitForGPU();
    void Present();
    void PopulateCommandList();

    // Debug/Helper Functions
    void ListDeviceCaps();

    // DirectX 12 objects - public for quick access
    ComPtr<ID3D12Device> m_pD3DDevice;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

private:
    // DirectX 12 initialization helpers
    bool InitializeD3D12();
    bool CreateCommandObjects();
    bool CreateSwapChain();
    bool CreateDescriptorHeaps();
    bool CreateRenderTargets();
    void CreateFence();

    // DirectX 12 core objects
    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    // Synchronization objects
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent;
    UINT64 m_fenceValue;

    // Render targets
    static const UINT m_frameCount = 2;
    ComPtr<ID3D12Resource> m_renderTargets[m_frameCount];
    ComPtr<ID3D12Resource> m_depthStencil;
    UINT m_frameIndex;

    // Descriptor sizes
    UINT m_rtvDescriptorSize;
    UINT m_dsvDescriptorSize;

    // Legacy compatibility
    D3DAdapterInfo* m_pAdapter;
    D3DDeviceInfo* m_pDevice;
    D3DModeInfo* m_pMode;
    bool m_bWindowed;
    RECT m_rcViewport;

    // Viewport data - converted to D3D12
    D3D12_VIEWPORT m_viewportData;
    D3D12_RECT m_scissorRect;

    bool m_bIn3D;
};

extern CD3D_Device g_Device;
#define PD3DDEVICE (g_Device.m_pD3DDevice.Get())

// Legacy compatibility function
static ID3D12Device* d3d_GetD3DDevice() { return PD3DDEVICE; }

#endif