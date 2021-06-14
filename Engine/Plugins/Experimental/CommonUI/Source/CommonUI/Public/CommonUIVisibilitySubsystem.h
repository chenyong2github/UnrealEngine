// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "CommonUIVisibilitySubsystem.generated.h"

class UWidget;
class ULocalPlayer;
class APlayerController;
struct FGameplayTagContainer;
class UCommonUIVisibilitySubsystem;
enum class ECommonInputType : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHardwareVisibilityTagsChangedDynamicEvent, UCommonUIVisibilitySubsystem*, TagSubsystem);

UCLASS(DisplayName = "UI Visibility Subsystem")
class COMMONUI_API UCommonUIVisibilitySubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UCommonUIVisibilitySubsystem* Get(const ULocalPlayer* LocalPlayer);

	UCommonUIVisibilitySubsystem();
	
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	DECLARE_EVENT_OneParam(UCommonUIVisibilitySubsystem, FHardwareVisibilityTagsChangedEvent, UCommonUIVisibilitySubsystem*);
	FHardwareVisibilityTagsChangedEvent OnVisibilityTagsChanged;

	/** Get the hardware visibility tags currently in play.  These can change over time, if input mode changes, or other groups are removed/added. */
	const FGameplayTagContainer& GetVisibilityTags() const { return ComputedVisibilityTags; }

	void AddUserVisibilityCondition(const FGameplayTag& UserTag);
	void RemoveUserVisibilityCondition(const FGameplayTag& UserTag);

protected:
	void RefreshVisibilityTags();
	void OnInputMethodChanged(ECommonInputType CurrentInputType);
	FGameplayTagContainer ComputeVisibilityTags() const;

private:
	FGameplayTagContainer ComputedVisibilityTags;
	FGameplayTagContainer UserVisibilityTags;
};