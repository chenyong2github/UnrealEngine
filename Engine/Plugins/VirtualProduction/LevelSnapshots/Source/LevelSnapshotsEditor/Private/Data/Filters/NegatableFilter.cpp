// Copyright Epic Games, Inc. All Rights Reserved.

#include "NegatableFilter.h"

UNegatableFilter* UNegatableFilter::CreateNegatableFilter(ULevelSnapshotFilter* ChildFilter, const TOptional<UObject*>& Outer)
{
	if (!ensure(ChildFilter))
	{
		return nullptr;
	}
	
	UObject* NewFilterOuter = Outer.Get(ChildFilter->GetOuter());
	UNegatableFilter* Result = NewObject<UNegatableFilter>(NewFilterOuter);
	Result->ChildFilter = ChildFilter;
	return Result;
}

void UNegatableFilter::SetShouldNegate(bool bNewShouldNegate)
{
	bShouldNegate = bNewShouldNegate;
}

bool UNegatableFilter::ShouldNegate() const
{
	return bShouldNegate;
}

ULevelSnapshotFilter* UNegatableFilter::GetChildFilter() const
{
	return ChildFilter;
}

FText UNegatableFilter::GetDisplayName() const
{
	/* TODO: Editor needs to create a details customization so user can give filters names */
	return ChildFilter->GetClass()->GetDisplayNameText();
}

bool UNegatableFilter::IsActorValid(const FName ActorName, const UClass* ActorClass) const
{
	if (ensure(ChildFilter))
	{
		const bool bResult = ChildFilter->IsActorValid(ActorName, ActorClass);
		return (bShouldNegate && !bResult) || (!bShouldNegate && bResult);
	}
	return true;
}

bool UNegatableFilter::IsPropertyValid(const FName ActorName, const UClass* ActorClass, const FString& PropertyName) const
{
	if (ensure(ChildFilter))
	{
		const bool bResult = ChildFilter->IsPropertyValid(ActorName, ActorClass, PropertyName);
		return (bShouldNegate && !bResult) || (!bShouldNegate && bResult);
	}
	return true;
}