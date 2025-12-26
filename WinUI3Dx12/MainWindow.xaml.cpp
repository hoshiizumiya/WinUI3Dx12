#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"
#include "DirectX12Renderer.h"

using namespace winrt;
using namespace winrt::Windows::Foundation; // Cause of DxAPI is also use Microsoft/winrt root namespace, we must use full namespace path to avoid ambiguity.
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::WinUI3Dx12::implementation
{
	MainWindow::MainWindow()
	{
		// winui3 window doesn't have onLoaded event, so we deal with it in Grid_Loaded event
		InitializeComponent();
		MainWindowSwapChainPanel().SizeChanged([this](auto const&, auto const& args)
			{
				auto size = args.NewSize();
				if (size.Width > 0 && size.Height > 0 && m_renderer != nullptr)
				{
					m_renderer->OnResize(
						static_cast<UINT>(size.Width),
						static_cast<UINT>(size.Height));
				}
			});
	}

	MainWindow::~MainWindow()
	{
		m_renderer.reset();
	}

	// begin to work
	void MainWindow::Grid_Loaded(winrt::Windows::Foundation::IInspectable const& /*sender*/, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& /*e*/)
	{
		m_renderer = std::make_unique<DirectX12Renderer>();
		m_renderer->Initialize(MainWindowSwapChainPanel());
		m_renderer->OnResize(static_cast<UINT>(MainWindowSwapChainPanel().ActualWidth()), static_cast<UINT>(MainWindowSwapChainPanel().ActualHeight()));

		m_renderTimer = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
		m_renderTimer.Interval(std::chrono::milliseconds(16));

		m_renderTimer.Tick([this](auto&&, auto&&)
			{
				if (m_renderer)
				{
					m_renderer->Render();
				}
			});

		m_renderTimer.Start();

	}



}