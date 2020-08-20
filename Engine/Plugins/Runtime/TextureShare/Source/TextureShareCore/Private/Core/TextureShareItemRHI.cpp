// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareItemRHI.h"

#if TEXTURESHARECORE_RHI

#include "TextureShareItemBase.h"
#include "TextureShareCoreLog.h"

#include "Misc/ScopeLock.h"

#include "RHI.h"
#include "RHIResources.h"
#include "RHIStaticStates.h"

#include "RenderResource.h"
#include "CommonRenderResources.h"

#include "DirectX/TextureShareDirectX.h"

#include "ITextureShareD3D11.h"
#include "ITextureShareD3D12.h"

namespace TextureShareItem
{
	/*
	 * FTextureShareItemRHI
	 */
	bool FTextureShareItemRHI::FindPixelFormats(DXGI_FORMAT InFormat, TArray<EPixelFormat>& OutPixelFormats)
	{
		for (uint32 PixelFormat = 0; PixelFormat < EPixelFormat::PF_MAX; ++PixelFormat)
		{
			if (GPixelFormats[PixelFormat].Supported && GPixelFormats[PixelFormat].PlatformFormat == InFormat)
			{
				OutPixelFormats.AddUnique((EPixelFormat)PixelFormat);
			}
		}

		return OutPixelFormats.Num() > 0;
	}

	static const FDXGIFormatMap& FindSharedTextureDXGIFormat(DXGI_FORMAT PlatformFormat)
	{
		const FDXGIFormatMap& Map = FTextureShareFormatMapHelper::FindSharedFormat(PlatformFormat);
		return Map;
	}

	static const FDXGIFormatMap& FindSharedTexturePixelFormat(EPixelFormat PixelFormat)
	{
		return FindSharedTextureDXGIFormat((DXGI_FORMAT)GPixelFormats[PixelFormat].PlatformFormat);
	}

	EPixelFormat FTextureShareItemRHI::FindSharedTextureFormat(const FTextureShareSurfaceDesc& InTextureDesc)
	{
		EPixelFormat PixelFormat = (EPixelFormat)InTextureDesc.PixelFormat;
		if (PixelFormat == EPixelFormat::PF_Unknown)
		{
			// get best fit from platform format
			DXGI_FORMAT PlatformFormat = (DXGI_FORMAT)InTextureDesc.PlatformFormat;
			const FDXGIFormatMap Map = FindSharedTextureDXGIFormat(PlatformFormat);

			return Map.PixelFormats[0];
		}

		return PixelFormat;
	}

	bool FTextureShareItemRHI::IsFormatResampleRequired(EPixelFormat SharedFormat, EPixelFormat PixelFormat)
	{
		const FDXGIFormatMap& Map = FindSharedTexturePixelFormat(SharedFormat);
		for (auto& It: Map.PixelFormats)
		{
			if (It == PixelFormat)
			{
				return false;
			}
		}

		return true;
	}

	bool FTextureShareItemRHI::InitializeRHISharedTextureFormat(ETextureShareDevice DeviceType, EPixelFormat InFormat, FTextureShareSurfaceDesc& OutTextureDesc) const
	{
		const FDXGIFormatMap& Map = FindSharedTexturePixelFormat(InFormat);
		if (Map.PixelFormats.Find(InFormat) != INDEX_NONE)
		{
			OutTextureDesc.PixelFormat = InFormat;
		}
		else
		{
			OutTextureDesc.PixelFormat = Map.PixelFormats[0];
		}

		OutTextureDesc.PlatformFormat = Map.UnormFormat;
		return true;
	}

	FSharedRHITexture* FTextureShareItemRHI::GetSharedRHITexture(const FSharedResourceTexture& LocalTextureData)
	{
		if (!SharedRHITextures.Contains(LocalTextureData.Index))
		{
			SharedRHITextures.Add(LocalTextureData.Index, new FSharedRHITexture());
		}

		return SharedRHITextures[LocalTextureData.Index];
	}

	void FTextureShareItemRHI::ReleaseSharedRHITextures()
	{
		for (auto& It : SharedRHITextures)
		{
			delete It.Value;
		}
		SharedRHITextures.Empty();
	}

	/*
	 * FSharedRHITexture
	 */
	bool FSharedRHITexture::Update_RenderThread(const FTextureShareItemBase& ShareItem, const FTextureShareSurfaceDesc& InTextureDesc, const FSharedResourceTexture& TextureData)
	{
		bool     bInTextureValid = InTextureDesc.IsValid();
		bool bCurrentTextureValid = TextureDesc.IsValid();

		// Check if shared texture format changed
		if ((!bInTextureValid && !bCurrentTextureValid) || InTextureDesc.IsEqual(TextureDesc))
		{
			return false;
		}

		// Texture changed. Release previous texture
		Release();

		// Create new valid shared texture
		if (bInTextureValid)
		{
			// Save new texture info
			TextureDesc = InTextureDesc;

			FIntPoint    TextureSize(TextureDesc.Width, TextureDesc.Height);
			EPixelFormat PixelFormat = FTextureShareItemRHI::FindSharedTextureFormat(InTextureDesc);
			check(PixelFormat!= EPixelFormat::PF_Unknown)

			// Create RHI shared texture
			switch(ShareItem.GetDeviceType())
			{

			case ETextureShareDevice::D3D11:
			{
				if (ITextureShareD3D11::Get().CreateSharedTexture(TextureSize, PixelFormat, RHITexture, SharedHandle))
				{
					ID3D11Texture2D* SharedResource = (ID3D11Texture2D*)RHITexture->GetNativeResource();
					return true;
				}
				break;
			}

			case ETextureShareDevice::D3D12:
			{
				bRequireCloseHandle = true;
				if (ITextureShareD3D12::Get().CreateSharedTexture(TextureSize, PixelFormat, RHITexture, SharedHandle, SharedHandleGuid))
				{
					return true;
				}
				break;
			}

			default:
				UE_LOG(LogTextureShareCore, Error, TEXT("Unsupported share '%s' device format"),*ShareItem.GetName());
				break;
			}

			UE_LOG(LogTextureShareCore, Error, TEXT("Failed create shared texture '%s' size=%s format=%d"), *ShareItem.GetName(), *TextureSize.ToString(), PixelFormat);
			return false;
		}

		return true;
	}

	void FSharedRHITexture::ReleaseHandle()
	{
		if (bRequireCloseHandle && SharedHandle)
		{
			verify(CloseHandle(SharedHandle));
		}
		SharedHandle = nullptr;
		bRequireCloseHandle = false;
	}

	void FSharedRHITexture::Release()
	{
		ReleaseHandle();
		RHITexture.SafeRelease();
		OpenedRHITexture.SafeRelease();
		TextureDesc = FTextureShareSurfaceDesc();
	}
};
#endif
