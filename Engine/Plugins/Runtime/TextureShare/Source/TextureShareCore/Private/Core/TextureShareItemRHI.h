// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShareItem.h"

#if TEXTURESHARECORE_RHI

#include <DXGIFormat.h>

struct FSharedResourceTexture;

namespace TextureShareItem
{
	class FTextureShareItemBase;

	class FSharedRHITexture
	{
	public:
		virtual ~FSharedRHITexture()
			{ Release(); }

		void Release();
		void ReleaseHandle();

		/** Update shared texture resource, if texture changed */
		bool Update_RenderThread(const FTextureShareItemBase& ShareItem, const FTextureShareSurfaceDesc& InTextureDesc, const FSharedResourceTexture& TextureData);

		FTexture2DRHIRef& GetSharedResource()
			{ return RHITexture; }
		FTexture2DRHIRef& GetOpenedResource()
			{ return OpenedRHITexture; }

		void* GetSharedHandle() const
			{ return SharedHandle; }
		FGuid GetSharedHandleGuid() const
			{ return SharedHandleGuid; }

	private:
		FTextureShareSurfaceDesc TextureDesc;

		FTexture2DRHIRef         RHITexture; // Server side created RHI texture
		FTexture2DRHIRef         OpenedRHITexture; // Client side opened RHI texture

		FGuid  SharedHandleGuid;
		void*  SharedHandle = nullptr;
		bool   bRequireCloseHandle = false;
	};

	class FTextureShareItemRHI
	{
		
	public:
		virtual ~FTextureShareItemRHI()
			{ ReleaseSharedRHITextures(); }

		bool InitializeRHISharedTextureFormat(ETextureShareDevice DeviceType, EPixelFormat InFormat, FTextureShareSurfaceDesc& OutTextureDesc) const;

		FSharedRHITexture* GetSharedRHITexture(const FSharedResourceTexture& LocalTextureData);
		void ReleaseSharedRHITextures();

		static bool FindPixelFormats(DXGI_FORMAT InFormat, TArray<EPixelFormat>& OutPixelFormats);
		static bool IsFormatResampleRequired(EPixelFormat SharedFormat, EPixelFormat PixelFormat);
		static EPixelFormat FindSharedTextureFormat(const FTextureShareSurfaceDesc& TextureDesc);

	private:
		TMap<int32, FSharedRHITexture*> SharedRHITextures;
	};
};
#endif
