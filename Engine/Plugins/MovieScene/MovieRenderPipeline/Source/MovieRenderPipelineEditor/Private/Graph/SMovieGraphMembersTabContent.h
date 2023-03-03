// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Input/SButton.h"
#include "Widgets/SCompoundWidget.h"

class FAssetEditorToolkit;
class SGraphActionMenu;
struct FEdGraphSchemaAction;
struct FGraphActionListBuilderBase;

/**
 * Contents of the "Members" tab in the graph asset editor.
 */
class SMovieGraphMembersTabContent : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_TwoParams(FOnActionSelected, const TArray<TSharedPtr<FEdGraphSchemaAction>>&, ESelectInfo::Type);
	
	SLATE_BEGIN_ARGS(SMovieGraphMembersTabContent)
		: _Graph(nullptr)
		, _Editor(nullptr)
	
		{}

		/** An event which is triggered when an action is selected. */
		SLATE_EVENT(FOnActionSelected, OnActionSelected)
		
		/** The graph that is currently displayed. */
		SLATE_ARGUMENT(class UMovieGraphConfig*, Graph)

		/** The editor that is displaying this widget. */
		SLATE_ARGUMENT(TSharedPtr<FAssetEditorToolkit>, Editor)
	
	SLATE_END_ARGS();
	
	void Construct(const FArguments& InArgs);

	/** Resets the selected members in the UI. */
	void ClearSelection() const;

	/** Deletes the member(s) which are currently selected from the graph and the UI. */
	void DeleteSelectedMembers() const;

	/** Determines if all selected member(s) can be deleted. */
	bool CanDeleteSelectedMembers() const;

private:
	/** The section identifier in the action widget. */
	enum class EActionSection : uint8
	{
		Invalid,
		
		Inputs,
		Outputs,
		Variables,

		COUNT
	};

	/** The names of the sections in the action widget. */
	static const TArray<FText> ActionMenuSectionNames;
	
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	void CollectStaticSections(TArray<int32>& StaticSectionIDs);
	FText GetSectionTitle(int32 InSectionID);
	TSharedRef<SWidget> GetSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID);

	/** Handler which deals with populating the context menu. */
	TSharedPtr<SWidget> OnContextMenuOpening();

	/** Handler which deals with clicking the add button for an action section. */
	FReply OnAddButtonClickedOnSection(const int32 InSectionID);

	/** Refresh/regenerate the action menu when the given variable is updated. */
	void RefreshActions(class UMovieGraphVariable* UpdatedVariable = nullptr) const;

private:
	/** The editor that this widget is associated with. */
	TWeakPtr<FAssetEditorToolkit> EditorToolkit;

	/** The action menu displayed in the UI which allows for creation/manipulation of graph members (eg, variables). */
	TSharedPtr<SGraphActionMenu> ActionMenu;

	/** The runtime graph that this UI gets/sets data on. */
	TObjectPtr<UMovieGraphConfig> CurrentGraph;
	
	/** Delegate to call when an action is selected */
	FOnActionSelected OnActionSelected;
};
