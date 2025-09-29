#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <memory>
#include <vector>
#include "d3dx12.h"

namespace winrt::Microsoft::UI::Xaml::Controls
{
	struct SwapChainPanel;
}

class DirectX12Renderer
{
public:
	void Initialize(const winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel& panel);
	~DirectX12Renderer();

private:
	void CreateDevice();
	void CreateCommandObjects();
	void CreateSwapChain(const winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel& panel);
	void CreateRtvAndDsvDescriptorHeaps();
	void CreateViewportAndScissor();
	void CreateGraphicsPipeline();
	void CreateVertexBuffer();
	void PopulateCommandList();
	void WaitForPreviousFrame();

	// COM 智能指针
	::Microsoft::WRL::ComPtr<ID3D12Device> m_d3d12Device;
	::Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
	::Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	::Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	::Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
	::Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	::Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[2];
	::Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
	::Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	::Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};

	UINT m_rtvDescriptorSize = 0;
	UINT m_frameIndex = 0;
	HANDLE m_fenceEvent = nullptr;
	::Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue = 0;

	float m_aspectRatio = 16.0f / 9.0f;
};