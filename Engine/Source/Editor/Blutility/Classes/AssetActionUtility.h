// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "EditorUtilityObject.h"
#include "UObject/ScriptMacros.h"
#include "IEditorUtilityExtension.h"
#include "AssetActionUtility.generated.h"

namespace AssetActionUtilityTags
{
	extern const FName BlutilityTagVersion;
	extern const FName SupportedClasses;
	extern const FName IsActionForBlueprint;
	extern const FName CallableFunctions;
}

struct FAssetData;

/** 
 * Base class for all asset action-related utilities
 * Any functions/events that are exposed on derived classes that have the correct signature will be
 * included as menu options when right-clicking on a group of assets in the content browser.
 */
UCLASS(Abstract, hideCategories=(Object), Blueprintable)
class BLUTILITY_API UAssetActionUtility : public UEditorUtilityObject, public IEditorUtilityExtension
{
	GENERATED_BODY()

public:
	/**
	 * Return the class that this asset action supports (if not implemented, it will show up for all asset types)
	 * Do not do custom logic here based on the currently selected assets.
	 */
	UE_DEPRECATED(5.2, "GetSupportedClasses() instead, but ideally you're not requesting this directly and are instead using the FAssetActionUtilityPrototype to wrap access to an unload utility asset.")
	UFUNCTION(BlueprintPure, BlueprintImplementableEvent, Category="Assets", meta=(DeprecatedFunction, DeprecationMessage="If you were just returning a single class add it to the SupportedClasses array (you can find it listed in the Class Defaults).  If you were doing complex logic to simulate having multiple classes act as filters, add them to the SupportedClasses array.  If you were doing 'other' logic, you'll need to do that upon action execution."))
	UClass* GetSupportedClass() const;

	/**
	 * Returns whether or not this action is designed to work specifically on Blueprints (true) or on all assets (false).
	 * If true, GetSupportedClass() is treated as a filter against the Parent Class of selected Blueprint assets
	 */
	UFUNCTION(BlueprintPure, BlueprintImplementableEvent, Category="Assets")
	bool IsActionForBlueprints() const;

	/**
	 * Gets the statically determined supported classes, these classes are used as a first pass filter when determining
	 * if we can utilize this asset utility action on the asset.
	 */
	UFUNCTION(BlueprintPure, Category = "Assets")
	const TArray<TSoftClassPtr<UObject>>& GetSupportedClasses() const { return SupportedClasses; }

public:
	// Begin UObject
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	// End UObject

protected:
	/**
	 * For simple Asset Action's you should fill out the supported class here.  Don't bother with GetSupportedClass()
	 * * unless you actually need to do specialized dynamic logic.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Assets", meta=(AllowAbstract))
	TArray<TSoftClassPtr<UObject>> SupportedClasses;
};