// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hierarchies/ClassBasedModifierHierarchyRules.h"

#include "VCamComponent.h"

#include "Algo/AllOf.h"

UClassBasedModifierHierarchyRules::UClassBasedModifierHierarchyRules()
{
	RootGroup = CreateDefaultSubobject<UClassBasedModifierGroup>(TEXT("Root"));
}

FName UClassBasedModifierHierarchyRules::GetRootGroup_Implementation() const
{
	return ensure(RootGroup)
		? RootGroup->GroupName
		: NAME_None;
}

bool UClassBasedModifierHierarchyRules::GetParentGroup_Implementation(FName ChildGroup, FName& ParentGroup) const
{
	bool bFound = false;
	ForEachGroup([ChildGroup, &ParentGroup, &bFound](UClassBasedModifierGroup& Group, UClassBasedModifierGroup* Parent)
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

bool UClassBasedModifierHierarchyRules::GetGroupOfModifier_Implementation(UVCamModifier* Modifier, FName& Group) const
{
	if (!Modifier)
	{
		return false;
	}

	if (UClassBasedModifierGroup* MatchingGroup = FindBestMatchFor(*Modifier))
	{
		Group = MatchingGroup->GroupName;
		return true;
	}
	return false;
}

TSet<FName> UClassBasedModifierHierarchyRules::GetChildGroups_Implementation(FName ParentGroup) const
{
	if (const UClassBasedModifierGroup* FoundGroup = FindGroupByName(ParentGroup))
	{
		TSet<FName> Result;
		Algo::TransformIf(FoundGroup->Children, Result, [](const TObjectPtr<UClassBasedModifierGroup>& Child){ return Child != nullptr; }, [](const TObjectPtr<UClassBasedModifierGroup>& Child){ return Child->GroupName; });
		return Result;
	}
	
	return {};
}

TSet<UVCamModifier*> UClassBasedModifierHierarchyRules::GetModifiersInGroup_Implementation(UVCamComponent* Component, FName GroupName) const
{
	UClassBasedModifierGroup* FoundGroup = FindGroupByName(GroupName);
	return FoundGroup
		? EnumerateModifiersInGroup(*FoundGroup, *Component)
		: TSet<UVCamModifier*>{};
}

UClassBasedModifierGroup* UClassBasedModifierHierarchyRules::FindGroupByName(FName GroupName) const
{
	UClassBasedModifierGroup* FoundGroup = nullptr;
	ForEachGroup([GroupName, &FoundGroup](UClassBasedModifierGroup& Group, UClassBasedModifierGroup* Parent)
	{
		if (Group.GroupName == GroupName)
		{
			FoundGroup = &Group;
			return EBreakBehavior::Break;
		}
		return EBreakBehavior::Continue;
	});
	return FoundGroup;
}

TSet<UVCamModifier*> UClassBasedModifierHierarchyRules::EnumerateModifiersInGroup(UClassBasedModifierGroup& Group, UVCamComponent& Component) const
{
	TSet<UVCamModifier*> AllModifiers;
	TArray<UVCamModifier*> ModifersOfSameClass;
		
	for (const TSubclassOf<UVCamModifier>& ModifierClass : Group.ModifierClasses)
	{
		ModifersOfSameClass.Empty(Component.GetNumberOfModifiers());
		Component.GetModifiersByClass(ModifierClass, ModifersOfSameClass);
		AllModifiers.Append(ModifersOfSameClass);
	}

	// GetModifiersByClass gets all modifiers that are a subclass.
	// If a more specific class is specified on a different node, then this modifier should not be put into the current group
	
	// This implementation has a really bad complexity of O(n*m) ~ O(n^2) ... where n = number of nodes, m = AllModifiers.Num()
	// We could improve it by caching with a TMap which nodes store what classes. 
	for (auto AllModifierIt = AllModifiers.CreateIterator(); AllModifierIt; ++AllModifierIt)
	{
		if (FindBestMatchFor(**AllModifierIt) != &Group)
		{
			AllModifierIt.RemoveCurrent();
		}
	}

	return AllModifiers;
}

UClassBasedModifierGroup* UClassBasedModifierHierarchyRules::FindBestMatchFor(UVCamModifier& Modifier) const
{
	TArray<TPair<TSubclassOf<UVCamModifier>, UClassBasedModifierGroup&>> MatchingGroups;
	ForEachGroup([&Modifier, &MatchingGroups](UClassBasedModifierGroup& Group, UClassBasedModifierGroup* Parent)
	{
		for (const TSubclassOf<UVCamModifier>& Class : Group.ModifierClasses)
		{
			if (Modifier.IsA(Class))
			{
				MatchingGroups.Emplace(Class, Group);
			}
		}
		
		return EBreakBehavior::Continue;
	});

	auto GetInheritanceDistance = [](TSubclassOf<UVCamModifier> Base, TSubclassOf<UVCamModifier> Subclass)
	{
		int32 Distance = 0;
		for (UClass* Current = Subclass; Current != Base; Current = Current->GetSuperClass())
		{
			++Distance;
		}
		return Distance;
	};
	
	uint32 CurrentLowestDistance = GetInheritanceDistance(UVCamModifier::StaticClass(), Modifier.GetClass());
	UClassBasedModifierGroup* ClosestGroup = nullptr;
	for (const TPair<TSubclassOf<UVCamModifier>, UClassBasedModifierGroup&>& Pair : MatchingGroups)
	{
		const uint32 CurrentDistance = GetInheritanceDistance(Pair.Key, Modifier.GetClass());
		if (CurrentLowestDistance > CurrentDistance)
		{
			CurrentLowestDistance = CurrentDistance;
			ClosestGroup = &Pair.Value;
		}
	}
	return ClosestGroup;
}

void UClassBasedModifierHierarchyRules::ForEachGroup(TFunctionRef<EBreakBehavior(UClassBasedModifierGroup& CurrentGroup, UClassBasedModifierGroup* Parent)> Callback) const
{
	if (!ensure(RootGroup))
	{
		return;
	}

	TQueue<TPair<UClassBasedModifierGroup*, UClassBasedModifierGroup*>> Queue;
	Queue.Enqueue({ RootGroup, nullptr });

	TPair<UClassBasedModifierGroup*, UClassBasedModifierGroup*> Current;
	while (Queue.Dequeue(Current))
	{
		UClassBasedModifierGroup* CurrentNode = Current.Key;
		UClassBasedModifierGroup* Parent = Current.Value;
		
		const EBreakBehavior BreakBehavior = Callback(*CurrentNode, Parent);
		if (BreakBehavior == EBreakBehavior::Break)
		{
			return;
		}
		
		for (UClassBasedModifierGroup* Child : CurrentNode->Children)
		{
			if (Child)
			{
				Queue.Enqueue({ Child, CurrentNode });
			}
		}
	}
}
