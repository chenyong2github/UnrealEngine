// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/ControlRigSortedControls.h"

void FControlRigSortedControls::CreateRigControlsRecursive(TSharedPtr<FRigControlTreeElement>& Element, const TArray<FRigControl>& Controls, TArray<FRigControl>& OutControls)
{
	if (Element->Key.Type == ERigElementType::Control)
	{
		check(Element->Index >= 0);
		const FRigControl&  RigControl = Controls[Element->Index];
		FRigControl NewControl(RigControl);
		OutControls.Add(NewControl);
	}
	for (int32 ChildIndex = 0; ChildIndex < Element->Children.Num(); ++ChildIndex)
	{
		FControlRigSortedControls::CreateRigControlsRecursive(Element->Children[ChildIndex], Controls, OutControls);
	}
}

void FControlRigSortedControls::GetControlsInOrder(IControlRigManipulatable* Manip, TArray<FRigControl>& SortedControls)
{
	TMap<FRigElementKey, TSharedPtr<FRigControlTreeElement>> ElementMap;
	TArray<TSharedPtr<FRigControlTreeElement>> RootElements;
	const TArray<FRigControl>& Controls = Manip->AvailableControls();
	const TArray<FRigSpace>& Spaces = Manip->AvailableSpaces();

	int32 Index = 0;
	for (const FRigControl& RigControl : Controls)
	{
		FControlRigSortedControls::AddControlElement(RigControl, Index, Controls, Spaces, ElementMap, RootElements);
		++Index;
	}

	for (Index = 0; Index < RootElements.Num(); ++Index)
	{
		FControlRigSortedControls::CreateRigControlsRecursive(RootElements[Index], Controls, SortedControls);
	}
}

void FControlRigSortedControls::AddControlElement(FRigControl InControl, int32 Index, const TArray<FRigControl>& Controls, const TArray<FRigSpace>& Spaces,
	TMap<FRigElementKey, TSharedPtr<FRigControlTreeElement>>& ElementMap,
	TArray<TSharedPtr<FRigControlTreeElement>>& RootElements)
{
	FRigElementKey ParentKey;
	if (InControl.SpaceIndex != INDEX_NONE)
	{
		FControlRigSortedControls::AddSpaceElement(Spaces[InControl.SpaceIndex], InControl.SpaceIndex, Controls, Spaces, ElementMap, RootElements);
		ParentKey = Spaces[InControl.SpaceIndex].GetElementKey();
	}
	else if (InControl.ParentIndex != INDEX_NONE)
	{
		FControlRigSortedControls::AddControlElement(Controls[InControl.ParentIndex], InControl.ParentIndex, Controls, Spaces, ElementMap, RootElements);
		ParentKey = Controls[InControl.ParentIndex].GetElementKey();
	}
	FControlRigSortedControls::AddElement(InControl.GetElementKey(), Index, ParentKey, ElementMap, RootElements);
}

void FControlRigSortedControls::AddSpaceElement(FRigSpace InSpace, int32 Index, const TArray<FRigControl>& Controls, const TArray<FRigSpace>& Spaces,
	TMap<FRigElementKey, TSharedPtr<FRigControlTreeElement>>& ElementMap, TArray<TSharedPtr<FRigControlTreeElement>>& RootElements)
{
	FRigElementKey ParentKey;
	if (InSpace.ParentIndex != INDEX_NONE)
	{
		switch (InSpace.SpaceType)
		{
		case ERigSpaceType::Control:
		{
			FControlRigSortedControls::AddControlElement(Controls[InSpace.ParentIndex], InSpace.ParentIndex, Controls, Spaces, ElementMap, RootElements);
			ParentKey = Controls[InSpace.ParentIndex].GetElementKey();
			break;
		}
		case ERigSpaceType::Space:
		{
			FControlRigSortedControls::AddSpaceElement(Spaces[InSpace.ParentIndex], InSpace.ParentIndex, Controls, Spaces, ElementMap, RootElements);
			ParentKey = Spaces[InSpace.ParentIndex].GetElementKey();
			break;
		}
		default:
		{
			break;
		}
		}
	}
	FControlRigSortedControls::AddElement(InSpace.GetElementKey(), Index, ParentKey, ElementMap, RootElements);
}

void FControlRigSortedControls::AddElement(FRigElementKey InKey, int32 Index, FRigElementKey InParentKey, TMap<FRigElementKey, TSharedPtr<FRigControlTreeElement>>& ElementMap,
	TArray<TSharedPtr<FRigControlTreeElement>>& RootElements)
{
	if (ElementMap.Contains(InKey))
	{
		return;
	}
	TSharedPtr<FRigControlTreeElement> NewItem = MakeShared<FRigControlTreeElement>(InKey, Index);
	ElementMap.Add(InKey, NewItem);
	if (InParentKey)
	{
		TSharedPtr<FRigControlTreeElement>* FoundItem = ElementMap.Find(InParentKey);
		check(FoundItem);
		FoundItem->Get()->Children.Add(NewItem);
	}
	else
	{
		RootElements.Add(NewItem);
	}
}
