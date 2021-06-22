// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisjunctiveNormalFormFilter.h"

#include "Data/Filters/ConjunctionFilter.h"

#include "Misc/ITransaction.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"

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

void UDisjunctiveNormalFormFilter::MarkTransactional()
{
	SetFlags(RF_Transactional);
	for (UConjunctionFilter* Child : Children)
	{
		Child->MarkTransactional();
	}
}

UConjunctionFilter* UDisjunctiveNormalFormFilter::CreateChild()
{
	UConjunctionFilter* Child;

	{
		FScopedTransaction Transaction(FText::FromString("Add filter row"));
		Modify();
		
		Child = NewObject<UConjunctionFilter>(this, UConjunctionFilter::StaticClass(), NAME_None, RF_Transactional);
		Children.Add(Child);

		OnFilterModified.Broadcast(EFilterChangeType::RowAdded);
		
		bHasJustCreatedNewChild = true;
	}
	// OnObjectTransacted will be triggered twice when a ConjunctionFilter is created, this scope ignores the second call
	bHasJustCreatedNewChild = false;
	
	
	return Child;
}

void UDisjunctiveNormalFormFilter::RemoveConjunction(UConjunctionFilter* Child)
{
	FScopedTransaction Transaction(FText::FromString("Remove filter row"));
	Modify();
	
	const bool bRemovedChild = Children.RemoveSingle(Child) != 0;
	if (ensure(bRemovedChild))
	{
		Child->OnRemoved();
		OnFilterModified.Broadcast(EFilterChangeType::RowRemoved);
	}
}

const TArray<UConjunctionFilter*>& UDisjunctiveNormalFormFilter::GetChildren() const
{
	return Children;
}

void UDisjunctiveNormalFormFilter::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		OnObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddLambda([this](UObject* ModifiedObject, const FTransactionObjectEvent& TransactionInfo)
		{	
			if (IsValid(ModifiedObject) && ModifiedObject->IsIn(this) && ModifiedObject != this && !bHasJustCreatedNewChild)
			{
				const bool bModifiedChildren = Cast<UConjunctionFilter>(ModifiedObject) && TransactionInfo.GetChangedProperties().Contains(UConjunctionFilter::GetChildrenMemberName());
				OnFilterModified.Broadcast(bModifiedChildren ? EFilterChangeType::RowChildFilterAddedOrRemoved : EFilterChangeType::FilterPropertyModified);
			}
		});
	}
}

void UDisjunctiveNormalFormFilter::BeginDestroy()
{
	if (OnObjectTransactedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectTransacted.Remove(OnObjectTransactedHandle);
		OnObjectTransactedHandle.Reset();
	}

	Super::BeginDestroy();
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
