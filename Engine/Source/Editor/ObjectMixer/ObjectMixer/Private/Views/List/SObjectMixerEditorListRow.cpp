// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/SObjectMixerEditorListRow.h"

#include "EditorClassUtils.h"
#include "ObjectMixerEditorStyle.h"
#include "Views/List/ObjectMixerEditorList.h"
#include "Views/List/SObjectMixerEditorList.h"

#include "Customizations/ColorStructCustomization.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Input/DragAndDrop.h"
#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditor"

// const FText InsertFormatText = LOCTEXT("InsertAboveFormatText", "Insert {0} {1} {2}");
// const FText AboveText = LOCTEXT("AboveListItem", "above");
// const FText BelowText = LOCTEXT("BelowListItem", "below");
// const FText MultiDragFormatText = LOCTEXT("MultiDragFormatText", "{0} Items");

// class FObjectMixerListRowDragDropOp : public FDecoratedDragDropOp
// {
// public:
// 	DRAG_DROP_OPERATOR_TYPE(FObjectMixerListRowDragDropOp, FDecoratedDragDropOp)
//
// 	/** The item being dragged and dropped */
// 	TArray<FObjectMixerEditorListRowPtr> DraggedItems;
//
// 	/** Constructs a new drag/drop operation */
// 	static TSharedRef<FObjectMixerListRowDragDropOp> New(const TArray<FObjectMixerEditorListRowPtr>& InItems)
// 	{
// 		check(InItems.Num() > 0);
//
// 		TSharedRef<FObjectMixerListRowDragDropOp> Operation = MakeShareable(
// 			new FObjectMixerListRowDragDropOp());
//
// 		Operation->DraggedItems = InItems;
//
// 		Operation->DefaultHoverIcon = FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.Error");
//
// 		// Set the display text and the transaction name based on whether we're dragging a single or multiple widgets
// 		if (InItems.Num() == 1)
// 		{
// 			//Operation->DefaultHoverText = FText::FromString(InItems[0]->GetCommandInfo().Pin()->Command);
// 		}
// 		else
// 		{
// 			Operation->DefaultHoverText =
// 				FText::Format(
// 					SObjectMixerEditorListRow::MultiDragFormatText,
// 					FText::AsNumber(Operation->DraggedItems.Num())
// 				);
// 		}
//
// 		Operation->Construct();
//
// 		return Operation;
// 	}
// };

void SObjectMixerEditorListRow::Construct(
	const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable,
	const TWeakPtr<FObjectMixerEditorListRow> InRow)
{
	check(InRow.IsValid());

	Item = InRow;
	const FObjectMixerEditorListRowPtr PinnedItem = Item.Pin();

	SMultiColumnTableRow<FObjectMixerEditorListRowPtr>::Construct(
		FSuperRowType::FArguments()
		.Padding(1.0f)
	// 	.OnCanAcceptDrop(this, &SObjectMixerEditorListRow::HandleCanAcceptDrop)
	// 	.OnAcceptDrop(this, &SObjectMixerEditorListRow::HandleAcceptDrop)
	// 	.OnDragDetected(this, &SObjectMixerEditorListRow::HandleDragDetected)
	// 	.OnDragLeave(this, &SObjectMixerEditorListRow::HandleDragLeave)
	 	, InOwnerTable
	);

	const FName VisibleHoveredBrushName = TEXT("Level.VisibleHighlightIcon16x");
	const FName VisibleNotHoveredBrushName = TEXT("Level.VisibleIcon16x");
	const FName NotVisibleHoveredBrushName = TEXT("Level.NotVisibleHighlightIcon16x");
	const FName NotVisibleNotHoveredBrushName = TEXT("Level.NotVisibleIcon16x");

	VisibleHoveredBrush = FAppStyle::Get().GetBrush(VisibleHoveredBrushName);
	VisibleNotHoveredBrush = FAppStyle::Get().GetBrush(VisibleNotHoveredBrushName);
	NotVisibleHoveredBrush = FAppStyle::Get().GetBrush(NotVisibleHoveredBrushName);
	NotVisibleNotHoveredBrush = FAppStyle::Get().GetBrush(NotVisibleNotHoveredBrushName);
}

TSharedRef<SWidget> SObjectMixerEditorListRow::GenerateWidgetForColumn(const FName& InColumnName)
{
	const FObjectMixerEditorListRowPtr PinnedItem = Item.Pin();

	if (const TSharedPtr<SWidget> CellWidget = GenerateCells(InColumnName, PinnedItem))
	{
		if (InColumnName == SObjectMixerEditorList::ItemNameColumnName)
		{
			// The first column gets the tree expansion arrow for this row
			return SNew(SBox)
				.MinDesiredHeight(20)
				[
					SNew( SHorizontalBox )

					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(6, 0, 0, 0)
					[
						SNew( SExpanderArrow, SharedThis(this) ).IndentAmount(12)
					]

					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						CellWidget.ToSharedRef()
					]
				];
		}
		
		return SNew(SBorder)
				   .HAlign(HAlign_Fill)
				   .VAlign(VAlign_Center)
				   .BorderImage(GetBorderImage(PinnedItem->GetRowType()))
			   [
				   CellWidget.ToSharedRef()
			   ];
	}

	return SNullWidget::NullWidget;
}

void SObjectMixerEditorListRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bIsHovered = true;
	
	SMultiColumnTableRow<FObjectMixerEditorListRowPtr>::OnMouseEnter(MyGeometry, MouseEvent);
}

void SObjectMixerEditorListRow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	bIsHovered = false;

	SMultiColumnTableRow<FObjectMixerEditorListRowPtr>::OnMouseLeave(MouseEvent);
}

SObjectMixerEditorListRow::~SObjectMixerEditorListRow()
{
	Item.Reset();
}

// FReply SObjectMixerEditorListRow::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
// {
// 	TArray<FObjectMixerEditorListRowPtr> DraggedItems = Item.Pin()->GetSelectedTreeViewItems();
// 	TSharedRef<FObjectMixerListRowDragDropOp> Operation =
// 		FObjectMixerListRowDragDropOp::New(DraggedItems);
//
// 	return FReply::Handled().BeginDragDrop(Operation);
// }
//
// void SObjectMixerEditorListRow::HandleDragLeave(const FDragDropEvent& DragDropEvent)
// {
// 	if (TSharedPtr<FObjectMixerListRowDragDropOp> Operation =
// 		DragDropEvent.GetOperationAs<FObjectMixerListRowDragDropOp>())
// 	{
// 		Operation->ResetToDefaultToolTip();
// 	}
// }

// TOptional<EItemDropZone> SObjectMixerEditorListRow::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent,
//                                                                              EItemDropZone DropZone,
//                                                                              FObjectMixerEditorListRowPtr
//                                                                              TargetItem)
// {
// 	TSharedPtr<FObjectMixerListRowDragDropOp> Operation =
// 		DragDropEvent.GetOperationAs<FObjectMixerListRowDragDropOp>();
//
// 	if (!Operation.IsValid())
// 	{
// 		return TOptional<EItemDropZone>();
// 	}
//
// 	Operation->SetToolTip(
// 		LOCTEXT("SortByCustomOrderDrgDropWarning", "Sort by custom order (\"#\") to drag & drop"),
// 		FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.Error")
// 	);
//
// 	const bool bIsDropDenied = false;
//
// 	FString BoolAsString = bIsDropDenied ? "true" : "false";
//
// 	if (bIsDropDenied)
// 	{
// 		Operation->ResetToDefaultToolTip();
//
// 		return TOptional<EItemDropZone>();
// 	}
//
// 	FText ItemNameText = FText::FromString("Light Label");
//
// 	if (Operation->DraggedItems.Num() > 1)
// 	{
// 		ItemNameText = FText::Format(MultiDragFormatText, FText::AsNumber(Operation->DraggedItems.Num()));
// 	}
//
// 	const FText DropPermittedText =
// 		FText::Format(InsertFormatText,
// 		              ItemNameText,
// 		              DropZone == EItemDropZone::BelowItem ? BelowText : AboveText,
// 		              ItemNameText
// 		);
//
// 	Operation->SetToolTip(
// 		DropPermittedText,
// 		FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.OK")
// 	);
//
// 	// We have no behaviour yet for dropping one item onto another, so we'll treat it like we dropped it above
// 	return DropZone == EItemDropZone::OntoItem ? EItemDropZone::AboveItem : DropZone;
// }

// FReply SObjectMixerEditorListRow::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone,
//                                                         FObjectMixerEditorListRowPtr TargetItem)
// {
// 	TSharedPtr<FObjectMixerListRowDragDropOp> Operation =
// 		DragDropEvent.GetOperationAs<FObjectMixerListRowDragDropOp>();
//
// 	if (!Operation.IsValid())
// 	{
// 		return FReply::Unhandled();
// 	}
//
// 	const TSharedPtr<SObjectMixerEditorList> ListView = Item.Pin()->GetListViewPtr().Pin();
//
// 	TArray<FObjectMixerEditorListRowPtr> DraggedItems = Operation->DraggedItems;
//
// 	TArray<FObjectMixerEditorListRowPtr> AllTreeItemsCopy = ListView->GetTreeViewItems();
//
// 	for (const FObjectMixerEditorListRowPtr& DraggedItem : DraggedItems)
// 	{
// 		if (!DraggedItem.IsValid() || !AllTreeItemsCopy.Contains(DraggedItem))
// 		{
// 			continue;
// 		}
//
// 		AllTreeItemsCopy.Remove(DraggedItem);
// 	}
//
// 	const int32 TargetIndex = AllTreeItemsCopy.IndexOfByKey(TargetItem);
//
// 	if (TargetIndex > -1)
// 	{
// 		for (int32 ItemIndex = DraggedItems.Num() - 1; ItemIndex >= 0; ItemIndex--)
// 		{
// 			const FObjectMixerEditorListRowPtr& DraggedItem = DraggedItems[ItemIndex];
//
// 			if (!DraggedItem.IsValid() || AllTreeItemsCopy.Contains(DraggedItem))
// 			{
// 				continue;
// 			}
//
// 			AllTreeItemsCopy.Insert(DraggedItem, DropZone == EItemDropZone::AboveItem ? TargetIndex : TargetIndex + 1);
// 		}
//
// 		ListView->SetTreeViewItems(AllTreeItemsCopy);
// 	}
//
// 	return FReply::Handled();
// }

bool SObjectMixerEditorListRow::IsVisible() const
{
	if (const TSharedPtr<FObjectMixerEditorListRow> PinnedItem = Item.Pin())
	{
		return PinnedItem->GetObjectVisibility();
	}

	return false;
}

FSlateColor SObjectMixerEditorListRow::GetVisibilityIconForegroundColor() const
{
	check(Item.IsValid());
	
	const bool bIsSelected = Item.Pin()->GetIsSelected();

	// make the foreground brush transparent if it is not selected and it is visible
	if (IsVisible() && !bIsHovered && !bIsSelected)
	{
		return FLinearColor::Transparent;
	}
	else if (bIsHovered && !bIsSelected)
	{
		return FAppStyle::Get().GetSlateColor("Colors.ForegroundHover");
	}

	return FSlateColor::UseForeground();
}

FSlateColor SObjectMixerEditorListRow::GetSoloIconForegroundColor() const
{
	check(Item.IsValid());

	if (Item.Pin()->IsThisRowSolo())
	{
		return bIsHovered ? FAppStyle::Get().GetSlateColor("Colors.ForegroundHover") : FSlateColor::UseForeground();
	}

	return FLinearColor::Transparent;
}

const FSlateBrush* SObjectMixerEditorListRow::GetVisibilityBrush() const
{
	if (IsVisible())
	{
		return bIsHovered ? VisibleHoveredBrush : VisibleNotHoveredBrush;
	}
	
	return bIsHovered ? NotVisibleHoveredBrush : NotVisibleNotHoveredBrush;
}

const FSlateBrush* SObjectMixerEditorListRow::GetBorderImage(
	const FObjectMixerEditorListRow::EObjectMixerEditorListRowType InRowType)
{
	return FObjectMixerEditorStyle::Get().GetBrush("ObjectMixerEditor.DefaultBorder");
}

TSharedPtr<SWidget> SObjectMixerEditorListRow::GenerateCells(
	const FName& InColumnName, const TSharedPtr<FObjectMixerEditorListRow> PinnedItem)
{
	check(PinnedItem.IsValid());
	
	if (InColumnName.IsEqual(SObjectMixerEditorList::ItemNameColumnName))
	{
		TSharedRef<SHorizontalBox> HBox = SNew(SHorizontalBox);
		
		HBox->AddSlot()
		.AutoWidth()
		[
			SNew(SImage)
			.Image_Lambda([PinnedItem]()
			{
				return PinnedItem->GetObjectIconBrush();
			})
			.ColorAndOpacity(FSlateColor::UseForeground())
		];

		bool bNeedsStandardTextBlock = true;
		const FText DisplayName = PinnedItem->GetDisplayName();
		if (TObjectPtr<UObject> Object = PinnedItem->GetObject())
		{
			if (UClass* ActorClass = Object->GetClass())
			{
				if (UBlueprint* AsBlueprint = UBlueprint::GetBlueprintFromClass(ActorClass))
				{
					bNeedsStandardTextBlock = false;
					
					FEditorClassUtils::FSourceLinkParams SourceLinkParams;
					SourceLinkParams.Object = Object;
					SourceLinkParams.bUseDefaultFormat = false;
					SourceLinkParams.bUseFormatIfNoLink = true;
					SourceLinkParams.BlueprintFormat = &DisplayName;

					HBox->AddSlot()
					.Padding(FMargin(10.0, 0, 0, 0))
					[
						FEditorClassUtils::GetSourceLink(ActorClass, SourceLinkParams)
					];
				}
			}
		}

		if (bNeedsStandardTextBlock)
		{
			HBox->AddSlot()
			.Padding(FMargin(10.0, 0, 0, 0))
			[
				SNew(STextBlock)
				.Visibility(EVisibility::Visible)
				.Justification(ETextJustify::Left)
				.Text(DisplayName)
				.ToolTipText(DisplayName)
			];
		}
		
		return SNew(SBox)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(FMargin(TextBlockLeftPadding, 0.f, 0.f, 0.f))
				[
					HBox
				];
	}

	if (InColumnName.IsEqual(SObjectMixerEditorList::EditorVisibilityColumnName))
	{
		if (PinnedItem->GetRowType() == FObjectMixerEditorListRow::None)
		{
			return nullptr;
		}

		if (PinnedItem->GetObject() && !PinnedItem->GetObject()->IsA(AActor::StaticClass()))
		{
			return nullptr;
		}
		
		return SNew(SBox)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.Padding(0.f)
				[
					SNew(SImage)
					.ColorAndOpacity(this, &SObjectMixerEditorListRow::GetVisibilityIconForegroundColor)
					.Image_Raw(this, &SObjectMixerEditorListRow::GetVisibilityBrush)
					.OnMouseButtonDown_Lambda(
						[PinnedItem] (const FGeometry& MyGeometry, const FPointerEvent& Event)
						{
							check (PinnedItem);

							FScopedTransaction Transaction( LOCTEXT("VisibilityChanged", "Object Mixer - Visibility Changed") );

							const bool bIsVisible = PinnedItem->GetObjectVisibility();
							
							if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = PinnedItem->GetListViewPtr().Pin();
								PinnedItem->GetIsSelected() && PinnedListView->GetSelectedTreeViewItemCount() > 0)
							{
								PinnedListView->SetSelectedTreeViewItemActorsEditorVisible(!bIsVisible);

								return FReply::Handled();
							}

							// Set Visibility Recursively
							PinnedItem->SetObjectVisibility(!bIsVisible, true);

							return FReply::Handled();
						}
					)
				]
			;
	}

	if (InColumnName.IsEqual(SObjectMixerEditorList::EditorVisibilitySoloColumnName))
	{
		if (PinnedItem->GetRowType() == FObjectMixerEditorListRow::None)
		{
			return nullptr;
		}

		if (PinnedItem->GetObject() && !PinnedItem->GetObject()->IsA(AActor::StaticClass()))
		{
			return nullptr;
		}
		
		return SNew(SBox)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.Padding(0.f)
				[
					SNew(SImage)
					.ColorAndOpacity(this, &SObjectMixerEditorListRow::GetSoloIconForegroundColor)
					.Image(FAppStyle::Get().GetBrush("MediaAsset.AssetActions.Solo.Small"))
					.OnMouseButtonDown_Lambda(
						[PinnedItem] (const FGeometry& MyGeometry, const FPointerEvent& Event)
						{
							check (PinnedItem);
							
							if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = PinnedItem->GetListViewPtr().Pin();
								PinnedListView->GetTreeViewItemCount() > 0)
							{
								const bool bIsRowSolo = PinnedItem->IsThisRowSolo();
								for (const TSharedPtr<FObjectMixerEditorListRow>& TreeItem : PinnedListView->GetTreeViewItems())
								{
									TreeItem->SetObjectVisibility(bIsRowSolo, true);
								}

								if (bIsRowSolo)
								{
									PinnedItem->ClearSoloRow();
								}
								else
								{
									PinnedItem->SetObjectVisibility(true, true);
									PinnedItem->SetThisAsSoloRow();
								}

								return FReply::Handled();
							}

							return FReply::Unhandled();
						}
					)
				]
			;
	}
	
	if (UObject* ObjectRef = PinnedItem->GetObject())
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		{
			FSinglePropertyParams Params;
			Params.NamePlacement = EPropertyNamePlacement::Hidden;
			
			const TSharedPtr<ISinglePropertyView> SinglePropertyView =
				PropertyEditorModule.CreateSingleProperty(ObjectRef, InColumnName, Params
			);

			if (SinglePropertyView)
			{
				if (const TSharedPtr<IPropertyHandle> Handle = SinglePropertyView->GetPropertyHandle())
				{
					if (const FProperty* Property = Handle->GetProperty())
					{
						// Simultaneously edit all selected rows with a similar property
						FSimpleDelegate OnPropertyValueChanged =
							FSimpleDelegate::CreateRaw(
								this,
								&SObjectMixerEditorListRow::OnPropertyChanged, Property, (void*)ObjectRef);
					
						SinglePropertyView->SetOnPropertyValueChanged(OnPropertyValueChanged);

						return SNew(SBox)
								.Visibility(EVisibility::SelfHitTestInvisible)
								.HAlign(HAlign_Fill)
								.VAlign(VAlign_Center)
								[
									SinglePropertyView.ToSharedRef()
								];
					}
				}
			}
		}
	}

	return nullptr;
}

void SObjectMixerEditorListRow::OnPropertyChanged(const FProperty* Property, void* ContainerWithChangedProperty)
{
	if (Property && ContainerWithChangedProperty)
	{
		if (const TSharedPtr<FObjectMixerEditorListRow> PinnedItem = Item.Pin())
		{
			if (PinnedItem->GetIsSelected())
			{
				if (const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerWithChangedProperty))
				{
					FScopedTransaction Transaction( LOCTEXT("PropertyChanged", "Object Mixer - Property Changed") );
					
					for (const TSharedPtr<FObjectMixerEditorListRow>& SelectedRow : PinnedItem->GetSelectedTreeViewItems())
					{
						if (UObject* SelectedRowObject = SelectedRow->GetObject())
						{
							Property->SetValue_InContainer(SelectedRowObject, ValuePtr);
						}
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
