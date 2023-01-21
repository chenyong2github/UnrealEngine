// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hierarchies/ModifierHierarchyAsset.h"

#include "Hierarchies/ClassBasedModifierHierarchyRules.h"
#include "Hierarchies/ModifierHierarchyRules.h"

FName UModifierHierarchyAsset::GetRootGroup() const
{
	return Rules ? Rules->GetRootGroup() : NAME_None;
}

bool UModifierHierarchyAsset::GetGroupOfModifier(UVCamModifier* Modifier, FName& Group) const
{
	return Rules && Rules->GetGroupOfModifier(Modifier, Group);
}

TSet<FName> UModifierHierarchyAsset::GetChildGroups_Implementation(FName ParentGroup) const
{
	return Rules ? Rules->GetChildGroups(ParentGroup) : TSet<FName>{};
}

TSet<UVCamModifier*> UModifierHierarchyAsset::GetModifiersInGroup(UVCamComponent* Component, FName GroupName) const
{
	return Rules ? Rules->GetModifiersInGroup(Component, GroupName) : TSet<UVCamModifier*>{};
}
