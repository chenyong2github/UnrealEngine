// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "EditorUtilityTask.h"
#include "Templates/UniquePtr.h"
#include "Templates/SubclassOf.h"

#include "AsyncCaptureScene.generated.h"

class ASceneCapture2D;
class UTextureRenderTarget2D;
template<typename PixelType>
struct TImagePixelData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAsyncCaptureSceneComplete, UTextureRenderTarget2D*, Texture);

UCLASS(MinimalAPI)
class UAsyncCaptureScene : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UAsyncCaptureScene();

	UFUNCTION(BlueprintCallable, meta=( BlueprintInternalUseOnly="true" ))
	static UAsyncCaptureScene* CaptureScreenshot(UCameraComponent* ViewCamera, TSubclassOf<ASceneCapture2D> SceneCaptureClass, int ResX, int ResY);

	virtual void Activate() override;
public:

	UPROPERTY(BlueprintAssignable)
	FOnAsyncCaptureSceneComplete Complete;

private:

	void NotifyComplete(UTextureRenderTarget2D* InTexture);
	void Start(UCameraComponent* ViewCamera, TSubclassOf<ASceneCapture2D> SceneCaptureClass, int ResX, int ResY);
	void ReadPixelsFromRT(UTextureRenderTarget2D* InRT, TArray<FColor>* OutPixels);
	void FinishLoadingBeforeScreenshot();

private:
	UPROPERTY()
	ASceneCapture2D* SceneCapture;

	UPROPERTY()
	UTextureRenderTarget2D* SceneCaptureRT;
};
