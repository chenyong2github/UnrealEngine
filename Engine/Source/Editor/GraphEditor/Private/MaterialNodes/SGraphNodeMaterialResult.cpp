// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialNodes/SGraphNodeMaterialResult.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
#include "SGraphPanel.h"
#include "TutorialMetaData.h"
#include "Widgets/SToolTip.h"
#include "IDocumentation.h"

/////////////////////////////////////////////////////
// SGraphNodeMaterialResult

void SGraphNodeMaterialResult::Construct(const FArguments& InArgs, UMaterialGraphNode_Root* InNode)
{
	this->GraphNode = InNode;
	this->RootNode = InNode;

	this->SetCursor(EMouseCursor::CardinalCross);

	this->UpdateGraphNode();
}

void SGraphNodeMaterialResult::CreatePinWidgets()
{
	// Create Pin widgets for each of the pins.
	UMaterialGraphNode_Base* MaterialGraphNode = Cast<UMaterialGraphNode_Base>(GraphNode);
	UMaterialGraph* MaterialGraph = CastChecked<UMaterialGraph>(GraphNode->GetGraph());

	bool bHideNoConnectionPins = false;
	if (OwnerGraphPanelPtr.IsValid())
	{
		bHideNoConnectionPins = OwnerGraphPanelPtr.Pin()->GetPinVisibility() == SGraphEditor::Pin_HideNoConnection;
	}

	for(const auto& It : MaterialGraphNode->PinInfoMap)
	{
		UEdGraphPin* CurPin = It.Key;
		const FMaterialGraphPinInfo& PinInfo = It.Value;
		const bool bPinHasConections = CurPin->LinkedTo.Num() > 0;

		bool bPinDesiresToBeHidden = bHideNoConnectionPins && !bPinHasConections;
		if (PinInfo.PinType == EMaterialGraphPinType::Data)
		{
			if (!MaterialGraph->MaterialInputs[PinInfo.Index].IsVisiblePin(MaterialGraph->Material))
			{
				bPinDesiresToBeHidden = true;
			}
		}

		if (!bPinDesiresToBeHidden)
		{
			TSharedPtr<SGraphPin> NewPin = CreatePinWidget(CurPin);
			check(NewPin.IsValid());

			if (PinInfo.PinType == EMaterialGraphPinType::Data)
			{
				TSharedPtr<SToolTip> ToolTipWidget = IDocumentation::Get()->CreateToolTip(MaterialGraph->MaterialInputs[PinInfo.Index].GetToolTip(), nullptr, FString(TEXT("")), FString(TEXT("")));
				NewPin->SetToolTip(ToolTipWidget.ToSharedRef());
			}

			this->AddPin(NewPin.ToSharedRef());
		}
	}
}

void SGraphNodeMaterialResult::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	SGraphNode::MoveTo(NewPosition, NodeFilter);

	RootNode->Material->EditorX = RootNode->NodePosX;
	RootNode->Material->EditorY = RootNode->NodePosY;
	RootNode->Material->MarkPackageDirty();
	RootNode->Material->MaterialGraph->MaterialDirtyDelegate.ExecuteIfBound();
}


void SGraphNodeMaterialResult::PopulateMetaTag(FGraphNodeMetaData* TagMeta) const
{
	if( (GraphNode != nullptr) && (RootNode != nullptr) )
	{		
		UMaterialGraph* OuterGraph = RootNode->GetTypedOuter<UMaterialGraph>();
		if (OuterGraph != nullptr)
		{
			TagMeta->OuterName = OuterGraph->OriginalMaterialFullName;
			// There is only one root node - so we dont need a guid. 
			TagMeta->Tag = FName(*FString::Printf(TEXT("MaterialResNode_%s"), *TagMeta->OuterName));
			TagMeta->GUID.Invalidate();
			TagMeta->FriendlyName = FString::Printf(TEXT("Material Result node in %s"), *TagMeta->OuterName);
 		}		
	}
}
