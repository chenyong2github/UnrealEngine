// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "MVVMDebugItemId.h"


namespace UE::MVVM
{
class FDebugSnapshot;
class SViewSelection;
class SViewModelSelection;

class SSelectionTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSelectionTab) { }
	SLATE_EVENT(FSimpleDelegate, OnSelectionChanged)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	void SetSnapshot(TSharedPtr<FDebugSnapshot> Snapshot);
	TArray<FDebugItemId> GetSelectedItems() const;
	TArray<UObject*> GetSelectedObjects() const;

private:
	void HandleViewSelectionChanged();
	void HandleViewModleSelectionChanged();

private:
	TSharedPtr<SViewSelection> ViewSelection;
	TSharedPtr<SViewModelSelection> ViewModelSelection;

	FSimpleDelegate OnSelectionChanged;
	enum class ESelection
	{
		None,
		View,
		ViewModel,
	} CurrentSelection = ESelection::None;
};

} //namespace
