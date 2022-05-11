// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphSchema.h"

#include "Elements/PCGExecuteBlueprint.h"
#include "PCGEditorCommon.h"
#include "PCGEditorGraph.h"
#include "PCGEditorGraphNodeBase.h"
#include "PCGEditorGraphSchemaActions.h"
#include "PCGGraph.h"
#include "PCGSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphSchema"


void UPCGEditorGraphSchema::GetPaletteActions(FGraphActionMenuBuilder& ActionMenuBuilder) const
{
	GetNativeElementActions(ActionMenuBuilder);
	GetSubgraphElementActions(ActionMenuBuilder);
	GetBlueprintElementActions(ActionMenuBuilder);
}

void UPCGEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	Super::GetGraphContextActions(ContextMenuBuilder);

	GetNativeElementActions(ContextMenuBuilder);
	GetSubgraphElementActions(ContextMenuBuilder);
	GetBlueprintElementActions(ContextMenuBuilder);
}

FLinearColor UPCGEditorGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

FConnectionDrawingPolicy* UPCGEditorGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const
{
	return new FPCGEditorConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

const FPinConnectionResponse UPCGEditorGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	const UEdGraphNode* NodeA = A->GetOwningNode();
	const UEdGraphNode* NodeB = B->GetOwningNode();

	if (NodeA == NodeB)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionSameNode", "Both pins are on same node"));
	}

	if (A->Direction == B->Direction)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("ConnectionSameDirection", "Both pins are the same direction"));
	}

	return FPinConnectionResponse();
}

bool UPCGEditorGraphSchema::TryCreateConnection(UEdGraphPin* InA, UEdGraphPin* InB) const
{
	bool bModified = Super::TryCreateConnection(InA, InB);

	if (bModified)
	{
		check(InA && InB);
		UEdGraphPin* A = (InA->Direction == EGPD_Output) ? InA : InB;
		UEdGraphPin* B = (InA->Direction == EGPD_Input) ? InA : InB;
		
		check(A->Direction == EGPD_Output && B->Direction == EGPD_Input);

		UEdGraphNode* NodeA = A->GetOwningNode();
		UEdGraphNode* NodeB = B->GetOwningNode();

		UPCGEditorGraphNodeBase* PCGGraphNodeA = CastChecked<UPCGEditorGraphNodeBase>(NodeA);
		UPCGEditorGraphNodeBase* PCGGraphNodeB = CastChecked<UPCGEditorGraphNodeBase>(NodeB);

		UPCGNode* PCGNodeA = PCGGraphNodeA->GetPCGNode();
		UPCGNode* PCGNodeB = PCGGraphNodeB->GetPCGNode();
		check(PCGNodeA && PCGNodeB);

		UPCGGraph* PCGGraph = PCGNodeA->GetGraph();
		check(PCGGraph);

		PCGGraph->AddLabeledEdge(PCGNodeA, A->PinName, PCGNodeB, B->PinName);
	}

	return bModified;
}

void UPCGEditorGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	const FScopedTransaction Transaction(*FPCGEditorCommon::ContextIdentifier, LOCTEXT("PCGEditorBreakPinLinks", "Break Pin Links"), nullptr);

	Super::BreakPinLinks(TargetPin, bSendsNodeNotification);

	UEdGraphNode* GraphNode = TargetPin.GetOwningNode();

	UPCGEditorGraphNodeBase* PCGGraphNode = CastChecked<UPCGEditorGraphNodeBase>(GraphNode);

	UPCGNode* PCGNode = PCGGraphNode->GetPCGNode();
	check(PCGNode);

	UPCGGraph* PCGGraph = PCGNode->GetGraph();
	check(PCGGraph);

	if (TargetPin.Direction == EEdGraphPinDirection::EGPD_Input)
	{
		PCGGraph->RemoveInboundEdges(PCGNode, TargetPin.PinName);
	}
	else if (TargetPin.Direction == EEdGraphPinDirection::EGPD_Output)
	{
		PCGGraph->RemoveOutboundEdges(PCGNode, TargetPin.PinName);
	}
}

void UPCGEditorGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	const FScopedTransaction Transaction(*FPCGEditorCommon::ContextIdentifier, LOCTEXT("PCGEditorBreakSinglePinLink", "Break Single Pin Link"), nullptr);

	Super::BreakSinglePinLink(SourcePin, TargetPin);

	UEdGraphNode* SourceGraphNode = SourcePin->GetOwningNode();
	UEdGraphNode* TargetGraphNode = TargetPin->GetOwningNode();

	UPCGEditorGraphNodeBase* SourcePCGGraphNode = CastChecked<UPCGEditorGraphNodeBase>(SourceGraphNode);
	UPCGEditorGraphNodeBase* TargetPCGGraphNode = CastChecked<UPCGEditorGraphNodeBase>(TargetGraphNode);

	UPCGNode* SourcePCGNode = SourcePCGGraphNode->GetPCGNode();
	UPCGNode* TargetPCGNode = TargetPCGGraphNode->GetPCGNode();
	check(SourcePCGNode && TargetPCGNode);

	UPCGGraph* PCGGraph = SourcePCGNode->GetGraph();
	PCGGraph->RemoveEdge(SourcePCGNode, SourcePin->PinName, TargetPCGNode, TargetPin->PinName);
}

void UPCGEditorGraphSchema::GetNativeElementActions(FGraphActionMenuBuilder& ActionMenuBuilder) const
{
	TArray<UClass*> SettingsClasses;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;

		if (Class->IsChildOf(UPCGSettings::StaticClass()) &&
			!Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden))
		{
			SettingsClasses.Add(Class);
		}
	}

	for (UClass* SettingsClass : SettingsClasses)
	{
		if (const UPCGSettings* PCGSettings = SettingsClass->GetDefaultObject<UPCGSettings>())
		{
			const FText MenuDesc = FText::FromName(PCGSettings->GetDefaultNodeName());
			const FText Category = StaticEnum<EPCGSettingsType>()->GetDisplayNameTextByValue(static_cast<__underlying_type(EPCGSettingsType)>(PCGSettings->GetType()));
			const FText Description = FText::GetEmpty();

			TSharedPtr<FPCGEditorGraphSchemaAction_NewNativeElement> NewAction(new FPCGEditorGraphSchemaAction_NewNativeElement(Category, MenuDesc, Description, 0));
			NewAction->SettingsClass = SettingsClass;
			ActionMenuBuilder.AddAction(NewAction);
		}
	}
}

void UPCGEditorGraphSchema::GetBlueprintElementActions(FGraphActionMenuBuilder& ActionMenuBuilder) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
	Filter.bRecursiveClasses = true;
	Filter.TagsAndValues.Add(FBlueprintTags::NativeParentClassPath, FString::Printf(TEXT("%s'%s'"), *UClass::StaticClass()->GetName(), *UPCGBlueprintElement::StaticClass()->GetPathName()));

	TArray<FAssetData> BlueprintElementAssets;
	AssetRegistryModule.Get().GetAssets(Filter, BlueprintElementAssets);

	for (const FAssetData& AssetData : BlueprintElementAssets)
	{
		const bool bExposeToLibrary = AssetData.GetTagValueRef<bool>(TEXT("bExposeToLibrary"));
		if (bExposeToLibrary)
		{
			const FText MenuDesc = FText::FromName(AssetData.AssetName);
			const FText Category = AssetData.GetTagValueRef<FText>(TEXT("Category"));
			const FText Description = AssetData.GetTagValueRef<FText>(TEXT("Description"));

			const FString GeneratedClass = AssetData.GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);

			TSharedPtr<FPCGEditorGraphSchemaAction_NewBlueprintElement> NewBlueprintAction(new FPCGEditorGraphSchemaAction_NewBlueprintElement(Category, MenuDesc, Description, 0));
			NewBlueprintAction->BlueprintClassPath = FSoftClassPath(GeneratedClass);
			ActionMenuBuilder.AddAction(NewBlueprintAction);
		}
	}
}

void UPCGEditorGraphSchema::GetSubgraphElementActions(FGraphActionMenuBuilder& ActionMenuBuilder) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssetsByClass(UPCGGraph::StaticClass()->GetFName(), AssetDataList);

	for (const FAssetData& AssetData : AssetDataList)
	{
		const bool bExposeToLibrary = AssetData.GetTagValueRef<bool>(TEXT("bExposeToLibrary"));
		if (bExposeToLibrary)
		{
			const FText MenuDesc = FText::FromName(AssetData.AssetName);
			const FText Category = AssetData.GetTagValueRef<FText>(TEXT("Category"));
			const FText Description = AssetData.GetTagValueRef<FText>(TEXT("Description"));

			TSharedPtr<FPCGEditorGraphSchemaAction_NewSubgraphElement> NewSubgraphAction(new FPCGEditorGraphSchemaAction_NewSubgraphElement(Category, MenuDesc, Description, 0));
			NewSubgraphAction->SubgraphObjectPath = AssetData.ObjectPath;
			ActionMenuBuilder.AddAction(NewSubgraphAction);
		}
	}
}



FPCGEditorConnectionDrawingPolicy::FPCGEditorConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraph)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
	, Graph(CastChecked<UPCGEditorGraph>(InGraph))
{
	ArrowImage = nullptr;
	ArrowRadius = FVector2D::ZeroVector;
}

void FPCGEditorConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params)
{
	FConnectionDrawingPolicy::DetermineWiringStyle(OutputPin, InputPin, Params);
	// Emphasize wire thickness on hovered pins
	if (HoveredPins.Contains(InputPin) && HoveredPins.Contains(OutputPin))
	{
		Params.WireThickness = Params.WireThickness * 3;
	}
}

#undef LOCTEXT_NAMESPACE
