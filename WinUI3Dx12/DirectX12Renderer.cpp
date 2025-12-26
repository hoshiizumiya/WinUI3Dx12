#include "pch.h"
#include "DirectX12Renderer.h"

#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <microsoft.ui.xaml.media.dxinterop.h>
#include "d3dx12.h"

using namespace DirectX;
using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml::Controls;

struct Vertex
{
    XMFLOAT3 Position;
    XMFLOAT4 Color;
};

// ======================================================
// 生命周期
// ======================================================

DirectX12Renderer::~DirectX12Renderer()
{
    WaitForGpu();
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
    }
}

// ======================================================
// 初始化入口
// ======================================================

void DirectX12Renderer::Initialize(const SwapChainPanel& panel)
{
#if defined(_DEBUG)
    {
        ::Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
        }
    }
#endif

CreateDevice();
CreateCommandQueue();
CreateSwapChain(panel);
CreateDescriptorHeaps();
CreateRenderTargets();
CreatePipelineState();
CreateVertexBuffer();
CreateSyncObjects();
}

// ======================================================
// 每帧调用
// ======================================================

void DirectX12Renderer::Render()
{
    PopulateCommandList();

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    m_swapChain->Present(1, 0);
    MoveToNextFrame();
}

// ======================================================
// 初始化实现
// ======================================================

void DirectX12Renderer::CreateDevice()
{
    D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&m_device));
}

void DirectX12Renderer::CreateCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue));

    for (auto& frame : m_frameResources)
    {
        m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&frame.CommandAllocator));
    }

    m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_frameResources[0].CommandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&m_commandList));

    m_commandList->Close();
}

void DirectX12Renderer::CreateSwapChain(const SwapChainPanel& panel)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 500;
    desc.Height = 500;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = FrameCount;
    desc.SampleDesc.Count = 1;

    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    ::Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
        throw winrt::hresult_error(hr, L"CreateDXGIFactory2 failed");

    ::Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForComposition(
        m_commandQueue.Get(),
        &desc,
        nullptr,
        &swapChain1);
    if (FAILED(hr))
        throw winrt::hresult_error(hr, L"CreateSwapChainForComposition failed");

    // 转成 SwapChain3
    ::Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
    hr = swapChain1.As(&swapChain3);
    if (FAILED(hr))
        throw winrt::hresult_error(hr, L"SwapChain1.As<SwapChain3> failed");

    // 保存
    m_swapChain = swapChain3;

    // 绑定到 SwapChainPanel
    auto nativePanel = panel.as<ISwapChainPanelNative>();
    hr = nativePanel->SetSwapChain(swapChain1.Get());
    if (FAILED(hr))
        throw winrt::hresult_error(hr, L"SetSwapChain failed");

    // 初始化帧索引
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DirectX12Renderer::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.NumDescriptors = FrameCount;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvHeap));
    m_rtvDescriptorSize =
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

void DirectX12Renderer::CreateRenderTargets()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(
            m_renderTargets[i].Get(), nullptr, handle);
        handle.Offset(1, m_rtvDescriptorSize);
    }
}

void DirectX12Renderer::CreatePipelineState()
{
    const char* vs =
        "struct VSIn{float3 pos:POSITION;float4 col:COLOR;};"
        "struct PSIn{float4 pos:SV_POSITION;float4 col:COLOR;};"
        "PSIn main(VSIn i){PSIn o;o.pos=float4(i.pos,1);o.col=i.col;return o;}";

    const char* ps =
        "struct PSIn{float4 pos:SV_POSITION;float4 col:COLOR;};"
        "float4 main(PSIn i):SV_Target{return i.col;}";

    ::Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob;
    D3DCompile(vs, strlen(vs), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &vsBlob, nullptr);
    D3DCompile(ps, strlen(ps), nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &psBlob, nullptr);

    CD3DX12_ROOT_SIGNATURE_DESC rootDesc;
    rootDesc.Init(0, nullptr, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ::Microsoft::WRL::ComPtr<ID3DBlob> sig;
    D3D12SerializeRootSignature(
        &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, nullptr);

    m_device->CreateRootSignature(
        0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature));

    D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0 },
        { "COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { layout,_countof(layout) };
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
    pso.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    pso.SampleDesc.Count = 1;

    m_device->CreateGraphicsPipelineState(
        &pso, IID_PPV_ARGS(&m_pipelineState));
}

void DirectX12Renderer::CreateVertexBuffer()
{
    Vertex vertices[] =
    {
        {{0.0f,  0.25f, 0.0f}, {1,0,0,1}},
        {{0.25f, -0.25f,0.0f}, {0,1,0,1}},
        {{-0.25f,-0.25f,0.0f}, {0,0,1,1}}
    };

    const UINT bufferSize = sizeof(vertices);

    // ✅ 关键：显式创建左值对象
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc =
        CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_vertexBuffer));

    if (FAILED(hr))
    {
        OutputDebugStringA("CreateCommittedResource failed\n");
        return;
    }

    void* mappedData = nullptr;
    m_vertexBuffer->Map(0, nullptr, &mappedData);
    memcpy(mappedData, vertices, bufferSize);
    m_vertexBuffer->Unmap(0, nullptr);

    m_vertexBufferView.BufferLocation =
        m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = bufferSize;
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
}

void DirectX12Renderer::CreateSyncObjects()
{
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&m_fence));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

// ======================================================
// 每帧核心
// ======================================================

void DirectX12Renderer::PopulateCommandList()
{
    auto& frame = m_frameResources[m_frameIndex];
    frame.CommandAllocator->Reset();

    m_commandList->Reset(
        frame.CommandAllocator.Get(),
        m_pipelineState.Get());

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    auto desc = m_renderTargets[m_frameIndex]->GetDesc();
    D3D12_VIEWPORT vp{ 0,0,(float)desc.Width,(float)desc.Height,0,1 };
    D3D12_RECT sc{ 0,0,(LONG)desc.Width,(LONG)desc.Height };
    m_commandList->RSSetViewports(1, &vp);
    m_commandList->RSSetScissorRects(1, &sc);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_frameIndex, m_rtvDescriptorSize);

    float clear[4] = { 0,0,0,1 };
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
}

void DirectX12Renderer::MoveToNextFrame()
{
    const UINT64 fenceValue = ++m_currentFenceValue;
    m_commandQueue->Signal(m_fence.Get(), fenceValue);

    auto& frame = m_frameResources[m_frameIndex];
    frame.FenceValue = fenceValue;

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < m_frameResources[m_frameIndex].FenceValue)
    {
        m_fence->SetEventOnCompletion(
            m_frameResources[m_frameIndex].FenceValue,
            m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void DirectX12Renderer::WaitForGpu()
{
    m_commandQueue->Signal(m_fence.Get(), ++m_currentFenceValue);
    m_fence->SetEventOnCompletion(m_currentFenceValue, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
}
void DirectX12Renderer::OnResize(UINT width, UINT height)
{
    WaitForGpu();

    // 释放旧的 RenderTarget
    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_renderTargets[i].Reset();
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    m_swapChain->GetDesc(&desc);

    HRESULT hr = m_swapChain->ResizeBuffers(
        FrameCount,
        width,
        height,
        desc.BufferDesc.Format,
        desc.Flags);

    if (FAILED(hr))
    {
        throw winrt::hresult_error(hr, L"ResizeBuffers failed");
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // 重新创建 RTV
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < FrameCount; ++i)
    {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(
            m_renderTargets[i].Get(), nullptr, handle);
        handle.Offset(1, m_rtvDescriptorSize);
    }
}
