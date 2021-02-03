// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisjunctiveNormalFormFilter.h"

#include "ConjunctionFilter.h"

UConjunctionFilter* UDisjunctiveNormalFormFilter::CreateChild()
{
	UConjunctionFilter* Child = NewObject<UConjunctionFilter>(this);
	Children.Add(Child);
	return Child;
}

void UDisjunctiveNormalFormFilter::RemoveConjunction(UConjunctionFilter* Child)
{
	const bool bRemovedChild = Children.RemoveSingle(Child) != 0;
	check(bRemovedChild);
}

const TArray<UConjunctionFilter*>& UDisjunctiveNormalFormFilter::GetChildren() const
{
	return Children;
}

bool UDisjunctiveNormalFormFilter::IsActorValid(const FName ActorName, const UClass* ActorClass) const
{
	if (Children.Num() == 0)
	{
		return true;
	}

	bool bResult = false;
	for (UConjunctionFilter* ChildFilter : Children)
	{
		bResult |= ChildFilter->IsActorValid(ActorName, ActorClass);
	}
	return bResult;
}

bool UDisjunctiveNormalFormFilter::IsPropertyValid(const FName ActorName, const UClass* ActorClass, const FString& PropertyName) const
{
	if (Children.Num() == 0)
	{
		return true;
	}

	bool bResult = false;
	for (UConjunctionFilter* ChildFilter : Children)
	{
		bResult |= ChildFilter->IsPropertyValid(ActorName, ActorClass, PropertyName);
	}
	return bResult;
}
