// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CineCameraComponent.h"
#include "CoreMinimal.h"
#include "Roles/LiveLinkCameraTypes.h"


#include "VCamModifier.generated.h"

class UVCamModifierContext;

UCLASS(Blueprintable, Abstract, EditInlineNew)
class VCAMCORE_API UVCamModifier : public UObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(UVCamModifierContext* Context);

	virtual void Apply(UVCamModifierContext* Context, const FLiveLinkCameraBlueprintData& InitialLiveLinkData,
		UCineCameraComponent* CameraComponent, const float DeltaTime) {};

	virtual void PostLoad();

	bool DoesRequireInitialization() const { return bRequiresInitialization; };

private:
	bool bRequiresInitialization = true;
};

UCLASS(EditInlineNew)
class VCAMCORE_API UVCamBlueprintModifier : public UVCamModifier
{
	GENERATED_BODY()

public:

	virtual void Initialize(UVCamModifierContext* Context) override;
	virtual void Apply(UVCamModifierContext* Context, const FLiveLinkCameraBlueprintData& InitialLiveLinkData,
        UCineCameraComponent* CameraComponent, const float DeltaTime) override;

	UFUNCTION(BlueprintImplementableEvent, Category="VirtualCamera")
	void OnInitialize(UVCamModifierContext* Context);

	UFUNCTION(BlueprintImplementableEvent, Category="VirtualCamera")
	void OnApply(UVCamModifierContext* Context, const FLiveLinkCameraBlueprintData& InitialLiveLinkData,
        UCineCameraComponent* CameraComponent, const float DeltaTime);
};