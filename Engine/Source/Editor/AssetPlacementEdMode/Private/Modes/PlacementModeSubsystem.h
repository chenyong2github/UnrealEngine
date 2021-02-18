// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "UObject/ObjectPtr.h"

#include "PlacementModeSubsystem.generated.h"

struct FTypedElementHandle;
class UAssetPlacementSettings;

UCLASS(Transient)
class UPlacementModeSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem Interface

	// @returns the settings object for the mode for sharing across all tools and tool builders.
	TWeakObjectPtr<const UAssetPlacementSettings> GetModeSettingsObject() const;

	/**
	 * Verifies if the given element handle is supported by the current mode settings' palette.
	 *
	 * @returns true if the element can be placed by the mode.
	 */
	bool DoesCurrentPaletteSupportElement(const FTypedElementHandle& InElementToCheck) const;

protected:
	UPROPERTY()
	TObjectPtr<UAssetPlacementSettings> ModeSettings;
};
