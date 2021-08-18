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
	using FOnMouseButtonDoubleClick = typename STreeView<TSharedPtr<FDebugObjectInfo>>::FOnMouseButtonDoubleClick;
	
public:
	SLATE_BEGIN_ARGS(SRewindDebuggerComponentTree) { }
		SLATE_ARGUMENT( TArray< TSharedPtr< FDebugObjectInfo > >*, DebugComponents );
		SLATE_EVENT( FOnSelectionChanged, OnSelectionChanged )
		SLATE_EVENT( FOnMouseButtonDoubleClick, OnMouseButtonDoubleClick )
		SLATE_EVENT( FOnContextMenuOpening, OnContextMenuOpening )
	SLATE_END_ARGS()

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

private:
	TArray<TSharedPtr<FDebugObjectInfo>>* DebugComponents;
    void OnComponentSelectionChanged(TSharedPtr<FDebugObjectInfo> SelectedItem, ESelectInfo::Type SelectInfo);
	TSharedPtr<STreeView<TSharedPtr<FDebugObjectInfo>>> ComponentTreeView;

	FOnSelectionChanged OnSelectionChanged;
};
