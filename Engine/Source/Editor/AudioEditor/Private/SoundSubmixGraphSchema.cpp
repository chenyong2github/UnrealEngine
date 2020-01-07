// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoundSubmixGraph/SoundSubmixGraphSchema.h"

#include "AssetData.h"
#include "GraphEditorActions.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphUtilities.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"

#include "ScopedTransaction.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundSubmix.h"
#include "SoundSubmixGraph/SoundSubmixGraphNode.h"
#include "SoundSubmixGraph/SoundSubmixGraph.h"
#include "SoundSubmixEditor.h"
#include "SoundSubmixEditorUtilities.h"
#include "Toolkits/AssetEditorManager.h"
#include "ToolMenus.h"


#define LOCTEXT_NAMESPACE "SoundSubmixSchema"


namespace
{
	static const FLinearColor SubmixGraphColor = FColor(175, 255, 0);
} // namespace <>


FConnectionDrawingPolicy* FSoundSubmixGraphConnectionDrawingPolicyFactory::CreateConnectionPolicy(
	const UEdGraphSchema* Schema,
	int32 InBackLayerID,
	int32 InFrontLayerID,
	float ZoomFactor,
	const FSlateRect& InClippingRect,
	FSlateWindowElementList& InDrawElements,
	UEdGraph* InGraphObj) const
{
	if (Schema->IsA(USoundSubmixGraphSchema::StaticClass()))
	{
		return new FSoundSubmixGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj);
	}
	return nullptr;
}

FSoundSubmixGraphConnectionDrawingPolicy::FSoundSubmixGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements)
	, GraphObj(InGraphObj)
{
	ActiveWireThickness = Settings->TraceAttackWireThickness;
	InactiveWireThickness = Settings->TraceReleaseWireThickness;
}

// Give specific editor modes a chance to highlight this connection or darken non-interesting connections
void FSoundSubmixGraphConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& OutParams)
{
	check(GraphObj);
	check(OutputPin);

	OutParams.AssociatedPin1 = InputPin;
	OutParams.AssociatedPin2 = OutputPin;

	// Get the schema and grab the default color from it
	const UEdGraphSchema* Schema = GraphObj->GetSchema();

	OutParams.WireColor = Schema->GetPinTypeColor(OutputPin->PinType);

	bool bExecuted = false;

	// Run through the predecessors, and on
	if (FExecPairingMap* PredecessorMap = PredecessorNodes.Find(OutputPin->GetOwningNode()))
	{
		if (FTimePair* Times = PredecessorMap->Find(InputPin->GetOwningNode()))
		{
			bExecuted = true;

			OutParams.WireThickness = ActiveWireThickness;
			OutParams.WireColor = SubmixGraphColor;
			OutParams.bDrawBubbles = true;
		}
	}

	if (!bExecuted)
	{
		OutParams.WireColor = SubmixGraphColor;
		OutParams.WireThickness = InactiveWireThickness;
	}
}

UEdGraphNode* FSoundSubmixGraphSchemaAction_NewNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode/* = true*/)
{
	FSoundSubmixEditorUtilities::CreateSoundSubmix(ParentGraph, FromPin, Location, NewSoundSubmixName);
	return nullptr;
}

USoundSubmixGraphSchema::USoundSubmixGraphSchema(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool USoundSubmixGraphSchema::ConnectionCausesLoop(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin) const
{
	USoundSubmixGraphNode* InputNode = CastChecked<USoundSubmixGraphNode>(InputPin->GetOwningNode());
	USoundSubmixGraphNode* OutputNode = CastChecked<USoundSubmixGraphNode>(OutputPin->GetOwningNode());

	// Master Submix cannot be an input as it would create an inferred loop for submixes without an explicit parent
	if (const UAudioSettings* Settings = GetDefault<UAudioSettings>())
	{
		if (USoundSubmix* MasterSubmix = Cast<USoundSubmix>(Settings->MasterSubmix.TryLoad()))
		{
			if (OutputNode->SoundSubmix == MasterSubmix)
			{
				return true;
			}

			if (OutputNode->SoundSubmix->RecurseCheckChild(MasterSubmix))
			{
				return true;
			}
		}
	}

	return OutputNode->SoundSubmix->RecurseCheckChild(InputNode->SoundSubmix);
}

void USoundSubmixGraphSchema::GetBreakLinkToSubMenuActions(UToolMenu* Menu, const FName SectionName, UEdGraphPin* InGraphPin)
{
	FToolMenuSection& Section = Menu->FindOrAddSection(SectionName);

	// Make sure we have a unique name for every entry in the list
	TMap<FString, uint32> LinkTitleCount;

	// Add all the links we could break from
	for(TArray<class UEdGraphPin*>::TConstIterator Links(InGraphPin->LinkedTo); Links; ++Links)
	{
		UEdGraphPin* Pin = *Links;
		FString TitleString = Pin->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString();
		FText Title = FText::FromString(TitleString);
		if (Pin->PinName != TEXT(""))
		{
			TitleString = FString::Printf(TEXT("%s (%s)"), *TitleString, *Pin->PinName.ToString());

			// Add name of connection if possible
			FFormatNamedArguments Args;
			Args.Add(TEXT("NodeTitle"), Title);
			Args.Add(TEXT("PinName"), Pin->GetDisplayName());
			Title = FText::Format(LOCTEXT("BreakDescPin", "{NodeTitle} ({PinName})"), Args);
		}

		uint32 &Count = LinkTitleCount.FindOrAdd(TitleString);

		FText Description;
		FFormatNamedArguments Args;
		Args.Add(TEXT("NodeTitle"), Title);
		Args.Add(TEXT("NumberOfNodes"), Count);

		if (Count == 0)
		{
			Description = FText::Format(LOCTEXT("BreakDesc", "Break link to {NodeTitle}"), Args);
		}
		else
		{
			Description = FText::Format(LOCTEXT("BreakDescMulti", "Break link to {NodeTitle} ({NumberOfNodes})"), Args);
		}
		++Count;

		Section.AddMenuEntry(NAME_None, Description, Description, FSlateIcon(), FUIAction(
			FExecuteAction::CreateUObject((USoundSubmixGraphSchema*const)this, &USoundSubmixGraphSchema::BreakSinglePinLink, const_cast< UEdGraphPin* >(InGraphPin), *Links)));
	}
}

void USoundSubmixGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	const FText Name = LOCTEXT("NewSoundSubmix", "New Sound Submix");
	const FText ToolTip = LOCTEXT("NewSoundSubmixTooltip", "Create a new sound submix");
	
	TSharedPtr<FSoundSubmixGraphSchemaAction_NewNode> NewAction(new FSoundSubmixGraphSchemaAction_NewNode(FText::GetEmpty(), Name, ToolTip, 0));

	ContextMenuBuilder.AddAction(NewAction);
}

void USoundSubmixGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (Context->Pin)
	{
		const UEdGraphPin* InGraphPin = Context->Pin;
		{
			const static FName SectionName = "SoundSubmixGraphSchemaPinActions";
			FToolMenuSection& Section = Menu->AddSection(SectionName, LOCTEXT("PinActionsMenuHeader", "Pin Actions"));
			// Only display the 'Break Links' option if there is a link to break!
			if (InGraphPin->LinkedTo.Num() > 0)
			{
				Section.AddMenuEntry(FGraphEditorCommands::Get().BreakPinLinks);

				// add sub menu for break link to
				if(InGraphPin->LinkedTo.Num() > 1)
				{
					Section.AddSubMenu(
						"BreakLinkTo",
						LOCTEXT("BreakLinkTo", "Break Link To..." ),
						LOCTEXT("BreakSpecificLinks", "Break a specific link..." ),
						FNewToolMenuDelegate::CreateUObject((USoundSubmixGraphSchema*const)this, &USoundSubmixGraphSchema::GetBreakLinkToSubMenuActions, SectionName, const_cast<UEdGraphPin*>(InGraphPin)));
				}
				else
				{
					((USoundSubmixGraphSchema*const)this)->GetBreakLinkToSubMenuActions(Menu, SectionName, const_cast<UEdGraphPin*>(InGraphPin));
				}
			}
		}
	}
	else if (Context->Node)
	{
		const USoundSubmixGraphNode* SoundGraphNode = Cast<const USoundSubmixGraphNode>(Context->Node);
		{
			FToolMenuSection& Section = Menu->AddSection("SoundSubmixGraphSchemaNodeActions", LOCTEXT("ClassActionsMenuHeader", "SoundSubmix Actions"));
			Section.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);
			Section.AddMenuEntry(FGenericCommands::Get().Delete);
		}
	}

	// No Super call so Node comments option is not shown
}

const FPinConnectionResponse USoundSubmixGraphSchema::CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	// Make sure the pins are not on the same node
	if (PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionSameNode", "Both are on the same node"));
	}

	// Compare the directions
	const UEdGraphPin* InputPin = nullptr;
	const UEdGraphPin* OutputPin = nullptr;

	if (!CategorizePinsByDirection(PinA, PinB, /*out*/ InputPin, /*out*/ OutputPin))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionIncompatible", "Directions are not compatible"));
	}

	if (ConnectionCausesLoop(InputPin, OutputPin))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionLoop", "Connection would cause loop"));
	}

	// Break existing connections on outputs only - multiple input connections are acceptable
	if (OutputPin->LinkedTo.Num() > 0)
	{
		ECanCreateConnectionResponse ReplyBreakInputs;
		if (OutputPin == PinA)
		{
			ReplyBreakInputs = CONNECT_RESPONSE_BREAK_OTHERS_A;
		}
		else
		{
			ReplyBreakInputs = CONNECT_RESPONSE_BREAK_OTHERS_B;
		}
		return FPinConnectionResponse(ReplyBreakInputs, LOCTEXT("ConnectionReplace", "Replace existing connections"));
	}

	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::GetEmpty());
}

bool USoundSubmixGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	check(PinA);
	check(PinB);

	bool bModified = UEdGraphSchema::TryCreateConnection(PinA, PinB);

	if (bModified)
	{
		USoundSubmixGraph* Graph = CastChecked<USoundSubmixGraph>(PinA->GetOwningNode()->GetGraph());
		Graph->LinkSoundSubmixes();

		USoundSubmix* SubmixA = CastChecked<USoundSubmixGraphNode>(PinA->GetOwningNode())->SoundSubmix;
		USoundSubmix* SubmixB = CastChecked<USoundSubmixGraphNode>(PinB->GetOwningNode())->SoundSubmix;

		bool bReopenEditors = false;

		// If re-basing root, re-open editor.  This will force the root to be the primary edited node
		if (Graph->GetRootSoundSubmix() == SubmixA && SubmixA->ParentSubmix != nullptr)
		{
			bReopenEditors = true;
		}
		else if (Graph->GetRootSoundSubmix() == SubmixB && SubmixB->ParentSubmix != nullptr)
		{
			bReopenEditors = true;
		}

		if (bReopenEditors)
		{
			check(GEditor);
			UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			TArray<IAssetEditorInstance*> SubmixEditors = EditorSubsystem->FindEditorsForAsset(SubmixA);
			for (IAssetEditorInstance* Editor : SubmixEditors)
			{
				Editor->CloseWindow();
			}

			EditorSubsystem->OpenEditorForAsset(SubmixA);
		}
	}

	return bModified;
}

bool USoundSubmixGraphSchema::ShouldHidePinDefaultValue(UEdGraphPin* Pin) const
{
	return true;
}

FLinearColor USoundSubmixGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return SubmixGraphColor;
}

void USoundSubmixGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
	Super::BreakNodeLinks(TargetNode);

	CastChecked<USoundSubmixGraph>(TargetNode.GetGraph())->LinkSoundSubmixes();
}

void USoundSubmixGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
	const FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "GraphEd_BreakPinLinks", "Break Pin Links") );

	Super::BreakPinLinks(TargetPin, bSendsNodeNotifcation);
	
	// if this would notify the node then we need to re-link sound classes
	if (bSendsNodeNotifcation)
	{
		CastChecked<USoundSubmixGraph>(TargetPin.GetOwningNode()->GetGraph())->LinkSoundSubmixes();
	}
}

void USoundSubmixGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	const FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "GraphEd_BreakSinglePinLink", "Break Pin Link") );
	Super::BreakSinglePinLink(SourcePin, TargetPin);

	CastChecked<USoundSubmixGraph>(SourcePin->GetOwningNode()->GetGraph())->LinkSoundSubmixes();
}

void USoundSubmixGraphSchema::DroppedAssetsOnGraph(const TArray<FAssetData>& Assets, const FVector2D& GraphPosition, UEdGraph* Graph) const
{
	check(GEditor);
	check(Graph);

	USoundSubmixGraph* SoundSubmixGraph = CastChecked<USoundSubmixGraph>(Graph);
	TSet<IAssetEditorInstance*> Editors;
	TSet<USoundSubmix*> UndisplayedSubmixes;
	for (const FAssetData& Asset : Assets)
	{
		// Walk to the root submix
		if (USoundSubmix* SoundSubmix = Cast<USoundSubmix>(Asset.GetAsset()))
		{
			while (SoundSubmix->ParentSubmix != nullptr)
			{
				SoundSubmix = SoundSubmix->ParentSubmix;
			}

			if (!SoundSubmixGraph->IsSubmixDisplayed(SoundSubmix))
			{
				UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				TArray<IAssetEditorInstance*> SubmixEditors = EditorSubsystem->FindEditorsForAsset(SoundSubmix);
				for (IAssetEditorInstance* Editor : SubmixEditors)
				{
					if (Editor)
					{
						Editors.Add(Editor);
					}
				}
				UndisplayedSubmixes.Add(SoundSubmix);
			}
		}
	}

	if (UndisplayedSubmixes.Num() > 0)
	{
		const FScopedTransaction Transaction(LOCTEXT("SoundSubmixEditorDropSubmixes", "Sound Submix Editor: Drag and Drop Sound Submix"));

		for (IAssetEditorInstance* Editor : Editors)
		{
			check(Editor);
			FSoundSubmixEditor* SubmixEditor = static_cast<FSoundSubmixEditor*>(Editor);

			// Close editors with dropped (and undisplayed) submix branches as they are now displayed locally in this graph
			// (to avoid modification of multiple graph editors representing the same branch of submixes)
			if (SubmixEditor->GetGraph() != Graph)
			{
				Editor->CloseWindow();
			}
		}

		// If editor is this graph's editor, update editable objects and select dropped submixes.
		if (USoundSubmix* RootSubmix = SoundSubmixGraph->GetRootSoundSubmix())
		{
			UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (IAssetEditorInstance* EditorInstance = EditorSubsystem->FindEditorForAsset(RootSubmix, false /* bFocusIfOpen */))
			{
				FSoundSubmixEditor* SubmixEditor = static_cast<FSoundSubmixEditor*>(EditorInstance);
				SoundSubmixGraph->AddDroppedSoundSubmixes(UndisplayedSubmixes, GraphPosition.X, GraphPosition.Y);
				SubmixEditor->AddMissingEditableSubmixes();
				SubmixEditor->SelectSubmixes(UndisplayedSubmixes);
			}
		}
	}
}
#undef LOCTEXT_NAMESPACE
