// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataprepGraph/DataprepGraph.h"

#include "DataprepAsset.h"

#include "EdGraphUtilities.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "SGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SWidget.h"

class SDataprepGraphActionNode;
class SDataprepGraphTrackNode;
class SDataprepGraphTrackWidget;
class UDataprepAsset;
// #ueent_toremove: Temp code for the nodes development
class UBlueprint;
class UEdGraph;

class SDataprepGraphEditorNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override;
};

/**
 * The SDataprepGraphEditor class is a specialization of SGraphEditor
 * to display and manipulate the actions of a Dataprep asset
 */
class SDataprepGraphEditor : public SGraphEditor
{
public:
	SLATE_BEGIN_ARGS(SDataprepGraphEditor)
		: _AdditionalCommands( static_cast<FUICommandList*>(nullptr) )
		, _GraphToEdit(nullptr)
	{}

	SLATE_ARGUMENT( TSharedPtr<FUICommandList>, AdditionalCommands )
		SLATE_ARGUMENT( TSharedPtr<SWidget>, TitleBar )
		SLATE_ATTRIBUTE( FGraphAppearanceInfo, Appearance )
		SLATE_ARGUMENT( UEdGraph*, GraphToEdit )
		SLATE_ARGUMENT( FGraphEditorEvents, GraphEvents)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UDataprepAsset* InDataprepAsset);

	// SWidget overrides
	virtual void CacheDesiredSize(float InLayoutScaleMultiplier) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// End of SWidget overrides

	// #ueent_toremove: Temp code for the nodes development
	void OnPipelineChanged(UBlueprint* InBlueprint);

	/** Called when a change has occurred in the set of the Dataprep asset's actions */
	void OnDataprepAssetActionChanged(UObject* InObject, FDataprepAssetChangeType ChangeType);

	/** Register/un-register the association between Dataprep's UEdGraph classes and SgraphNode classes */
	static void RegisterFactories();
	static void UnRegisterFactories();

private:
	/** Recompute the layout of the displayed graph after a pan, resize and/or zoom */
	void UpdateLayout( const FVector2D& LocalSize, const FVector2D& Location, float ZoomAmount );

	/** Recompute the boundaries of a the displayed graph */
	void UpdateBoundaries(const FVector2D& LocalSize, float ZoomAmount);

private:
	/** When false, indicates the graph editor has not been drawn yet */
	mutable bool bIsComplete;

	/** Indicates layout must be recomputed */
	mutable bool bMustRearrange;

	/** Last size of the window displaying the graph's canvas */
	FVector2D LastLocalSize;

	/** Last location of the upper left corner of the visible section of the graph's canvas */
	FVector2D LastLocation;

	/** Last zoom factor applied to the graph's canvas */
	float LastZoomAmount;

	/** Indicates min and max of ordinates in canvas */
	FVector2D ViewLocationRangeOnY;

	/** Size of graph being displayed */
	mutable FVector2D CachedTrackNodeSize;

	/** Size of graph being displayed */
	TWeakObjectPtr<UDataprepAsset> DataprepAssetPtr;

	/** Size of graph being displayed */
	mutable TWeakPtr<SDataprepGraphTrackNode> TrackGraphNodePtr;

	bool bCachedControlKeyDown;

	/** Padding used on the borders of the canvas */
	// #ueent_wip: Will be moved to the Dataprep editor's style
	static const float TopPadding;
	static const float BottomPadding;
	static const float HorizontalPadding;

	/** Factory to create the associated SGraphNode classes for Dataprep graph's UEdGraph classes */
	static TSharedPtr<SDataprepGraphEditorNodeFactory> NodeFactory;

	friend SDataprepGraphTrackNode;
};
