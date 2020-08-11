// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "PixelFormat.h"
#include <DXGIFormat.h>

//
// Windows only include
//

#if PLATFORM_WINDOWS
THIRD_PARTY_INCLUDES_START
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#include <d3d11.h>
#include <mftransform.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <codecapi.h>
#include <shlwapi.h>
#include <mfreadwrite.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_END
#endif

inline const FString GetComErrorDescription(HRESULT Res)
{
	const uint32 BufSize = 4096;
	WIDECHAR buffer[4096];
	if (::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr,
		Res,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
		buffer,
		sizeof(buffer) / sizeof(*buffer),
		nullptr))
	{
		return buffer;
	}
	else
	{
		return TEXT("[cannot find error description]");
	}
}

// macro to deal with COM calls inside a function that returns `{}` on error
#define CHECK_HR_DEFAULT(COM_call)\
	{\
		HRESULT Res = COM_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(LogTextureShareCoreD3D, Error, TEXT("`" #COM_call "` failed: 0x%X - %s"), Res, *GetComErrorDescription(Res)); \
			return {};\
		}\
	}

namespace TextureShareItem
{
	struct FDXGIFormatMap
	{
		DXGI_FORMAT           UnormFormat;
		DXGI_FORMAT           TypelessFormat;
		TArray<EPixelFormat>  PixelFormats;
		TArray<DXGI_FORMAT>   PlatformFormats;
	};

	class FTextureShareFormatMapHelper
	{
	public:
		~FTextureShareFormatMapHelper()
		{
			ReleaseDXGIFormatMap();
		}

		static const FDXGIFormatMap& FindSharedFormat(DXGI_FORMAT InFormat)
		{ 
			if(!DXGIFormatMap.Num())
			{
				CreateDXGIFormatMap();
			}
			return *DXGIFormatMap[(uint32)InFormat]; 
		}

	protected:
		static bool CreateDXGIFormatMap();
		static void ReleaseDXGIFormatMap();
		static void AddDXGIMap(TArray<FDXGIFormatMap*>& DstMap, DXGI_FORMAT UnormFormat, DXGI_FORMAT TypelessFormat, const TArray<DXGI_FORMAT>& SrcFormats);

	private:
		static TArray<FDXGIFormatMap*> DXGIFormatMap;
		static FDXGIFormatMap*         DXGIFormatMapDefault;
	};
};
