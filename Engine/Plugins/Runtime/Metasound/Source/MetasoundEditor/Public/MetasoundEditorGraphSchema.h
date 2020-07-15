// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetData.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraphSchema.generated.h"

class UEdGraph;
class UMetasound;
class UEdGraphNode;

/** Action to add a node to the graph */
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_NewNode : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	/** Class of node we want to create */
	UPROPERTY()
	UClass* MetasoundNodeClass;

	FMetasoundGraphSchemaAction_NewNode() 
		: FEdGraphSchemaAction()
		, MetasoundNodeClass(nullptr)
	{}

	FMetasoundGraphSchemaAction_NewNode(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
		, MetasoundNodeClass(nullptr)
	{}

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface

private:
	/** Connects new node to output of selected nodes */
	void ConnectToSelectedNodes(UMetasound* NewNodeclass, UEdGraph* ParentGraph) const;
};

/** Action to add nodes to the graph based on selected objects*/
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_NewFromSelected : public FMetasoundGraphSchemaAction_NewNode
{
	GENERATED_USTRUCT_BODY();

	FMetasoundGraphSchemaAction_NewFromSelected() 
		: FMetasoundGraphSchemaAction_NewNode()
	{}

	FMetasoundGraphSchemaAction_NewFromSelected(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping)
		: FMetasoundGraphSchemaAction_NewNode(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping) 
	{}

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface
};

/** Action to create new comment */
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_NewComment : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	FMetasoundGraphSchemaAction_NewComment() 
		: FEdGraphSchemaAction()
	{}

	FMetasoundGraphSchemaAction_NewComment(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
	{}

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface
};

/** Action to paste clipboard contents into the graph */
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_Paste : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	FMetasoundGraphSchemaAction_Paste() 
		: FEdGraphSchemaAction()
	{}

	FMetasoundGraphSchemaAction_Paste(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
	{}

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface
};

UCLASS(MinimalAPI)
class UMetasoundEditorGraphSchema : public UEdGraphSchema
{
	GENERATED_UCLASS_BODY()

	/** Check whether connecting these pins would cause a loop */
	bool ConnectionCausesLoop(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin) const;

	/** Helper method to add items valid to the palette list */
	METASOUNDEDITOR_API void GetPaletteActions(FGraphActionMenuBuilder& ActionMenuBuilder) const;

	//~ Begin EdGraphSchema Interface
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	virtual bool ShouldHidePinDefaultValue(UEdGraphPin* Pin) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual void BreakNodeLinks(UEdGraphNode& TargetNode) const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const override;
	virtual void GetAssetsGraphHoverMessage(const TArray<FAssetData>& Assets, const UEdGraph* HoverGraph, FString& OutTooltipText, bool& OutOkIcon) const override;
	virtual void DroppedAssetsOnGraph(const TArray<struct FAssetData>& Assets, const FVector2D& GraphPosition, UEdGraph* Graph) const override;
	virtual void DroppedAssetsOnNode(const TArray<FAssetData>& Assets, const FVector2D& GraphPosition, UEdGraphNode* Node) const override;
	virtual int32 GetNodeSelectionCount(const UEdGraph* Graph) const override;
	virtual TSharedPtr<FEdGraphSchemaAction> GetCreateCommentAction() const override;
	//~ End EdGraphSchema Interface

private:
	/** Adds actions for creating every type of GraphNode */
	void GetAllMetasoundActions(FGraphActionMenuBuilder& ActionMenuBuilder, bool bShowSelectedActions) const;

	/** Adds action for creating a comment */
	void GetCommentAction(FGraphActionMenuBuilder& ActionMenuBuilder, const UEdGraph* CurrentGraph = nullptr) const;
};
