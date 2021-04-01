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

ULevelSnapshotFilter* UNegatableFilter::GetChildFilter() const
{
	return ChildFilter;
}

FText UNegatableFilter::GetDisplayName() const
{
	const bool bHasName = Name.Len() != 0;
	return bHasName ? FText::FromString(Name) : ChildFilter->GetClass()->GetDisplayNameText();
}

EFilterResult::Type UNegatableFilter::IsActorValid(const FIsActorValidParams& Params) const
{
	if (EditorFilterBehavior == EEditorFilterBehavior::Ignore)
	{
		return EFilterResult::DoNotCare; // This filter is always used in an AND-chain
	}

	if (ensure(ChildFilter))
	{
		EFilterResult::Type Result = ChildFilter->IsActorValid(Params);
		const bool bShouldNegate = EditorFilterBehavior == EEditorFilterBehavior::Negate;
		return bShouldNegate ? EFilterResult::Negate(Result) : Result;
	}
	return EFilterResult::DoNotCare;
}

EFilterResult::Type UNegatableFilter::IsPropertyValid(const FIsPropertyValidParams& Params) const
{
	if (EditorFilterBehavior == EEditorFilterBehavior::Ignore)
	{
		return EFilterResult::DoNotCare; // This filter is always used in an AND-chain
	}
	
	if (ensure(ChildFilter))
	{
		EFilterResult::Type Result = ChildFilter->IsPropertyValid(Params);
		const bool bShouldNegate = EditorFilterBehavior == EEditorFilterBehavior::Negate;
		return bShouldNegate ? EFilterResult::Negate(Result) : Result;
	}
	return EFilterResult::DoNotCare;
}
