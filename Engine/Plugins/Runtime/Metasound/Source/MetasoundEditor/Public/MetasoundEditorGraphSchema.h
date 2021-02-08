// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetData.h"
#include "ConnectionDrawingPolicy.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphUtilities.h"
#include "MetasoundEditorModule.h"
#include "MetasoundFrontend.h"
#include "Templates/Function.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraphSchema.generated.h"


// Forward Declarations
class UEdGraph;
class UEdGraphNode;
class UMetasound;
class UMetasoundEditorGraphNode;


namespace Metasound
{
	namespace Editor
	{
		using FInputFilterFunction = TFunction<bool(const FMetasoundFrontendClassInput&)>;
		using FOutputFilterFunction = TFunction<bool(const FMetasoundFrontendClassOutput&)>;
		using FDataTypeFilterFunction = TFunction<bool(const FEditorDataType&)>;

		struct FActionClassFilters
		{
			FInputFilterFunction InputFilterFunction;
			FOutputFilterFunction OutputFilterFunction;
		};

		struct FGraphConnectionDrawingPolicyFactory : public FGraphPanelPinConnectionFactory
		{
		public:
			virtual ~FGraphConnectionDrawingPolicyFactory() = default;

			// FGraphPanelPinConnectionFactory
			virtual class FConnectionDrawingPolicy* CreateConnectionPolicy(
				const UEdGraphSchema* Schema,
				int32 InBackLayerID,
				int32 InFrontLayerID,
				float ZoomFactor,
				const FSlateRect& InClippingRect,
				FSlateWindowElementList& InDrawElements,
				UEdGraph* InGraphObj) const override;
			// ~FGraphPanelPinConnectionFactory
		};

		// This class draws the connections for an UEdGraph using a SoundCue schema
		class FGraphConnectionDrawingPolicy : public FConnectionDrawingPolicy
		{
		protected:
			// Times for one execution pair within the current graph
			struct FTimePair
			{
				double PredExecTime;
				double ThisExecTime;

				FTimePair()
					: PredExecTime(0.0)
					, ThisExecTime(0.0)
				{
				}
			};

			// Map of pairings
			using FExecPairingMap = TMap<UEdGraphNode*, FTimePair>;

			// Map of nodes that preceded before a given node in the execution sequence (one entry for each pairing)
			TMap<UEdGraphNode*, FExecPairingMap> PredecessorNodes;

			UEdGraph* GraphObj = nullptr;

			float ActiveWireThickness;
			float InactiveWireThickness;

		public:
			FGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);

			// FConnectionDrawingPolicy interface
			virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
			// End of FConnectionDrawingPolicy interface
		};
	} // namespace Editor
} // namespace Metasound

/** Action to add an input to the graph */
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_NewInput : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY()
	FName NodeTypeName;

	FMetasoundGraphSchemaAction_NewInput()
		: FEdGraphSchemaAction()
	{}

	FMetasoundGraphSchemaAction_NewInput(FText InNodeCategory, FText InDisplayName, FName InTypeName, FText InToolTip, const int32 InGrouping);

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
	FName NodeTypeName;

	FMetasoundGraphSchemaAction_NewOutput()
		: FEdGraphSchemaAction()
	{}

	FMetasoundGraphSchemaAction_NewOutput(FText InNodeCategory, FText InDisplayName, FName InTypeName, FText InToolTip, const int32 InGrouping);

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface
};

/** Action to add a node to the graph */
USTRUCT()
struct METASOUNDEDITOR_API FMetasoundGraphSchemaAction_NewNode : public FEdGraphSchemaAction
{
	GENERATED_USTRUCT_BODY();

	/** ClassInfo of node to create */
	Metasound::Frontend::FNodeClassInfo NodeClassInfo;

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

private:
	/** Adds actions for creating actions associated with graph DataTypes */
	void GetConversionActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FActionClassFilters InFilters = Metasound::Editor::FActionClassFilters(), bool bShowSelectedActions = true) const;
	void GetFunctionActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FActionClassFilters InFilters = Metasound::Editor::FActionClassFilters(), bool bShowSelectedActions = true) const;

	void GetDataTypeInputNodeActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FDataTypeFilterFunction InFilter = Metasound::Editor::FDataTypeFilterFunction(), bool bShowSelectedActions = true) const;
	void GetDataTypeOutputNodeActions(FGraphActionMenuBuilder& ActionMenuBuilder, Metasound::Editor::FDataTypeFilterFunction InFilter = Metasound::Editor::FDataTypeFilterFunction(), bool bShowSelectedActions = true) const;

	/** Adds action for creating a comment */
	void GetCommentAction(FGraphActionMenuBuilder& ActionMenuBuilder, const UEdGraph* CurrentGraph = nullptr) const;
};
