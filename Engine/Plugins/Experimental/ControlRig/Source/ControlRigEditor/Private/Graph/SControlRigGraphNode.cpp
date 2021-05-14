// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigGraphNode.h"
#include "ControlRig.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraph.h"
#include "SGraphPin.h"
#include "Graph/ControlRigGraphSchema.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "SLevelOfDetailBranchNode.h"
#include "SGraphPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "GraphEditorSettings.h"
#include "ControlRigEditorStyle.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Engine/Engine.h"
#include "KismetNodes/KismetNodeInfoContext.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyPathHelpers.h"
#include "UObject/PropertyPortFlags.h"
#include "ControlRigBlueprint.h"
#include "RigVMCore/RigVM.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "IDocumentation.h"
#include "DetailLayoutBuilder.h"
#include "EditorStyleSet.h"
#include "SControlRigGraphPinVariableBinding.h"
#include "Slate/SlateTextures.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#endif

#define LOCTEXT_NAMESPACE "SControlRigGraphNode"

const FSlateBrush* SControlRigGraphNode::CachedImg_CR_Pin_Connected = nullptr;
const FSlateBrush* SControlRigGraphNode::CachedImg_CR_Pin_Disconnected = nullptr;

void SControlRigGraphNode::Construct( const FArguments& InArgs )
{
	static const float PinWidgetSidePadding = 6.f;
	static const float EmptySidePadding = 60.f;
	static const float TopPadding = 2.f; 
	static const float MaxHeight = 30.f;

	if (CachedImg_CR_Pin_Connected == nullptr)
	{
		static const FName NAME_CR_Pin_Connected("ControlRig.Bug.Solid");
		static const FName NAME_CR_Pin_Disconnected("ControlRig.Bug.Open");
		CachedImg_CR_Pin_Connected = FControlRigEditorStyle::Get().GetBrush(NAME_CR_Pin_Connected);
		CachedImg_CR_Pin_Disconnected = FControlRigEditorStyle::Get().GetBrush(NAME_CR_Pin_Disconnected);
	}

	check(InArgs._GraphNodeObj);
	this->GraphNode = InArgs._GraphNodeObj;
	this->SetCursor( EMouseCursor::CardinalCross );

 	UControlRigGraphNode* EdGraphNode = InArgs._GraphNodeObj;
	ModelNode = EdGraphNode->GetModelNode();
	if (!ModelNode.IsValid())
	{
		return;
	}

	Blueprint = Cast<UControlRigBlueprint>(FBlueprintEditorUtils::FindBlueprintForNode(this->GraphNode));

	// Re-cache variable info here (unit structure could have changed since last reconstruction, e.g. array add/remove)
	// and also create missing pins if it hasn't created yet
	EdGraphNode->AllocateDefaultPins();
	
	NodeErrorType = int32(EMessageSeverity::Info) + 1;
	this->UpdateGraphNode();

	SetIsEditable(false);

	URigVMController* Controller = EdGraphNode->GetController();
	Controller->OnModified().AddSP(this, &SControlRigGraphNode::HandleModifiedEvent);

	TMap<UEdGraphPin*, int32> EdGraphPinToInputPin;
	for(int32 InputPinIndex = 0; InputPinIndex < InputPins.Num(); InputPinIndex++)
	{
		EdGraphPinToInputPin.Add(InputPins[InputPinIndex]->GetPinObj(), InputPinIndex);
	}
	TMap<UEdGraphPin*, int32> EdGraphPinToOutputPin;
	for(int32 OutputPinIndex = 0; OutputPinIndex < OutputPins.Num(); OutputPinIndex++)
	{
		EdGraphPinToOutputPin.Add(OutputPins[OutputPinIndex]->GetPinObj(), OutputPinIndex);
	}

	TArray<URigVMPin*> RootModelPins = ModelNode->GetPins();
	TArray<URigVMPin*> ModelPins;

	// sort model pins
	// a) execute IOs, b) IO pins, c) input / visible pins, d) output pins
	struct Local
	{
		static void VisitPinRecursively(URigVMPin* InPin, TArray<URigVMPin*>& OutPins)
		{
			OutPins.Add(InPin);
			for (URigVMPin* SubPin : InPin->GetSubPins())
			{
				VisitPinRecursively(SubPin, OutPins);
			}
		}
	};
	for(int32 SortPhase = 0; SortPhase < 4; SortPhase++)
	{
		for(URigVMPin* RootPin : RootModelPins)
		{
			switch (SortPhase)
			{
				case 0: // execute IO pins
				{
					if(RootPin->IsExecuteContext() && RootPin->GetDirection() == ERigVMPinDirection::IO)
					{
						Local::VisitPinRecursively(RootPin, ModelPins);
					}
					break;
				}
				case 1: // IO pins
				{
					if(!RootPin->IsExecuteContext() && RootPin->GetDirection() == ERigVMPinDirection::IO)
					{
						Local::VisitPinRecursively(RootPin, ModelPins);
					}
					break;
				}
				case 2: // input / visible pins
				{
					if(RootPin->GetDirection() == ERigVMPinDirection::Input || RootPin->GetDirection() == ERigVMPinDirection::Visible)
					{
						Local::VisitPinRecursively(RootPin, ModelPins);
					}
					break;
				}
				case 3: // output pins
				default:
				{
					if(RootPin->GetDirection() == ERigVMPinDirection::Output)
					{
						Local::VisitPinRecursively(RootPin, ModelPins);
					}
					break;
				}
			}
		}
	}

	// add spacer widget at the start
	LeftNodeBox->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	.AutoHeight()
	[
		SNew(SSpacer)
		.Size(FVector2D(1.f, 2.f))
	];

	TMap<URigVMPin*, int32> ModelPinToInfoIndex;
	for(URigVMPin* ModelPin : ModelPins)
	{
		FPinInfo PinInfo;
		PinInfo.Index = PinInfos.Num();
		PinInfo.ParentIndex = INDEX_NONE;
		PinInfo.bHasChildren = (ModelPin->GetSubPins().Num() > 0);
		PinInfo.bIsContainer = ModelPin->IsArray();
		PinInfo.Depth = 0;
		PinInfo.bExpanded = ModelPin->IsExpanded();
		PinInfo.ModelPinPath = ModelPin->GetPinPath();

		PinInfo.bHideInputWidget = ModelPin->IsStruct() || PinInfo.bIsContainer;
		if (!PinInfo.bHideInputWidget)
		{
			if (ModelPin->GetSubPins().Num() == 0)
			{
				if (const UControlRigGraphSchema* RigSchema = Cast<UControlRigGraphSchema>(EdGraphNode->GetSchema()))
				{
					PinInfo.bHideInputWidget = RigSchema->IsStructEditable(ModelPin->GetScriptStruct());
				}
			}
		}
		
		if(URigVMPin* ParentPin = ModelPin->GetParentPin())
		{
			const int32* ParentIndexPtr = ModelPinToInfoIndex.Find(ParentPin);
			if(ParentIndexPtr == nullptr)
			{
				continue;
			}
			PinInfo.ParentIndex = *ParentIndexPtr;
			PinInfo.Depth = PinInfos[PinInfo.ParentIndex].Depth + 1;
		}

		TAttribute<EVisibility> PinVisibilityAttribute = TAttribute<EVisibility>::CreateSP(this, &SControlRigGraphNode::GetPinVisibility, PinInfo.Index);

		bool bPinWidgetForExpanderLeft = false;
		TSharedPtr<SGraphPin> PinWidgetForExpander;

		bool bPinInfoIsValid = false;
		if(UEdGraphPin* OutputEdGraphPin = EdGraphNode->FindPin(ModelPin->GetPinPath(), EEdGraphPinDirection::EGPD_Output))
		{
			if(const int32* PinIndexPtr = EdGraphPinToOutputPin.Find(OutputEdGraphPin))
			{
				PinInfo.OutputPinWidget = OutputPins[*PinIndexPtr];
				PinInfo.OutputPinWidget->SetVisibility(PinVisibilityAttribute);
				PinWidgetForExpander = PinInfo.OutputPinWidget;
				bPinWidgetForExpanderLeft = false;
				bPinInfoIsValid = true;
			}
		}
		if(UEdGraphPin* InputEdGraphPin = EdGraphNode->FindPin(ModelPin->GetPinPath(), EEdGraphPinDirection::EGPD_Input))
		{
			if(const int32* PinIndexPtr = EdGraphPinToInputPin.Find(InputEdGraphPin))
			{
				PinInfo.InputPinWidget = InputPins[*PinIndexPtr];
				PinInfo.InputPinWidget->SetVisibility(PinVisibilityAttribute);
				PinWidgetForExpander = PinInfo.InputPinWidget;
				bPinWidgetForExpanderLeft = true;
				bPinInfoIsValid = true;
			}
		}

		if(!bPinInfoIsValid)
		{
			continue;
		}

		ModelPinToInfoIndex.Add(ModelPin, PinInfos.Add(PinInfo));
		
		// check if this pin has sub pins
		TSharedPtr<SHorizontalBox> FullPinHorizontalRowWidget = PinWidgetForExpander->GetFullPinHorizontalRowWidget().Pin();
		if(FullPinHorizontalRowWidget.IsValid())
		{
			// indent the pin by padding
			const float DepthIndentation = 12.f * float(PinInfo.Depth + (PinInfo.bHasChildren ? 0 : 1));
			const float LeftIndentation = bPinWidgetForExpanderLeft ? DepthIndentation : 0.f;
			const float RightIndentation = bPinWidgetForExpanderLeft ? 0.f : DepthIndentation;
			
			if(PinInfo.bHasChildren)
			{
				// only inject the expander arrow for inputs on input / IO
				// or for output pins
				if(
					(
						(
							(ModelPin->GetDirection() == ERigVMPinDirection::Input) ||
							(ModelPin->GetDirection() == ERigVMPinDirection::IO)
						) &&
						bPinWidgetForExpanderLeft
					) ||
					(
						(ModelPin->GetDirection() == ERigVMPinDirection::Output) &&
						(!bPinWidgetForExpanderLeft)
					)
				)
				{
					// Add the expander arrow
					FullPinHorizontalRowWidget->InsertSlot(bPinWidgetForExpanderLeft ? 1 : FullPinHorizontalRowWidget->GetChildren()->Num() - 1)
					.Padding(LeftIndentation, 0.f, RightIndentation, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
						.ContentPadding(0)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.ClickMethod( EButtonClickMethod::MouseDown )
						.OnClicked(this, &SControlRigGraphNode::OnExpanderArrowClicked, PinInfo.Index)
						.ToolTipText(LOCTEXT("ExpandSubPin", "Expand Pin"))
						[
							SNew(SImage)
							.Image(this, &SControlRigGraphNode::GetExpanderImage, PinInfo.Index, bPinWidgetForExpanderLeft, false)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					];
				}
			}
			else
			{
				TPanelChildren<SBoxPanel::FSlot>* Slots = (TPanelChildren<SBoxPanel::FSlot>*)FullPinHorizontalRowWidget->GetChildren();
				const int32 SlotToAdjustIndex = bPinWidgetForExpanderLeft ? 0 : Slots->Num() - 1;
				SBoxPanel::FSlot& Slot = (*Slots)[SlotToAdjustIndex];

				FMargin Padding;
				if(Slot.SlotPadding.IsSet() || Slot.SlotPadding.IsBound())
				{
					Padding = Slot.SlotPadding.Get();
				}
				Padding = FMargin(RightIndentation + Padding.Left, Padding.Top, LeftIndentation + Padding.Right, Padding.Bottom);
				Slot.SlotPadding = Padding; 
			}
		}
	}

	for(const FPinInfo& PinInfo : PinInfos)
	{
		if(PinInfo.InputPinWidget.IsValid())
		{
			if(PinInfo.bHideInputWidget)
			{
				if(PinInfo.InputPinWidget->GetValueWidget() != SNullWidget::NullWidget)
				{
					PinInfo.InputPinWidget->GetValueWidget()->SetVisibility(EVisibility::Collapsed);
				}
			}
				
			// input pins
			if(!PinInfo.OutputPinWidget.IsValid())
			{
				TSharedPtr<SHorizontalBox> SlotLayout;
				SHorizontalBox::FSlot* FirstSlot = nullptr;

				const float MyEmptySidePadding = PinInfo.bHideInputWidget ? EmptySidePadding : 0.f; 
				
				LeftNodeBox->AddSlot()
                .HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.AutoHeight()
				.MaxHeight(MaxHeight)
                [
                    SAssignNew(SlotLayout, SHorizontalBox)
                    .Visibility(this, &SControlRigGraphNode::GetPinVisibility, PinInfo.Index)
		            
                    +SHorizontalBox::Slot()
                    .Expose(FirstSlot)
                    .FillWidth(1.f)
                    .HAlign(HAlign_Left)
                    .Padding(PinWidgetSidePadding, TopPadding, PinInfo.bIsContainer ? 0.f : MyEmptySidePadding, 0.f)
                    [
                        PinInfo.InputPinWidget.ToSharedRef()
                    ]
                ];

				if(PinInfo.bIsContainer)
				{
					// make sure to minimize the width of the label
					FirstSlot->AutoWidth();
					
					// add array plus button
					SlotLayout->AddSlot()
					.AutoWidth()
					.HAlign(HAlign_Left)
                    .Padding(PinWidgetSidePadding, TopPadding, MyEmptySidePadding, 0.f)
					[
						SNew(SButton)
						.ContentPadding(0.0f)
						.ButtonStyle(FEditorStyle::Get(), "NoBorder")
						.OnClicked(this, &SControlRigGraphNode::HandleAddArrayElement, PinInfo.ModelPinPath)
						.IsEnabled(this, &SGraphNode::IsNodeEditable)
						.Cursor(EMouseCursor::Default)
						.ToolTipText(LOCTEXT("AddArrayElement", "Add Array Element"))
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush(TEXT("Icons.PlusCircle")))
							]
						]
					];
				}
			}
			// io pins
			else
			{
				PinInfo.OutputPinWidget->SetShowLabel(false);
			
				LeftNodeBox->AddSlot()
                .HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
                .AutoHeight()
				.MaxHeight(MaxHeight)
                [
                    SNew(SHorizontalBox)
                    .Visibility(this, &SControlRigGraphNode::GetPinVisibility, PinInfo.Index)

                    +SHorizontalBox::Slot()
                    .FillWidth(1.f)
                    .HAlign(HAlign_Left)
                    .VAlign(VAlign_Center)
                    .Padding(PinWidgetSidePadding, TopPadding, 0.f, 0.f)
                    [
                        PinInfo.InputPinWidget.ToSharedRef()
                    ]

                    +SHorizontalBox::Slot()
                    .FillWidth(1.f)
                    .HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
                    .Padding(0.f, TopPadding, PinWidgetSidePadding, 0.f)
                    [
                        PinInfo.OutputPinWidget.ToSharedRef()
                    ]
                ];
			}
		}
		// output pins
		else if(PinInfo.OutputPinWidget.IsValid())
		{
			LeftNodeBox->AddSlot()
            .HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
            .AutoHeight()
			.MaxHeight(MaxHeight)
            [
            	SNew(SHorizontalBox)
	            .Visibility(this, &SControlRigGraphNode::GetPinVisibility, PinInfo.Index)
	            
				+SHorizontalBox::Slot()
				.FillWidth(1.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
	            .Padding(EmptySidePadding, TopPadding, PinWidgetSidePadding, 0.f)
				[
	                PinInfo.OutputPinWidget.ToSharedRef()
				]
            ];
		}
	}

	if(URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(ModelNode))
	{
		TWeakObjectPtr<URigVMFunctionReferenceNode> WeakFunctionReferenceNode = FunctionReferenceNode;
		TWeakObjectPtr<UControlRigBlueprint> WeakControlRigBlueprint = Blueprint;

		// add the entries for the variable remapping
		for(const TSharedPtr<FRigVMExternalVariable>& ExternalVariable : EdGraphNode->ExternalVariables)
		{
			LeftNodeBox->AddSlot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.AutoHeight()
			.MaxHeight(MaxHeight)
			[
				SNew(SHorizontalBox)
	            
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(PinWidgetSidePadding, TopPadding, PinWidgetSidePadding, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromName(ExternalVariable->Name))
					.TextStyle(FEditorStyle::Get(), NAME_DefaultPinLabelStyle)
					.ColorAndOpacity(this, &SControlRigGraphNode::GetVariableLabelTextColor, WeakFunctionReferenceNode, ExternalVariable->Name)
					.ToolTipText(this, &SControlRigGraphNode::GetVariableLabelTooltipText, WeakControlRigBlueprint, ExternalVariable->Name)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(PinWidgetSidePadding, TopPadding, PinWidgetSidePadding, 0.f)
				[
					SNew(SControlRigVariableBinding)
					.Blueprint(Blueprint.Get())
					.FunctionReferenceNode(FunctionReferenceNode)
					.InnerVariableName(ExternalVariable->Name)
				]
			];
		}
	}
	
	// add spacer widget at the end
	LeftNodeBox->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	.AutoHeight()
	[
		SNew(SSpacer)
		.Size(FVector2D(1.f, 4.f))
	];

	const FSlateBrush* ImageBrush = FControlRigEditorStyle::Get().GetBrush(TEXT("ControlRig.Bug.Dot"));

	VisualDebugIndicatorWidget =
		SNew(SImage)
		.Image(ImageBrush)
		.Visibility(EVisibility::Visible);

	static const FSlateColorBrush WhiteBrush(FLinearColor::White);
	
	SAssignNew(InstructionCountTextBlockWidget, STextBlock)
	.Margin(FMargin(2.0f, 2.0f, 2.0f, 1.0f))
	.Text(this, &SControlRigGraphNode::GetInstructionCountText)
	.Font(IDetailLayoutBuilder::GetDetailFont())
	.ColorAndOpacity(FLinearColor::White)
	.ShadowColorAndOpacity(FLinearColor(0.1f, 0.1f, 0.1f, 1.f))
	.Visibility(EVisibility::Visible)
	.ToolTipText(LOCTEXT("NodeHitCountToolTip", "This number represents the hit count for a node.\nFor functions / collapse nodes it represents the sum of all hit counts of contained nodes.\n\nYou can enable / disable the display of the number in the Class Settings\n(Rig Graph Display Settings -> Show Node Run Counts)"));

	EdGraphNode->GetNodeTitleDirtied().BindSP(this, &SControlRigGraphNode::HandleNodeTitleDirtied);

	LastHighDetailSize = FVector2D::ZeroVector;
}

TSharedRef<SWidget> SControlRigGraphNode::CreateNodeContentArea()
{
	return SNew(SLevelOfDetailBranchNode)
		.UseLowDetailSlot(this, &SControlRigGraphNode::UseLowDetailNodeContent)
		.LowDetail()
		[
			SNew(SSpacer)
			.Size(this, &SControlRigGraphNode::GetLowDetailDesiredSize)
		]
		.HighDetail()
		[
			SAssignNew(LeftNodeBox, SVerticalBox)
		];
}

bool SControlRigGraphNode::UseLowDetailPinNames() const
{
	if (const SGraphPanel* MyOwnerPanel = GetOwnerPanel().Get())
	{
		return (MyOwnerPanel->GetCurrentLOD() <= EGraphRenderingLOD::LowDetail);
	}
	return false;
}

bool SControlRigGraphNode::UseLowDetailNodeContent() const
{
	if(LastHighDetailSize.IsNearlyZero())
	{
		return false;
	}
	
	if (const SGraphPanel* MyOwnerPanel = GetOwnerPanel().Get())
	{
		return (MyOwnerPanel->GetCurrentLOD() <= EGraphRenderingLOD::LowestDetail);
	}
	return false;
}

FVector2D SControlRigGraphNode::GetLowDetailDesiredSize() const
{
	return LastHighDetailSize;
}

void SControlRigGraphNode::EndUserInteraction() const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif

	if (GraphNode)
	{
		if (const UControlRigGraphSchema* RigSchema = Cast<UControlRigGraphSchema>(GraphNode->GetSchema()))
		{
			RigSchema->EndGraphNodeInteraction(GraphNode);
		}
	}

	SGraphNode::EndUserInteraction();
}

void SControlRigGraphNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	if (!NodeFilter.Find(SharedThis(this)))
	{
		if (GraphNode && !RequiresSecondPassLayout())
		{
			if (const UControlRigGraphSchema* RigSchema = Cast<UControlRigGraphSchema>(GraphNode->GetSchema()))
			{
				RigSchema->SetNodePosition(GraphNode, NewPosition);
			}
		}
	}
}

void SControlRigGraphNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd) 
{
	if(ModelNode.IsValid())
	{
		const UEdGraphPin* EdPinObj = PinToAdd->GetPinObj();

		// Customize the look for pins with injected nodes
		FString NodeName, PinPath;
		if (URigVMPin::SplitPinPathAtStart(EdPinObj->GetName(), NodeName, PinPath))
		{
			if (URigVMPin* ModelPin = ModelNode->FindPin(PinPath))
			{
				if (ModelPin->HasInjectedNodes())
				{
					PinToAdd->SetCustomPinIcon(CachedImg_CR_Pin_Connected, CachedImg_CR_Pin_Disconnected);
				}
				PinToAdd->SetToolTipText(ModelPin->GetToolTipText());
			}
		}

		// reformat the pin by
		// 1. taking out the swrapbox widget
		// 2. re-inserting all widgets from the label and value wrap box back in the horizontal box
		TSharedPtr<SHorizontalBox> FullPinHorizontalRowWidget = PinToAdd->GetFullPinHorizontalRowWidget().Pin();
		TSharedPtr<SWrapBox> LabelAndValueWidget = PinToAdd->GetLabelAndValue();
		if(FullPinHorizontalRowWidget.IsValid() && LabelAndValueWidget.IsValid())
		{
			int32 LabelAndValueWidgetIndex = INDEX_NONE;
			for(int32 ChildIndex = 0; ChildIndex < FullPinHorizontalRowWidget->GetChildren()->Num(); ChildIndex++)
			{
				TSharedRef<SWidget> ChildWidget = FullPinHorizontalRowWidget->GetChildren()->GetChildAt(ChildIndex);
				if(ChildWidget == LabelAndValueWidget)
				{
					LabelAndValueWidgetIndex = ChildIndex;
					break;
				}
			}
			check(LabelAndValueWidgetIndex != INDEX_NONE);
			
			FullPinHorizontalRowWidget->RemoveSlot(LabelAndValueWidget.ToSharedRef());
			
			for(int32 ChildIndex = 0; ChildIndex < LabelAndValueWidget->GetChildren()->Num(); ChildIndex++)
			{
				TSharedRef<SWidget> ChildWidget = LabelAndValueWidget->GetChildren()->GetChildAt(ChildIndex);
				if(ChildWidget != SNullWidget::NullWidget)
				{
					ChildWidget->AssignParentWidget(FullPinHorizontalRowWidget.ToSharedRef());
					
					FullPinHorizontalRowWidget->InsertSlot(LabelAndValueWidgetIndex + ChildIndex)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.Padding(EdPinObj->Direction == EGPD_Input ? 0.f : 2.f, 0.f, EdPinObj->Direction == EGPD_Input ? 2.f : 0.f, 0.f)
					.AutoWidth()
					[
						ChildWidget
					];
				}
			}
		}

		PinToAdd->SetOwner(SharedThis(this));
		if(EdPinObj->Direction == EGPD_Input)
		{
			InputPins.Add(PinToAdd);
		}
		else
		{
			OutputPins.Add(PinToAdd);
		}
	}
}

const FSlateBrush * SControlRigGraphNode::GetNodeBodyBrush() const
{
	return FEditorStyle::GetBrush("Graph.Node.Body");
}

FReply SControlRigGraphNode::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = SGraphNode::OnMouseButtonDown(MyGeometry, MouseEvent);

	if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphNode))
	{
		if (UControlRigGraph* RigGraph = Cast<UControlRigGraph>(RigNode->GetGraph()))
		{
			RigGraph->OnGraphNodeClicked.Broadcast(RigNode);
		}
	}

	return Reply;
}

FReply SControlRigGraphNode::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (!InMouseEvent.GetModifierKeys().AnyModifiersDown())
	{
		if (ModelNode.IsValid())
		{
			if(Blueprint.IsValid())
			{
				Blueprint->BroadcastNodeDoubleClicked(ModelNode.Get());
				return FReply::Handled();
			}
		}
	}
	return SGraphNode::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}

EVisibility SControlRigGraphNode::GetTitleVisibility() const
{
	return UseLowDetailNodeTitles() ? EVisibility::Hidden : EVisibility::Visible;
}

TSharedRef<SWidget> SControlRigGraphNode::CreateTitleWidget(TSharedPtr<SNodeTitle> InNodeTitle)
{
	NodeTitle = InNodeTitle;

	TSharedRef<SWidget> WidgetRef = SGraphNode::CreateTitleWidget(NodeTitle);
	WidgetRef->SetVisibility(MakeAttributeSP(this, &SControlRigGraphNode::GetTitleVisibility));
	if (NodeTitle.IsValid())
	{
		NodeTitle->SetVisibility(MakeAttributeSP(this, &SControlRigGraphNode::GetTitleVisibility));
	}

	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.Padding(0.0f)
		[
			WidgetRef
		];
}

FText SControlRigGraphNode::GetPinLabel(TWeakPtr<SGraphPin> GraphPin) const
{
	if(GraphPin.IsValid())
	{
		if (GraphNode)
		{
			return GraphNode->GetPinDisplayName(GraphPin.Pin()->GetPinObj());
		}
	}

	return FText();
}

FSlateColor SControlRigGraphNode::GetPinTextColor(TWeakPtr<SGraphPin> GraphPin) const
{
	if(GraphPin.IsValid())
	{
		if (GraphPin.Pin()->GetPinObj()->bOrphanedPin)
		{
			return FLinearColor::Red;
		}

		// If there is no schema there is no owning node (or basically this is a deleted node)
		if (GraphNode)
		{
			if(!GraphNode->IsNodeEnabled() || GraphNode->IsDisplayAsDisabledForced() || !GraphPin.Pin()->IsEditingEnabled() || GraphNode->IsNodeUnrelated())
			{
				return FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);
			}
		}
	}

	return FLinearColor::White;
}

FSlateColor SControlRigGraphNode::GetVariableLabelTextColor(
	TWeakObjectPtr<URigVMFunctionReferenceNode> FunctionReferenceNode, FName InVariableName) const
{
	if(FunctionReferenceNode.IsValid())
	{
		if(FunctionReferenceNode->GetOuterVariableName(InVariableName).IsNone())
		{
			return FLinearColor::Red;
		}
	}
	return FLinearColor::White;
}

FText SControlRigGraphNode::GetVariableLabelTooltipText(TWeakObjectPtr<UControlRigBlueprint> InBlueprint,
	FName InVariableName) const
{
	if(InBlueprint.IsValid())
	{
		for(const FBPVariableDescription& Variable : InBlueprint->NewVariables)
		{
			if(Variable.VarName == InVariableName)
			{
				FString Message = FString::Printf(TEXT("Variable from %s"), *InBlueprint->GetPathName()); 
				if(Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
				{
					const FString Tooltip = Variable.GetMetaData(FBlueprintMetadata::MD_Tooltip);
					Message = FString::Printf(TEXT("%s\n%s"), *Message, *Tooltip);
				}
				return FText::FromString(Message);
			}
		}
	}
	return FText();
}

FReply SControlRigGraphNode::HandleAddArrayElement(FString InModelPinPath)
{
	if(!InModelPinPath.IsEmpty())
	{
		if (UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(GraphNode))
		{
			ControlRigGraphNode->HandleAddArrayElement(InModelPinPath);
		}
	}
	return FReply::Handled();
}

/** Populate the brushes array with any overlay brushes to render */
void SControlRigGraphNode::GetOverlayBrushes(bool bSelected, const FVector2D WidgetSize, TArray<FOverlayBrushInfo>& Brushes) const
{
	UControlRigGraphNode* RigGraphNode = Cast<UControlRigGraphNode>(GraphNode);

	const URigVMNode* VMNode = RigGraphNode->GetModelNode();
	const bool bHasBreakpoint = VMNode->HasBreakpoint();
	if (bHasBreakpoint)
	{
		FOverlayBrushInfo BreakpointOverlayInfo;

		BreakpointOverlayInfo.Brush = FEditorStyle::GetBrush(TEXT("Kismet.DebuggerOverlay.Breakpoint.EnabledAndValid"));
		if (BreakpointOverlayInfo.Brush != NULL)
		{
			BreakpointOverlayInfo.OverlayOffset -= BreakpointOverlayInfo.Brush->ImageSize / 2.f;
		}

		Brushes.Add(BreakpointOverlayInfo);
	}

	// Paint red arrow pointing at breakpoint node that caused a halt in execution
	{
		FOverlayBrushInfo IPOverlayInfo;
		if (VMNode->ExecutionIsHaltedAtThisNode())
		{
			IPOverlayInfo.Brush = FEditorStyle::GetBrush( TEXT("Kismet.DebuggerOverlay.InstructionPointerBreakpoint") );
			if (IPOverlayInfo.Brush != NULL)
			{
				float Overlap = 10.f;
				IPOverlayInfo.OverlayOffset.X = (WidgetSize.X/2.f) - (IPOverlayInfo.Brush->ImageSize.X/2.f);
				IPOverlayInfo.OverlayOffset.Y = (Overlap - IPOverlayInfo.Brush->ImageSize.Y);
			}

			IPOverlayInfo.AnimationEnvelope = FVector2D(0.f, 10.f);

			Brushes.Add(IPOverlayInfo);
		}
	}
}

void SControlRigGraphNode::GetNodeInfoPopups(FNodeInfoContext* Context, TArray<FGraphInformationPopupInfo>& Popups) const
{
	FKismetNodeInfoContext* K2Context = (FKismetNodeInfoContext*)Context;

	const FLinearColor LatentBubbleColor(1.f, 0.5f, 0.25f);
	const FLinearColor PinnedWatchColor(0.35f, 0.25f, 0.25f);

	UControlRig* ActiveObject = Cast<UControlRig>(K2Context->ActiveObjectBeingDebugged);
	UControlRigGraphNode* RigGraphNode = Cast<UControlRigGraphNode>(GraphNode);
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(K2Context->SourceBlueprint);

	// Display any pending latent actions
	if (ActiveObject && RigBlueprint && RigGraphNode)
	{
		// Display pinned watches
		if (K2Context->WatchedNodeSet.Contains(GraphNode))
		{
			const UEdGraphSchema* Schema = GraphNode->GetSchema();

			FString PinnedWatchText;
			int32 ValidWatchCount = 0;
			for (int32 PinIndex = 0; PinIndex < GraphNode->Pins.Num(); ++PinIndex)
			{
				UEdGraphPin* WatchPin = GraphNode->Pins[PinIndex];
				if (K2Context->WatchedPinSet.Contains(WatchPin))
				{
					if (URigVMPin* ModelPin = RigGraphNode->GetModel()->FindPin(WatchPin->GetName()))
					{
						if (ValidWatchCount > 0)
						{
							PinnedWatchText += TEXT("\n");
						}

						FString PinName = Schema->GetPinDisplayName(WatchPin).ToString();
						PinName += TEXT(" (");
						PinName += UEdGraphSchema_K2::TypeToText(WatchPin->PinType).ToString();
						PinName += TEXT(")");

						FString WatchText;
						FString PinHash = URigVMCompiler::GetPinHash(ModelPin, nullptr, true);
						if (const FRigVMOperand* WatchOperand = RigBlueprint->PinToOperandMap.Find(PinHash))
						{
							FRigVMMemoryContainer& Memory = ActiveObject->GetVM()->GetDebugMemory();

							TArray<FString> DefaultValues = Memory.GetRegisterValueAsString(*WatchOperand, ModelPin->GetCPPType(), ModelPin->GetCPPTypeObject());
							if (DefaultValues.Num() == 1)
							{
								WatchText = DefaultValues[0];
							}
							else if (DefaultValues.Num() > 1)
							{
								WatchText = FString::Printf(TEXT("%s"), *FString::Join(DefaultValues, TEXT("\n")));
							}
							if (!WatchText.IsEmpty())
							{
								PinnedWatchText += FText::Format(LOCTEXT("WatchingAndValidFmt", "{0}\n\t{1}"), FText::FromString(PinName), FText::FromString(WatchText)).ToString();//@TODO: Print out object being debugged name?
							}
							else
							{
								PinnedWatchText += FText::Format(LOCTEXT("InvalidPropertyFmt", "No watch found for {0}"), Schema->GetPinDisplayName(WatchPin)).ToString();//@TODO: Print out object being debugged name?
							}

							ValidWatchCount++;
						}
					}
				}
			}

			if (ValidWatchCount)
			{
				new (Popups) FGraphInformationPopupInfo(NULL, PinnedWatchColor, PinnedWatchText);
			}
		}
	}
}

TArray<FOverlayWidgetInfo> SControlRigGraphNode::GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const
{
	TArray<FOverlayWidgetInfo> Widgets = SGraphNode::GetOverlayWidgets(bSelected, WidgetSize);

	if (ModelNode.IsValid())
	{
		bool bSetColor = false;
		FLinearColor Color = FLinearColor::Black;
		int32 PreviousNumWidgets = Widgets.Num();
		VisualDebugIndicatorWidget->SetColorAndOpacity(Color);

		for (URigVMPin* ModelPin : ModelNode->GetPins())
		{
			if (ModelPin->HasInjectedNodes())
			{
				for (URigVMInjectionInfo* Injection : ModelPin->GetInjectedNodes())
				{
					URigVMUnitNode* VisualDebugNode = Injection->UnitNode;

					FString PrototypeName;
					if (VisualDebugNode->GetScriptStruct()->GetStringMetaDataHierarchical(TEXT("PrototypeName"), &PrototypeName))
					{
						if (PrototypeName == TEXT("VisualDebug"))
						{
							if (!bSetColor)
							{
								if (VisualDebugNode->FindPin(TEXT("bEnabled"))->GetDefaultValue() == TEXT("True"))
								{
									if (URigVMPin* ColorPin = VisualDebugNode->FindPin(TEXT("Color")))
									{
										TBaseStructure<FLinearColor>::Get()->ImportText(*ColorPin->GetDefaultValue(), &Color, nullptr, PPF_None, nullptr, TBaseStructure<FLinearColor>::Get()->GetName());
									}
									else
									{
										Color = FLinearColor::White;
									}

									VisualDebugIndicatorWidget->SetColorAndOpacity(Color);
									bSetColor = true;
								}
							}

							if (Widgets.Num() == PreviousNumWidgets)
							{
								const FVector2D ImageSize = VisualDebugIndicatorWidget->GetDesiredSize();

								FOverlayWidgetInfo Info;
								Info.OverlayOffset = FVector2D(WidgetSize.X - ImageSize.X - 6.f, 6.f);
								Info.Widget = VisualDebugIndicatorWidget;

								Widgets.Add(Info);
							}
						}
					}
				}
			}
		}

		if(Blueprint.IsValid())
		{
			if(Blueprint->RigGraphDisplaySettings.bShowNodeRunCounts)
			{
				if(UControlRig* DebuggedControlRig = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
				{
					const int32 Count = ModelNode->GetInstructionVisitedCount(DebuggedControlRig->GetVM(), FRigVMASTProxy(), false);
					if(Count > Blueprint->RigGraphDisplaySettings.NodeRunLowerBound)
					{
						const int32 VOffset = bSelected ? -2 : 2;
						const FVector2D TextSize = InstructionCountTextBlockWidget->GetDesiredSize();
						FOverlayWidgetInfo Info;
						Info.OverlayOffset = FVector2D(WidgetSize.X - TextSize.X - 8.f, VOffset - TextSize.Y);
						Info.Widget = InstructionCountTextBlockWidget;
						Widgets.Add(Info);
					}
				}
			}
		}
	}

	return Widgets;
}

void SControlRigGraphNode::RefreshErrorInfo()
{
	if (GraphNode)
	{
		if (NodeErrorType != GraphNode->ErrorType)
		{
			SGraphNode::RefreshErrorInfo();
			NodeErrorType = GraphNode->ErrorType;
		}
	}
}

void SControlRigGraphNode::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SGraphNode::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (GraphNode)
	{
		GraphNode->NodeWidth = (int32)AllottedGeometry.Size.X;
		GraphNode->NodeHeight = (int32)AllottedGeometry.Size.Y;
		RefreshErrorInfo();
	}

	if((!UseLowDetailNodeContent()) && (LeftNodeBox != nullptr))
	{
		LastHighDetailSize = LeftNodeBox->GetTickSpaceGeometry().Size;
	}
}

void SControlRigGraphNode::HandleNodeTitleDirtied()
{
	if (NodeTitle.IsValid())
	{
		NodeTitle->MarkDirty();
	}
}

FText SControlRigGraphNode::GetInstructionCountText() const
{
	if(Blueprint.IsValid())
	{
		if(Blueprint->RigGraphDisplaySettings.bShowNodeRunCounts)
		{
			if (ModelNode.IsValid())
			{
				if(UControlRig* DebuggedControlRig = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
				{
					const int32 Count = ModelNode->GetInstructionVisitedCount(DebuggedControlRig->GetVM(), FRigVMASTProxy(), true);
					if(Count > Blueprint->RigGraphDisplaySettings.NodeRunLowerBound)
					{
						return FText::FromString(FString::FromInt(Count));
					}
				}
			}
		}
	}

	return FText();
}

int32 SControlRigGraphNode::GetNodeTopologyVersion() const
{
	if(UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(GraphNode))
	{
		return ControlRigGraphNode->GetNodeTopologyVersion();
	}
	return INDEX_NONE;
}

EVisibility SControlRigGraphNode::GetPinVisibility(int32 InPinInfoIndex) const
{
	if(PinInfos.IsValidIndex(InPinInfoIndex))
	{
		const int32 ParentPinIndex = PinInfos[InPinInfoIndex].ParentIndex;
		if(ParentPinIndex != INDEX_NONE)
		{
			const EVisibility ParentPinVisibility = GetPinVisibility(ParentPinIndex); 
			if(GetPinVisibility(ParentPinIndex) != EVisibility::Visible)
			{
				return ParentPinVisibility;
			}

			if(!PinInfos[ParentPinIndex].bExpanded)
			{
				return EVisibility::Collapsed;
			}
		}
	}

	return EVisibility::Visible;
}

const FSlateBrush* SControlRigGraphNode::GetExpanderImage(int32 InPinInfoIndex, bool bLeft, bool bHovered) const
{
	static const FSlateBrush* ExpandedHoveredLeftBrush = nullptr;
	static const FSlateBrush* ExpandedHoveredRightBrush = nullptr;
	static const FSlateBrush* ExpandedLeftBrush = nullptr;
	static const FSlateBrush* ExpandedRightBrush = nullptr;
	static const FSlateBrush* CollapsedHoveredLeftBrush = nullptr;
	static const FSlateBrush* CollapsedHoveredRightBrush = nullptr;
	static const FSlateBrush* CollapsedLeftBrush = nullptr;
	static const FSlateBrush* CollapsedRightBrush = nullptr;
	
	if(ExpandedHoveredLeftBrush == nullptr)
	{
		static const TCHAR* ExpandedHoveredLeftName = TEXT("ControlRig.Node.PinTree.Arrow_Expanded_Hovered_Left");
		static const TCHAR* ExpandedHoveredRightName = TEXT("ControlRig.Node.PinTree.Arrow_Expanded_Hovered_Right");
		static const TCHAR* ExpandedLeftName = TEXT("ControlRig.Node.PinTree.Arrow_Expanded_Left");
		static const TCHAR* ExpandedRightName = TEXT("ControlRig.Node.PinTree.Arrow_Expanded_Right");
		static const TCHAR* CollapsedHoveredLeftName = TEXT("ControlRig.Node.PinTree.Arrow_Collapsed_Hovered_Left");
		static const TCHAR* CollapsedHoveredRightName = TEXT("ControlRig.Node.PinTree.Arrow_Collapsed_Hovered_Right");
		static const TCHAR* CollapsedLeftName = TEXT("ControlRig.Node.PinTree.Arrow_Collapsed_Left");
		static const TCHAR* CollapsedRightName = TEXT("ControlRig.Node.PinTree.Arrow_Collapsed_Right");
		
		ExpandedHoveredLeftBrush = FControlRigEditorStyle::Get().GetBrush(ExpandedHoveredLeftName);
		ExpandedHoveredRightBrush = FControlRigEditorStyle::Get().GetBrush(ExpandedHoveredRightName);
		ExpandedLeftBrush = FControlRigEditorStyle::Get().GetBrush(ExpandedLeftName);
		ExpandedRightBrush = FControlRigEditorStyle::Get().GetBrush(ExpandedRightName);
		CollapsedHoveredLeftBrush = FControlRigEditorStyle::Get().GetBrush(CollapsedHoveredLeftName);
		CollapsedHoveredRightBrush = FControlRigEditorStyle::Get().GetBrush(CollapsedHoveredRightName);
		CollapsedLeftBrush = FControlRigEditorStyle::Get().GetBrush(CollapsedLeftName);
		CollapsedRightBrush = FControlRigEditorStyle::Get().GetBrush(CollapsedRightName);
	}

	if(PinInfos[InPinInfoIndex].bExpanded)
	{
		if(bHovered)
		{
			return bLeft ? ExpandedHoveredLeftBrush : ExpandedHoveredRightBrush;
		}
		return bLeft ? ExpandedLeftBrush : ExpandedRightBrush;
	}

	if(bHovered)
	{
		return bLeft ? CollapsedHoveredLeftBrush : CollapsedHoveredRightBrush;
	}
	return bLeft ? CollapsedLeftBrush : CollapsedRightBrush;
}

FReply SControlRigGraphNode::OnExpanderArrowClicked(int32 InPinInfoIndex)
{
	if(UControlRigGraphNode* EdGraphNode = Cast<UControlRigGraphNode>(GraphNode))
	{
		if(URigVMController* Controller = EdGraphNode->GetController())
		{
			const FPinInfo& PinInfo = PinInfos[InPinInfoIndex];
			TArray<FString> PinPathsToModify;
			PinPathsToModify.Add(PinInfo.ModelPinPath);

			// with shift clicked we expand recursively
			if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
			{
				if(URigVMGraph* ModelGraph = EdGraphNode->GetModel())
				{
					if(URigVMPin* ModelPin = ModelGraph->FindPin(PinInfo.ModelPinPath))
					{
						TArray<URigVMPin*> SubPins = ModelPin->GetNode()->GetAllPinsRecursively();
						for(URigVMPin* SubPin : SubPins)
						{
							if(SubPin->IsInOuter(ModelPin))
							{
								PinPathsToModify.Add(SubPin->GetPinPath());
							}
						}

						Algo::Reverse(PinPathsToModify);
					}
				}
			}

			Controller->OpenUndoBracket(PinInfo.bExpanded ? TEXT("Collapsing Pin") : TEXT("Expanding Pin"));
			for(const FString& PinPathToModify : PinPathsToModify)
			{
				Controller->SetPinExpansion(PinPathToModify, !PinInfo.bExpanded, true);
			}
			Controller->CloseUndoBracket();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

void SControlRigGraphNode::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph,
	UObject* InSubject)
{
	switch(InNotifType)
	{
		case ERigVMGraphNotifType::PinExpansionChanged:
		{
			if(!ModelNode.IsValid())
			{
				return;
			}
				
			if(URigVMPin* Pin = Cast<URigVMPin>(InSubject))
			{
				if(Pin->GetNode() == ModelNode.Get())
				{
					const FString PinPath = Pin->GetPinPath();
					for(FPinInfo& PinInfo : PinInfos)
					{
						if(PinInfo.ModelPinPath == PinPath)
						{
							PinInfo.bExpanded = Pin->IsExpanded();
							break;
						}
					}
				}
			}
			break;
		}
		default:
		{
			break;
		}			
	}
}

#undef LOCTEXT_NAMESPACE
