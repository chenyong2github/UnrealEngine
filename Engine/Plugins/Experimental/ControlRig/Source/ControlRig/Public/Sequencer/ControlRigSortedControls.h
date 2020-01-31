// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Manipulatable/IControlRigManipulatable.h"

/**
*  Class to hold sorted order of control/space elements so we sort them correctly in sequencer
*/
class CONTROLRIG_API FRigControlTreeElement : public TSharedFromThis<FRigControlTreeElement>
{
public:
	FRigControlTreeElement(const FRigElementKey& InKey, int32 InIndex) : Key(InKey), Index(InIndex) {};
public:
	FRigElementKey Key;
	int32 Index; //location in hierarchy array
	TArray<TSharedPtr<FRigControlTreeElement>> Children;

};

/**
* Functions to set up the control rig controls so they are in hierarchical order
*/
class CONTROLRIG_API FControlRigSortedControls
{
public:
	//Given an Array of Available Controls, return a set of them sorted.
	static void GetControlsInOrder(IControlRigManipulatable* ControlRig, TArray<FRigControl>& SortedControls);
private:
	//Helper functions
	static void AddElement(FRigElementKey InKey, int32 InIndex, FRigElementKey InParentKey, TMap<FRigElementKey, TSharedPtr<FRigControlTreeElement>>& ElementMap,
		TArray<TSharedPtr<FRigControlTreeElement>>& RootElements);
	static void AddSpaceElement(FRigSpace InSpace, int32 InIndex, const TArray<FRigControl>& Controls, const TArray<FRigSpace>& Spaces,
		TMap<FRigElementKey, TSharedPtr<FRigControlTreeElement>>& ElementMap, TArray<TSharedPtr<FRigControlTreeElement>>& RootElements);
	static void AddControlElement(FRigControl InControl, int32 InIndex, const TArray<FRigControl>& Controls, const TArray<FRigSpace>& Spaces,
		TMap<FRigElementKey, TSharedPtr<FRigControlTreeElement>>& ElementMap,
		TArray<TSharedPtr<FRigControlTreeElement>>& RootElements);
	static void CreateRigControlsRecursive(TSharedPtr<FRigControlTreeElement>& Element, const TArray<FRigControl>& Controls, TArray<FRigControl>& OutControls);

};
