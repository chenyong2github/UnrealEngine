// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CineCameraComponent.h"
#include "CoreMinimal.h"
#include "Roles/LiveLinkCameraTypes.h"

#include "VCamModifier.generated.h"

class UInputMappingContext;
class UVCamComponent;
class UVCamModifierContext;
class UInputComponent;

struct FModifierStackEntry;

UCLASS(Blueprintable, Abstract, EditInlineNew)
class VCAMCORE_API UVCamModifier : public UObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(UVCamModifierContext* Context, UInputComponent* InputComponent = nullptr);

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

	// Allows a modifier to return Input Mapping Context which will get automatically registered with the input system
	// The Input Priority of the mapping context will be set by reference
	virtual const UInputMappingContext* GetInputMappingContext(int32& InputPriority) const;

private:
	FModifierStackEntry* GetCorrespondingStackEntry() const;

	bool bRequiresInitialization = true;
};

UCLASS(EditInlineNew)
class VCAMCORE_API UVCamBlueprintModifier : public UVCamModifier
{
	GENERATED_BODY()

public:
	virtual void Initialize(UVCamModifierContext* Context, UInputComponent* InputComponent=nullptr) override;
	virtual void Apply(UVCamModifierContext* Context, UCineCameraComponent* CameraComponent, const float DeltaTime) override;
	virtual const UInputMappingContext* GetInputMappingContext(int32& InputPriority) const override;
	
	UFUNCTION(BlueprintImplementableEvent, Category="VirtualCamera")
	void OnInitialize(UVCamModifierContext* Context);

	UFUNCTION(BlueprintImplementableEvent, Category="VirtualCamera")
	void OnApply(UVCamModifierContext* Context, UCineCameraComponent* CameraComponent, const float DeltaTime);

	// Allows a modifier to return Input Mapping Context which will get automatically registered with the input system
	UFUNCTION(BlueprintImplementableEvent, Category="VirtualCamera")
	void GetInputMappingContextAndPriority(UInputMappingContext*& InputMappingContext, int32& InputPriority) const;
};