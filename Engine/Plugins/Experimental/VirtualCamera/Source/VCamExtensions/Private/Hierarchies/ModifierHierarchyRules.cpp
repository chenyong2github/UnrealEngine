// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hierarchies/ModifierHierarchyRules.h"

FName UModifierHierarchyRules::GetRootGroup_Implementation() const
{
	unimplemented();
	return {};
}

bool UModifierHierarchyRules::GetParentGroup_Implementation(FName ChildGroup, FName& ParentGroup) const
{
	return false;
}

bool UModifierHierarchyRules::GetGroupOfModifier_Implementation(UVCamModifier* Modifier, FName& Group) const
{
	unimplemented();
	return false;
}

TSet<FName> UModifierHierarchyRules::GetChildGroups_Implementation(FName ParentGroup) const
{
	unimplemented();
	return {};
}

TSet<UVCamModifier*> UModifierHierarchyRules::GetModifiersInGroup_Implementation(UVCamComponent* Component, FName GroupName) const
{
	unimplemented();
	return {};
}

