#include "pch.h"
#include "App.h"
#include "Strings.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\DashMaid.Toolbar");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        return 0;
    }

    int code = 1;
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        SetCurrentProcessExplicitAppUserModelID(L"DashMaid.Toolbar");
        dm::App app;
        if (app.Initialize(instance))
        {
            code = app.Run();
        }
    }
    catch (const winrt::hresult_error& e)
    {
        const std::wstring text = std::wstring(dm::strings::StartFailed) + L"\n\n" + std::wstring(e.message());
        MessageBoxW(nullptr, text.c_str(), dm::strings::AppName, MB_ICONERROR);
    }

    if (mutex)
    {
        CloseHandle(mutex);
    }
    return code;
}
