// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetData.h"
#include "ConnectionDrawingPolicy.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphUtilities.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "Templates/Function.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraphSchema.generated.h"


// Forward Declarations
class UEdGraph;
class UEdGraphNode;
class UMetaSound;
class UMetasoundEditorGraphNode;


namespace Metasound
{
	namespace Editor
	{
		using FInputFilterFunction = TFunction<bool(const FMetasoundFrontendClassInput&)>;
		using FOutputFilterFunction = TFunction<bool(const FMetasoundFrontendClassOutput&)>;
		using FInterfaceNodeFilterFunction = TFunction<bool(Frontend::FConstNodeHandle)>;

		struct FActionClassFilters
		{
			FInputFilterFunction InputFilterFunction;
			FOutputFilterFunction OutputFilterFunction;
		};
	} // namespace Editor
} // namespace Metasound

/** Action to add an input reference to the graph */
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_NewInput : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY()
	FGuid NodeID;

	FMetasoundGraphSchemaAction_NewInput()
		: FEdGraphSchemaAction()
	{}

	FMetasoundGraphSchemaAction_NewInput(FText InNodeCategory, FText InDisplayName, FGuid InInputNodeID, FText InToolTip, const int32 InGrouping);

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface
};

/** Action to promote a literal to a graph input */
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_PromoteToInput : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	FMetasoundGraphSchemaAction_PromoteToInput()
		: FEdGraphSchemaAction()
	{}

	FMetasoundGraphSchemaAction_PromoteToInput(FText InNodeCategory, FText InDisplayName, FText InToolTip, const int32 InGrouping);

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface
};

/** Action to add an output to the graph */
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_NewOutput : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY()
	FGuid NodeID;

	FMetasoundGraphSchemaAction_NewOutput()
		: FEdGraphSchemaAction()
	{}

	FMetasoundGraphSchemaAction_NewOutput(FText InNodeCategory, FText InDisplayName, FGuid InOutputNodeID, FText InToolTip, const int32 InGrouping);

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface
};

/** Action to add a node to the graph */
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_NewNode : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	/** Class Metadata of node to create */
	FMetasoundFrontendClassMetadata ClassMetadata;

	FMetasoundGraphSchemaAction_NewNode() 
		: FEdGraphSchemaAction()
	{}

	FMetasoundGraphSchemaAction_NewNode(const FText& InNodeCategory, const FText& InMenuDesc, const FText& InToolTip, const int32 InGrouping)
		: FEdGraphSchemaAction(InNodeCategory, InMenuDesc, InToolTip, InGrouping)
	{}

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface
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
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override;
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

	void BreakNodeLinks(UEdGraphNode& TargetNode, bool bShouldActuallyTransact) const;

private:
	/** Adds actions for creating actions associated with graph DataTypes */
	void GetConversionActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FActionClassFilters InFilters = Metasound::Editor::FActionClassFilters(), bool bShowSelectedActions = true) const;
	void GetFunctionActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FActionClassFilters InFilters = Metasound::Editor::FActionClassFilters(), bool bShowSelectedActions = true) const;

	void GetDataTypeInputNodeActions(FGraphContextMenuBuilder& InMenuBuilder, Metasound::Frontend::FConstGraphHandle InGraphHandle, Metasound::Editor::FInterfaceNodeFilterFunction InFilter = Metasound::Editor::FInterfaceNodeFilterFunction(), bool bShowSelectedActions = true) const;
	void GetDataTypeOutputNodeActions(FGraphContextMenuBuilder& InMenuBuilder, Metasound::Frontend::FConstGraphHandle InGraphHandle, Metasound::Editor::FInterfaceNodeFilterFunction InFilter = Metasound::Editor::FInterfaceNodeFilterFunction(), bool bShowSelectedActions = true) const;

	/** Adds action for creating a comment */
	void GetCommentAction(FGraphActionMenuBuilder& ActionMenuBuilder, const UEdGraph* CurrentGraph = nullptr) const;
};
