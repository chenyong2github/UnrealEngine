// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConjunctionFilter.h"

#include "NegatableFilter.h"

UNegatableFilter* UConjunctionFilter::CreateChild(const TSubclassOf<ULevelSnapshotFilter>& FilterClass)
{
	if (!ensure(FilterClass.Get()))
	{
		return nullptr;
	}

	ULevelSnapshotFilter* FilterImplementation = NewObject<ULevelSnapshotFilter>(this, FilterClass.Get());
	UNegatableFilter* Child = UNegatableFilter::CreateNegatableFilter(FilterImplementation, this);
	Children.Add(Child);
	OnChildAdded.Broadcast(Child);
	
	return Child;
}

void UConjunctionFilter::RemoveChild(UNegatableFilter* Child)
{
	const bool bRemovedChild = Children.RemoveSingle(Child) != 0;
	check(bRemovedChild);
	if (bRemovedChild)
	{
		OnChildRemoved.Broadcast(Child);
	}
}

const TArray<UNegatableFilter*>& UConjunctionFilter::GetChildren() const
{
	return Children;
}

bool UConjunctionFilter::IsActorValid(const FName ActorName, const UClass* ActorClass) const
{
	if (Children.Num() == 0)
	{
		return false; // UConjunctionFilter is used by UDisjunctiveNormalFormFilter. Returning true would make the entire disjunction go true, which is unexpected to user.
	}
	
	bool Result = true;
	for (UNegatableFilter* ChildFilter : Children)
	{
		Result &= ChildFilter->IsActorValid(ActorName, ActorClass);
	}
	return Result;
}

bool UConjunctionFilter::IsPropertyValid(const FName ActorName, const UClass* ActorClass, const FString& PropertyName) const
{
	if (Children.Num() == 0)
	{
		return false; // UConjunctionFilter is used by UDisjunctiveNormalFormFilter. Returning true would make the entire disjunction go true, which is unexpected to user.
	}
	
	bool Result = true;
	for (UNegatableFilter* ChildFilter : Children)
	{
		Result &= ChildFilter->IsPropertyValid(ActorName, ActorClass, PropertyName);
	}
	return Result;
}