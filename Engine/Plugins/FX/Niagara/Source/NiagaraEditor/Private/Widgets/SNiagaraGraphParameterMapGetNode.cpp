// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraGraphParameterMapGetNode.h"
#include "NiagaraNodeParameterMapGet.h"
#include "Widgets/Input/SButton.h"
#include "GraphEditorSettings.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SBoxPanel.h"
#include "SGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraScriptVariable.h"
#include "SDropTarget.h"
#include "NiagaraEditorStyle.h"


#define LOCTEXT_NAMESPACE "SNiagaraGraphParameterMapGetNode"


void SNiagaraGraphParameterMapGetNode::Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode)
{
	BackgroundBrush = FEditorStyle::GetBrush("Graph.Pin.Background");
	BackgroundHoveredBrush = FEditorStyle::GetBrush("PlainBorder");

	GraphNode = InGraphNode; 
	RegisterNiagaraGraphNode(InGraphNode);

	UpdateGraphNode();
}

void SNiagaraGraphParameterMapGetNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));

	const UEdGraphPin* PinObj = PinToAdd->GetPinObj();
	const bool bAdvancedParameter = (PinObj != nullptr) && PinObj->bAdvancedView;
	const bool bInvisiblePin = (PinObj != nullptr) && PinObj->bDefaultValueIsReadOnly;
	if (bAdvancedParameter)
	{
		PinToAdd->SetVisibility(TAttribute<EVisibility>(PinToAdd, &SGraphPin::IsPinVisibleAsAdvanced));
	}

	// Save the UI building for later...
	if (PinToAdd->GetDirection() == EEdGraphPinDirection::EGPD_Input)
	{
		if (bInvisiblePin)
		{
			//PinToAdd->SetOnlyShowDefaultValue(true);
			PinToAdd->SetPinColorModifier(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		}
		InputPins.Add(PinToAdd);
	}
	else // Direction == EEdGraphPinDirection::EGPD_Output
	{
		OutputPins.Add(PinToAdd);
	}	
}

TSharedRef<SWidget> SNiagaraGraphParameterMapGetNode::CreateNodeContentArea()
{
	// NODE CONTENT AREA
	return 	SNew(SDropTarget)
		.OnDrop(this, &SNiagaraGraphParameterMapGetNode::OnDroppedOnTarget)
		.OnAllowDrop(this, &SNiagaraGraphParameterMapGetNode::OnAllowDrop)
		.HorizontalImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.DropTarget.BorderHorizontal"))
		.VerticalImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.DropTarget.BorderVertical"))
		.BackgroundColor(FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.DropTarget.BackgroundColor"))
		.BackgroundColorHover(FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.DropTarget.BackgroundColorHover"))
		.Content()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(0, 3))
			[
				SAssignNew(PinContainerRoot, SVerticalBox)
			]
		];
}

void SNiagaraGraphParameterMapGetNode::CreatePinWidgets()
{
	SGraphNode::CreatePinWidgets();
	
	UNiagaraNodeParameterMapGet* GetNode = Cast<UNiagaraNodeParameterMapGet>(GraphNode);


	// Deferred pin adding to line up input/output pins by name.
	for (int32 i = 0; i < OutputPins.Num() + 1; i++)
	{
		SVerticalBox::FSlot& Slot = PinContainerRoot->AddSlot();
		Slot.AutoHeight();

		// Get nodes have an unequal number of pins. 
		TSharedPtr<SWidget> Widget;
		if (i == 0)
		{

			SAssignNew(Widget, SHorizontalBox)
				.Visibility(EVisibility::Visible)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.FillWidth(1.0f)
				.Padding(Settings->GetInputPinPadding())
				[
					(InputPins.Num() > 0 ? InputPins[0] : SNullWidget::NullWidget)
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.Padding(Settings->GetOutputPinPadding())
				[
					SNullWidget::NullWidget
				];
		}
		else
		{
			TSharedRef<SGraphPin> OutputPin = OutputPins[i - 1];
			UEdGraphPin* SrcOutputPin = OutputPin->GetPinObj();

			UEdGraphPin* MatchingInputPin = GetNode->GetDefaultPin(SrcOutputPin);

			TSharedPtr<SWidget> InputPin = SNullWidget::NullWidget;
			for (TSharedRef<SGraphPin> Pin : InputPins)
			{
				UEdGraphPin* SrcInputPin = Pin->GetPinObj();
				if (SrcInputPin == MatchingInputPin)
				{
					InputPin = Pin;
					Pin->SetShowLabel(false);
				}
			}

			SAssignNew(Widget, SHorizontalBox)
				.Visibility(EVisibility::Visible)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.FillWidth(1.0f)
				.Padding(Settings->GetInputPinPadding())
				[
					InputPin.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.Padding(Settings->GetOutputPinPadding())
				[
					OutputPin
				];
		}

		TSharedPtr<SBorder> Border;
		SAssignNew(Border, SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(0, 3))
			//.OnMouseButtonDown(this, &SNiagaraGraphParameterMapGetNode::OnBorderMouseButtonDown, i)
			[
				Widget.ToSharedRef()
			];
		Border->SetBorderImage(TAttribute<const FSlateBrush*>::Create(TAttribute<const FSlateBrush*>::FGetter::CreateRaw(this, &SNiagaraGraphParameterMapGetNode::GetBackgroundBrush, Widget)));

		Slot.AttachWidget(Border.ToSharedRef());
	}
}


const FSlateBrush* SNiagaraGraphParameterMapGetNode::GetBackgroundBrush(TSharedPtr<SWidget> Border) const
{
	return Border->IsHovered() ? BackgroundHoveredBrush	: BackgroundBrush;
}


FReply SNiagaraGraphParameterMapGetNode::OnBorderMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 InWhichPin)
{
	if (InWhichPin >= 0 && InWhichPin < OutputPins.Num() + 1)
	{
		UNiagaraNodeParameterMapGet* GetNode = Cast<UNiagaraNodeParameterMapGet>(GraphNode);
		if (GetNode)
		{
			UNiagaraGraph* Graph = GetNode->GetNiagaraGraph();
			if (Graph && InWhichPin > 0)
			{
				const UEdGraphSchema_Niagara* Schema = Graph->GetNiagaraSchema();
				if (Schema)
				{
					FNiagaraVariable Var = Schema->PinToNiagaraVariable(OutputPins[InWhichPin-1]->GetPinObj());
					UNiagaraScriptVariable** PinAssociatedScriptVariable = Graph->GetAllMetaData().Find(Var);
					if (PinAssociatedScriptVariable != nullptr)
					{
						Graph->OnSubObjectSelectionChanged().Broadcast(*PinAssociatedScriptVariable);
					}
				}
			}
		}

	}
	return FReply::Unhandled();
}

FReply SNiagaraGraphParameterMapGetNode::OnDroppedOnTarget(TSharedPtr<FDragDropOperation> DropOperation)
{
	UNiagaraNodeParameterMapBase* MapNode = Cast<UNiagaraNodeParameterMapBase>(GraphNode);
	if (MapNode != nullptr && MapNode->HandleDropOperation(DropOperation))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

bool SNiagaraGraphParameterMapGetNode::OnAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	UNiagaraNodeParameterMapBase* MapNode = Cast<UNiagaraNodeParameterMapBase>(GraphNode);
	return MapNode != nullptr && MapNode->CanHandleDropOperation(DragDropOperation);
}

#undef LOCTEXT_NAMESPACE
