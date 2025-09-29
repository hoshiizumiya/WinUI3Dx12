#pragma once

#include "MainWindow.g.h"
#include <memory>

class DirectX12Renderer; // forward declaration

namespace winrt::WinUI3Dx12::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

    private:
        std::unique_ptr<DirectX12Renderer> m_renderer;
    public:
        void Grid_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    };
}

namespace winrt::WinUI3Dx12::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
