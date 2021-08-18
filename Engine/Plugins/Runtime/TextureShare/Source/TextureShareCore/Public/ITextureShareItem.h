// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "GenericPlatform/GenericPlatformTime.h"
#include "HAL/PlatformTime.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/PlatformProcess.h"

#include "Templates/RefCounting.h"

#include "Containers/TextureShareCoreGenericContainers.h"
#include "TextureShareCoreContainers.h"

#include "ITextureShareItemRHI.h"

class ITextureShareItemD3D11;
class ITextureShareItemD3D12;

class ITextureShareItem
	: public ITextureShareItemRHI
{
public:
	virtual ~ITextureShareItem() = 0
	{}

	/** Get share name */
	virtual const FString&              GetName() const = 0;

	virtual bool IsValid() const = 0;
	virtual bool IsSessionValid() const = 0;
	virtual bool IsClient() const = 0;

	virtual bool IsLocalFrameLocked() const = 0;

	virtual bool IsLocalTextureUsed(const FString& TextureName) const = 0;
	virtual bool IsRemoteTextureUsed(const FString& TextureName) const = 0;

	virtual bool RegisterTexture(const FString& TextureName, const FIntPoint& InSize, ETextureShareFormat InFormat, uint32 InFormatValue, ETextureShareSurfaceOp OperationType) = 0;

	/* MGPU Transfer */
	virtual bool SetTextureGPUIndex(const FString& TextureName, uint32 GPUIndex) = 0;
	virtual bool SetDefaultGPUIndex(uint32 GPUIndex) = 0;

	/* Sync wait time */
	virtual void SetSyncWaitTime(float InSyncWaitTime) = 0;

	virtual bool GetRemoteTextureDesc(const FString& TextureName, FTextureShareSurfaceDesc& OutSharedTextureDesc) const = 0;

	/** Session scope */
	virtual bool BeginSession() = 0;
	virtual void EndSession() = 0;

	/** Frame scope */
	virtual bool BeginFrame_RenderThread() = 0;
	virtual bool EndFrame_RenderThread() = 0;

	/** texture access */
	virtual bool UnlockTexture_RenderThread(const FString& TextureName)
		{ return false; }

	/** return device api ptr */
	virtual ETextureShareDevice    GetDeviceType() const = 0;
	virtual ITextureShareItemD3D11* GetD3D11() = 0;
	virtual ITextureShareItemD3D12* GetD3D12() = 0;

	/** Frame additional data */
	virtual bool SetLocalAdditionalData(const FTextureShareAdditionalData& InAdditionalData) = 0;
	virtual bool GetRemoteAdditionalData(FTextureShareAdditionalData& OutAdditionalData) = 0;

	// NOT IMPLEMENTED
	// Set custom projection data (override UE4 matrix)
	virtual bool SetCustomProjectionData(const FTextureShareCustomProjectionData& InCustomProjectionData) = 0;

	// Release this object
	virtual void Release() = 0;
};
