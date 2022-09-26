// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Streaming/TextureMipDataProvider.h"
#include "Engine/TextureMipDataProviderFactory.h"
#include "CustomizableObjectSystem.h"
#include "MutableRuntime/Public/System.h"
#include "MutableRuntime/Public/Model.h"
#include "MutableRuntime/Public/Parameters.h"

#include "CustomizableObjectMipDataProvider.generated.h"

class UCustomizableObjectInstance;

/** This struct stores the data relevant for the construction of a specific texture. 
* This includes all the data required to rebuild the image (or any of its mips).
*/
struct FMutableUpdateContext
{
	mu::SystemPtr System;
	mu::ModelPtr Model;
	mu::ParametersPtrConst Parameters;
	int32 State = -1;

	TArray<mu::ImagePtrConst> ImageParameterValues;
};


/** Runtime data used during a mutable image mipmap update */
struct FMutableImageOperationData
{
	/** This option comes from the operation request. It is used to reduce the number of mipmaps that mutable must generate for images.  */
	int32 MipsToSkip = 0;
	FMutableImageReference RequestedImage;

	TSharedPtr<FMutableUpdateContext> UpdateContext;

	mu::ImagePtrConst Result;

	TArray<FMutableMipUpdateLevel> Levels;

	// Used to sync with the FMutableTextureMipDataProvider and FRenderAssetUpdate::Tick
	FThreadSafeCounter* Counter;
	FTextureUpdateSyncOptions::FCallback RescheduleCallback;

	/** Access to the Counter must be protected with this because it may be accessed from another thread to null it. */
	FCriticalSection CounterTaskLock;
};


class FMutableTextureMipDataProvider : public FTextureMipDataProvider
{
public:
	FMutableTextureMipDataProvider(const UTexture* Texture, UCustomizableObjectInstance* InCustomizableObjectInstance, const FMutableImageReference& InImageRef);

	virtual void Init(const FTextureUpdateContext& Context, const FTextureUpdateSyncOptions& SyncOptions) override;
	virtual int32 GetMips(const FTextureUpdateContext& Context, int32 StartingMipIndex, const FTextureMipInfoArray& MipInfos, const FTextureUpdateSyncOptions& SyncOptions) override;
	virtual bool PollMips(const FTextureUpdateSyncOptions& SyncOptions) override;
	virtual void CleanUp(const FTextureUpdateSyncOptions& SyncOptions) override;
	virtual void Cancel(const FTextureUpdateSyncOptions& SyncOptions) override;
	virtual ETickThread GetCancelThread() const override;
	void AbortPollMips() override;

private:
	void CancelCounterSafely();

public:
	// Todo: Simplify by replacing the reference to the Instance with some static parametrization or hash with enough info to reconstruct the texture
	UPROPERTY(Transient)
	UCustomizableObjectInstance* CustomizableObjectInstance = nullptr;

	FMutableImageReference ImageRef;
	TSharedPtr<FMutableUpdateContext> UpdateContext;

	bool bRequestAborted = false;

	TSharedPtr<FMutableImageOperationData> OperationData;
	FGraphEventRef UpdateImageMutableTaskEvent;
};


UCLASS(hidecategories=Object)
class CUSTOMIZABLEOBJECT_API UMutableTextureMipDataProviderFactory : public UTextureMipDataProviderFactory
{
	GENERATED_UCLASS_BODY()

public:
	virtual FTextureMipDataProvider* AllocateMipDataProvider(UTexture* Asset) override
	{
		check(ImageRef.ImageID > 0);
		FMutableTextureMipDataProvider* Result = new FMutableTextureMipDataProvider(Asset, CustomizableObjectInstance, ImageRef);
		Result->UpdateContext = UpdateContext;		
		return Result;
	}

	virtual bool WillProvideMipDataWithoutDisk() const override
	{ 
		return true;
	}

	// Todo: Simplify by replacing the reference to the Instance with some static parametrization or hash with enough info to reconstruct the texture
	UPROPERTY(Transient)
	TObjectPtr<UCustomizableObjectInstance> CustomizableObjectInstance = nullptr;

	FMutableImageReference ImageRef;
	TSharedPtr<FMutableUpdateContext> UpdateContext;

};
