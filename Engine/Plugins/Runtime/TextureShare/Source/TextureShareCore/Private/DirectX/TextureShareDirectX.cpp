// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareDirectX.h"
#include "Core/TextureShareItemRHI.h"
//@todo: Format conversion.Test, optimize

namespace TextureShareItem
{
	enum Limits
	{
		DXGI_FORMAT_MAX = 132
	};
	TArray<FDXGIFormatMap*> FTextureShareFormatMapHelper::DXGIFormatMap;
	FDXGIFormatMap*         FTextureShareFormatMapHelper::DXGIFormatMapDefault=nullptr;

	static bool ImplFindPixelFormats(DXGI_FORMAT UnormFormat, DXGI_FORMAT TypelessFormat, const TArray<DXGI_FORMAT>& SrcFormats, TArray<EPixelFormat>& OutPixelFormats)
	{
#if TEXTURESHARECORE_RHI
		// Find best fit UE format:
		for (auto& ItArray : { TArray<DXGI_FORMAT>{ UnormFormat , TypelessFormat}, SrcFormats })
		{
			for(auto& ItFormat: ItArray)
			{ 
				if (ItFormat != DXGI_FORMAT_UNKNOWN)
				{
					FTextureShareItemRHI::FindPixelFormats(ItFormat, OutPixelFormats);
				}
			}
		}
#endif
		return OutPixelFormats.Num() > 0;
	}

	static bool ImplCreateDXGIMap(FDXGIFormatMap* &OutDXGIFormatMap, DXGI_FORMAT UnormFormat, DXGI_FORMAT TypelessFormat, const TArray<DXGI_FORMAT>& SrcFormats)
	{
		TArray<EPixelFormat> PixelFormats;
		if (ImplFindPixelFormats(UnormFormat, TypelessFormat, SrcFormats, PixelFormats))
		{
			OutDXGIFormatMap = new FDXGIFormatMap();
			OutDXGIFormatMap->UnormFormat = UnormFormat;
			OutDXGIFormatMap->TypelessFormat = TypelessFormat;
			OutDXGIFormatMap->PixelFormats.Append(PixelFormats);
		
			// Collect formats:
			if (UnormFormat != DXGI_FORMAT_UNKNOWN)
				{ OutDXGIFormatMap->PlatformFormats.AddUnique(UnormFormat); }
			if (TypelessFormat != DXGI_FORMAT_UNKNOWN)
				{ OutDXGIFormatMap->PlatformFormats.AddUnique(TypelessFormat); }

			for (auto& It : SrcFormats)
			{
				if (It != DXGI_FORMAT_UNKNOWN)
					{ OutDXGIFormatMap->PlatformFormats.AddUnique(It); }
			}
			return true;
		}
		return false;
	}

	void FTextureShareFormatMapHelper::AddDXGIMap(TArray<FDXGIFormatMap*>& DstMap, DXGI_FORMAT UnormFormat, DXGI_FORMAT TypelessFormat, const TArray<DXGI_FORMAT>& SrcFormats)
	{
		FDXGIFormatMap* FormatMapItem = DXGIFormatMapDefault;
		ImplCreateDXGIMap(FormatMapItem, UnormFormat, TypelessFormat, SrcFormats);

		for (auto It : FormatMapItem->PlatformFormats)
		{
			if (It != DXGI_FORMAT_UNKNOWN)
			{
				DstMap[(uint32)It] = FormatMapItem;
			}
		}
	}

	void FTextureShareFormatMapHelper::ReleaseDXGIFormatMap()
	{
		for (auto& It : DXGIFormatMap)
		{
			delete It;
		}
		DXGIFormatMap.Empty();
		DXGIFormatMapDefault = nullptr;
	}

	bool FTextureShareFormatMapHelper::CreateDXGIFormatMap()
	{
		ReleaseDXGIFormatMap();

		// Initialize after renderer
		TArray<FDXGIFormatMap*>& Result =  DXGIFormatMap;

		// Create default format
		if (!ImplCreateDXGIMap(DXGIFormatMapDefault, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_TYPELESS,
			{
				DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
			}))
		{
			return false;
		}

		// Initialize default values:
		Result.Reserve(DXGI_FORMAT_MAX);

		for (uint32 Index = 0; Index < DXGI_FORMAT_MAX;++Index)
		{
			Result.EmplaceAt(Index, DXGIFormatMapDefault);
		}

		// Map DXGI formats to supported for GPU-GPU sharing
		AddDXGIMap(Result, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_TYPELESS,
			{
				DXGI_FORMAT_R32G32B32A32_FLOAT,
				DXGI_FORMAT_R32G32B32A32_SINT
			});

		AddDXGIMap(Result, DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32_TYPELESS,
			{
				DXGI_FORMAT_R32G32B32_FLOAT,
				DXGI_FORMAT_R32G32B32_SINT
			});
		AddDXGIMap(Result, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_TYPELESS,
			{
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				DXGI_FORMAT_R16G16B16A16_UINT,
				DXGI_FORMAT_R16G16B16A16_SNORM,
				DXGI_FORMAT_R16G16B16A16_SINT
			});
		AddDXGIMap(Result, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_TYPELESS,
			{
				DXGI_FORMAT_R32G32_FLOAT,
				DXGI_FORMAT_R32G32_SINT,
			});
		AddDXGIMap(Result, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_TYPELESS,
			{
				DXGI_FORMAT_R10G10B10A2_UINT
			});

		AddDXGIMap(Result, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_TYPELESS,
			{
				DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
				DXGI_FORMAT_R8G8B8A8_UINT,
				DXGI_FORMAT_R8G8B8A8_SNORM,
				DXGI_FORMAT_R8G8B8A8_SINT
			});

		AddDXGIMap(Result, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_TYPELESS,
			{
				DXGI_FORMAT_R16G16_FLOAT,
				DXGI_FORMAT_R16G16_UINT,
				DXGI_FORMAT_R16G16_SNORM,
				DXGI_FORMAT_R16G16_SINT
			});

		AddDXGIMap(Result, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_TYPELESS,
			{
				DXGI_FORMAT_D32_FLOAT,
				DXGI_FORMAT_R32_FLOAT,
				DXGI_FORMAT_R32_SINT
			});

		AddDXGIMap(Result, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_TYPELESS,
			{
				DXGI_FORMAT_R8G8_TYPELESS,
				DXGI_FORMAT_R8G8_UNORM,
				DXGI_FORMAT_R8G8_UINT,
				DXGI_FORMAT_R8G8_SNORM,
				DXGI_FORMAT_R8G8_SINT
			});

		AddDXGIMap(Result, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_TYPELESS,
			{
				DXGI_FORMAT_R16_FLOAT,
				DXGI_FORMAT_D16_UNORM,
				DXGI_FORMAT_R16_UINT,
				DXGI_FORMAT_R16_SNORM,
				DXGI_FORMAT_R16_SINT
			});

		AddDXGIMap(Result, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_TYPELESS,
			{
				DXGI_FORMAT_R8_UINT,
				DXGI_FORMAT_R8_SNORM,
				DXGI_FORMAT_R8_SINT
			});

		AddDXGIMap(Result, DXGI_FORMAT_A8_UNORM, DXGI_FORMAT_UNKNOWN,
			{});

		AddDXGIMap(Result, DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_FORMAT_B8G8R8X8_TYPELESS,
			{
				DXGI_FORMAT_B8G8R8X8_UNORM_SRGB
			});
	
		//@todo: Test, Add\Remove shared formats
		return true;
	}

};
