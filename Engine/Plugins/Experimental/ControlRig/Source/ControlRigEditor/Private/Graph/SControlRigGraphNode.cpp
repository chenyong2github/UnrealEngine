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
#include "Widgets/Layout/SScrollBar.h"
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

#if WITH_EDITOR
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#endif

#define LOCTEXT_NAMESPACE "SControlRigGraphNode"

const FSlateBrush* SControlRigGraphNode::CachedImg_CR_Pin_Connected = nullptr;
const FSlateBrush* SControlRigGraphNode::CachedImg_CR_Pin_Disconnected = nullptr;

void SControlRigGraphNode::Construct( const FArguments& InArgs )
{
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

 	UControlRigGraphNode* ControlRigGraphNode = InArgs._GraphNodeObj;
	if (ControlRigGraphNode->GetModelNode() == nullptr)
	{
		return;
	}

	Blueprint = Cast<UControlRigBlueprint>(FBlueprintEditorUtils::FindBlueprintForNode(this->GraphNode));

	// Re-cache variable info here (unit structure could have changed since last reconstruction, e.g. array add/remove)
	// and also create missing pins if it hasn't created yet
	ControlRigGraphNode->AllocateDefaultPins();
	
	NodeErrorType = int32(EMessageSeverity::Info) + 1;
	InputTree = nullptr;
	OutputTree = nullptr;
	InputOutputTree = nullptr;
	this->UpdateGraphNode();

	SetIsEditable(false);

	ScrollBar = SNew(SScrollBar);

	// create pin-collapse areas
	LeftNodeBox->AddSlot()
		.AutoHeight()
		[
			SAssignNew(ExecutionTree, STreeView<URigVMPin*>)
			.Visibility(this, &SControlRigGraphNode::GetExecutionTreeVisibility)
			.TreeItemsSource(&ControlRigGraphNode->ExecutePins)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SControlRigGraphNode::MakePinTableRowWidget)
			.OnGetChildren(this, &SControlRigGraphNode::HandleGetChildrenForTree)
			.OnExpansionChanged(this, &SControlRigGraphNode::HandleExpansionChanged)
			.OnSetExpansionRecursive(this, &SControlRigGraphNode::HandleExpandRecursively, &ExecutionTree)
			.ExternalScrollbar(ScrollBar)
			.ItemHeight(20.0f)
		];

	LeftNodeBox->AddSlot()
		.AutoHeight()
		[
			SAssignNew(InputTree, STreeView<URigVMPin*>)
			.Visibility(this, &SControlRigGraphNode::GetInputTreeVisibility)
			.TreeItemsSource(&ControlRigGraphNode->InputPins)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SControlRigGraphNode::MakePinTableRowWidget)
			.OnGetChildren(this, &SControlRigGraphNode::HandleGetChildrenForTree)
			.OnExpansionChanged(this, &SControlRigGraphNode::HandleExpansionChanged)
			.OnSetExpansionRecursive(this, &SControlRigGraphNode::HandleExpandRecursively, &InputTree)
			.ExternalScrollbar(ScrollBar)
			.ItemHeight(20.0f)
		];

	LeftNodeBox->AddSlot()
		.AutoHeight()
		[
			SAssignNew(InputOutputTree, STreeView<URigVMPin*>)
			.Visibility(this, &SControlRigGraphNode::GetInputOutputTreeVisibility)
			.TreeItemsSource(&ControlRigGraphNode->InputOutputPins)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SControlRigGraphNode::MakePinTableRowWidget)
			.OnGetChildren(this, &SControlRigGraphNode::HandleGetChildrenForTree)
			.OnExpansionChanged(this, &SControlRigGraphNode::HandleExpansionChanged)
			.OnSetExpansionRecursive(this, &SControlRigGraphNode::HandleExpandRecursively, &InputOutputTree)
			.ExternalScrollbar(ScrollBar)
			.ItemHeight(20.0f)
		];

	LeftNodeBox->AddSlot()
		.AutoHeight()
		[
			SAssignNew(OutputTree, STreeView<URigVMPin*>)
			.Visibility(this, &SControlRigGraphNode::GetOutputTreeVisibility)
			.TreeItemsSource(&ControlRigGraphNode->OutputPins)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SControlRigGraphNode::MakePinTableRowWidget)
			.OnGetChildren(this, &SControlRigGraphNode::HandleGetChildrenForTree)
			.OnExpansionChanged(this, &SControlRigGraphNode::HandleExpansionChanged)
			.OnSetExpansionRecursive(this, &SControlRigGraphNode::HandleExpandRecursively, &OutputTree)
			.ExternalScrollbar(ScrollBar)
			.ItemHeight(20.0f)
		];

	LeftNodeBox->AddSlot()
        .AutoHeight()
        [
            SAssignNew(VariableRemappingList, SListView<TSharedPtr<FRigVMExternalVariable>>)
            .Visibility(this, &SControlRigGraphNode::GetVariableListVisibility)
            .ListItemsSource(&ControlRigGraphNode->ExternalVariables)
            .SelectionMode(ESelectionMode::None)
            .OnGenerateRow(this, &SControlRigGraphNode::MakeVariableTableRowWidget)
            .ExternalScrollbar(ScrollBar)
            .ItemHeight(20.0f)
        ];

	struct Local
	{
		static void SetItemExpansion_Recursive(UControlRigGraphNode* InControlRigGraphNode, TSharedPtr<STreeView<URigVMPin*>>& TreeWidget, const TArray<URigVMPin*>& InItems)
		{
			for(URigVMPin* Pin : InItems)
			{
				if(InControlRigGraphNode->IsPinExpanded(Pin->GetPinPath()))
				{
					TreeWidget->SetItemExpansion(Pin, true);
					SetItemExpansion_Recursive(InControlRigGraphNode, TreeWidget, Pin->GetSubPins());
				}
			}
		}
	};

	Local::SetItemExpansion_Recursive(ControlRigGraphNode, ExecutionTree, ControlRigGraphNode->ExecutePins);
	Local::SetItemExpansion_Recursive(ControlRigGraphNode, InputTree, ControlRigGraphNode->InputPins);
	Local::SetItemExpansion_Recursive(ControlRigGraphNode, InputOutputTree, ControlRigGraphNode->InputOutputPins);
	Local::SetItemExpansion_Recursive(ControlRigGraphNode, OutputTree, ControlRigGraphNode->OutputPins);


	// force the regeneration of all pins.
	// the treeview is lazy - to ensure we draw the connections properly we need
	// to ensure that it updates it's items at least once.
	FGeometry DummyGeometry(FVector2D(), FVector2D(), FVector2D(FLT_MAX, FLT_MAX), 1.f);
	ExecutionTree->RequestTreeRefresh();
	InputTree->RequestTreeRefresh();
	InputOutputTree->RequestTreeRefresh();
	OutputTree->RequestTreeRefresh();
	ExecutionTree->Tick(DummyGeometry, 0.f, 0.f);
	InputTree->Tick(DummyGeometry, 0.f, 0.f);
	InputOutputTree->Tick(DummyGeometry, 0.f, 0.f);
	OutputTree->Tick(DummyGeometry, 0.f, 0.f);
	VariableRemappingList->Tick(DummyGeometry, 0.f, 0.f);

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

	ControlRigGraphNode->GetNodeTitleDirtied().BindSP(this, &SControlRigGraphNode::HandleNodeTitleDirtied);
}

TSharedRef<SWidget> SControlRigGraphNode::CreateNodeContentArea()
{
	// We only use the LeftNodeBox
	return SNew(SBorder)
		.BorderImage( FEditorStyle::GetBrush("NoBorder") )
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0,3))
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.FillWidth(1.0f)
			[
				SAssignNew(LeftNodeBox, SVerticalBox)
			]
		];
}

TSharedPtr<SGraphPin> SControlRigGraphNode::GetHoveredPin(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	TSharedPtr<SGraphPin> HoveredPin = SGraphNode::GetHoveredPin(MyGeometry, MouseEvent);
	if(!HoveredPin.IsValid())
	{
 		TArray<TSharedRef<SWidget>> ExtraWidgetArray;
 		ExtraWidgetToPinMap.GenerateKeyArray(ExtraWidgetArray);
 		TSet<TSharedRef<SWidget>> ExtraWidgets(ExtraWidgetArray);
 		
 		TMap<TSharedRef<SWidget>, FArrangedWidget> Result;
 		FindChildGeometries(MyGeometry, ExtraWidgets, Result);

		if (Result.Num() > 0)
		{
			FArrangedChildren ArrangedWidgets(EVisibility::Visible);
			Result.GenerateValueArray(ArrangedWidgets.GetInternalArray());
			int32 HoveredWidgetIndex = SWidget::FindChildUnderMouse(ArrangedWidgets, MouseEvent);
			if (HoveredWidgetIndex != INDEX_NONE)
			{
				return *ExtraWidgetToPinMap.Find(ArrangedWidgets[HoveredWidgetIndex].Widget);
			}
		}
	}
	return HoveredPin;
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
	// We show our own label
	PinToAdd->SetShowLabel(false);

	UControlRigGraphNode* ControlRigGraphNode = CastChecked<UControlRigGraphNode>(GraphNode);
	if(URigVMNode* ModelNode = ControlRigGraphNode->GetModelNode())
	{
		const UEdGraphPin* EdPinObj = PinToAdd->GetPinObj();

		// Remove value widget from combined pin content
		TSharedPtr<SWrapBox> LabelAndValueWidget = PinToAdd->GetLabelAndValue();
		TSharedPtr<SHorizontalBox> FullPinHorizontalRowWidget = PinToAdd->GetFullPinHorizontalRowWidget().Pin();
		if(LabelAndValueWidget.IsValid() && FullPinHorizontalRowWidget.IsValid())
		{
			FullPinHorizontalRowWidget->RemoveSlot(LabelAndValueWidget.ToSharedRef());
		}

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
			}
		}

		PinToAdd->SetOwner(SharedThis(this));
		PinWidgetMap.Add(EdPinObj, PinToAdd);
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
	return FEditorStyle::GetBrush("Graph.Node.TintedBody");
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
		if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphNode))
		{
			if (URigVMNode* ModelNode = RigNode->GetModelNode())
			{
				if(Blueprint.IsValid())
				{
					Blueprint->BroadcastNodeDoubleClicked(ModelNode);
					return FReply::Handled();
				}
			}
		}
	}
	return SGraphNode::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}

bool SControlRigGraphNode::UseLowDetailNodeTitles() const
{
	return ParentUseLowDetailNodeTitles();
}

EVisibility SControlRigGraphNode::GetTitleVisibility() const
{
	return ParentUseLowDetailNodeTitles() ? EVisibility::Hidden : EVisibility::Visible;
}

EVisibility SControlRigGraphNode::GetExecutionTreeVisibility() const
{
	UControlRigGraphNode* ControlRigGraphNode = CastChecked<UControlRigGraphNode>(GraphNode);
	return ControlRigGraphNode->ExecutePins.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SControlRigGraphNode::GetInputTreeVisibility() const
{
	UControlRigGraphNode* ControlRigGraphNode = CastChecked<UControlRigGraphNode>(GraphNode);
	return ControlRigGraphNode->InputPins.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SControlRigGraphNode::GetInputOutputTreeVisibility() const
{
	UControlRigGraphNode* ControlRigGraphNode = CastChecked<UControlRigGraphNode>(GraphNode);
	return ControlRigGraphNode->InputOutputPins.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SControlRigGraphNode::GetOutputTreeVisibility() const
{
	UControlRigGraphNode* ControlRigGraphNode = CastChecked<UControlRigGraphNode>(GraphNode);
	return ControlRigGraphNode->OutputPins.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SControlRigGraphNode::GetVariableListVisibility() const
{
	UControlRigGraphNode* ControlRigGraphNode = CastChecked<UControlRigGraphNode>(GraphNode);
	return ControlRigGraphNode->ExternalVariables.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
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

class SControlRigExpanderArrow : public SExpanderArrow
{
	SLATE_BEGIN_ARGS(SControlRigExpanderArrow) {}

	SLATE_ARGUMENT(bool, LeftAligned)

	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, const TSharedPtr<class ITableRow>& TableRow)
	{
		bLeftAligned = InArgs._LeftAligned;

		SExpanderArrow::Construct(
			SExpanderArrow::FArguments()
			.IndentAmount(8.0f),
			TableRow);

		// override padding
		ChildSlot.Padding(TAttribute<FMargin>(this, &SControlRigExpanderArrow::GetExpanderPadding_Extended));

		// override image
		ExpanderArrow->SetContent(
			SNew(SImage)
			.Image(this, &SControlRigExpanderArrow::GetExpanderImage_Extended)
			.ColorAndOpacity(FSlateColor::UseForeground()));
	}

	FMargin GetExpanderPadding_Extended() const
	{
		const int32 NestingDepth = FMath::Max(0, OwnerRowPtr.Pin()->GetIndentLevel() - BaseIndentLevel.Get());
		const float Indent = IndentAmount.Get(8.0f);
		return bLeftAligned ? FMargin( NestingDepth * Indent, 0,0,0 ) : FMargin( 0,0, NestingDepth * Indent,0 );
	}

	const FSlateBrush* GetExpanderImage_Extended() const
	{
		const bool bIsItemExpanded = OwnerRowPtr.Pin()->IsItemExpanded();

		FName ResourceName;
		if (bIsItemExpanded)
		{
			if ( ExpanderArrow->IsHovered() )
			{
				static FName ExpandedHoveredLeftName = "ControlRig.Node.PinTree.Arrow_Expanded_Hovered_Left";
				static FName ExpandedHoveredRightName = "ControlRig.Node.PinTree.Arrow_Expanded_Hovered_Right";
				ResourceName = bLeftAligned ? ExpandedHoveredLeftName : ExpandedHoveredRightName;
			}
			else
			{
				static FName ExpandedLeftName = "ControlRig.Node.PinTree.Arrow_Expanded_Left";
				static FName ExpandedRightName = "ControlRig.Node.PinTree.Arrow_Expanded_Right";
				ResourceName = bLeftAligned ? ExpandedLeftName : ExpandedRightName;
			}
		}
		else
		{
			if ( ExpanderArrow->IsHovered() )
			{
				static FName CollapsedHoveredLeftName = "ControlRig.Node.PinTree.Arrow_Collapsed_Hovered_Left";
				static FName CollapsedHoveredRightName = "ControlRig.Node.PinTree.Arrow_Collapsed_Hovered_Right";
				ResourceName = bLeftAligned ? CollapsedHoveredLeftName : CollapsedHoveredRightName;
			}
			else
			{
				static FName CollapsedLeftName = "ControlRig.Node.PinTree.Arrow_Collapsed_Left";
				static FName CollapsedRightName = "ControlRig.Node.PinTree.Arrow_Collapsed_Right";
				ResourceName = bLeftAligned ? CollapsedLeftName : CollapsedRightName;
			}
		}

		return FControlRigEditorStyle::Get().GetBrush(ResourceName);
	}

	virtual FCursorReply OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const
	{
		return FCursorReply::Cursor(EMouseCursor::Default);
	}

	bool bLeftAligned;
};

static TAutoConsoleVariable<int32> CVarEnableShowBackground(
    TEXT("controlrig.ShowBackground"),
    0,
    TEXT("Enable unsupported Experimental prototype Modeling Tools"));


class SControlRigPinTreeRow : public STableRow<URigVMPin*>
{
public:
	SLATE_BEGIN_ARGS(SControlRigPinTreeRow) {}

	SLATE_ARGUMENT(bool, LeftAligned)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		bLeftAligned = InArgs._LeftAligned;

		STableRow<URigVMPin*>::Construct(STableRow<URigVMPin*>::FArguments(), InOwnerTableView);
	}

	const FSlateBrush* GetBorder() const
	{
		bool bShowBG = (CVarEnableShowBackground.GetValueOnGameThread() > 0);

		if (bShowBG)
			return STableRow<URigVMPin*>::GetBorder();
		else
		// We want a transparent background.
		return FCoreStyle::Get().GetBrush("NoBrush");
	}

	virtual void ConstructChildren( ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent ) override
	{
		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		FMargin InputPadding = Settings->GetInputPinPadding();
		InputPadding.Top = InputPadding.Bottom = 3.0f;
		InputPadding.Right = 0.0f;

		FMargin OutputPadding = Settings->GetOutputPinPadding();
		OutputPadding.Top = OutputPadding.Bottom = 3.0f;
		OutputPadding.Left = 2.0f;

		this->Content = InContent;

		SHorizontalBox::FSlot* InnerContentSlotNativePtr = nullptr;

		TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox);

		if(bLeftAligned)
		{
			ContentBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.Padding(InputPadding)
			[
				SAssignNew(LeftContentBox, SBox)
			];

			ContentBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			[
				SNew(SControlRigExpanderArrow, SharedThis(this))
				.ToolTipText(LOCTEXT("ExpandSubPin", "Expand Pin"))
				.LeftAligned(bLeftAligned)
			];

			ContentBox->AddSlot()
			.FillWidth(1.0f)
			.Expose(InnerContentSlotNativePtr)
			[
				SAssignNew(RightContentBox, SBox)
				[
					InContent
				]
			];
		}
		else
		{
			ContentBox->AddSlot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			[
				SAssignNew(LeftContentBox, SBox)
			];

			ContentBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(SControlRigExpanderArrow, SharedThis(this))
				.LeftAligned(bLeftAligned)
			];

			ContentBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.Expose(InnerContentSlotNativePtr)
			.Padding(OutputPadding)
			[
				SAssignNew(RightContentBox, SBox)
				[
					InContent
				]
			];
		}

		this->ChildSlot
		[
			ContentBox
		];

		InnerContentSlot = InnerContentSlotNativePtr;
	}

	/** Exposed boxes to slot pin widgets into */
	TSharedPtr<SBox> LeftContentBox;
	TSharedPtr<SBox> RightContentBox;

	/** Whether we align our content left or right */
	bool bLeftAligned;
};

class SControlRigVariableListRow : public STableRow<TSharedPtr<FRigVMExternalVariable>>
{
public:
	SLATE_BEGIN_ARGS(SControlRigVariableListRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		STableRow<TSharedPtr<FRigVMExternalVariable>>::Construct(STableRow<TSharedPtr<FRigVMExternalVariable>>::FArguments(), InOwnerTableView);
	}

	const FSlateBrush* GetBorder() const
	{
		bool bShowBG = (CVarEnableShowBackground.GetValueOnGameThread() > 0);

		if (bShowBG)
			return STableRow<TSharedPtr<FRigVMExternalVariable>>::GetBorder();
		else
		// We want a transparent background.
		return FCoreStyle::Get().GetBrush("NoBrush");
	}

	virtual void ConstructChildren( ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent ) override
	{
		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		FMargin InputPadding = Settings->GetInputPinPadding();
		InputPadding.Top = InputPadding.Bottom = 3.0f;
		InputPadding.Right = 8.0f;

		this->Content = InContent;

		SHorizontalBox::FSlot* InnerContentSlotNativePtr = nullptr;

		TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox);

		ContentBox->AddSlot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(InputPadding)
		[
			SAssignNew(LeftContentBox, SBox)
		];

		ContentBox->AddSlot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.MaxWidth(200.f)
		.Expose(InnerContentSlotNativePtr)
		[
			SAssignNew(RightContentBox, SBox)
			[
				InContent
			]
		];

		this->ChildSlot
		[
			ContentBox
		];

		InnerContentSlot = InnerContentSlotNativePtr;
	}

	/** Exposed boxes to slot pin widgets into */
	TSharedPtr<SBox> LeftContentBox;
	TSharedPtr<SBox> RightContentBox;
};

TSharedRef<SWidget> SControlRigGraphNode::AddContainerPinContent(URigVMPin* InItem, FText InTooltipText)
{
	return SNew(SButton)
	.ContentPadding(0.0f)
	.ButtonStyle(FEditorStyle::Get(), "NoBorder")
	.OnClicked(this, &SControlRigGraphNode::HandleAddArrayElement, InItem)
	.IsEnabled(this, &SGraphNode::IsNodeEditable)
	.ToolTipText(InTooltipText)
	.Cursor(EMouseCursor::Default)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(7,0,0,0)
		[
			SNew(SImage)
			.Image(FEditorStyle::GetBrush(TEXT("Icons.PlusCircle")))
		]
	];
}

TSharedRef<ITableRow> SControlRigGraphNode::MakePinTableRowWidget(URigVMPin* InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	const bool bLeaf = InItem->GetSubPins().Num() == 0;
	const bool bIsContainer = InItem->IsArray();

	TSharedPtr<SGraphPin> InputPinWidget;
	TSharedPtr<SGraphPin> OutputPinWidget;
	TSharedPtr<SWidget> InputPinValueWidget;

	UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphNode);
	const UControlRigGraphNode::PinPair& Pair = RigNode->CachedPins.FindChecked(InItem);

	if (Pair.InputPin)
	{
		TSharedPtr<SGraphPin>* InputGraphPinPtr = PinWidgetMap.Find(Pair.InputPin);
		if(InputGraphPinPtr != nullptr)
		{
			InputPinWidget = *InputGraphPinPtr;

			bool bIsPlainOrEditableStruct = !InItem->IsStruct();
			if (!bIsPlainOrEditableStruct)
			{
				if (InItem->GetSubPins().Num() == 0)
				{
					if (const UControlRigGraphSchema* RigSchema = Cast<UControlRigGraphSchema>(RigNode->GetSchema()))
					{
						bIsPlainOrEditableStruct = RigSchema->IsStructEditable(InItem->GetScriptStruct());
					}
				}
			}

			// Only leaf pins have value widgets, but not containers
			if(((bLeaf && bIsPlainOrEditableStruct) || InItem->IsBoundToVariable()) && !bIsContainer)
			{
				InputPinValueWidget = (*InputGraphPinPtr)->GetValueWidget();
			}
			else if(bIsContainer)
			{
				// Add a 'new item' widget for containers
				InputPinValueWidget = AddContainerPinContent(InItem, LOCTEXT("AddToContainer", "Add a new value to this container"));
			}
		}
	}

	if (Pair.OutputPin)
	{
		TSharedPtr<SGraphPin>* OutputGraphPinPtr = PinWidgetMap.Find(Pair.OutputPin);
		if(OutputGraphPinPtr != nullptr)
		{
			OutputPinWidget = *OutputGraphPinPtr;
		}
	}

	TSharedRef<SControlRigPinTreeRow> ControlRigPinTreeRow = SNew(SControlRigPinTreeRow, OwnerTable)
		.LeftAligned(!(OutputPinWidget.IsValid() && !InputPinWidget.IsValid()))
		.ToolTipText(InItem->GetToolTipText());

	if(InputPinWidget.IsValid() || OutputPinWidget.IsValid())
	{
		TWeakPtr<SGraphPin> WeakPin = InputPinWidget.IsValid() ? InputPinWidget : OutputPinWidget;

		TSharedRef<SWidget> LabelWidget = SNew(STextBlock)
		.Text(this, &SControlRigGraphNode::GetPinLabel, WeakPin)
		.TextStyle(FEditorStyle::Get(), NAME_DefaultPinLabelStyle)
		.ColorAndOpacity(this, &SControlRigGraphNode::GetPinTextColor, WeakPin);

		// add to mapping that allows labels to act as hover widgets
		if(InputPinWidget.IsValid())
		{
			ExtraWidgetToPinMap.Emplace(LabelWidget, InputPinWidget.ToSharedRef());
		}
		else if(OutputPinWidget.IsValid())
		{
			ExtraWidgetToPinMap.Emplace(LabelWidget, OutputPinWidget.ToSharedRef());
		}

		FMargin OutputPadding = Settings->GetOutputPinPadding();
		OutputPadding.Top = OutputPadding.Bottom = 3.0f;
		OutputPadding.Left = 2.0f;

		if(OutputPinWidget.IsValid() && !InputPinWidget.IsValid())
		{
			TSharedRef<SWidget> InputWidget = 
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(50.0f, 2.0f, 2.0f, 2.0f)
				[
					LabelWidget
				];

			TSharedRef<SWidget> OutputWidget = 
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.MinDesiredWidth(24.0f)
					[
						OutputPinWidget.IsValid() ? StaticCastSharedRef<SWidget>(OutputPinWidget.ToSharedRef()) : StaticCastSharedRef<SWidget>(SNew(SSpacer).Size(FVector2D(16.0f, 14.0f)))
					]
				];

			ControlRigPinTreeRow->LeftContentBox->SetContent(InputWidget);
			ControlRigPinTreeRow->RightContentBox->SetContent(OutputWidget);
		}
		else
		{
			TSharedRef<SWidget> InputWidget = 
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.MinDesiredWidth(24.0f)
					[
						InputPinWidget.IsValid() ? StaticCastSharedRef<SWidget>(InputPinWidget.ToSharedRef()) : StaticCastSharedRef<SWidget>(SNew(SSpacer).Size(FVector2D(16.0f, 14.0f)))
					]
				];
				
			TSharedRef<SWidget> OutputWidget = 
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(2.0f)
				[
					LabelWidget
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(2.0f, 2.0f, 18.0f, 2.0f)
				[
					InputPinValueWidget.IsValid() ? InputPinValueWidget.ToSharedRef() : SNew(SSpacer)
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(OutputPadding)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.MinDesiredWidth(24.0f)
					[
						OutputPinWidget.IsValid() ? StaticCastSharedRef<SWidget>(OutputPinWidget.ToSharedRef()) : StaticCastSharedRef<SWidget>(SNew(SSpacer).Size(FVector2D(16.0f, 14.0f)))
					]
				];

			ControlRigPinTreeRow->LeftContentBox->SetContent(InputWidget);
			ControlRigPinTreeRow->RightContentBox->SetContent(OutputWidget);
		}
	}

	return ControlRigPinTreeRow;
}

TSharedRef<ITableRow> SControlRigGraphNode::MakeVariableTableRowWidget(TSharedPtr<FRigVMExternalVariable> InVariable,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SControlRigVariableListRow> ControlRigVariableListRow = SNew(SControlRigVariableListRow, OwnerTable);

	UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphNode);
	URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(RigNode->GetModelNode());
	check(FunctionReferenceNode);
	TWeakObjectPtr<URigVMFunctionReferenceNode> WeakFunctionReferenceNode = FunctionReferenceNode;

	UControlRigBlueprint* ControlRigBlueprint = FunctionReferenceNode->GetReferencedNode()->GetTypedOuter<UControlRigBlueprint>(); 
	check(ControlRigBlueprint);
	TWeakObjectPtr<UControlRigBlueprint> WeakControlRigBlueprint = ControlRigBlueprint;

	ControlRigVariableListRow->LeftContentBox->SetContent(SNew(STextBlock)
		.Text(FText::FromName(InVariable->Name))
        .TextStyle(FEditorStyle::Get(), NAME_DefaultPinLabelStyle)
		.ColorAndOpacity(this, &SControlRigGraphNode::GetVariableLabelTextColor, WeakFunctionReferenceNode, InVariable->Name)
		.ToolTipText(this, &SControlRigGraphNode::GetVariableLabelTooltipText, WeakControlRigBlueprint, InVariable->Name)
    );

	ControlRigVariableListRow->RightContentBox->SetContent(SNew(SControlRigVariableBinding)
		.Blueprint(Blueprint.Get())
		.FunctionReferenceNode(FunctionReferenceNode)
		.InnerVariableName(InVariable->Name)
	);

	return ControlRigVariableListRow;
}

void SControlRigGraphNode::HandleGetChildrenForTree(URigVMPin* InItem, TArray<URigVMPin*>& OutChildren)
{
	OutChildren.Append(InItem->GetSubPins());
}

void SControlRigGraphNode::HandleExpansionChanged(URigVMPin* InItem, bool bExpanded)
{
	if (UControlRigGraphNode* RigGraphNode = Cast< UControlRigGraphNode>(GraphNode))
	{
		RigGraphNode->GetController()->SetPinExpansion(InItem->GetPinPath(), bExpanded, true);
	}
}

void SControlRigGraphNode::HandleExpandRecursively(URigVMPin* InItem, bool bExpanded, TSharedPtr<STreeView<URigVMPin*>>* TreeWidgetPtr)
{
	TSharedPtr<STreeView<URigVMPin*>>& TreeWidget = *TreeWidgetPtr;

	if (UControlRigGraphNode* RigGraphNode = Cast< UControlRigGraphNode>(GraphNode))
	{
		RigGraphNode->GetController()->OpenUndoBracket(TEXT("Expand pin recursively"));

		TArray<URigVMPin*> ModelPins;
		ModelPins.Add(InItem);

		for (int32 PinIndex = 0; PinIndex < ModelPins.Num(); PinIndex++)
		{
			URigVMPin* ModelPin = ModelPins[PinIndex];
			ModelPins.Append(ModelPin->GetSubPins());
		}

		if (!bExpanded)
		{
			Algo::Reverse(ModelPins);
		}

		for (int32 PinIndex = 0; PinIndex < ModelPins.Num(); PinIndex++)
		{
			URigVMPin* ModelPin = ModelPins[PinIndex];
			if (ModelPin->IsExpanded() != bExpanded && ModelPin->GetSubPins().Num() > 0)
			{
				TreeWidget->SetItemExpansion(ModelPin, bExpanded);
			}
		}

		RigGraphNode->GetController()->CloseUndoBracket();
	}
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

FReply SControlRigGraphNode::HandleAddArrayElement(URigVMPin* InItem)
{
	if(InItem)
	{
		if (UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(GraphNode))
		{
			ControlRigGraphNode->HandleAddArrayElement(InItem->GetPinPath());
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
							FRigVMMemoryContainer& Memory = 
								WatchOperand->GetMemoryType() == ERigVMMemoryType::Literal ?
								ActiveObject->GetVM()->GetLiteralMemory() : ActiveObject->GetVM()->GetWorkMemory();

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

	if (UControlRigGraphNode* RigNode = CastChecked<UControlRigGraphNode>(GraphNode, ECastCheckedType::NullAllowed))
	{
		if (URigVMNode* ModelNode = RigNode->GetModelNode())
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
			if (UControlRigGraphNode* RigNode = CastChecked<UControlRigGraphNode>(GraphNode, ECastCheckedType::NullAllowed))
			{
				if (URigVMNode* ModelNode = RigNode->GetModelNode())
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
	}

	return FText();
}

#undef LOCTEXT_NAMESPACE