// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "ModifierHierarchyRules.generated.h"

class UVCamComponent;
class UVCamModifier;

/**
 * Defines a tree hierarchy. Each node is called a group.
 * A group consists of modifiers and (sub) groups.
 * 
 * An example use case is if you want to have a button menu which should procedurally generate sub-button menus depending
 * on the modifiers in the component. 
 * One group could be a Lens group which groups together modifiers that e.g. modify focal distance, filmback, and FOV.
 * The rules are defined generically enough so as modifiers are added or removed, the groupings also update accordingly.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew)
class VCAMEXTENSIONS_API UModifierHierarchyRules : public UObject
{
	GENERATED_BODY()
public:

	/** Gets the root of the tree. */
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Virtual Camera|Hierarchies")
	FName GetRootGroup() const;

	/** Gets the parent of this given group. Fails if called on the root node. */
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Virtual Camera|Hierarchies")
	bool GetParentGroup(FName ChildGroup, FName& ParentGroup) const;

	/**
	 * Gets the group the modifier belongs to.
	 * @return True if the modifier belongs to any group
	 */
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Virtual Camera|Hierarchies")
	bool GetGroupOfModifier(UVCamModifier* Modifier, FName& Group) const;

	/** Gets the child groups of the given group. */
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Virtual Camera|Hierarchies")
	TSet<FName> GetChildGroups(FName ParentGroup) const;

	/** Gets all the modifiers on the component that belong in the given group. */
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Virtual Camera|Hierarchies")
	TSet<UVCamModifier*> GetModifiersInGroup(UVCamComponent* Component, FName GroupName) const;
};
