#include "pch.h"
#include "Effects.h"

using Inspectable = winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::PropertyValue;
using winrt::Windows::Graphics::Effects::IGraphicsEffect;
using winrt::Windows::Graphics::Effects::IGraphicsEffectSource;

namespace dm
{
    D2DEffect::D2DEffect(const GUID& id, std::vector<Inspectable> properties, std::vector<IGraphicsEffectSource> sources)
        : id_(id), properties_(std::move(properties)), sources_(std::move(sources))
    {
    }

    STDMETHODIMP D2DEffect::GetEffectId(GUID* id)
    {
        *id = id_;
        return S_OK;
    }

    // No property can be animated.
    STDMETHODIMP D2DEffect::GetNamedPropertyMapping(LPCWSTR, UINT*, ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING*)
    {
        return E_INVALIDARG;
    }

    STDMETHODIMP D2DEffect::GetPropertyCount(UINT* count)
    {
        *count = static_cast<UINT>(properties_.size());
        return S_OK;
    }

    STDMETHODIMP D2DEffect::GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value)
    {
        if (index >= properties_.size())
        {
            return E_INVALIDARG;
        }
        *value = properties_[index].as<ABI::Windows::Foundation::IPropertyValue>().detach();
        return S_OK;
    }

    STDMETHODIMP D2DEffect::GetSource(UINT index, ABI::Windows::Graphics::Effects::IGraphicsEffectSource** source)
    {
        if (index >= sources_.size())
        {
            return E_INVALIDARG;
        }
        *source = sources_[index].as<ABI::Windows::Graphics::Effects::IGraphicsEffectSource>().detach();
        return S_OK;
    }

    STDMETHODIMP D2DEffect::GetSourceCount(UINT* count)
    {
        *count = static_cast<UINT>(sources_.size());
        return S_OK;
    }

    IGraphicsEffect BlurEffect(float sigma, const IGraphicsEffectSource& source)
    {
        return winrt::make<D2DEffect>(CLSID_D2D1GaussianBlur,
                                      std::vector<Inspectable>{
                                          PropertyValue::CreateSingle(sigma),
                                          PropertyValue::CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_QUALITY),
                                          PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD),
                                      },
                                      std::vector<IGraphicsEffectSource>{ source });
    }

    // The matrix of the CSS Filter Effects specification. Each column gives
    // one output channel. Direct2D multiplies the row vector (R, G, B, A, 1).
    IGraphicsEffect SaturateEffect(float s, const IGraphicsEffectSource& source)
    {
        const float m[20] = {
            0.213f + 0.787f * s, 0.213f - 0.213f * s, 0.213f - 0.213f * s, 0.f,
            0.715f - 0.715f * s, 0.715f + 0.285f * s, 0.715f - 0.715f * s, 0.f,
            0.072f - 0.072f * s, 0.072f - 0.072f * s, 0.072f + 0.928f * s, 0.f,
            0.f, 0.f, 0.f, 1.f,
            0.f, 0.f, 0.f, 0.f,
        };
        return winrt::make<D2DEffect>(CLSID_D2D1ColorMatrix,
                                      std::vector<Inspectable>{
                                          PropertyValue::CreateSingleArray(m),
                                          PropertyValue::CreateUInt32(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED),
                                          PropertyValue::CreateBoolean(true),
                                      },
                                      std::vector<IGraphicsEffectSource>{ source });
    }
}
