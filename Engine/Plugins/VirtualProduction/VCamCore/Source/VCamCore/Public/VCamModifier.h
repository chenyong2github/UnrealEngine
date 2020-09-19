// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CineCameraComponent.h"
#include "CoreMinimal.h"
#include "Roles/LiveLinkCameraTypes.h"

#include "VCamModifier.generated.h"

class UVCamComponent;
class UVCamModifierContext;

struct FModifierStackEntry;

UCLASS(Blueprintable, Abstract, EditInlineNew)
class VCAMCORE_API UVCamModifier : public UObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(UVCamModifierContext* Context);

	virtual void Apply(UVCamModifierContext* Context, UCineCameraComponent* CameraComponent, const float DeltaTime) {};

	virtual void PostLoad();

	bool DoesRequireInitialization() const { return bRequiresInitialization; };

	UFUNCTION(BlueprintCallable, Category="VirtualCamera")
	UVCamComponent* GetOwningVCamComponent() const;

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void GetCurrentLiveLinkDataFromOwningComponent(FLiveLinkCameraBlueprintData& LiveLinkData);

	UFUNCTION(BlueprintCallable, Category = "VirtualCamera")
	void SetEnabled(bool bNewEnabled);

	UFUNCTION(BlueprintPure, Category = "VirtualCamera", meta=(ReturnDisplayName="Enabled"))
	bool IsEnabled() const;

private:
	FModifierStackEntry* GetCorrespondingStackEntry() const;

	bool bRequiresInitialization = true;
};

UCLASS(EditInlineNew)
class VCAMCORE_API UVCamBlueprintModifier : public UVCamModifier
{
	GENERATED_BODY()

public:
	virtual void Initialize(UVCamModifierContext* Context) override;
	virtual void Apply(UVCamModifierContext* Context, UCineCameraComponent* CameraComponent, const float DeltaTime) override;

	UFUNCTION(BlueprintImplementableEvent, Category="VirtualCamera")
	void OnInitialize(UVCamModifierContext* Context);

	UFUNCTION(BlueprintImplementableEvent, Category="VirtualCamera")
	void OnApply(UVCamModifierContext* Context, UCineCameraComponent* CameraComponent, const float DeltaTime);
};