#pragma once

#include "Spring.h"

namespace dm
{
    struct IconData
    {
        winrt::com_ptr<IWICBitmapSource> bitmap;  // 32bppPBGRA
        Rgb main;                                 // Main color, 0..255 per channel.
    };

    // Gets the icon of a file, a folder, or a "shell:AppsFolder\..." item.
    // Uses the generic application icon when the shell cannot supply one.
    IconData LoadAppIcon(IWICImagingFactory* wic, const std::wstring& path, UINT sizePx);

    std::wstring ExpandPath(const std::wstring& path);
}
