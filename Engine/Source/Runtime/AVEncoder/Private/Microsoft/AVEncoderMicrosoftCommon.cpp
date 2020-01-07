// Copyright Epic Games, Inc. All Rights Reserved.

#include "AVEncoderMicrosoftCommon.h"

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE

#if WMFMEDIA_SUPPORTED_PLATFORM
	#pragma comment(lib, "mfplat")
	#pragma comment(lib, "mfuuid")
	#pragma comment(lib, "Mfreadwrite")
#endif

namespace AVEncoder
{

//
// Windows only code
//
#if PLATFORM_WINDOWS
ID3D11Device* GetUE4DxDevice()
{
	auto Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
	checkf(Device != nullptr, TEXT("Failed to get UE4's ID3D11Device"));
	return Device;
}
#endif

//
// XboxOne only code
// 
#if PLATFORM_XBOXONE 
ID3D12Device* GetUE4DxDevice()
{
	auto Device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
	checkf(Device != nullptr, TEXT("Failed to get UE4's ID3D12Device"));
	return Device;
}

#endif


} // namespace AVEncoder

#endif // PLATFORM_WINDOWS || PLATFORM_XBOXONE

