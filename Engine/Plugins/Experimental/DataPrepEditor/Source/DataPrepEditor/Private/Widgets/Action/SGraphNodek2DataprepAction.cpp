// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Action/SGraphNodeK2DataprepAction.h"

// Dataprep includes
#include "BlueprintNodes/K2Node_DataprepAction.h"
#include "DataprepActionAsset.h"
#include "Widgets/Action/SDataprepActionSteps.h"

// Engine Includes
#include "Widgets/SBoxPanel.h"

void SGraphNodeK2DataprepAction::Construct(const FArguments& InArgs, UK2Node_DataprepAction* InActionNode)
{
	DataprepActionPtr = InActionNode->GetDataprepAction();

	this->GraphNode = InActionNode;
	this->SetCursor(EMouseCursor::CardinalCross);
	this->UpdateGraphNode();
}

void SGraphNodeK2DataprepAction::CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox)
{
	UDataprepActionAsset* DataprepAction = DataprepActionPtr.Get();
	if ( DataprepAction )
	{
		MainBox->AddSlot()
		.AutoHeight()
		[
			SNew( SDataprepActionSteps, DataprepAction )
		];
	}
	else
	{
		MainBox->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.ColorAndOpacity( FSlateColor( FLinearColor::Red ) )
				.Text( FText::FromString( TEXT("This node doesn't have a dataprep action!") ) )
			];
	}
}
