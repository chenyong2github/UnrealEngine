// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorFilter.h"

void UEditorFilter::PostLoad()
{
	Super::PostLoad();

	TArray<UEditorFilter*> Children = GetEditorChildren();

	// Set parent to child if it nullptr
	for (UEditorFilter* ChildFilter : Children)
	{
		if (ChildFilter->GetParentFilter() == nullptr)
		{
			ChildFilter->SetParentFilter(this);
		}
	}
}

void UEditorFilter::SetEditorFilterBehavior(const EEditorFilterBehavior InFilterBehavior, const bool bIncludeChildren)
{
	EditorFilterBehavior = InFilterBehavior;

	if (bIncludeChildren)
	{
		UpdateAllChildrenEditorFilterBehavior(InFilterBehavior, bIncludeChildren);
	}
}

void UEditorFilter::IncrementEditorFilterBehavior(const bool bIncludeChildren)
{
	switch (EditorFilterBehavior)
	{
	case EEditorFilterBehavior::DoNotNegate:
		EditorFilterBehavior = EEditorFilterBehavior::Negate;
		break;
	case EEditorFilterBehavior::Mixed:
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

void UEditorFilter::UpdateAllChildrenEditorFilterBehavior(EEditorFilterBehavior InFilterBehavior, const bool bIncludeGrandChildren)
{
	for (UEditorFilter* ChildFilter : GetEditorChildren())
	{
		ChildFilter->SetEditorFilterBehavior(InFilterBehavior, bIncludeGrandChildren);
	}
}

void UEditorFilter::SetEditorFilterBehaviorFromChild()
{
	TArray<UEditorFilter*> Children = GetEditorChildren();

	if (Children.IsValidIndex(0))
	{
		SetEditorFilterBehaviorFromChild(Children[0]->GetEditorFilterBehavior(), Children);
	}
	else
	{
		// Set default filter
		EditorFilterBehavior = EEditorFilterBehavior::DoNotNegate;
	}
}

void UEditorFilter::SetEditorFilterBehaviorFromChild(const EEditorFilterBehavior InFilterBehavior, TArray<UEditorFilter*> InChildren)
{
	TArray<UEditorFilter*> Children = InChildren.Num() ? InChildren : GetEditorChildren();

	bool bAllChildrenSame = true;

	for (const UEditorFilter* ChildFilter : Children)
	{
		if (ChildFilter->GetEditorFilterBehavior() != InFilterBehavior)
		{
			bAllChildrenSame = false;
			break;
		}
	}

	// If all children filters are the same we can apply that one to the parent as well
	if (bAllChildrenSame)
	{
		EditorFilterBehavior = InFilterBehavior;
	}
	// Otherwice DoNotCare
	else
	{
		EditorFilterBehavior = EEditorFilterBehavior::Mixed;
	}
}

void UEditorFilter::UpdateParentFilterFromChild(const bool bRecursively)
{
	if (ParentFilter != nullptr)
	{
		ParentFilter->SetEditorFilterBehaviorFromChild(EditorFilterBehavior);

		if (bRecursively)
		{
			ParentFilter->UpdateParentFilterFromChild(bRecursively);
		}
	}
}

void UEditorFilter::SetParentFilter(UEditorFilter* InEditorFilter)
{
	ParentFilter = InEditorFilter;
}

