// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "TextureShareEnums.h"
#include "TextureShareCoreContainers.h"
#include "Containers/TextureShareCoreGenericContainers.h"

#include "RHI.h"
#include "RHIResources.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"

#include "TextureShareContainers.generated.h"

USTRUCT(BlueprintType)
struct FTextureShareBPAdditionalData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	int FrameNumber = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	FMatrix PrjMatrix;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	FVector ViewLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	FRotator ViewRotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	FVector ViewScale;

	//@todo: add more data

	FTextureShareBPAdditionalData()
		: PrjMatrix(ForceInitToZero)
		, ViewLocation(ForceInitToZero)
		, ViewRotation(ForceInitToZero)
		, ViewScale(ForceInitToZero)
	{}

	// Convert type from bp back to cpp
	FTextureShareAdditionalData operator*() const;
};


USTRUCT(BlueprintType)
struct FTextureShareBPTexture2D
{
	GENERATED_BODY()

	// Texture unique name
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	FString Id;

	// Texture source or target (optional)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	UTextureRenderTarget2D* RTT;

	// Texture source (optional)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	UTexture2D* Texture;

	FTextureShareBPTexture2D()
		: RTT(nullptr)
		, Texture(nullptr)
	{}

	//Helpers:
	bool          IsValid() const;
	FIntPoint     GetSizeXY() const;
	EPixelFormat  GetFormat() const;

	bool GetTexture2DRHI_RenderThread(FTexture2DRHIRef& OutTexture2DRHIRef) const;
};

USTRUCT(BlueprintType)
struct FTextureShareBPPostprocess
{
	GENERATED_BODY()

	// Send Additional data
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	FTextureShareBPAdditionalData AdditionalData;

	// Send this textures to remote process
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	TArray<FTextureShareBPTexture2D> Send;

	// Receive this texture from remote process
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	TArray<FTextureShareBPTexture2D> Receive;
};

USTRUCT(BlueprintType)
struct FTextureShareBPSyncPolicy
{
	GENERATED_BODY()

	// Synchronize Session state events (BeginSession/EndSession)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	ETextureShareBPSyncConnect Connection = ETextureShareBPSyncConnect::Default;

	// Synchronize frame events (BeginFrame/EndFrame)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	ETextureShareBPSyncFrame   Frame      = ETextureShareBPSyncFrame::Default;

	// Synchronize texture events (LockTexture/UnlockTexture)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	ETextureShareBPSyncSurface Texture    = ETextureShareBPSyncSurface::Default;

	// Convert type from bp back to cpp
	FTextureShareBPSyncPolicy()
	{}

	FTextureShareBPSyncPolicy(const FTextureShareSyncPolicy& Init);
	FTextureShareSyncPolicy operator*() const;
};

USTRUCT(BlueprintType)
struct FTextureShareBPTimeOut
{
	GENERATED_BODY()

	// Wait for processes shares connection (ETextureShareBPSyncConnect::Sync) [Seconds, zero for infinite]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	float ConnectionSync = 0;

	// Wait for frame index sync (ETextureShareBPSyncFrame::Sync) [Seconds, zero for infinite]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	float FrameSync = 0;

	// Wait for remote process texture registering (ETextureShareBPSyncSurface::SyncPairingRead) [Seconds, zero for infinite]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	float TexturePairingSync = 0;

	// Wait for remote resource(GPU) handle ready (ETextureShareBPSyncFrame::Sync) [Seconds, zero for infinite]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	float TextureResourceSync = 0;

	// Wait before Read op texture until remote process finished texture write op [Seconds, zero for infinite]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	float TextureSync = 0;

	FTextureShareBPTimeOut()
	{}

	FTextureShareBPTimeOut(const FTextureShareTimeOut& Init);

	FTextureShareTimeOut operator*() const;
};

USTRUCT(BlueprintType)
struct FTextureShareBPSyncPolicySettings
{
	GENERATED_BODY()

	// Default values for sync policy on local process
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	FTextureShareBPSyncPolicy DefaultSyncPolicy;

	// Timeout values (in seconds) for sync policy on local process
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	FTextureShareBPTimeOut    TimeOut;

	FTextureShareBPSyncPolicySettings()
	{};

	FTextureShareBPSyncPolicySettings(const FTextureShareSyncPolicySettings& Init);

	FTextureShareSyncPolicySettings operator*() const;
};
