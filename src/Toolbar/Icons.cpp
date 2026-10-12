#include "pch.h"
#include "Icons.h"

namespace dm
{
    namespace
    {
        // Weighted mean of the opaque pixels. Saturated pixels get more weight,
        // so the result is the color that a person sees as the icon color.
        Rgb MainColor(IWICBitmapSource* src)
        {
            UINT w = 0, h = 0;
            if (FAILED(src->GetSize(&w, &h)) || w == 0 || h == 0)
            {
                return { 100, 116, 139 };
            }
            std::vector<BYTE> px(static_cast<size_t>(w) * h * 4);
            if (FAILED(src->CopyPixels(nullptr, w * 4, static_cast<UINT>(px.size()), px.data())))
            {
                return { 100, 116, 139 };
            }
            double sr = 0, sg = 0, sb = 0, sw = 0;
            for (size_t i = 0; i < px.size(); i += 4)
            {
                const double a = px[i + 3] / 255.0;
                if (a < 0.5)
                {
                    continue;
                }
                const double b = px[i] / 255.0 / a, g = px[i + 1] / 255.0 / a, r = px[i + 2] / 255.0 / a;
                const double mx = std::max({ r, g, b }), mn = std::min({ r, g, b });
                const double sat = mx > 0 ? (mx - mn) / mx : 0;
                const double weight = a * (0.04 + sat * sat) * (mx > 0.12 ? 1.0 : 0.1);
                sr += r * weight;
                sg += g * weight;
                sb += b * weight;
                sw += weight;
            }
            if (sw < 1e-3)
            {
                return { 100, 116, 139 };
            }
            auto ch = [&](double v) { return static_cast<float>(std::clamp(v / sw, 0.0, 1.0) * 255.0); };
            return { ch(sr), ch(sg), ch(sb) };
        }

        winrt::com_ptr<IWICBitmap> StockIcon(IWICImagingFactory* wic)
        {
            winrt::com_ptr<IWICBitmap> bmp;
            SHSTOCKICONINFO sii{ sizeof(sii) };
            if (SUCCEEDED(SHGetStockIconInfo(SIID_APPLICATION, SHGSI_ICON | SHGSI_LARGEICON, &sii)))
            {
                wic->CreateBitmapFromHICON(sii.hIcon, bmp.put());
                DestroyIcon(sii.hIcon);
            }
            return bmp;
        }
    }

    std::wstring ExpandPath(const std::wstring& path)
    {
        const DWORD n = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
        if (n == 0)
        {
            return path;
        }
        std::wstring out(n, L'\0');
        ExpandEnvironmentStringsW(path.c_str(), out.data(), n);
        out.resize(n - 1);
        return out;
    }

    IconData LoadAppIcon(IWICImagingFactory* wic, const std::wstring& path, UINT sizePx)
    {
        winrt::com_ptr<IWICBitmap> raw;
        winrt::com_ptr<IShellItemImageFactory> factory;
        const std::wstring full = ExpandPath(path);
        if (SUCCEEDED(SHCreateItemFromParsingName(full.c_str(), nullptr, IID_PPV_ARGS(factory.put()))))
        {
            HBITMAP hbm = nullptr;
            const SIZE size{ static_cast<LONG>(sizePx), static_cast<LONG>(sizePx) };
            if (SUCCEEDED(factory->GetImage(size, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbm)))
            {
                wic->CreateBitmapFromHBITMAP(hbm, nullptr, WICBitmapUsePremultipliedAlpha, raw.put());
                DeleteObject(hbm);
            }
        }
        if (!raw)
        {
            raw = StockIcon(wic);
        }

        IconData data;
        if (!raw)
        {
            data.main = { 100, 116, 139 };
            return data;
        }

        winrt::com_ptr<IWICFormatConverter> conv;
        winrt::check_hresult(wic->CreateFormatConverter(conv.put()));
        winrt::check_hresult(conv->Initialize(raw.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        winrt::com_ptr<IWICBitmap> cached;
        winrt::check_hresult(wic->CreateBitmapFromSource(conv.get(), WICBitmapCacheOnLoad, cached.put()));

        data.bitmap = cached.as<IWICBitmapSource>();
        data.main = MainColor(data.bitmap.get());
        return data;
    }
}
