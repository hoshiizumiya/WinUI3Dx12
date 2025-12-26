#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <array>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

class DirectX12Renderer
{
public:
    DirectX12Renderer() = default;
    ~DirectX12Renderer();

    void Initialize(const winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel& panel);
    void Render();
    void OnResize(UINT width, UINT height);

private:
    static constexpr UINT FrameCount = 2;

    struct FrameResource
    {
        ::Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator;
        UINT64 FenceValue = 0;
    };

private:
    // --- Initialize steps ---
    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain(const winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel& panel);
    void CreateDescriptorHeaps();
    void CreateRenderTargets();
    void CreatePipelineState();
    void CreateVertexBuffer();
    void CreateSyncObjects();

    // --- Each frame ---
    void PopulateCommandList();
    void MoveToNextFrame();
    void WaitForGpu();


private:
    // --- DX12 Core ---
    ::Microsoft::WRL::ComPtr<ID3D12Device>        m_device;
    ::Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    ::Microsoft::WRL::ComPtr<IDXGISwapChain3>    m_swapChain;
    
    ::Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    std::array<FrameResource, FrameCount> m_frameResources;
    UINT m_frameIndex = 0;

    // --- Render Targets ---
    ::Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    std::array<::Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> m_renderTargets;
    UINT m_rtvDescriptorSize = 0;

    // --- Pipeline ---
    ::Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    ::Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;

    // --- Geometry ---
    ::Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};

    // --- Sync ---
    ::Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_currentFenceValue = 0;
};
