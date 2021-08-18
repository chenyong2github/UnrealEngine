// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "TextureShareDLL.h"
#include "TextureShareContainers.h"

#include <DXGIFormat.h>

struct ID3D11Device;
struct ID3D11Texture2D;

struct ID3D12Device;
struct ID3D12Resource;

class ITextureShareCore;

class FTextureShareInterface
{
public:
	TEXTURE_SHARE_SDK_API static FTextureShareSyncPolicySettings GetSyncPolicySettings();
	TEXTURE_SHARE_SDK_API static void SetSyncPolicySettings(const FTextureShareSyncPolicySettings& InSyncPolicySettings);

	// Create shared resource object
	TEXTURE_SHARE_SDK_API static bool CreateTextureShare(const TCHAR* ShareName, ETextureShareProcess Process, FTextureShareSyncPolicy SyncMode, ETextureShareDevice DeviceType, float SyncWaitTime = 0.03);
	TEXTURE_SHARE_SDK_API static bool ReleaseTextureShare(const TCHAR* ShareName);

	TEXTURE_SHARE_SDK_API static bool IsValid(const TCHAR* ShareName);
	TEXTURE_SHARE_SDK_API static bool IsSessionValid(const TCHAR* ShareName);
	TEXTURE_SHARE_SDK_API static ETextureShareDevice GetDeviceType(const TCHAR* ShareName);

	TEXTURE_SHARE_SDK_API static bool RegisterTexture(const TCHAR* ShareName, const TCHAR* TextureName, int Width, int Height, ETextureShareFormat InFormat, uint32 InFormatValue, ETextureShareSurfaceOp OperationType);

	/* MGPU support*/
	TEXTURE_SHARE_SDK_API static bool SetTextureGPUIndex(const TCHAR* ShareName, const TCHAR* TextureName, uint32 GPUIndex);
	TEXTURE_SHARE_SDK_API static bool SetDefaultGPUIndex(const TCHAR* ShareName, uint32 GPUIndex);

	TEXTURE_SHARE_SDK_API static bool IsRemoteTextureUsed(const TCHAR* ShareName, const TCHAR* TextureName);

	TEXTURE_SHARE_SDK_API static bool BeginSession(const TCHAR* ShareName);
	TEXTURE_SHARE_SDK_API static bool EndSession(const TCHAR* ShareName);

	TEXTURE_SHARE_SDK_API static bool BeginFrame_RenderThread(const TCHAR* ShareName);
	TEXTURE_SHARE_SDK_API static bool EndFrame_RenderThread(const TCHAR* ShareName);

	TEXTURE_SHARE_SDK_API static bool SetLocalAdditionalData(const TCHAR* ShareName, const FTextureShareSDKAdditionalData& InData);
	TEXTURE_SHARE_SDK_API static bool GetRemoteAdditionalData(const TCHAR* ShareName, FTextureShareSDKAdditionalData& OutData);

	TEXTURE_SHARE_SDK_API static bool LockTextureD3D11_RenderThread(ID3D11Device* pD3D11Device, const TCHAR* ShareName, const TCHAR* TextureName, ID3D11Texture2D*& OutD3D11Texture);
	TEXTURE_SHARE_SDK_API static bool LockTextureD3D12_RenderThread(ID3D12Device* pD3D12Device, const TCHAR* ShareName, const TCHAR* TextureName, ID3D12Resource*& OutD3D12Resource);

	TEXTURE_SHARE_SDK_API static bool UnlockTexture_RenderThread(const TCHAR* ShareName, const TCHAR* TextureName);

	TEXTURE_SHARE_SDK_API static bool SetCustomProjectionData(const TCHAR* ShareName, const FTextureShareSDKCustomProjectionData& InData);

	TEXTURE_SHARE_SDK_API static bool BeginSyncFrame();
	TEXTURE_SHARE_SDK_API static bool EndSyncFrame();
};

