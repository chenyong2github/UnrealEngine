// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CineCameraComponent.h"
#include "CoreMinimal.h"
#include "Roles/LiveLinkCameraTypes.h"


#include "VCamModifier.generated.h"

UCLASS(Blueprintable, Abstract)
class VCAMCORE_API UVCamModifier : public UObject
{
	GENERATED_BODY()

public:

	virtual void Initialize()  PURE_VIRTUAL(UVCamModifier::Initialize);

	virtual void Apply(const FLiveLinkCameraBlueprintData& InitialLiveLinkData,
		UCineCameraComponent* CameraComponent, const float DeltaTime) PURE_VIRTUAL(UVCamModifier::Apply);

	virtual void SetActive(const bool InActive) { bIsActive = InActive; };
	virtual bool IsActive() const { return bIsActive; };

private:
	UPROPERTY()
	bool bIsActive = true;
};

UCLASS()
class VCAMCORE_API UVCamBlueprintModifier : public UVCamModifier
{
	GENERATED_BODY()

public:

	virtual void Initialize() override;
	virtual void Apply(const FLiveLinkCameraBlueprintData& InitialLiveLinkData,
        UCineCameraComponent* CameraComponent, const float DeltaTime) override;

	UFUNCTION(BlueprintImplementableEvent, Category="VirtualCamera")
	void OnInitialize();

	UFUNCTION(BlueprintImplementableEvent, Category="VirtualCamera")
	void OnApply(const FLiveLinkCameraBlueprintData& InitialLiveLinkData,
        UCineCameraComponent* CameraComponent, const float DeltaTime);
};