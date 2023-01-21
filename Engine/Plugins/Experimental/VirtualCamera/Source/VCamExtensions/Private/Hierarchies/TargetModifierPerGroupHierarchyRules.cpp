// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hierarchies/TargetModifierPerGroupHierarchyRules.h"

#include "HierarchyUtils.h"
#include "VCamComponent.h"
#include "VCamExtensionsLog.h"

UTargetModifierPerGroupHierarchyRules::UTargetModifierPerGroupHierarchyRules()
{
	Root = CreateDefaultSubobject<USingleModifierPerGroupWithTargetSettings>(TEXT("Root"));
}

FName UTargetModifierPerGroupHierarchyRules::GetRootGroup_Implementation() const
{
	return ensure(Root) ? Root->GroupName : NAME_None;
}

bool UTargetModifierPerGroupHierarchyRules::GetParentGroup_Implementation(FName ChildGroup, FName& ParentGroup) const
{
	bool bFound = false;
	ForEachGroup([ChildGroup, &ParentGroup, &bFound](USingleModifierPerGroupWithTargetSettings& Group, USingleModifierPerGroupWithTargetSettings* Parent)
	{
		if (Group.GroupName == ChildGroup)
		{
			ParentGroup = Parent->GroupName;
			bFound = true;
			return EBreakBehavior::Break;
		}
		return EBreakBehavior::Continue;
	});
	return bFound;
}

bool UTargetModifierPerGroupHierarchyRules::GetGroupOfModifier_Implementation(UVCamModifier* Modifier, FName& Group) const
{
	if (const USingleModifierPerGroupWithTargetSettings* FoundGroup = GetNodeForModifier(Modifier))
	{
		Group = FoundGroup->GroupName;
		return true;
	}
	return false;
}

bool UTargetModifierPerGroupHierarchyRules::GetConnectionPointForModifier_Implementation(UVCamModifier* Modifier, FName& ConnectionPoint) const
{
	const USingleModifierPerGroupWithTargetSettings* FoundGroup = GetNodeForModifier(Modifier);
	if (!FoundGroup || !Modifier->ConnectionPoints.Contains(FoundGroup->TargetSettings.TargetConnectionPoint))
	{
		return false;
	}

	ConnectionPoint = FoundGroup->TargetSettings.TargetConnectionPoint;
	return true;
}

TSet<FName> UTargetModifierPerGroupHierarchyRules::GetChildGroups_Implementation(FName ParentGroup) const
{
	if (const USingleModifierPerGroupWithTargetSettings* Group = FindGroupByName(ParentGroup))
	{
		TSet<FName> Result;
		Algo::TransformIf(Group->ChildElements, Result, [](const TObjectPtr<USingleModifierPerGroupWithTargetSettings>& Child){ return Child != nullptr; }, [](const TObjectPtr<USingleModifierPerGroupWithTargetSettings>& Child){ return Child->GroupName; });
		return Result;
	}
	return {};
}

TSet<UVCamModifier*> UTargetModifierPerGroupHierarchyRules::GetModifiersInGroup_Implementation(UVCamComponent* Component, FName GroupName) const
{
	if (const USingleModifierPerGroupWithTargetSettings* Group = FindGroupByName(GroupName))
	{
		UVCamModifier* Modifier = Component->GetModifierByName(Group->TargetSettings.TargetModifierName);
		return Modifier ? TSet<UVCamModifier*>{ Modifier } : TSet<UVCamModifier*>{};
	}

	return {};
}

USingleModifierPerGroupWithTargetSettings* UTargetModifierPerGroupHierarchyRules::GetNodeForModifier(UVCamModifier* Modifier) const
{
	if (!Modifier)
	{
		return nullptr;
	}

	USingleModifierPerGroupWithTargetSettings* FoundGroup = nullptr;
	ForEachGroup([&FoundGroup, SearchName = Modifier->GetStackEntryName()](USingleModifierPerGroupWithTargetSettings& CurrentGroup, USingleModifierPerGroupWithTargetSettings* Parent)
	{
		if (CurrentGroup.TargetSettings.TargetModifierName == SearchName)
		{
			FoundGroup = &CurrentGroup;
			return EBreakBehavior::Break; 
		}
		return EBreakBehavior::Continue;
	});
	return FoundGroup;
}

USingleModifierPerGroupWithTargetSettings* UTargetModifierPerGroupHierarchyRules::FindGroupByName(FName GroupName) const
{
	USingleModifierPerGroupWithTargetSettings* FoundGroup = nullptr;
	ForEachGroup([GroupName, &FoundGroup](USingleModifierPerGroupWithTargetSettings& Group, USingleModifierPerGroupWithTargetSettings* Parent)
	{
		if (Group.GroupName == GroupName)
		{
			FoundGroup = &Group;
			return EBreakBehavior::Break;
		}
		return EBreakBehavior::Continue;
	});
	UE_CLOG(FoundGroup == nullptr, LogVCamExtensions, Warning, TEXT("GroupName %s not found (%s)"), *GroupName.ToString(), *GetPathName());
	return FoundGroup;
}

void UTargetModifierPerGroupHierarchyRules::ForEachGroup(TFunctionRef<EBreakBehavior(USingleModifierPerGroupWithTargetSettings& CurrentGroup, USingleModifierPerGroupWithTargetSettings* Parent)> Callback) const
{
	if (!ensure(Root))
	{
		return;
	}

	using namespace UE::VCamExtensions;
	HierarchyUtils::ForEachGroup(
		*Root,
		[Callback](USingleModifierPerGroupWithTargetSettings& CurrentGroup, USingleModifierPerGroupWithTargetSettings* Parent)
		{
			return Callback(CurrentGroup, Parent) == EBreakBehavior::Continue ? HierarchyUtils::EBreakBehavior::Continue : HierarchyUtils::EBreakBehavior::Break;
		},
		[](USingleModifierPerGroupWithTargetSettings& CurrentGroup){ return CurrentGroup.ChildElements; }
		);
}
