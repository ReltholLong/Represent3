#include "precompile.h"
#include "d3d_device.h"
#include "d3d_shell.h"
#include "d3d_utils.h"

// The global D3D Device
CD3D_Device g_Device;

bool CD3D_Device::CreateDevice(D3DAdapterInfo* pAdapter, D3DDeviceInfo* pDevice, D3DModeInfo* pMode)
{
    FreeDevice();

    m_pAdapter = pAdapter;
    m_pDevice = pDevice;
    m_pMode = pMode;

    if (!InitializeD3D12())
    {
        g_DebugLog("[D3DRender]Error: DirectX 12 initialization failed.");
        return false;
    }

    if (!CreateCommandObjects())
    {
        g_DebugLog("[D3DRender]Error: Command objects creation failed.");
        return false;
    }

    if (!CreateSwapChain())
    {
        g_DebugLog("[D3DRender]Error: Swap chain creation failed.");
        return false;
    }

    if (!CreateDescriptorHeaps())
    {
        g_DebugLog("[D3DRender]Error: Descriptor heaps creation failed.");
        return false;
    }

    if (!CreateRenderTargets())
    {
        g_DebugLog("[D3DRender]Error: Render targets creation failed.");
        return false;
    }

    CreateFence();
    SetDefaultRenderStates();

    g_DebugLog("[D3DRender]DirectX 12 device created successfully.");
    return true;
}

bool CD3D_Device::InitializeD3D12()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable debug layer
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    // Create DXGI factory
    if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory))))
    {
        return false;
    }

    // Create device
    ComPtr<IDXGIAdapter1> hardwareAdapter;

    for (UINT adapterIndex = 0; SUCCEEDED(m_factory->EnumAdapters1(adapterIndex, &hardwareAdapter)); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        hardwareAdapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pD3DDevice))))
        {
            break;
        }
    }

    if (!m_pD3DDevice)
    {
        return false;
    }

    return true;
}

bool CD3D_Device::CreateCommandObjects()
{
    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    if (FAILED(m_pD3DDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
    {
        return false;
    }

    // Create command allocator
    if (FAILED(m_pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator))))
    {
        return false;
    }

    // Create command list
    if (FAILED(m_pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList))))
    {
        return false;
    }

    // Close command list (it starts in recording state)
    m_commandList->Close();

    return true;
}

bool CD3D_Device::CreateSwapChain()
{
    // Create swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = m_frameCount;
    swapChainDesc.Width = m_pMode->Width;
    swapChainDesc.Height = m_pMode->Height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    if (FAILED(m_factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        g_hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain)))
    {
        return false;
    }

    // Disable fullscreen transitions
    m_factory->MakeWindowAssociation(g_hWnd, DXGI_MWA_NO_ALT_ENTER);

    if (FAILED(swapChain.As(&m_swapChain)))
    {
        return false;
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    return true;
}

bool CD3D_Device::CreateDescriptorHeaps()
{
    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = m_frameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(m_pD3DDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
    {
        return false;
    }

    m_rtvDescriptorSize = m_pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create DSV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(m_pD3DDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))))
    {
        return false;
    }

    m_dsvDescriptorSize = m_pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    return true;
}

bool CD3D_Device::CreateRenderTargets()
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    // Create render targets
    for (UINT n = 0; n < m_frameCount; n++)
    {
        if (FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n]))))
        {
            return false;
        }

        m_pD3DDevice->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }

    return true;
}

void CD3D_Device::CreateFence()
{
    m_pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_fenceValue = 1;

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void CD3D_Device::SetDefaultRenderStates()
{
    // Setup viewport
    m_viewportData.TopLeftX = 0;
    m_viewportData.TopLeftY = 0;
    m_viewportData.Width = static_cast<float>(m_pMode->Width);
    m_viewportData.Height = static_cast<float>(m_pMode->Height);
    m_viewportData.MinDepth = 0.0f;
    m_viewportData.MaxDepth = 1.0f;

    // Setup scissor rect
    m_scissorRect.left = 0;
    m_scissorRect.top = 0;
    m_scissorRect.right = m_pMode->Width;
    m_scissorRect.bottom = m_pMode->Height;

    g_DebugLog("[D3DRender]Default render states set for DirectX 12.");
}

bool CD3D_Device::Start3D()
{
    if (g_Device.m_bIn3D || !g_Device.m_pD3DDevice)
        return false;

    // Reset command allocator and list
    g_Device.m_commandAllocator->Reset();
    g_Device.m_commandList->Reset(g_Device.m_commandAllocator.Get(), nullptr);

    // Set viewport and scissor
    g_Device.m_commandList->RSSetViewports(1, &g_Device.m_viewportData);
    g_Device.m_commandList->RSSetScissorRects(1, &g_Device.m_scissorRect);

    // Transition render target to render target state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = g_Device.m_renderTargets[g_Device.m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    g_Device.m_commandList->ResourceBarrier(1, &barrier);

    // Set render target
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_Device.m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += g_Device.m_frameIndex * g_Device.m_rtvDescriptorSize;
    g_Device.m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Clear render target
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    g_Device.m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    return (g_Device.m_bIn3D = true);
}

bool CD3D_Device::End3D()
{
    if (!g_Device.m_bIn3D || !g_Device.m_pD3DDevice)
        return false;

    g_Device.m_bIn3D = false;

    // Transition render target to present state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = g_Device.m_renderTargets[g_Device.m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    g_Device.m_commandList->ResourceBarrier(1, &barrier);

    // Close and execute command list
    g_Device.m_commandList->Close();

    ID3D12CommandList* ppCommandLists[] = { g_Device.m_commandList.Get() };
    g_Device.m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    return true;
}

void CD3D_Device::Present()
{
    m_swapChain->Present(1, 0);

    WaitForGPU();

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void CD3D_Device::WaitForGPU()
{
    const UINT64 fence = m_fenceValue;
    m_commandQueue->Signal(m_fence.Get(), fence);
    m_fenceValue++;

    if (m_fence->GetCompletedValue() < fence)
    {
        m_fence->SetEventOnCompletion(fence, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

bool CD3D_Device::IsIn3D()
{
    return g_Device.m_bIn3D;
}

void CD3D_Device::FreeDevice()
{
    WaitForGPU();

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
    }

    ResetDeviceVars();
}

void CD3D_Device::ResetDeviceVars()
{
    m_pD3DDevice.Reset();
    m_commandQueue.Reset();
    m_commandList.Reset();
    m_commandAllocator.Reset();
    m_swapChain.Reset();
    m_factory.Reset();
    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_fence.Reset();

    for (UINT i = 0; i < m_frameCount; i++)
    {
        m_renderTargets[i].Reset();
    }
    m_depthStencil.Reset();

    m_pAdapter = nullptr;
    m_pDevice = nullptr;
    m_pMode = nullptr;
    m_bWindowed = false;
    m_bIn3D = false;
    m_fenceEvent = nullptr;
    m_fenceValue = 0;
    m_frameIndex = 0;
}

void CD3D_Device::Reset()
{
    ResetDeviceVars();

    m_rcViewport.left = 0;
    m_rcViewport.right = 0;
    m_rcViewport.top = 0;
    m_rcViewport.bottom = 0;
}

void CD3D_Device::FreeAll()
{
    FreeDevice();
    Reset();
}

bool CD3D_Device::SetMode(D3DModeInfo* pMode)
{
    // DirectX 12 mode switching would require recreating swap chain
    // For now, return true as a placeholder
    m_pMode = pMode;
    return true;
}

bool CD3D_Device::ResetDevice()
{
    // DirectX 12 device reset would require recreating resources
    // For now, return true as a placeholder
    return true;
}

bool CD3D_Device::ReleaseDevObjects()
{
    return true;
}

bool CD3D_Device::RestoreDevObjects()
{
    return true;
}

void CD3D_Device::SetupViewport(uint32 iLeft, uint32 iRight, uint32 iTop, uint32 iBottom, float fMinZ, float fMaxZ)
{
    if (m_rcViewport.left == iLeft && m_rcViewport.right == iRight &&
        m_rcViewport.top == iTop && m_rcViewport.bottom == iBottom)
        return;

    m_rcViewport.left = iLeft;
    m_rcViewport.right = iRight;
    m_rcViewport.top = iTop;
    m_rcViewport.bottom = iBottom;

    m_viewportData.TopLeftX = static_cast<float>(iLeft);
    m_viewportData.TopLeftY = static_cast<float>(iTop);
    m_viewportData.Width = static_cast<float>(iRight - iLeft);
    m_viewportData.Height = static_cast<float>(iBottom - iTop);
    m_viewportData.MinDepth = fMinZ;
    m_viewportData.MaxDepth = fMaxZ;

    // Update scissor rect as well
    m_scissorRect.left = iLeft;
    m_scissorRect.top = iTop;
    m_scissorRect.right = iRight;
    m_scissorRect.bottom = iBottom;
}

void CD3D_Device::ListDeviceCaps()
{
    if (!m_pD3DDevice || !m_pAdapter || !m_pMode)
        return;

    g_DebugLog("[D3DRender]---------------------------------------------------------------");
    g_DebugLog("[D3DRender]DirectX 12 Device Initialized");
    g_DebugLog("[D3DRender]Width: %d, Height: %d", m_pMode->Width, m_pMode->Height);
    g_DebugLog("[D3DRender]---------------------------------------------------------------");
}