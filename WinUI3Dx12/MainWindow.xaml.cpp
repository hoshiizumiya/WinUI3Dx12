// MainWindow.xaml.cpp
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
        InitializeComponent();
        // Loaded({ this, &MainWindow::OnLoaded });
    }

    //void MainWindow::OnLoaded(IInspectable const&, RoutedEventArgs const&)
    //{
    //}

    MainWindow::~MainWindow()
    {
        m_renderer.reset();
    }

}
void winrt::WinUI3Dx12::implementation::MainWindow::Grid_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e)
{
        m_renderer = std::make_unique<DirectX12Renderer>();
        m_renderer->Initialize(MainWindowSwapChainPanel());

}
