// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tree/CurveEditorTreeFilter.h"
#include "DisplayNodes/SequencerDisplayNode.h"


/**
 * A specialized filter for showing items in the curve editor selected from the sequencer panel.
 * This filter will store the selected nodes and all the parents of the selected nodes in a NodesToFilter set.
 * An item will pass if it is either directly selected or has a parent in the set.
 */
struct FSequencerSelectionCurveFilter : FCurveEditorTreeFilter
{
	static const int32 FilterPass = -1000;

	FSequencerSelectionCurveFilter()
		: FCurveEditorTreeFilter(ISequencerModule::GetSequencerSelectionFilterType(), FilterPass)
	{}

	/**
	 * Adds all selected nodes and their object parents to the NodesToFilter set
	 */
	void Update(const TSet<TSharedRef<FSequencerDisplayNode>>& SelectedNodes)
	{
		NodesToFilter.Reserve(SelectedNodes.Num());

		for (const TSharedRef<FSequencerDisplayNode>& SelectedNode : SelectedNodes)
		{
			NodesToFilter.Add(SelectedNode);

			TSharedPtr<FSequencerDisplayNode> Parent = SelectedNode->GetParent();
			while (Parent.IsValid())
			{
				if (Parent->GetType() == ESequencerNode::Object)
				{
					NodesToFilter.Add(Parent.ToSharedRef());
					break;
				}

				Parent = Parent->GetParent();
			}
		}
	}

	bool Match(TSharedRef<const FSequencerDisplayNode> InNode) const
	{
		return NodesToFilter.Contains(InNode);
	}

private:

	TSet<TSharedRef<const FSequencerDisplayNode>> NodesToFilter;
};