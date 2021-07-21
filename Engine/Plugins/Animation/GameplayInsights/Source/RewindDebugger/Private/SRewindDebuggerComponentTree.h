// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IRewindDebugger.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"


class SRewindDebuggerComponentTree : public SCompoundWidget
{
	using FOnSelectionChanged = typename STreeView<TSharedPtr<FDebugObjectInfo>>::FOnSelectionChanged;

public:
	SLATE_BEGIN_ARGS(SRewindDebuggerComponentTree) { }
		SLATE_ARGUMENT( TArray< TSharedPtr< FDebugObjectInfo > >*, DebugComponents );
		SLATE_EVENT( FOnSelectionChanged, OnSelectionChanged )
	SLATE_END_ARGS()
	
public:

	/**
	* Default constructor.
	*/
	SRewindDebuggerComponentTree();
	virtual ~SRewindDebuggerComponentTree();

	/**
	* Constructs the application.
	*
	* @param InArgs The Slate argument list.
	*/
	void Construct(const FArguments& InArgs);

	void Refresh();

	TSharedPtr<SWidget> ComponentTreeOnContextMenuOpening();

private:
	TArray<TSharedPtr<FDebugObjectInfo>>* DebugComponents;
    void OnComponentSelectionChanged(TSharedPtr<FDebugObjectInfo> SelectedItem, ESelectInfo::Type SelectInfo);
	TSharedPtr<STreeView<TSharedPtr<FDebugObjectInfo>>> ComponentTreeView;

	FOnSelectionChanged OnSelectionChanged;
};
