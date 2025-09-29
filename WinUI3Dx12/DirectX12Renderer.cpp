// DirectX12Renderer.cpp
#include "pch.h"
#include "DirectX12Renderer.h"
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "d3dx12.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <windows.ui.xaml.media.dxinterop.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace DirectX;

struct Vertex
{
	XMFLOAT3 position;
	XMFLOAT4 color;
};

void DirectX12Renderer::Initialize(const SwapChainPanel& panel)
{
	CreateDevice();
	CreateCommandObjects();
	CreateSwapChain(panel);
	CreateRtvAndDsvDescriptorHeaps();
	CreateGraphicsPipeline();
	CreateVertexBuffer();
	PopulateCommandList();
}

DirectX12Renderer::~DirectX12Renderer()
{
	WaitForPreviousFrame();
	if (m_fenceEvent) CloseHandle(m_fenceEvent);
}

void DirectX12Renderer::CreateDevice()
{
	D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_d3d12Device));
}

void DirectX12Renderer::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.NodeMask = 0;

	m_d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
	m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
	m_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList));
	m_commandList->Close();
}

void DirectX12Renderer::CreateSwapChain(const SwapChainPanel& panel)
{
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = 0;
	desc.Height = 0;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 2;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

	::Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
	HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory));
	if (FAILED(hr) || !dxgiFactory)
	{
		char buf[128];
		sprintf_s(buf, "CreateDXGIFactory2 failed: 0x%08X\n", static_cast<unsigned>(hr));
		OutputDebugStringA(buf);
		return;
	}

	// Ensure command queue exists before passing it to DXGI
	if (!m_commandQueue)
	{
		OutputDebugStringA("CreateSwapChain: m_commandQueue is null\n");
		return;
	}
	// 
	::Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
	dxgiFactory->CreateSwapChainForComposition(m_commandQueue.Get(), &desc, nullptr, swapChain1.GetAddressOf());
	dxgiFactory->MakeWindowAssociation(nullptr, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);

	swapChain1.As(&m_swapChain);

	// Set swap chain on SwapChainPanel
	auto interop = panel.try_as<ISwapChainPanelNative>();
	if (interop)
	{
		interop->SetSwapChain(m_swapChain.Get());
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DirectX12Renderer::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 2;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	m_d3d12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
	m_rtvDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < 2; i++)
	{
		m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
		m_d3d12Device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);
	}
}

void DirectX12Renderer::CreateGraphicsPipeline()
{
	const char* vsCode = R"(
        struct VSInput { float3 pos : POSITION; float4 color : COLOR; };
        struct PSInput { float4 pos : SV_POSITION; float4 color : COLOR; };
        PSInput main(VSInput input) {
            PSInput output;
            output.pos = float4(input.pos, 1.0f);
            output.color = input.color;
            return output;
        }
    )";

	const char* psCode = R"(
        struct PSInput { float4 pos : SV_POSITION; float4 color : COLOR; };
        float4 main(PSInput input) : SV_Target { return input.color; }
    )";

	ID3DBlob* vsBlob = nullptr, * psBlob = nullptr;
	D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, nullptr);
	D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, nullptr);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	::Microsoft::WRL::ComPtr<ID3DBlob> signature, error;
	D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
	m_d3d12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vsBlob);
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(psBlob);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	m_d3d12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));

	if (vsBlob) vsBlob->Release();
	if (psBlob) psBlob->Release();
}

void DirectX12Renderer::CreateVertexBuffer()
{
	Vertex triangleVertices[] = {
		{ { 0.0f, 0.25f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
		{ { 0.25f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
		{ { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
	};

	const UINT vertexBufferSize = sizeof(triangleVertices);
	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
	m_d3d12Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer));

	UINT8* pVertexDataBegin = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
	memcpy(pVertexDataBegin, triangleVertices, vertexBufferSize);
	m_vertexBuffer->Unmap(0, nullptr);

	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.StrideInBytes = static_cast<UINT>(sizeof(Vertex));
	m_vertexBufferView.SizeInBytes = vertexBufferSize;
}

void DirectX12Renderer::PopulateCommandList()
{
	m_commandAllocator->Reset();
	m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());

	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f };
	m_commandList->RSSetViewports(1, &viewport);
	D3D12_RECT scissor = { 0, 0, LONG_MAX, LONG_MAX };
	m_commandList->RSSetScissorRects(1, &scissor);

	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	m_commandList->ResourceBarrier(1, &barrier);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_commandList->DrawInstanced(3, 1, 0, 0);

	barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);
	m_commandList->ResourceBarrier(1, &barrier);

	m_commandList->Close();
}

void DirectX12Renderer::WaitForPreviousFrame()
{
	m_fenceValue++;
	if (m_fence) {
		m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
		if (m_fence->GetCompletedValue() < m_fenceValue)
		{
			if (!m_fenceEvent) m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}
	if (m_swapChain) m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}