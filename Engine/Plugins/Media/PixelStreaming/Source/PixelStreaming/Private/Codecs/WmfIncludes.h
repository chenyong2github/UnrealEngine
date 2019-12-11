// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Windows/AllowWindowsPlatformTypes.h"

THIRD_PARTY_INCLUDES_START

#define WIN32_LEAN_AND_MEAN
#include <mftransform.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <Codecapi.h> // `CODECAPI_AVDecVideoAcceleration_H264`

#pragma warning(push)
// macro redefinition in DirectX headers from ThirdParty folder while they are already defined by <winerror.h> included 
// from "Windows/AllowWindowsPlatformTypes.h"
#pragma warning(disable: 4005)
#include <d3d11.h>
#include <d3d10.h> // for ID3D10Multithread
#include <d3d9.h>
#pragma warning(pop)

#include <dxva2api.h>
#include <DxErr.h>

THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformTypes.h"

#include "D3D11RHIPrivate.h"

// following commented include causes name clash between UE4 and Windows `IMediaEventSink`,
// we just need a couple of GUIDs from there so the solution is to duplicate them below
//#include "wmcodecdsp.h"

const GUID CLSID_CMSAACDecMFT = { 0x32d186a7, 0x218f, 0x4c75, { 0x88, 0x76, 0xdd, 0x77, 0x27, 0x3a, 0x89, 0x99 } };
const GUID CLSID_CMSH264DecoderMFT = { 0x62CE7E72, 0x4C71, 0x4d20, { 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D } };

// `MF_LOW_LATENCY` is defined in "mfapi.h" for >= WIN8
// UE4 supports lower Windows versions at the moment and so `WINVER` is < `_WIN32_WINNT_WIN8`
// to be able to use `MF_LOW_LATENCY` with default UE4 build we define it ourselves and check actual
// Windows version in runtime
#if (WINVER < _WIN32_WINNT_WIN8)
	const GUID MF_LOW_LATENCY = { 0x9c27891a, 0xed7a, 0x40e1,{ 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee } };
	const GUID MF_SA_D3D11_AWARE = { 0x206b4fc8, 0xfcf9, 0x4c51, { 0xaf, 0xe3, 0x97, 0x64, 0x36, 0x9e, 0x33, 0xa0 } };
#endif

#include "Containers/UnrealString.h"

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

// a convenience macro to deal with COM result codes
// is designed to be used in functions returning `bool`: CHECK_HR(COM_function_call());
#define CHECK_HR(COM_call)\
	{\
		HRESULT Res = COM_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(PixelPlayer, Error, TEXT("`" #COM_call "` failed: 0x%X - %s"), Res, *GetComErrorDescription(Res));\
			return false;\
		}\
	}

#define CHECK_HR_DX9(DX9_call)\
	{\
		HRESULT Res = DX9_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(PixelPlayer, Error, TEXT("`" #DX9_call "` failed 0x%X: %s - %s"), Res, DXGetErrorString(Res), DXGetErrorDescription(Res));\
			return false;\
		}\
	}

#define CHECK_HR_DX9_VOID(DX9_call)\
	{\
		HRESULT Res = DX9_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(PixelPlayer, Error, TEXT("`" #DX9_call "` failed 0x%X: %s - %s"), Res, DXGetErrorString(Res), DXGetErrorDescription(Res));\
			return;\
		}\
	}

