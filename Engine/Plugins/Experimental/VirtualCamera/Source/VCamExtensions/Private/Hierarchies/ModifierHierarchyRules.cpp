// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hierarchies/ModifierHierarchyRules.h"

#include "VCamExtensionsLog.h"

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

bool UModifierHierarchyRules::GetConnectionPointForModifier_Implementation(UVCamModifier* Modifier, FName& ConnectionPoint) const
{
	UE_LOG(LogVCamExtensions, Warning, TEXT("%s does not support GetConnectionPointForModifier"), *GetClass()->GetName());
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
