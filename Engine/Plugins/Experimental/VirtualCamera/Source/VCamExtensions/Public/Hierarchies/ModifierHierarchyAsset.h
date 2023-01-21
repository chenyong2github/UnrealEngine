// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "ModifierHierarchyAsset.generated.h"

class UModifierHierarchyRules;
class UVCamComponent;
class UVCamModifier;

/**
 * An asset intended to be referenced by Slate widgets.
 *
 * Defines a tree hierarchy. Nodes are called groups.
 * A group consists of modifiers and (sub) groups.
 * 
 * An example use case is if you want to have a button menu which should procedurally generate sub-button menus depending
 * on the modifiers in the component. 
 * One group could be a Lens group which groups together modifiers that e.g. modify focal distance, filmback, and FOV.
 * The rules are defined generically enough so as modifiers are added or removed, the groupings also update accordingly.
 */
UCLASS(BlueprintType)
class VCAMEXTENSIONS_API UModifierHierarchyAsset : public UObject
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Virtual Camera")
	TObjectPtr<UModifierHierarchyRules> Rules;

	/** Gets the root of the tree. */
	UFUNCTION(BlueprintPure, Category = "Virtual Camera")
	FName GetRootGroup() const;

	/**
	 * Gets the group the modifier belongs to.
	 * @return True if the modifier belongs to any group
	 */
	UFUNCTION(BlueprintPure, Category = "Virtual Camera")
	bool GetGroupOfModifier(UVCamModifier* Modifier, FName& Group) const;

	/** Gets the child groups of the given group. */
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Virtual Camera")
	TSet<FName> GetChildGroups(FName ParentGroup) const;

	/** Gets all the modifiers on the component that belong in the given group. */
	UFUNCTION(BlueprintPure, Category = "Virtual Camera")
	TSet<UVCamModifier*> GetModifiersInGroup(UVCamComponent* Component, FName GroupName) const;
};