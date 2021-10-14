// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "EditorUtilityTask.h"
#include "Templates/UniquePtr.h"
#include "Templates/SubclassOf.h"

#include "AsyncImageExport.generated.h"

class UTextureRenderTarget2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnExportImageAsyncComplete, bool, bSuccess);

UCLASS(MinimalAPI)
class UAsyncImageExport : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UAsyncImageExport();

	UFUNCTION(BlueprintCallable, meta=( BlueprintInternalUseOnly="true" ))
	static UAsyncImageExport* ExportImageAsync(UTexture* Texture, const FString& OutputFile, int Quality = 100);

	virtual void Activate() override;
public:

	UPROPERTY(BlueprintAssignable)
	FOnExportImageAsyncComplete Complete;

private:

	void NotifyComplete(bool bSuccess);
	void Start(UTexture* Texture, const FString& OutputFile);
	void ReadPixelsFromRT(UTextureRenderTarget2D* InRT, TArray<FColor>* OutPixels);
	void ExportImage(TArray<FColor>&& RawPixels, FIntPoint ImageSize);

private:
	UPROPERTY()
	TObjectPtr<UTexture> TextureToExport;

	UPROPERTY()
	int32 Quality;

	UPROPERTY()
	FString TargetFile;
};
