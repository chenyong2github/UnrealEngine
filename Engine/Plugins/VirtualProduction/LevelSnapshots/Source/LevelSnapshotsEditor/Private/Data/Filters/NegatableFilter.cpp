// Copyright Epic Games, Inc. All Rights Reserved.

#include "NegatableFilter.h"

namespace
{
	EFilterResult::Type ApplyFilter(EFilterBehavior NegationBehaviour, TFunction<EFilterResult::Type()> ApplyFilter)
	{
		if (NegationBehaviour == EFilterBehavior::Ignore)
		{
			return EFilterResult::DoNotCare; 
		}

		const EFilterResult::Type Result = ApplyFilter();
		const bool bShouldNegate = NegationBehaviour == EFilterBehavior::Negate;
		return bShouldNegate ? EFilterResult::Negate(Result) : Result;
	}
}

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

void UNegatableFilter::IncrementFilterBehaviour()
{
	switch (FilterBehavior)
	{
	case EFilterBehavior::DoNotNegate:
		SetFilterBehaviour(EFilterBehavior::Negate);
		break;
	case EFilterBehavior::Negate:
		SetFilterBehaviour(EFilterBehavior::Ignore);
		break;
	case EFilterBehavior::Ignore:
		SetFilterBehaviour(EFilterBehavior::DoNotNegate);
		break;
	default:
		// Needed for data migration
		SetFilterBehaviour(EFilterBehavior::DoNotNegate);
	}
}

void UNegatableFilter::SetFilterBehaviour(EFilterBehavior NewFilterBehavior)
{
	FilterBehavior = NewFilterBehavior;
}

void UNegatableFilter::OnRemoved()
{
	OnFilterDestroyed.Broadcast(this);
}

FText UNegatableFilter::GetDisplayName() const
{
	// Child filter is nullptr when user deletes filter class and force deletes
	if (!ChildFilter)
	{
		return FText::FromString(TEXT("Missing Filter Class"));
	}

	const bool bHasName = Name.Len() != 0;
	return bHasName ? FText::FromString(Name) : ChildFilter->GetClass()->GetDisplayNameText();
}

EFilterResult::Type UNegatableFilter::IsActorValid(const FIsActorValidParams& Params) const
{
	return ApplyFilter(FilterBehavior, [this, &Params]()
	{
		// Child filter is nullptr when user deletes filter class and force deletes
		return ChildFilter ? ChildFilter->IsActorValid(Params) : EFilterResult::DoNotCare;
	});
}

EFilterResult::Type UNegatableFilter::IsPropertyValid(const FIsPropertyValidParams& Params) const
{
	return ApplyFilter(FilterBehavior, [this, &Params]()
	{
		// Child filter is nullptr when user deletes filter class and force deletes
		return ChildFilter ? ChildFilter->IsPropertyValid(Params) : EFilterResult::DoNotCare;
	});
}

EFilterResult::Type UNegatableFilter::IsDeletedActorValid(const FIsDeletedActorValidParams& Params) const
{
	return ApplyFilter(FilterBehavior, [this, &Params]()
	{
		// Child filter is nullptr when user deletes filter class and force deletes
		return ChildFilter ? ChildFilter->IsDeletedActorValid(Params) : EFilterResult::DoNotCare;
	});
}

EFilterResult::Type UNegatableFilter::IsAddedActorValid(const FIsAddedActorValidParams& Params) const
{
	return ApplyFilter(FilterBehavior, [this, &Params]()
	{
		// Child filter is nullptr when user deletes filter class and force deletes
		return ChildFilter ? ChildFilter->IsAddedActorValid(Params) : EFilterResult::DoNotCare;
	});
}
