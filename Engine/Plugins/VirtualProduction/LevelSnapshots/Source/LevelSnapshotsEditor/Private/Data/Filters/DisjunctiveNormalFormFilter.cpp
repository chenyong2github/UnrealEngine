// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisjunctiveNormalFormFilter.h"

#include "ConjunctionFilter.h"

namespace
{
	bool AreAllChildrenEmpty(const TArray<UConjunctionFilter*>& AndFilters)
	{
		if (AndFilters.Num() == 0)
		{
			return true;
		}
		
		for (UConjunctionFilter* Child : AndFilters)
		{
			const bool bIsEmpty = Child->GetChildren().Num() == 0;
			if (!bIsEmpty)
			{
				return false;
			}
		}
		return true;
	}
	
	using FilterCallback = TFunction<EFilterResult::Type(UConjunctionFilter* Child)>;
	EFilterResult::Type ExecuteOrChain(const TArray<UConjunctionFilter*>& Children, FilterCallback&& FilterCallback)
	{
		if (AreAllChildrenEmpty(Children))
		{
			// "Illogical" edge case: No filter specified
			// For better UX, we show all actors and properties to user
			// Logic says we should return DoNotCare
			return EFilterResult::Include;
		}
		
		bool bNoFilterSaidExclude = true;
		
		for (UConjunctionFilter* ChildFilter : Children)
		{
			const TEnumAsByte<EFilterResult::Type> ChildResult = FilterCallback(ChildFilter);

			// Suppose: A or B. If A == true, no need to evaluate B.
			const bool bShortCircuitOrChain = EFilterResult::ShouldInclude(ChildResult);
			if (bShortCircuitOrChain)
			{
				return EFilterResult::Include;
			}
			
			bNoFilterSaidExclude &= EFilterResult::CanInclude(ChildResult);
		}
		
		return bNoFilterSaidExclude ? EFilterResult::DoNotCare : EFilterResult::Exclude;
	}
}

UConjunctionFilter* UDisjunctiveNormalFormFilter::CreateChild()
{
	UConjunctionFilter* Child = NewObject<UConjunctionFilter>(this);
	Children.Add(Child);
	return Child;
}

void UDisjunctiveNormalFormFilter::RemoveConjunction(UConjunctionFilter* Child)
{
	const bool bRemovedChild = Children.RemoveSingle(Child) != 0;
	if (ensure(bRemovedChild))
	{
		Child->OnRemoved();
	}
}

const TArray<UConjunctionFilter*>& UDisjunctiveNormalFormFilter::GetChildren() const
{
	return Children;
}

EFilterResult::Type UDisjunctiveNormalFormFilter::IsActorValid(const FIsActorValidParams& Params) const
{
	return ExecuteOrChain(Children, [&Params](UConjunctionFilter* Child)
	{
		return Child->IsActorValid(Params);
	});
}

EFilterResult::Type UDisjunctiveNormalFormFilter::IsPropertyValid(const FIsPropertyValidParams& Params) const
{
	return ExecuteOrChain(Children, [&Params](UConjunctionFilter* Child)
	{
		return Child->IsPropertyValid(Params);
	});
}

EFilterResult::Type UDisjunctiveNormalFormFilter::IsDeletedActorValid(const FIsDeletedActorValidParams& Params) const
{
	return ExecuteOrChain(Children, [&Params](UConjunctionFilter* Child)
	{
		return Child->IsDeletedActorValid(Params);
	});
}

EFilterResult::Type UDisjunctiveNormalFormFilter::IsAddedActorValid(const FIsAddedActorValidParams& Params) const
{
	return ExecuteOrChain(Children, [&Params](UConjunctionFilter* Child)
	{
		return Child->IsAddedActorValid(Params);
	});
}
