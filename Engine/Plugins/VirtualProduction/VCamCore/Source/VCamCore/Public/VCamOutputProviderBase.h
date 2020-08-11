// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UnrealClient.h"
#include "CineCameraComponent.h"
#include "VPFullScreenUserWidget.h"

#if WITH_EDITOR
#endif

#include "VCamOutputProviderBase.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVCamOutputProvider, Log, All);

class UUserWidget;
class UVPFullScreenUserWidget;

UCLASS(Abstract, EditInlineNew)
class VCAMCORE_API UVCamOutputProviderBase : public UObject
{
	GENERATED_BODY()

public:
	UVCamOutputProviderBase();
	~UVCamOutputProviderBase();

	virtual void InitializeSafe();
	virtual void Destroy();
	virtual void Tick(const float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Output")
	virtual void SetActive(const bool InActive);

	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetPause(const bool InPause);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintPure, Category = "Output")
	bool IsActive() const { return bIsActive; };

	UFUNCTION(BlueprintPure, Category = "Output")
	bool IsPaused() const { return bIsPaused; };

	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetTargetCamera(const UCineCameraComponent* InTargetCamera);

	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetUMGClass(const TSubclassOf<UUserWidget> InUMGClass);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayPriority = "2"))
	TSubclassOf<UUserWidget> UMGClass;

	// Override the default output resolution with a custom value - NOTE you must toggle bIsActive off then back on for this to take effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayPriority = "3"))
	bool bUseOverrideResolution = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayPriority = "4"), meta = (EditCondition = "bUseOverrideResolution", ClampMin = 1))
	FIntPoint OverrideResolution = { 2048, 1536 };

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayPriority = "1"))
	bool bIsActive = false;

	UPROPERTY()
	bool bIsPaused = false;

	bool bInitialized = false;

	EVPWidgetDisplayType DisplayType = EVPWidgetDisplayType::PostProcess;

	virtual void CreateUMG();

	void DisplayUMG();
	void DestroyUMG();

	UPROPERTY(Transient)
	UVPFullScreenUserWidget* UMGWidget = nullptr;

private:
	void NotifyWidgetOfComponentChange() const;

	TSoftObjectPtr<UCineCameraComponent> TargetCamera;
};
