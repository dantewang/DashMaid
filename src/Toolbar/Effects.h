#pragma once

namespace dm
{
    // A Direct2D effect description for CompositionEffectBrush. It does the
    // work of the Win2D effect classes, so the app does not need Win2D.
    // The properties are in the order of the Direct2D property indexes.
    struct D2DEffect : winrt::implements<D2DEffect, winrt::Windows::Graphics::Effects::IGraphicsEffect,
                                         winrt::Windows::Graphics::Effects::IGraphicsEffectSource,
                                         ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop>
    {
        D2DEffect(const GUID& id, std::vector<winrt::Windows::Foundation::IInspectable> properties,
                  std::vector<winrt::Windows::Graphics::Effects::IGraphicsEffectSource> sources);

        winrt::hstring Name() { return name_; }
        void Name(const winrt::hstring& name) { name_ = name; }

        STDMETHODIMP GetEffectId(GUID* id) override;
        STDMETHODIMP GetNamedPropertyMapping(LPCWSTR name, UINT* index,
                                             ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) override;
        STDMETHODIMP GetPropertyCount(UINT* count) override;
        STDMETHODIMP GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) override;
        STDMETHODIMP GetSource(UINT index, ABI::Windows::Graphics::Effects::IGraphicsEffectSource** source) override;
        STDMETHODIMP GetSourceCount(UINT* count) override;

    private:
        GUID id_;
        winrt::hstring name_;
        std::vector<winrt::Windows::Foundation::IInspectable> properties_;
        std::vector<winrt::Windows::Graphics::Effects::IGraphicsEffectSource> sources_;
    };

    // Gaussian blur with the standard deviation 'sigma' in pixels. The edges repeat the border pixels.
    winrt::Windows::Graphics::Effects::IGraphicsEffect BlurEffect(
        float sigma, const winrt::Windows::Graphics::Effects::IGraphicsEffectSource& source);

    // CSS saturate(amount).
    winrt::Windows::Graphics::Effects::IGraphicsEffect SaturateEffect(
        float amount, const winrt::Windows::Graphics::Effects::IGraphicsEffectSource& source);
}
