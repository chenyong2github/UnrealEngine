// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConjunctionFilter.h"

#include "NegatableFilter.h"
#include "Templates/SubclassOf.h"

namespace
{
	using ConjunctionFilterCallback = TFunction<EFilterResult::Type(UNegatableFilter* Child)>;

	EFilterResult::Type AndChain(const TArray<UNegatableFilter*>& Children, ConjunctionFilterCallback FilterCallback)
	{
		bool bAtLeastOneFilterSaidInclude = false;

		for (UNegatableFilter* ChildFilter : Children)
		{
			const EFilterResult::Type ChildFilterResult = FilterCallback(ChildFilter);

			// Suppose: A and B. If A == false, no need to evaluate B.
			const bool bShortCircuitAndChain = !EFilterResult::CanInclude(ChildFilterResult);
			if (bShortCircuitAndChain)
			{
				return EFilterResult::Exclude;
			}
			
			bAtLeastOneFilterSaidInclude |= EFilterResult::ShouldInclude(ChildFilterResult);
		}

		return bAtLeastOneFilterSaidInclude ? EFilterResult::Include : EFilterResult::DoNotCare;
	}
	
	EFilterResult::Type ExecuteAndChain(const TArray<UNegatableFilter*>& Children, EEditorFilterBehavior EditorFilterBehavior, ConjunctionFilterCallback FilterCallback)
	{
		if (Children.Num() == 0 
            || EditorFilterBehavior == EEditorFilterBehavior::Ignore
            || !ensure(EditorFilterBehavior != EEditorFilterBehavior::Mixed))
		{
			return EFilterResult::DoNotCare;
		}
		
		const EFilterResult::Type IntermediateResult = AndChain(Children, FilterCallback);
		const bool bShouldNegate = EditorFilterBehavior == EEditorFilterBehavior::Negate;
		return bShouldNegate ? EFilterResult::Negate(IntermediateResult) : IntermediateResult;
	}
}

UNegatableFilter* UConjunctionFilter::CreateChild(const TSubclassOf<ULevelSnapshotFilter>& FilterClass)
{
	if (!ensure(FilterClass.Get()))
	{
		return nullptr;
	}

	ULevelSnapshotFilter* FilterImplementation = NewObject<ULevelSnapshotFilter>(this, FilterClass.Get());
	UNegatableFilter* Child = UNegatableFilter::CreateNegatableFilter(FilterImplementation, this);

	// Set Child parent
	Child->SetParentFilter(this);

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

EFilterResult::Type UConjunctionFilter::IsActorValid(const FIsActorValidParams& Params) const
{
	return ExecuteAndChain(Children, EditorFilterBehavior, [&Params](UNegatableFilter* Child)
		{
			return Child->IsActorValid(Params);
		});
}

EFilterResult::Type UConjunctionFilter::IsPropertyValid(const FIsPropertyValidParams& Params) const
{
	return ExecuteAndChain(Children, EditorFilterBehavior, [&Params](UNegatableFilter* Child)
		{
			return Child->IsPropertyValid(Params);
		});
}

EFilterResult::Type UConjunctionFilter::IsDeletedActorValid(const FIsDeletedActorValidParams& Params) const
{
	return ExecuteAndChain(Children, EditorFilterBehavior, [&Params](UNegatableFilter* Child)
		{
			return Child->IsDeletedActorValid(Params);
		});
}

EFilterResult::Type UConjunctionFilter::IsAddedActorValid(const FIsAddedActorValidParams& Params) const
{
	return ExecuteAndChain(Children, EditorFilterBehavior, [&Params](UNegatableFilter* Child)
		{
			return Child->IsAddedActorValid(Params);
		});
}

TArray<UEditorFilter*> UConjunctionFilter::GetEditorChildren()
{
	TArray<UEditorFilter*> EditorFilterChildren;

	for (UNegatableFilter* ChildFilter : Children)
	{
		EditorFilterChildren.Add(ChildFilter);
	}

	return EditorFilterChildren;
}

void UConjunctionFilter::IncrementEditorFilterBehavior(const bool bIncludeChildren)
{
	switch (EditorFilterBehavior)
	{
	case EEditorFilterBehavior::DoNotNegate:
		EditorFilterBehavior = EEditorFilterBehavior::Negate;
		break;
	case EEditorFilterBehavior::Negate:
		EditorFilterBehavior = EEditorFilterBehavior::Ignore;
		break;
	case EEditorFilterBehavior::Ignore:
		EditorFilterBehavior = EEditorFilterBehavior::DoNotNegate;
		break;
	default:
		checkNoEntry();
	}

	if (bIncludeChildren)
	{
		UpdateAllChildrenEditorFilterBehavior(EditorFilterBehavior, bIncludeChildren);
	}
}

void UConjunctionFilter::SetEditorFilterBehavior(const EEditorFilterBehavior InFilterBehavior, const bool bIncludeChildren)
{
	if (ensureMsgf(EditorFilterBehavior != EEditorFilterBehavior::Mixed, TEXT("Internal error. Conjunction filter cannot have mixed behaviour.")))
	{
		EditorFilterBehavior = InFilterBehavior;
	}
}
