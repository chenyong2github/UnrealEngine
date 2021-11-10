// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HLMediaPrivate.h"

#include "IMediaTextureSample.h"
#include "MediaUtils/Public/MediaObjectPool.h"
// Fix build error.
#define INTEL_EXTENSIONS 0
#include "D3D11RHIPrivate.h"
#include "D3D11Resources.h"

#define GetRenderTargetFormat GetRenderTargetFormat_D3D12
#define FindShaderResourceDXGIFormat FindShaderResourceDXGIFormat_D3D12
#define FindUnorderedAccessDXGIFormat FindUnorderedAccessDXGIFormat_D3D12
#define FindDepthStencilDXGIFormat FindDepthStencilDXGIFormat_D3D12
#define HasStencilBits HasStencilBits_D3D12
#define FD3DGPUProfiler FD3D12GPUProfiler

#include "D3D12RHIPrivate.h"

#undef GetRenderTargetFormat
#undef FindShaderResourceDXGIFormat
#undef FindUnorderedAccessDXGIFormat
#undef FindDepthStencilDXGIFormat
#undef HasStencilBits
#undef FD3DGPUProfiler

class FHLMediaTextureSample
    : public IMediaTextureSample
    , public IMediaPoolable
{
public:
	FHLMediaTextureSample(ID3D11Texture2D* InTexture, ID3D11ShaderResourceView* InShaderResourceView, HANDLE InSharedTextureHandle)
        : Duration(FTimespan::Zero())
        , Time(FTimespan::Zero())
    {
        D3D11_TEXTURE2D_DESC Desc;
        InTexture->GetDesc(&Desc);

		FString RHIString = FApp::GetGraphicsRHI();
		if (RHIString == TEXT("DirectX 12"))
		{
			// MediaFoundation creates a DX11 texture.  To use this texture with DX12, open the shared handle with the DX12 RHI.
			FD3D12DynamicRHI* DX12RHI = StaticCast<FD3D12DynamicRHI*>(GDynamicRHI);
			TComPtr<ID3D12Resource> sharedMediaTexture;
			if (FAILED(DX12RHI->GetAdapter().GetD3DDevice()->OpenSharedHandle(InSharedTextureHandle, IID_PPV_ARGS(&sharedMediaTexture))))
			{
				UE_LOG(LogHLMediaPlayer, Log, TEXT("ID3D12Device::OpenSharedHandle failed in FHLMediaTextureSample"));
				return;
			}

			Texture = DX12RHI->RHICreateTexture2DFromResource(PF_B8G8R8A8, TexCreate_Dynamic, FClearValueBinding::None, sharedMediaTexture.Get());
		}
		else
		{
			TArray<TRefCountPtr<ID3D11RenderTargetView>> RenderTargetViews;
			Texture = new FD3D11Texture2D(
				GD3D11RHI,
				InTexture,
				InShaderResourceView,
				false,
				RenderTargetViews.Num(),
				RenderTargetViews,
				nullptr,
				Desc.Width,
				Desc.Height,
				0,
				1,
				1,
				PF_B8G8R8A8,
				false,
				TexCreate_None,
				false,
				FClearValueBinding::Transparent);
		}
    }

    virtual ~FHLMediaTextureSample()
    { 
    }

    bool Update(
        FTimespan InTime,
        FTimespan InDuration)
    {
        if (!Texture.IsValid())
        {
            return false;
        }

        Time = InTime;
        Duration = InDuration;

        return true;
    }

    // IMediaTextureSample
    virtual const void* GetBuffer() override
    {
        return nullptr;
    }

    virtual FIntPoint GetDim() const override
    {
        return Texture.IsValid() ? Texture->GetTexture2D()->GetSizeXY() : FIntPoint::ZeroValue;
    }

    virtual FTimespan GetDuration() const override
    {
        return Duration;
    }

    virtual EMediaTextureSampleFormat GetFormat() const override
    {
        return EMediaTextureSampleFormat::CharBGRA;
    }

    virtual FIntPoint GetOutputDim() const override
    {
        return Texture.IsValid() ? Texture->GetTexture2D()->GetSizeXY() : FIntPoint::ZeroValue;
    }

    virtual uint32 GetStride() const override
    {
        return Texture.IsValid() ? Texture->GetTexture2D()->GetSizeX() * 4 : 0;
    }

    virtual FRHITexture* GetTexture() const override
    {
        return Texture;
    }

    virtual FMediaTimeStamp GetTime() const override
    {
        return FMediaTimeStamp(Time);
    }

    virtual bool IsCacheable() const override
    {
        return true;
    }

    virtual bool IsOutputSrgb() const override
    {
        return true;
    }

    virtual void Reset() override
    {
        Time = FTimespan::Zero();
        Duration = FTimespan::Zero();
        Texture = nullptr;
    }

protected:
    /** The sample's texture resource. */
    TRefCountPtr<FRHITexture2D> Texture;

    /** Duration for which the sample is valid. */
    FTimespan Duration;

    /** Sample time. */
    FTimespan Time;
};
