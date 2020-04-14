// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sensors/4MLSensor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "4MLSensor_Camera.generated.h"


class U4MLAgent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UGameViewportClient;

/** Observing player's camera */
UCLASS(Blueprintable)
class UE4ML_API U4MLSensor_Camera : public U4MLSensor
{
	GENERATED_BODY()
public:
	U4MLSensor_Camera(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool ConfigureForAgent(U4MLAgent& Agent) override;
	virtual void Configure(const TMap<FName, FString>& Params) override;
	virtual TSharedPtr<F4ML::FSpace> ConstructSpaceDef() const override;
	virtual void GetObservations(F4MLMemoryWriter& Ar) override;

protected:
	virtual void SenseImpl(const float DeltaTime) override;

	virtual void OnAvatarSet(AActor* Avatar) override;
	virtual void ClearPawn(APawn& InPawn) override;

	void HandleScreenshotData(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData);

protected:
	UPROPERTY(EditAnywhere, Category = UE4ML, meta=(UIMin=1, ClampMin=1))
	uint32 Width;

	UPROPERTY(EditAnywhere, Category = UE4ML, meta=(UIMin=1, ClampMin=1))
	uint32 Height;

	UPROPERTY(EditAnywhere, Category = UE4ML)
	uint32 bShowUI : 1;

	UPROPERTY(Transient)
	USceneCaptureComponent2D* CaptureComp;

	UPROPERTY(Transient)
	UTextureRenderTarget2D* RenderTarget2D;

	UPROPERTY()
	UGameViewportClient* CachedViewportClient;

	ETextureRenderTargetFormat RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	ESceneCaptureSource CaptureSource = SCS_FinalColorLDR;
	uint8 CameraIndex = 0;

	FDelegateHandle ScreenshotDataCallbackHandle;
	
public: // tmp, just for prototyping
	TArray<FLinearColor> LastTickData;
};

