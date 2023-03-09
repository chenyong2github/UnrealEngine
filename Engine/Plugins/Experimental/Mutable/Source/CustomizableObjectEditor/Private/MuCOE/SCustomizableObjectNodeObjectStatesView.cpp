// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/SCustomizableObjectNodeObjectStatesView.h"

#include "Framework/Views/TableViewMetadata.h"
#include "MuCOE/CustomizableObjectGraph.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObject.h"
#include "SlotBase.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Templates/Casts.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/SListView.h"

class STableViewBase;
struct FSlateBrush;


#define LOCTEXT_NAMESPACE "CustomizableObjectNodeObjectStatesView"


TSharedRef<FDragAndDropOpWithWidget> FDragAndDropOpWithWidget::New(int32 InSlotIndexBeingDragged, SVerticalBox::FSlot* InSlotBeingDragged,
	TSharedPtr<SWidget> InWidgetToShow)
{
	TSharedRef<FDragAndDropOpWithWidget> Operation = MakeShareable(new FDragAndDropOpWithWidget);
	{
		Operation->MouseCursor = EMouseCursor::GrabHandClosed;
		Operation->SlotIndexBeingDragged = InSlotIndexBeingDragged;
		Operation->SlotBeingDragged = InSlotBeingDragged;
		Operation->WidgetToShow = InWidgetToShow;
	}

	Operation->Construct();
	
	return Operation;
}

TSharedPtr<SWidget> FDragAndDropOpWithWidget::GetDefaultDecorator() const
{
	return SNew(SBorder)
	.BorderImage(FAppStyle::GetBrush("ContentBrowser.AssetDragDropTooltipBackground"))
	.Content()
	[
		WidgetToShow.ToSharedRef()
	];
}



void SCustomizableObjectState::Construct(const FArguments& InArgs)
{
	Node = InArgs._Node;
	StateIndex = InArgs._StateIndex;
	
	SAssignNew(VerticalSlots, SVerticalBox);
	
	// Images and Runtime parameters widgets are stored in arrays to control the visibility
	SAssignNew(RuntimeParametersWidget,SCustomizableObjectRuntimeParameterList)
	.Node(Node)
	.StateIndex(StateIndex);

	RuntimeParametersWidget->SetCollapsed(true);
	RuntimeParametersWidget->SetVisibility(GetCollapsed());

	SAssignNew(CollapsedArrow,SImage)
	.Image(GetExpressionPreviewArrow());

	VerticalSlots->AddSlot()
	.AutoHeight()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0.0f, 0.0f, 3.0f, 5.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.WidthOverride(16.0f)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("VerticalBoxDragIndicatorShort"))
				]
			]
			
			// State variable label
			+ SHorizontalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			.AutoWidth()
			.Padding(2.0f, 0.0f, 3.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString("State:"))
			]
			
			// State name
			+ SHorizontalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			.AutoWidth()
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Node->States[StateIndex].Name))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			.Padding(10.0f, 0.0f, 3.0f, 3.0f)
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				// Collapsed Arrow checkbox
				+SHorizontalBox::Slot()
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.AutoWidth()
				.Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					SNew(SCheckBox)
					.OnCheckStateChanged(this, &SCustomizableObjectState::OnCollapseChanged)
					.IsChecked(ECheckBoxState::Unchecked)
					.Cursor(EMouseCursor::Default)
					.Style(FAppStyle::Get(), "Graph.Node.AdvancedView")
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						[
							CollapsedArrow.ToSharedRef()
						]
					]
				]
				
				// Number of runtime parameters
				+ SHorizontalBox::Slot()
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.AutoWidth()
				.Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this,&SCustomizableObjectState::GetStateParameterCountText)
				]

				+ SHorizontalBox::Slot()
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.AutoWidth()
				.Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					SNew(SButton)
					.OnClicked(this, &SCustomizableObjectState::OnAddRuntimeParameterPressed)
					.ToolTipText(LOCTEXT("AddRuntimeParameter", "Add Runtime Parameter"))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush(TEXT("Plus")))
						]
					]
				]
			]
			
			// Runtime parameters widget
			+ SVerticalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			.Padding(20.0f, 0.0f, 0.0f, 0.0f)
			.AutoHeight()
			[
				RuntimeParametersWidget.ToSharedRef()
			]
		]
	];
	
	// Add the widget to the child slot
	ChildSlot
	[
		VerticalSlots.ToSharedRef()
	];
}

void SCustomizableObjectState::OnCollapseChanged(const ECheckBoxState NewCheckedState)
{
	bool bCollapse = (NewCheckedState != ECheckBoxState::Checked);

	RuntimeParametersWidget->SetCollapsed(bCollapse);
	RuntimeParametersWidget->SetVisibility(GetCollapsed());
	CollapsedArrow->SetImage(GetExpressionPreviewArrow());
}

EVisibility SCustomizableObjectState::GetCollapsed()
{
	return RuntimeParametersWidget->IsCollapsed() ? EVisibility::Collapsed : EVisibility::Visible;
}

const FSlateBrush* SCustomizableObjectState::GetExpressionPreviewArrow() const
{
	return FAppStyle::GetBrush(RuntimeParametersWidget->IsCollapsed() ? TEXT("SurfaceDetails.PanUPositive") : TEXT("SurfaceDetails.PanVPositive"));
}

FText SCustomizableObjectState::GetStateParameterCountText() const
{
	return FText::FromString(FString::Printf(TEXT("Runtime Parameters:  %d"), Node->States[StateIndex].RuntimeParameters.Num()));
}

FReply SCustomizableObjectState::OnAddRuntimeParameterPressed()
{
	Node->States[StateIndex].RuntimeParameters.Add("NONE");
	RuntimeParametersWidget->BuildList();

	return FReply::Handled();
}

void SCustomizableObjectState::UpdateStateIndex(int32 NewStateIndex)
{
	StateIndex = NewStateIndex;
	RuntimeParametersWidget->UpdateStateIndex(StateIndex);
}


void SCustomizableObjectNodeObjectSatesView::Construct(const FArguments& InArgs)
{
	Node = InArgs._Node;

	if (Node)
	{
		SAssignNew(VerticalSlots, SDragAndDropVerticalBox)
		.OnDragDetected(this,&SCustomizableObjectNodeObjectSatesView::OnStateDragDetected)
		.OnCanAcceptDrop(this,&SCustomizableObjectNodeObjectSatesView::OnCanAcceptStateDrop)
		.OnAcceptDrop(this,&SCustomizableObjectNodeObjectSatesView::OnAcceptStateDrop);
		
		for (int32 i = 0; i < Node->States.Num(); i++)
		{
			VerticalSlots->AddSlot()
			.AutoHeight()
			[
				 SNew(SCustomizableObjectState)
				.Node(Node)
				.StateIndex(i)
			];
		}

		// Add the widget to the child slot
		ChildSlot
		[
			VerticalSlots.ToSharedRef()
		];
	}
}

/** drag and drop of states */


FReply SCustomizableObjectNodeObjectSatesView::OnStateDragDetected(const FGeometry& Geometry,
	const FPointerEvent& PointerEvent, int SlotBeingDraggedIndex, SVerticalBox::FSlot* Slot)
{
	if ( Slot)
	{
		// Widget being displayed during the drag and drop
		TSharedPtr<STextBlock> WidgetToDisplay =
			SNew(STextBlock)
			.Text(FText::FromString( Node->States[SlotBeingDraggedIndex].Name));
		
		return FReply::Handled().BeginDragDrop( FDragAndDropOpWithWidget::New(SlotBeingDraggedIndex,Slot, WidgetToDisplay));
	}

	return FReply::Unhandled();
}

TOptional<SDragAndDropVerticalBox::EItemDropZone> SCustomizableObjectNodeObjectSatesView::OnCanAcceptStateDrop(
	const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone ItemDropZone, SVerticalBox::FSlot* Slot)
{
	const TSharedPtr<FDragAndDropOpWithWidget> Operation = DragDropEvent.GetOperationAs<FDragAndDropOpWithWidget>();
	if (Operation.IsValid())		
	{
		// only move states over same parent widget 
		if (Operation->SlotBeingDragged->GetOwnerWidget()->GetId() == VerticalSlots->GetId())
		{
			// And if the value to be dragged over is different than the origin one
			if (Operation->SlotBeingDragged != Slot)
			{
				return ItemDropZone;
			}
		}
	}

	// Operation will not succeed
	return TOptional<SDragAndDropVerticalBox::EItemDropZone>();
}

FReply SCustomizableObjectNodeObjectSatesView::OnAcceptStateDrop(const FDragDropEvent& DragDropEvent,
	SDragAndDropVerticalBox::EItemDropZone ItemDropZone, int NewIndex, SVerticalBox::FSlot* Slot)
{
	// Move the data around to match the drag and drop operation performed
	TSharedPtr<FDragAndDropOpWithWidget> Operation = DragDropEvent.GetOperationAs<FDragAndDropOpWithWidget>();
	if (Operation.IsValid())
	{
		// Apply drop operation on the states array to later update the states each state slate targets (Tick)
		{
			const FCustomizableObjectState DraggedParamTemp = Node->States[Operation->SlotIndexBeingDragged];
			Node->States.RemoveAt(Operation->SlotIndexBeingDragged);

			// Array structure changed, update new index accordingly
			if (ItemDropZone == SDragAndDropVerticalBox::EItemDropZone::BelowItem &&
				Operation->SlotIndexBeingDragged - NewIndex >= 1)
			{
				NewIndex -= 1;
			}

			Node->States.Insert(DraggedParamTemp,NewIndex);
		}

		// Set flag so on next tick all the slates get updated state indices
		bWasStateDropPerformed = true;
		return FReply::Handled();
	}

	return FReply::Unhandled();
}


void SCustomizableObjectNodeObjectSatesView::UpdateStatesIndex()
{
	FChildren* Children = VerticalSlots->GetChildren();
	int32 NewSlateIndex = 0;
	for (int32 ChildIndex = 0 ; ChildIndex < Children->Num(); ChildIndex++)
	{
		TSharedRef<SWidget> ChildSlate = Children->GetChildAt(ChildIndex);
		SCustomizableObjectState* CustomizableObjectState =
			StaticCast<SCustomizableObjectState*>(ChildSlate.ToSharedPtr().Get());

		// Do not expect for all children to be SCustomizableObjectState
		if (CustomizableObjectState)
		{
			CustomizableObjectState->UpdateStateIndex(NewSlateIndex);
			NewSlateIndex++;
		}
	}
}

/** end of drag and drop */

void SCustomizableObjectNodeObjectSatesView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
												  const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// After a drop operation has been performed update the indices Update the state index for each of the state slates
	// so it matches the index each have as children of the slate
	if (bWasStateDropPerformed )
	{
		UpdateStatesIndex();
		bWasStateDropPerformed = false;
	}
}

// Widget for a list of runtime parameters --------------------------------------------------------------------------


void SCustomizableObjectRuntimeParameterList::Construct(const FArguments& InArgs)
{
	Node = InArgs._Node;
	StateIndex = InArgs._StateIndex;

	if (Node)
	{
		UCustomizableObject* CustomizableObject = Cast<UCustomizableObject>(Node->GetCustomizableObjectGraph()->GetOuter());

		if (CustomizableObject)
		{
			SAssignNew(VerticalSlots, SDragAndDropVerticalBox)
			.OnDragDetected(this,&SCustomizableObjectRuntimeParameterList::OnParamDragDetected)
			.OnCanAcceptDrop(this,&SCustomizableObjectRuntimeParameterList::OnCanAcceptParamDrop)
			.OnAcceptDrop(this,&SCustomizableObjectRuntimeParameterList::OnAcceptParamDrop);

			if (VerticalSlots.IsValid())
			{
				BuildList();

				ChildSlot
				[
					VerticalSlots.ToSharedRef()
				];
			}
		}
	}
}


void SCustomizableObjectRuntimeParameterList::BuildList()
{
	if (VerticalSlots.IsValid())
	{
		VerticalSlots->ClearChildren();

		for (int32 i = 0; i < Node->States[StateIndex].RuntimeParameters.Num(); ++i)
		{
			VerticalSlots->AddSlot()
			.Padding(0.0f, 2.0f, 0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SCustomizableObjectRuntimeParameterList::OnDeleteRuntimeParameter, i)
					.ToolTipText(LOCTEXT("RemoveRuntimeParameter", "Remove Runtime Parameter"))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush(TEXT("Cross")))
						]
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6,0,0,0)
				[
					SNew(SBox)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.WidthOverride(16.0f)
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("VerticalBoxDragIndicatorShort"))
					]
				]
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4,0)
				[
					SNew(SCustomizableObjectRuntimeParameter)
					.Node(Node)
					.StateIndex(StateIndex)
					.RuntimeParameterIndex(i)
				]
			];
		}
	}
}


FReply SCustomizableObjectRuntimeParameterList::OnDeleteRuntimeParameter(int32 ParameterIndex)
{
	Node->States[StateIndex].RuntimeParameters.RemoveAt(ParameterIndex);
	BuildList();

	return FReply::Handled();
}

void SCustomizableObjectRuntimeParameterList::UpdateStateIndex(int32 NewStateIndex)
{
	StateIndex = NewStateIndex;
	BuildList();
}

/** drag and drop of parameters */


FReply SCustomizableObjectRuntimeParameterList::OnParamDragDetected(const FGeometry& Geometry,
                                                                    const FPointerEvent& PointerEvent, int SlotBeingDraggedIndex, SVerticalBox::FSlot* Slot)
{
	if ( Slot)
	{
		// Widget being displayed during the drag and drop
		TSharedPtr<STextBlock> WidgetToDisplay =
			SNew(STextBlock)
			.Text(FText::FromString( Node->States[StateIndex].RuntimeParameters[SlotBeingDraggedIndex]));
		
		return FReply::Handled().BeginDragDrop( FDragAndDropOpWithWidget::New(SlotBeingDraggedIndex,Slot, WidgetToDisplay));
	}

	return FReply::Unhandled();
}

TOptional<SDragAndDropVerticalBox::EItemDropZone> SCustomizableObjectRuntimeParameterList::OnCanAcceptParamDrop(
	const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone ItemDropZone, SVerticalBox::FSlot* Slot)
{
	const TSharedPtr<FDragAndDropOpWithWidget> Operation = DragDropEvent.GetOperationAs<FDragAndDropOpWithWidget>();
	if (Operation.IsValid())		
	{
		// only move parameters over same state
		if (Operation->SlotBeingDragged->GetOwnerWidget()->GetId() == VerticalSlots->GetId())
		{
			// And if the value to be dragged over is different than the origin one
			if (Operation->SlotBeingDragged != Slot)
			{
				return ItemDropZone;
			}
		}
	}

	return TOptional<SDragAndDropVerticalBox::EItemDropZone>();
}

FReply SCustomizableObjectRuntimeParameterList::OnAcceptParamDrop(const FDragDropEvent& DragDropEvent,
	SDragAndDropVerticalBox::EItemDropZone ItemDropZone, int NewIndex, SVerticalBox::FSlot* Slot)
{
	// Move the data around to later rebuild the parameter UI objects with the re-structured data
	TSharedPtr<FDragAndDropOpWithWidget> Operation = DragDropEvent.GetOperationAs<FDragAndDropOpWithWidget>();
	if (Operation.IsValid())
	{
		const FString DraggedParamTemp = Node->States[StateIndex].RuntimeParameters[Operation->SlotIndexBeingDragged];
		Node->States[StateIndex].RuntimeParameters.RemoveAt(Operation->SlotIndexBeingDragged);
		
		// Array structure changed, update new index accordingly
		if (ItemDropZone == SDragAndDropVerticalBox::EItemDropZone::BelowItem &&
			Operation->SlotIndexBeingDragged - NewIndex > 0)
		{
			NewIndex -= 1;
		}
	
		Node->States[StateIndex].RuntimeParameters.Insert(DraggedParamTemp,NewIndex);
	}
	
	// DO not perform the actual slate drop (only update the data and build the list again)
	BuildList();
	return FReply::Unhandled();
}

/** end of drag and drop */


// Widget for each Runtime parameter -------------------------------------------------------------------------------


void SCustomizableObjectRuntimeParameter::Construct(const FArguments& InArgs)
{
	Node = InArgs._Node;
	StateIndex = InArgs._StateIndex;
	RuntimeParameterIndex = InArgs._RuntimeParameterIndex;

	if (Node)
	{
		UCustomizableObject* CustomizableObject = Cast<UCustomizableObject>(Node->GetCustomizableObjectGraph()->GetOuter());

		if (CustomizableObject)
		{
			ListViewOptions.Empty();
			int32 ParameterCount = CustomizableObject->GetParameterCount();

			for (int32 i = 0; i < ParameterCount; ++i)
			{
				ListViewOptions.Add(MakeShareable(new FString(CustomizableObject->GetParameterName(i))));
			}

			for (int32 i = 0; i < Node->States[StateIndex].RuntimeParameters.Num(); ++i)
			{
				bool bFound = false;

				for (int32 j = 0; j < ListViewOptions.Num(); ++j)
				{
					if (*ListViewOptions[j].Get() == Node->States[StateIndex].RuntimeParameters[i])
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					ListViewOptions.Add(MakeShareable(new FString(Node->States[StateIndex].RuntimeParameters[i])));
				}
			}


			// Alphabetical Order
			ListViewOptions.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B)
			{
				FString StringA = *A.Get();
				FString StringB = *B.Get();

				int32 length = StringA.Len() > StringB.Len() ? StringB.Len() : StringA.Len();

				for (int32 i = 0; i <length; ++i)
				{
					if (StringA[i] != StringB[i])
					{
						return StringA[i] < StringB[i];
					}
				}

				return true;
			}
			);

			ChildSlot
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.0f, 4.0f, 2.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Parameter %d:"), RuntimeParameterIndex)))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(ComboButton,SComboButton)
					.OnGetMenuContent(this, &SCustomizableObjectRuntimeParameter::GetComboButtonContent)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(this, &SCustomizableObjectRuntimeParameter::GetCurrentItemLabel)
					]
				]
			];
		}
	}
}

TSharedRef<SWidget> SCustomizableObjectRuntimeParameter::GetComboButtonContent()
{
	SearchItem.Reset();

	// Listview Init
	SAssignNew(RowNameComboListView, SListView<TSharedPtr<FString> >)
		.ListItemsSource(&ListViewOptions)
		.OnSelectionChanged(this, &SCustomizableObjectRuntimeParameter::OnComboButtonSelectionChanged)
		.OnGenerateRow(this, &SCustomizableObjectRuntimeParameter::RowNameComboButtonGenerateWidget)
		.SelectionMode(ESelectionMode::Single);

	// SearchBox Init
	SearchBoxWidget = SNew(SSearchBox)
		.OnTextChanged(this, &SCustomizableObjectRuntimeParameter::OnSearchBoxFilterTextChanged)
		.OnTextCommitted(this, &SCustomizableObjectRuntimeParameter::OnSearchBoxFilterTextCommitted);
	
	//Setting the focus to the SearchBox when the Combo button is open
	ComboButton->SetMenuContentWidgetToFocus(SearchBoxWidget);

	// Creating a widget that gives navigation to the SearchBox and the listview
	return  SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SearchBoxWidget.ToSharedRef()
			]
			+SVerticalBox::Slot()
			.MaxHeight(100.0f)
			[
				RowNameComboListView.ToSharedRef()
			];
}


void SCustomizableObjectRuntimeParameter::OnComboButtonSelectionChanged(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::OnNavigation)
	{
		if (SelectedItem.IsValid())
		{
			// Sets the value of the displayed name of the combo button
			Node->States[StateIndex].RuntimeParameters[RuntimeParameterIndex] = *(SelectedItem.Get());

			//Close the combobox when a selection is made
			ComboButton->SetIsOpen(false);
		}
	}
}


TSharedRef<ITableRow> SCustomizableObjectRuntimeParameter::RowNameComboButtonGenerateWidget(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	//This is needed because the filter made in the function OnSearchBocFilterTextChanged Only works for the rendered items
	const EVisibility WidgetVisibility = IsItemVisible(InItem) ? EVisibility::Visible : EVisibility::Collapsed;

	return
		SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Visibility(WidgetVisibility)
		[
			SNew(STextBlock).Text(FText::FromString(*InItem))
		];
}


FText SCustomizableObjectRuntimeParameter::GetRowNameComboButtonContentText() const
{
	TSharedPtr<FString> SelectedRowName = ComboButtonSelection;

	if (SelectedRowName.IsValid())
	{
		return FText::FromString(*SelectedRowName);
	}
	else
	{
		return LOCTEXT("None", "None");
	}
}


FText SCustomizableObjectRuntimeParameter::GetCurrentItemLabel() const
{
	// Due to the drag and drop operation the parameter index may point to an out of range entry. Hold a temp value
	// until the data gets set up after the drop operation
	if (Node->States[StateIndex].RuntimeParameters.IsValidIndex(RuntimeParameterIndex))
	{
		return FText::FromString(Node->States[StateIndex].RuntimeParameters[RuntimeParameterIndex]);
	}
	else
	{
		return LOCTEXT("None", "None");
	}
}


void SCustomizableObjectRuntimeParameter::OnSearchBoxFilterTextChanged(const FText& InText)
{
	SearchItem = InText.ToString();

	//This filter is just applied to the items that are rendered of the ListView
	for (int32 i = 0; i < ListViewOptions.Num(); i++)
	{
		TSharedPtr<ITableRow> Row = RowNameComboListView->WidgetFromItem(ListViewOptions[i]);
		if (Row)
		{
			Row->AsWidget()->SetVisibility(IsItemVisible(ListViewOptions[i]) ? EVisibility::Visible : EVisibility::Collapsed);
		}
	}

	RowNameComboListView->RequestListRefresh();
}


void SCustomizableObjectRuntimeParameter::OnSearchBoxFilterTextCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	if (CommitInfo == ETextCommit::OnEnter)
	{
		bool bExists = false;

		for (int32 i = 0; i < ListViewOptions.Num(); i++)
		{
			TSharedPtr<ITableRow> Row = RowNameComboListView->WidgetFromItem(ListViewOptions[i]);
			if (Row && ListViewOptions[i].Get()->Equals(InText.ToString(), ESearchCase::IgnoreCase))
			{
				bExists = true;
				Node->States[StateIndex].RuntimeParameters[RuntimeParameterIndex] = *ListViewOptions[i].Get();
				break;
			}
		}

		if (!bExists)
		{			
			UCustomizableObject* CustomizableObject = Cast<UCustomizableObject>(Node->GetCustomizableObjectGraph()->GetOuter());

			if (CustomizableObject)
			{
				Node->States[StateIndex].RuntimeParameters[RuntimeParameterIndex] = InText.ToString();
				ListViewOptions.Add(MakeShareable(new FString(InText.ToString())));
			}
		}

		ComboButton->SetIsOpen(false);
	}
}


bool SCustomizableObjectRuntimeParameter::IsItemVisible(TSharedPtr<FString> Item)
{
	bool bVisible = false;

	if (SearchItem == "" || Item.Get()->Contains(SearchItem, ESearchCase::IgnoreCase))
	{
		bVisible = true;
	}
	
	return bVisible;
}

#undef LOCTEXT_NAMESPACE
