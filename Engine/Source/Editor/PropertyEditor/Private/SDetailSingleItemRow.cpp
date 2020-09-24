// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDetailSingleItemRow.h"
#include "ObjectPropertyNode.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Settings/EditorExperimentalSettings.h"
#include "DetailWidgetRow.h"
#include "IDetailKeyframeHandler.h"
#include "IDetailPropertyExtensionHandler.h"
#include "DetailPropertyRow.h"
#include "DetailGroup.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Editor.h"
#include "PropertyHandleImpl.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "SResetToDefaultPropertyEditor.h"

void SConstrainedBox::Construct(const FArguments& InArgs)
{
	MinWidth = InArgs._MinWidth;
	MaxWidth = InArgs._MaxWidth;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

FVector2D SConstrainedBox::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const float MinWidthVal = MinWidth.Get().Get(0.0f);
	const float MaxWidthVal = MaxWidth.Get().Get(0.0f);

	if (MinWidthVal == 0.0f && MaxWidthVal == 0.0f)
	{
		return SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
	}
	else
	{
		FVector2D ChildSize = ChildSlot.GetWidget()->GetDesiredSize();

		float XVal = FMath::Max(MinWidthVal, ChildSize.X);
		if (MaxWidthVal >= MinWidthVal)
		{
			XVal = FMath::Min(MaxWidthVal, XVal);
		}

		return FVector2D(XVal, ChildSize.Y);
	}
}

namespace DetailWidgetConstants
{
	const FMargin LeftRowPadding( 0.0f, 2.5f, 12.0f, 2.5f );
	const FMargin RightRowPadding( 12.0f, 2.5f, 2.0f, 2.5f );
}

namespace SDetailSingleItemRow_Helper
{
	//Get the node item number in case it is expand we have to recursively count all expanded children
	void RecursivelyGetItemShow(TSharedRef<FDetailTreeNode> ParentItem, int32 &ItemShowNum)
	{
		if (ParentItem->GetVisibility() == ENodeVisibility::Visible)
		{
			ItemShowNum++;
		}

		if (ParentItem->ShouldBeExpanded())
		{
			TArray< TSharedRef<FDetailTreeNode> > Childrens;
			ParentItem->GetChildren(Childrens);
			for (TSharedRef<FDetailTreeNode> ItemChild : Childrens)
			{
				RecursivelyGetItemShow(ItemChild, ItemShowNum);
			}
		}
	}
}

void SDetailSingleItemRow::OnFavoriteMenuToggle()
{
	if (Customization->GetPropertyNode().IsValid() && Customization->GetPropertyNode()->CanDisplayFavorite())
	{
		bool bToggle = !Customization->GetPropertyNode()->IsFavorite();
		Customization->GetPropertyNode()->SetFavorite(bToggle);
		if (OwnerTreeNode.IsValid())
		{
			//////////////////////////////////////////////////////////////////////////
			// Calculate properly the scrolling offset (by item) to make sure the mouse stay over the same property

			//Get the node item number in case it is expand we have to recursively count all childrens
			int32 ExpandSize = 0;
			if (OwnerTreeNode.Pin()->ShouldBeExpanded())
			{
				SDetailSingleItemRow_Helper::RecursivelyGetItemShow(OwnerTreeNode.Pin().ToSharedRef(), ExpandSize);
			}
			else
			{
				//if the item is not expand count is 1
				ExpandSize = 1;
			}
			
			//Get the number of favorite child (simple and advance) to know if the favorite category will be create or remove
			int32 SimplePropertiesNum = 0;
			int32 AdvancePropertiesNum = 0;

			FDetailLayoutBuilderImpl& DetailLayout = OwnerTreeNode.Pin()->GetParentCategory()->GetParentLayoutImpl();

			const FName FavoritesCategoryName = TEXT("Favorites");
			bool HasCategoryFavorite = DetailLayout.HasCategory(FavoritesCategoryName);
			if(HasCategoryFavorite)
			{
				DetailLayout.DefaultCategory(FavoritesCategoryName).GetCategoryInformation(SimplePropertiesNum, AdvancePropertiesNum);
			}

			// Check if the property we toggle is an advance property
			bool IsAdvanceProperty = Customization->GetPropertyNode()->HasNodeFlags(EPropertyNodeFlags::IsAdvanced) == 0 ? false : true;

			// Compute the scrolling offset by item
			int32 ScrollingOffsetAdd = ExpandSize;
			int32 ScrollingOffsetRemove = -ExpandSize;
			if (HasCategoryFavorite)
			{
				// Adding the advance button in a category add 1 item
				ScrollingOffsetAdd += (IsAdvanceProperty && AdvancePropertiesNum == 0) ? 1 : 0;

				if (IsAdvanceProperty && AdvancePropertiesNum == 1)
				{
					//Removing the advance button count as 1 item
					ScrollingOffsetRemove -= 1;
				}
				if (AdvancePropertiesNum + SimplePropertiesNum == 1)
				{
					//Removing a full category count as 2 items
					ScrollingOffsetRemove -= 2;
				}
			}
			else
			{
				// Adding new category (2 items) adding advance button (1 item)
				ScrollingOffsetAdd += IsAdvanceProperty ? 3 : 2;
				
				// We should never remove an item from favorite if there is no favorite category
				// Set the remove offset to 0
				ScrollingOffsetRemove = 0;
			}

			// Apply the calculated offset
			OwnerTreeNode.Pin()->GetDetailsView()->MoveScrollOffset(bToggle ? ScrollingOffsetAdd : ScrollingOffsetRemove);

			// Refresh the tree
			OwnerTreeNode.Pin()->GetDetailsView()->ForceRefresh();
		}
	}
}

void SDetailSingleItemRow::OnArrayDragEnter(const FDragDropEvent& DragDropEvent)
{
	bIsHoveredDragTarget = true;
}

void SDetailSingleItemRow::OnArrayDragLeave(const FDragDropEvent& DragDropEvent)
{
	bIsHoveredDragTarget = false;
}

bool SDetailSingleItemRow::CheckValidDrop(const TSharedPtr<SDetailSingleItemRow> RowPtr) const
{
	TSharedPtr<FPropertyNode> SwappingPropertyNode = RowPtr->SwappablePropertyNode;
	if (SwappingPropertyNode.IsValid() && SwappablePropertyNode.IsValid())
	{
		if (SwappingPropertyNode != SwappablePropertyNode)
		{
			int32 OriginalIndex = SwappingPropertyNode->GetArrayIndex();
			int32 NewIndex = SwappablePropertyNode->GetArrayIndex();
			TSharedPtr<IPropertyHandle> SwappingHandle = PropertyEditorHelpers::GetPropertyHandle(SwappingPropertyNode.ToSharedRef(), OwnerTreeNode.Pin()->GetDetailsView()->GetNotifyHook(), OwnerTreeNode.Pin()->GetDetailsView()->GetPropertyUtilities());
			TSharedPtr<IPropertyHandleArray> ParentHandle = SwappingHandle->GetParentHandle()->AsArray();

			if (ParentHandle.IsValid() && SwappablePropertyNode->GetParentNode() == SwappingPropertyNode->GetParentNode())
			{
				return true;
			}
		}
	}
	return false;
}

FReply SDetailSingleItemRow::OnArrayDrop(const FDragDropEvent& DragDropEvent)
{
	bIsHoveredDragTarget = false;
	TSharedPtr<FArrayRowDragDropOp> ArrayDropOp = DragDropEvent.GetOperationAs< FArrayRowDragDropOp >();
	TSharedPtr<SDetailSingleItemRow> RowPtr = nullptr;
	if (ArrayDropOp.IsValid() && ArrayDropOp->Row.IsValid())
	{
		RowPtr = ArrayDropOp->Row.Pin();
	}
	if (!RowPtr.IsValid())
	{
		return FReply::Unhandled();
	}

	if (CheckValidDrop(RowPtr))
	{
		TSharedPtr<FPropertyNode> SwappingPropertyNode = RowPtr->SwappablePropertyNode;
		TSharedPtr<IPropertyHandle> SwappingHandle = PropertyEditorHelpers::GetPropertyHandle(SwappingPropertyNode.ToSharedRef(), OwnerTreeNode.Pin()->GetDetailsView()->GetNotifyHook(), OwnerTreeNode.Pin()->GetDetailsView()->GetPropertyUtilities());
		TSharedPtr<IPropertyHandleArray> ParentHandle = SwappingHandle->GetParentHandle()->AsArray();
		int32 OriginalIndex = SwappingPropertyNode->GetArrayIndex();
		int32 NewIndex = SwappablePropertyNode->GetArrayIndex();

		// Need to swap the moving and target expansion states before saving
		bool bOriginalSwappableExpansion = SwappablePropertyNode->HasNodeFlags(EPropertyNodeFlags::Expanded) != 0;
		bool bOriginalSwappingExpansion = SwappingPropertyNode->HasNodeFlags(EPropertyNodeFlags::Expanded) != 0;
		SwappablePropertyNode->SetNodeFlags(EPropertyNodeFlags::Expanded, bOriginalSwappingExpansion);
		SwappingPropertyNode->SetNodeFlags(EPropertyNodeFlags::Expanded, bOriginalSwappableExpansion);

		IDetailsViewPrivate* DetailsView = OwnerTreeNode.Pin()->GetDetailsView();
		DetailsView->SaveExpandedItems(SwappablePropertyNode->GetParentNodeSharedPtr().ToSharedRef());
		FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "MoveRow", "Move Row"));

		SwappingHandle->GetParentHandle()->NotifyPreChange();

		ParentHandle->MoveElementTo(OriginalIndex, NewIndex);

		FPropertyChangedEvent MoveEvent(SwappingHandle->GetParentHandle()->GetProperty(), EPropertyChangeType::Unspecified);
		SwappingHandle->GetParentHandle()->NotifyPostChange(EPropertyChangeType::Unspecified);
		if (DetailsView->GetPropertyUtilities().IsValid())
		{
			DetailsView->GetPropertyUtilities()->NotifyFinishedChangingProperties(MoveEvent);
		}
	}
	return FReply::Handled();
}

TOptional<EItemDropZone> SDetailSingleItemRow::OnArrayCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr< FDetailTreeNode > Type)
{
	bIsHoveredDragTarget = false;
	TSharedPtr<FArrayRowDragDropOp> ArrayDropOp = DragDropEvent.GetOperationAs< FArrayRowDragDropOp >();
	TSharedPtr<SDetailSingleItemRow> RowPtr = nullptr;
	if (ArrayDropOp.IsValid() && ArrayDropOp->Row.IsValid())
	{
		RowPtr = ArrayDropOp->Row.Pin();
	}
	if (!RowPtr.IsValid())
	{
		return TOptional<EItemDropZone>();
	}

	if (CheckValidDrop(RowPtr))
	{
		ArrayDropOp->DecoratorText->SetText(NSLOCTEXT("ArrayDragDrop", "PlaceRowHere", "Place Row Here"));
	}
	else
	{
		ArrayDropOp->DecoratorText->SetText(NSLOCTEXT("ArrayDragDrop", "CannotPlaceRowHere", "Cannot Place Row Here"));
	}
	return TOptional<EItemDropZone>();
}

FReply SDetailSingleItemRow::OnArrayHeaderDrop(const FDragDropEvent& DragDropEvent)
{
	OnArrayDragLeave(DragDropEvent);
	return FReply::Handled();
}

TSharedPtr<FPropertyNode> SDetailSingleItemRow::GetPropertyNode() const
{
	TSharedPtr<FPropertyNode> PropertyNode = Customization->GetPropertyNode();
	if (!PropertyNode.IsValid() && Customization->DetailGroup.IsValid())
	{
		PropertyNode = Customization->DetailGroup->GetHeaderPropertyNode();
	}

	// See if a custom builder has an associated node
	if (!PropertyNode.IsValid() && Customization->HasCustomBuilder())
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = Customization->CustomBuilderRow->GetPropertyHandle();

		if (PropertyHandle.IsValid())
		{
			PropertyNode = StaticCastSharedPtr<FPropertyHandleBase>(PropertyHandle)->GetPropertyNode();
		}
	}

	return PropertyNode;
}

TSharedPtr<IPropertyHandle> SDetailSingleItemRow::GetPropertyHandle() const
{
	TSharedPtr<IPropertyHandle> Handle;
	TSharedPtr<FPropertyNode> PropertyNode = GetPropertyNode();
	if (PropertyNode.IsValid())
	{
		Handle = PropertyEditorHelpers::GetPropertyHandle(PropertyNode.ToSharedRef(), OwnerTreeNode.Pin()->GetDetailsView()->GetNotifyHook(), OwnerTreeNode.Pin()->GetDetailsView()->GetPropertyUtilities());
	}
	else if (Customization->GetWidgetRow().PropertyHandles.Num() > 0)
	{
		// @todo: Handle more than 1 property handle?
		Handle = Customization->GetWidgetRow().PropertyHandles[0];
	}

	return Handle;
}

void SDetailSingleItemRow::Construct( const FArguments& InArgs, FDetailLayoutCustomization* InCustomization, bool bHasMultipleColumns, TSharedRef<FDetailTreeNode> InOwnerTreeNode, const TSharedRef<STableViewBase>& InOwnerTableView )
{
	OwnerTreeNode = InOwnerTreeNode;
	bAllowFavoriteSystem = InArgs._AllowFavoriteSystem;
	Customization = InCustomization;

	TSharedRef<SWidget> Widget = SNullWidget::NullWidget;

	FOnTableRowDragEnter ArrayDragDelegate;
	FOnTableRowDragLeave ArrayDragLeaveDelegate;
	FOnTableRowDrop ArrayDropDelegate;
	FOnCanAcceptDrop ArrayAcceptDropDelegate;

	FDetailColumnSizeData& ColumnSizeData = InOwnerTreeNode->GetDetailsView()->GetColumnSizeData();

	const bool bIsValidTreeNode = InOwnerTreeNode->GetParentCategory().IsValid() && InOwnerTreeNode->GetParentCategory()->IsParentLayoutValid();
	if (bIsValidTreeNode)
	{
		if (Customization->IsValidCustomization())
		{
			TAttribute<bool> IsPropertyEditingEnabled = InOwnerTreeNode->IsPropertyEditingEnabled();

			FDetailWidgetRow Row = InCustomization->GetWidgetRow();

			TSharedPtr<SWidget> NameWidget, ValueWidget, ExtensionWidget;

			NameWidget = Row.NameWidget.Widget;

			ValueWidget =
				SNew(SConstrainedBox)
				.MinWidth(Row.ValueWidget.MinWidth)
				.MaxWidth(Row.ValueWidget.MaxWidth)
				.IsEnabled(IsPropertyEditingEnabled)
				[
					Row.ValueWidget.Widget
				];

			ExtensionWidget = CreateExtensionWidget(ValueWidget.ToSharedRef(), *Customization, InOwnerTreeNode);

			TAttribute<bool> NameWidgetEnabled;
			if (Row.IsEnabledAttr.IsBound())
			{
				NameWidgetEnabled = Row.IsEnabledAttr;
				NameWidget->SetEnabled(Row.IsEnabledAttr);
				ValueWidget->SetEnabled(Row.IsEnabledAttr);
				ExtensionWidget->SetEnabled(Row.IsEnabledAttr);
			}

			TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);

			// create outer splitter
			TSharedRef<SSplitter> OuterSplitter =
				SNew(SSplitter)
				.Style(FEditorStyle::Get(), "DetailsView.Splitter.Outer")
				.PhysicalSplitterHandleSize(1.0f)
				.HitDetectionSplitterHandleSize(5.0f);

			Widget = OuterSplitter;

			// create Left column:
			// | Left  | Name | Value | Right |
			TSharedRef<SHorizontalBox> LeftColumnBox =
				SNew(SHorizontalBox)
				.Clipping(EWidgetClipping::OnDemand);

			// edit condition widget
			LeftColumnBox->AddSlot()
				.Padding(0.0f, 0.0f, 3.0f, 0.0f)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SConstrainedBox)
					.MinWidth(20)
					[
						SNew(SEditConditionWidget)
						.EditConditionValue(Row.EditConditionValue)
						.OnEditConditionValueChanged(Row.OnEditConditionValueChanged)
					]
				];

			OuterSplitter->AddSlot()
				.SizeRule(SSplitter::ESizeRule::SizeToContent)
				[
					LeftColumnBox
				];

			TSharedPtr<SSplitter> InnerSplitter;

			// create inner splitter
			OuterSplitter->AddSlot()
				.Value(ColumnSizeData.PropertyColumnWidth)
				.OnSlotResized(ColumnSizeData.OnPropertyColumnResized)
			[
				SAssignNew(InnerSplitter, SSplitter)
				.Style(FEditorStyle::Get(), "DetailsView.Splitter")
				.PhysicalSplitterHandleSize(1.0f)
				.HitDetectionSplitterHandleSize(5.0f)
				.HighlightedHandleIndex(ColumnSizeData.HoveredSplitterIndex)
				.OnHandleHovered(ColumnSizeData.OnSplitterHandleHovered)
			];

			// create Name column:
			// | Left  | Name | Value | Right |
			TSharedRef<SHorizontalBox> NameColumnBox = SNew(SHorizontalBox)
				.Clipping(EWidgetClipping::OnDemand);
			
			TSharedPtr<FPropertyNode> PropertyNode = Customization->GetPropertyNode();
			if (PropertyNode.IsValid())
			{
				if (PropertyNode->IsReorderable())
				{
					TSharedPtr<SDetailSingleItemRow> InRow = SharedThis(this);
					TSharedRef<SWidget> Handle = PropertyEditorHelpers::MakePropertyReorderHandle(PropertyNode.ToSharedRef(), InRow);
					Handle->SetEnabled(IsPropertyEditingEnabled);

					NameColumnBox->AddSlot()
						.Padding(0.0f, 0.0f, 10.0f, 0.0f)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							Handle
						];

					ArrayDragDelegate = FOnTableRowDragEnter::CreateSP(this, &SDetailSingleItemRow::OnArrayDragEnter);
					ArrayDragLeaveDelegate = FOnTableRowDragLeave::CreateSP(this, &SDetailSingleItemRow::OnArrayDragLeave);
					ArrayDropDelegate = FOnTableRowDrop::CreateSP(this, &SDetailSingleItemRow::OnArrayDrop);
					ArrayAcceptDropDelegate = FOnCanAcceptDrop::CreateSP(this, &SDetailSingleItemRow::OnArrayCanAcceptDrop);
					SwappablePropertyNode = PropertyNode;
				}
				else if (CastField<FArrayProperty>(PropertyNode->GetProperty()) != nullptr // Is an array
					&& CastField<FObjectProperty>(CastField<FArrayProperty>(PropertyNode->GetProperty())->Inner) != nullptr) // Is an object array
				{
					ArrayDragDelegate = FOnTableRowDragEnter::CreateSP(this, &SDetailSingleItemRow::OnArrayDragEnter);
					ArrayDragLeaveDelegate = FOnTableRowDragLeave::CreateSP(this, &SDetailSingleItemRow::OnArrayDragLeave);
					ArrayDropDelegate = FOnTableRowDrop::CreateSP(this, &SDetailSingleItemRow::OnArrayHeaderDrop);
					ArrayAcceptDropDelegate = FOnCanAcceptDrop::CreateSP(this, &SDetailSingleItemRow::OnArrayCanAcceptDrop);
				}
			}

			// expander arrow
			NameColumnBox->AddSlot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(0,0,3,0)
				.AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
					.BaseIndentLevel(1)
				];

			if (bHasMultipleColumns)
			{
				// If the NameWidget has already been disabled, don't re-enable it if IsPropertyEditingEnabled is true.
				NameWidget->SetEnabled(NameWidgetEnabled.IsBound() ?
					TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda(
						[NameWidgetEnabled, IsPropertyEditingEnabled]()
						{
							return NameWidgetEnabled.Get() && IsPropertyEditingEnabled.Get();
						}))
					: IsPropertyEditingEnabled);

				NameColumnBox->AddSlot()
					.HAlign(Row.NameWidget.HorizontalAlignment)
					.VAlign(Row.NameWidget.VerticalAlignment)
					.Padding(DetailWidgetConstants::LeftRowPadding)
					[
						NameWidget.ToSharedRef()
					];
		

				InnerSplitter->AddSlot()
					.Value(ColumnSizeData.NameColumnWidth)
					.OnSlotResized(ColumnSizeData.OnNameColumnResized)
					[
						NameColumnBox
					];

				// create Value column:
				// | Left  | Name | Value | Right |
				InnerSplitter->AddSlot()
					.Value(ColumnSizeData.ValueColumnWidth)
					.OnSlotResized(ColumnSizeData.OnValueColumnResized) 
					[
						SNew(SHorizontalBox)
						.Clipping(EWidgetClipping::OnDemand)
						+ SHorizontalBox::Slot()
						.HAlign(Row.ValueWidget.HorizontalAlignment)
						.VAlign(Row.ValueWidget.VerticalAlignment)
						.Padding(DetailWidgetConstants::RightRowPadding)
						[
							ValueWidget.ToSharedRef()
						]
					];
			}
			else
			{
				NameColumnBox->SetEnabled(IsPropertyEditingEnabled);
				NameColumnBox->AddSlot()
					.HAlign(Row.WholeRowWidget.HorizontalAlignment)
					.VAlign(Row.WholeRowWidget.VerticalAlignment)
					.Padding(DetailWidgetConstants::LeftRowPadding)
					[
						Row.WholeRowWidget.Widget
					];

				InnerSplitter->AddSlot()
					[
						NameColumnBox
					];
			}

			TSharedRef<SHorizontalBox> RightColumnBox = SNew(SHorizontalBox)
				.Clipping(EWidgetClipping::OnDemand);

			// reset to default widget
			TSharedPtr<IPropertyHandle> PropertyHandle = GetPropertyHandle();
			if (PropertyHandle.IsValid() && !PropertyHandle->HasMetaData("NoResetToDefualt") && !PropertyHandle->GetInstanceMetaData("NoResetToDefault"))
			{
				TSharedPtr<SWidget> ResetWidget = nullptr;

				if (PropertyHandle->IsResetToDefaultCustomized())
				{
					// FIXME: Workaround for JIRA UE-73210.
					// We had an oscillating SPropertyValueWidget width while dragging a UMG widget in the designer.
					// The way drag&drop is implemented (SDesignerView::ProcessDropAndAddWidget), a new UCanvasPanelSlot gets 
					// recreated every frame, so the details panel gets refreshed every frame. Since new property rows are created 
					// before old ones are destroyed in the details panel, the HasCustomResetToDefault flag on the property node 
					// toggles from frame to frame, so we alternate between having a ResetToDefaultPropertyEditor and not having one.
					// By having a spacer fill the blank, the property row layout doesn't change while dragging, but we still see 
					// a flashing yellow reset arrow (when visible).
					const FSlateBrush* DiffersFromDefaultBrush = FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault");
					ResetWidget = SNew(SSpacer)
						.Size(DiffersFromDefaultBrush != nullptr ? DiffersFromDefaultBrush->ImageSize : FVector2D(8.0f, 8.0f));
				}
				else
				{
					SAssignNew(ResetWidget, SResetToDefaultPropertyEditor, PropertyHandle)
						.IsEnabled(IsPropertyEditingEnabled)
						.CustomResetToDefault(Row.CustomResetToDefault);
				}

				RightColumnBox->AddSlot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.Padding(10,0,0,0)
					.AutoWidth()
					[
						ResetWidget.ToSharedRef()
					];
			} 

			// keyframe button
			RightColumnBox->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(5,0,0,0)
				.AutoWidth()
				[
					CreateKeyframeButton(*Customization, InOwnerTreeNode)
				];

			// extension widget
			RightColumnBox->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(5,0,0,0)
				.AutoWidth()
				[
					ExtensionWidget.ToSharedRef()
				];

			// fetch global extension widgets 
			FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

			TArray<TSharedRef<SWidget>> GlobalWidgetExtensions;
			FOnGenerateGlobalRowExtensionArgs Args { GetPropertyHandle(), GetPropertyNode(), OwnerTreeNode };
			PropertyEditorModule.GetGlobalRowExtensionDelegate().Broadcast(Args, GlobalWidgetExtensions);

			if (GlobalWidgetExtensions.Num() > 0)
			{
				for (TSharedRef<SWidget>& GlobalRowExtension : GlobalWidgetExtensions)
				{
					RightColumnBox->AddSlot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							MoveTemp(GlobalRowExtension)
						];
				}
			}

			OuterSplitter->AddSlot()
				.Value(ColumnSizeData.RightColumnWidth)
				.OnSlotResized(ColumnSizeData.OnRightColumnResized)
			[
				RightColumnBox
			];
		}
	}
	else
	{
		// details panel layout became invalid.  This is probably a scenario where a widget is coming into view in the parent tree but some external event previous in the frame has invalidated the contents of the details panel.
		// The next frame update of the details panel will fix it
		Widget = SNew(SSpacer);
	}

	this->ChildSlot
	[
		SNew( SBorder )
		.BorderImage( FEditorStyle::GetBrush( "DetailsView.GridLine" ) )
		.Padding( FMargin( 0, 0, 0, 1 ) )
		[
			SNew( SBorder )
			.BorderImage( this, &SDetailSingleItemRow::GetBorderImage )
			.Padding( FMargin( 0.0f, 0.0f, SDetailTableRowBase::ScrollbarPaddingSize, 0.0f ) )
			[
				Widget
			]
		]
	];

	STableRow< TSharedPtr< FDetailTreeNode > >::ConstructInternal(
		STableRow::FArguments()
			.Style(FEditorStyle::Get(), "DetailsView.TreeView.TableRow")
			.ShowSelection(false)
			.OnDragEnter(ArrayDragDelegate)
			.OnDragLeave(ArrayDragLeaveDelegate)
			.OnDrop(ArrayDropDelegate)
			.OnCanAcceptDrop(ArrayAcceptDropDelegate),
		InOwnerTableView
	);
}

bool SDetailSingleItemRow::OnContextMenuOpening(FMenuBuilder& MenuBuilder)
{
	const bool bIsCopyPasteBound = Customization->GetWidgetRow().IsCopyPasteBound();

	FUIAction CopyAction;
	FUIAction PasteAction;

	if (bIsCopyPasteBound)
	{
		CopyAction = Customization->GetWidgetRow().CopyMenuAction;
		PasteAction = Customization->GetWidgetRow().PasteMenuAction;
	}
	else
	{
		TSharedPtr<FPropertyNode> PropertyNode = GetPropertyNode();
		static const FName DisableCopyPasteMetaDataName("DisableCopyPaste");
		if (PropertyNode.IsValid() && !PropertyNode->ParentOrSelfHasMetaData(DisableCopyPasteMetaDataName))
		{
			CopyAction.ExecuteAction = FExecuteAction::CreateSP(this, &SDetailSingleItemRow::OnCopyProperty);
			PasteAction.ExecuteAction = FExecuteAction::CreateSP(this, &SDetailSingleItemRow::OnPasteProperty);
			PasteAction.CanExecuteAction = FCanExecuteAction::CreateSP(this, &SDetailSingleItemRow::CanPasteProperty);
		}
		else
		{
			CopyAction.ExecuteAction = FExecuteAction::CreateLambda([](){});
			CopyAction.CanExecuteAction = FCanExecuteAction::CreateLambda([]() { return false; });
			PasteAction.ExecuteAction = FExecuteAction::CreateLambda([](){});
			PasteAction.CanExecuteAction = FCanExecuteAction::CreateLambda([]() { return false; });
		}
	}

	bool bAddedMenuEntry = false;
	if (CopyAction.IsBound() && PasteAction.IsBound())
	{
		// Hide separator line if it only contains the SearchWidget, making the next 2 elements the top of the list
		if (MenuBuilder.GetMultiBox()->GetBlocks().Num() > 1)
		{
			MenuBuilder.AddMenuSeparator();
		}

		MenuBuilder.AddMenuEntry(
			NSLOCTEXT("PropertyView", "CopyProperty", "Copy"),
			NSLOCTEXT("PropertyView", "CopyProperty_ToolTip", "Copy this property value"),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "GenericCommands.Copy"),
			CopyAction);

		MenuBuilder.AddMenuEntry(
			NSLOCTEXT("PropertyView", "PasteProperty", "Paste"),
			NSLOCTEXT("PropertyView", "PasteProperty_ToolTip", "Paste the copied value here"),
			FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "GenericCommands.Paste"),
			PasteAction);
	}

	FUIAction FavoriteAction;
	FavoriteAction.ExecuteAction = FExecuteAction::CreateSP(this, &SDetailSingleItemRow::OnFavoriteMenuToggle);
	FavoriteAction.CanExecuteAction = FCanExecuteAction::CreateLambda([this]()
		{
			return Customization->GetPropertyNode().IsValid() && Customization->GetPropertyNode()->CanDisplayFavorite();
		});

	FText FavoriteText = NSLOCTEXT("PropertyView", "FavoriteProperty", "Add to Favorites");
	FText FavoriteTooltipText = NSLOCTEXT("PropertyView", "FavoriteProperty_ToolTip", "Add this property to your favorites.");
	FName FavoriteIcon = "DetailsView.PropertyIsFavorite";

	bool IsFavorite = Customization->GetPropertyNode().IsValid() && Customization->GetPropertyNode()->IsFavorite();
	if (IsFavorite)
	{
		FavoriteText = NSLOCTEXT("PropertyView", "RemoveFavoriteProperty", "Remove from Favorites");
		FavoriteTooltipText = NSLOCTEXT("PropertyView", "RemoveFavoriteProperty_ToolTip", "Remove this property from your favorites.");
		FavoriteIcon = "DetailsView.PropertyIsNotFavorite";
	}

	MenuBuilder.AddMenuEntry(
		FavoriteText,
		FavoriteTooltipText,
		FSlateIcon(FEditorStyle::Get().GetStyleSetName(), FavoriteIcon),
		FavoriteAction);

	const TArray<FDetailWidgetRow::FCustomMenuData>& CustomMenuActions = Customization->GetWidgetRow().CustomMenuItems;
	if (CustomMenuActions.Num() > 0)
	{
		// Hide separator line if it only contains the SearchWidget, making the next 2 elements the top of the list
		if (MenuBuilder.GetMultiBox()->GetBlocks().Num() > 1)
		{
			MenuBuilder.AddMenuSeparator();
		}

		for (const FDetailWidgetRow::FCustomMenuData& CustomMenuData : CustomMenuActions)
		{
			//Add the menu entry
			MenuBuilder.AddMenuEntry(
				CustomMenuData.Name,
				CustomMenuData.Tooltip,
				CustomMenuData.SlateIcon,
				CustomMenuData.Action);
		}

	}

	return true;
}

void SDetailSingleItemRow::OnCopyProperty()
{
	if (OwnerTreeNode.IsValid())
	{
		TSharedPtr<FPropertyNode> PropertyNode = GetPropertyNode();
		if (PropertyNode.IsValid())
		{
			TSharedPtr<IPropertyHandle> Handle = PropertyEditorHelpers::GetPropertyHandle(PropertyNode.ToSharedRef(), OwnerTreeNode.Pin()->GetDetailsView()->GetNotifyHook(), OwnerTreeNode.Pin()->GetDetailsView()->GetPropertyUtilities());

			FString Value;
			if (Handle->GetValueAsFormattedString(Value, PPF_Copy) == FPropertyAccess::Success)
			{
				FPlatformApplicationMisc::ClipboardCopy(*Value);
			}
		}
	}
}

void SDetailSingleItemRow::OnPasteProperty()
{
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	if (!ClipboardContent.IsEmpty() && OwnerTreeNode.IsValid())
	{
		TSharedPtr<FPropertyNode> PropertyNode = GetPropertyNode();
		if (!PropertyNode.IsValid() && Customization->DetailGroup.IsValid())
		{
			PropertyNode = Customization->DetailGroup->GetHeaderPropertyNode();
		}
		if (PropertyNode.IsValid())
		{
			TSharedPtr<IPropertyHandle> Handle = PropertyEditorHelpers::GetPropertyHandle(PropertyNode.ToSharedRef(), OwnerTreeNode.Pin()->GetDetailsView()->GetNotifyHook(), OwnerTreeNode.Pin()->GetDetailsView()->GetPropertyUtilities());

			Handle->SetValueFromFormattedString(ClipboardContent);

			// Cache expansion state and then rebuild child nodes, in case we're pasting an array of a different size. This ensures instanced properties can be rebuild properly
			TSet<FString> ExpandedChildPropertyPaths;
			PropertyNode->GetExpandedChildPropertyPaths(ExpandedChildPropertyPaths);
			PropertyNode->RebuildChildren();
			PropertyNode->SetExpandedChildPropertyNodes(ExpandedChildPropertyPaths);

			TArray<TSharedPtr<IPropertyHandle>> CopiedHandles;

			CopiedHandles.Add(Handle);

			while (CopiedHandles.Num() > 0)
			{

				Handle = CopiedHandles.Pop();

				// Add all child properties to the list so we can check them next
				uint32 NumChildren;
				Handle->GetNumChildren(NumChildren);
				for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ChildIndex++)
				{
					CopiedHandles.Add(Handle->GetChildHandle(ChildIndex));
				}

				UObject* NewValueAsObject = nullptr;
				if (FPropertyAccess::Success == Handle->GetValue(NewValueAsObject))
				{
				
					// if the object is instanced, then we need to do a deep copy.
					if (Handle->GetProperty() != nullptr
						&& (Handle->GetProperty()->PropertyFlags & (CPF_InstancedReference | CPF_ContainsInstancedReference)) != 0)
					{
						UObject* DuplicateOuter = nullptr;

						TArray<UObject*> Outers;
						Handle->GetOuterObjects(Outers);

						// Update the duplicate's outer to point to this outer. The source's outer may be some other object/asset
						// but we want this to own the duplicate.
						if (Outers.Num() > 0)
						{
							DuplicateOuter = Outers[0];
						}

						// This does a deep copy of NewValueAsObject. It's subobjects and property data will be 
						// copied.
						UObject* DuplicateOfNewValue = DuplicateObject<UObject>(NewValueAsObject, DuplicateOuter);
						TArray<FString> DuplicateValueAsString;
						DuplicateValueAsString.Add(DuplicateOfNewValue->GetPathName());
						Handle->SetPerObjectValues(DuplicateValueAsString);

					}
				}
			}

			// Need to refresh the details panel in case a property was pasted over another.
			OwnerTreeNode.Pin()->GetDetailsView()->ForceRefresh();
		}
	}
}

bool SDetailSingleItemRow::CanPasteProperty() const
{
	// Prevent paste from working if the property's edit condition is not met.
	TSharedPtr<FDetailPropertyRow> PropertyRow = Customization->PropertyRow;
	if (!PropertyRow.IsValid() && Customization->DetailGroup.IsValid())
	{
		PropertyRow = Customization->DetailGroup->GetHeaderPropertyRow();
	}

	if (PropertyRow.IsValid())
	{
		FPropertyEditor* PropertyEditor = PropertyRow->GetPropertyEditor().Get();
		if (PropertyEditor)
		{
			return !PropertyEditor->IsEditConst();
		}
	}

	FString ClipboardContent;
	if( OwnerTreeNode.IsValid() )
	{
		FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	}

	return !ClipboardContent.IsEmpty();
}

const FSlateBrush* SDetailSingleItemRow::GetBorderImage() const
{
	if( IsHighlighted() )
	{
		return FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Highlighted");
	}
	else if (bIsDragDropObject)
	{
		return FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Active");
	}
	else if (IsHovered() && !bIsHoveredDragTarget)
	{
		return FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Hovered");
	}
	else if (bIsHoveredDragTarget)
	{
		return FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Highlighted");
	}
	else
	{
		return FEditorStyle::GetBrush("DetailsView.CategoryMiddle");
	}
}

TSharedRef<SWidget> SDetailSingleItemRow::CreateExtensionWidget(TSharedRef<SWidget> ValueWidget, FDetailLayoutCustomization& InCustomization, TSharedRef<FDetailTreeNode> InTreeNode)
{
	TSharedPtr<SWidget> ExtensionWidget = SNullWidget::NullWidget;

	if(InTreeNode->GetParentCategory().IsValid())
	{ 
		IDetailsViewPrivate* DetailsView = InTreeNode->GetDetailsView();
		TSharedPtr<IDetailPropertyExtensionHandler> ExtensionHandler = DetailsView->GetExtensionHandler();

		if(ExtensionHandler.IsValid() && InCustomization.HasPropertyNode())
		{
			TSharedPtr<IPropertyHandle> Handle = PropertyEditorHelpers::GetPropertyHandle(InCustomization.GetPropertyNode().ToSharedRef(), nullptr, nullptr);

			FObjectPropertyNode* ObjectItemParent = InCustomization.GetPropertyNode()->FindObjectItemParent();
			UClass* ObjectClass = (ObjectItemParent) ? ObjectItemParent->GetObjectBaseClass() : nullptr;
			if(Handle->IsValidHandle() && ObjectClass && ExtensionHandler->IsPropertyExtendable(ObjectClass, *Handle))
			{
				FDetailLayoutBuilderImpl& DetailLayout = OwnerTreeNode.Pin()->GetParentCategory()->GetParentLayoutImpl();
				ExtensionWidget = ExtensionHandler->GenerateExtensionWidget(DetailLayout, ObjectClass, Handle);
			}
		}
	}

	return ExtensionWidget.ToSharedRef();
}

TSharedRef<SWidget> SDetailSingleItemRow::CreateKeyframeButton( FDetailLayoutCustomization& InCustomization, TSharedRef<FDetailTreeNode> InTreeNode )
{
	IDetailsViewPrivate* DetailsView = InTreeNode->GetDetailsView();

	KeyframeHandler = DetailsView->GetKeyframeHandler();

	EVisibility SetKeyVisibility = EVisibility::Collapsed;

	if (InCustomization.HasPropertyNode() && KeyframeHandler.IsValid() )
	{
		TSharedPtr<IPropertyHandle> Handle = PropertyEditorHelpers::GetPropertyHandle(InCustomization.GetPropertyNode().ToSharedRef(), nullptr, nullptr);

		FObjectPropertyNode* ObjectItemParent = InCustomization.GetPropertyNode()->FindObjectItemParent();
		UClass* ObjectClass = ObjectItemParent != nullptr ? ObjectItemParent->GetObjectBaseClass() : nullptr;
		SetKeyVisibility = ObjectClass != nullptr && KeyframeHandler.Pin()->IsPropertyKeyable(ObjectClass, *Handle) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	return
		SNew(SButton)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.ContentPadding(0.0f)
		.ButtonStyle(FEditorStyle::Get(), "Sequencer.AddKey.Details")
		.Visibility(SetKeyVisibility)
		.IsEnabled( this, &SDetailSingleItemRow::IsKeyframeButtonEnabled, InTreeNode )
		.ToolTipText( NSLOCTEXT("PropertyView", "AddKeyframeButton_ToolTip", "Adds a keyframe for this property to the current animation") )
		.OnClicked( this, &SDetailSingleItemRow::OnAddKeyframeClicked );
}

bool SDetailSingleItemRow::IsKeyframeButtonEnabled(TSharedRef<FDetailTreeNode> InTreeNode) const
{
	return
		InTreeNode->IsPropertyEditingEnabled().Get() &&
		KeyframeHandler.IsValid() &&
		KeyframeHandler.Pin()->IsPropertyKeyingEnabled();
}

FReply SDetailSingleItemRow::OnAddKeyframeClicked()
{	
	if( KeyframeHandler.IsValid() )
	{
		TSharedPtr<IPropertyHandle> Handle = PropertyEditorHelpers::GetPropertyHandle(Customization->GetPropertyNode().ToSharedRef(), nullptr, nullptr);

		KeyframeHandler.Pin()->OnKeyPropertyClicked(*Handle);
	}

	return FReply::Handled();
}

bool SDetailSingleItemRow::IsHighlighted() const
{
	return OwnerTreeNode.Pin()->IsHighlighted();
}

void SDetailSingleItemRow::SetIsDragDrop(bool bInIsDragDrop)
{
	bIsDragDropObject = bInIsDragDrop;
}

void SArrayRowHandle::Construct(const FArguments& InArgs)
{
	ParentRow = InArgs._ParentRow;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

FReply SArrayRowHandle::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		TSharedPtr<FDragDropOperation> DragDropOp = CreateDragDropOperation(ParentRow.Pin());
		if (DragDropOp.IsValid())
		{
			return FReply::Handled().BeginDragDrop(DragDropOp.ToSharedRef());
		}
	}

	return FReply::Unhandled();

}

TSharedPtr<FArrayRowDragDropOp> SArrayRowHandle::CreateDragDropOperation(TSharedPtr<SDetailSingleItemRow> InRow)
{
	TSharedPtr<FArrayRowDragDropOp> Operation = MakeShareable(new FArrayRowDragDropOp(InRow));

	return Operation;
}

FArrayRowDragDropOp::FArrayRowDragDropOp(TSharedPtr<SDetailSingleItemRow> InRow)
{
	Row = InRow;

	TSharedPtr<SDetailSingleItemRow> RowPtr = nullptr;
	if (Row.IsValid())
	{
		RowPtr = Row.Pin();
		// mark row as being used for drag and drop
		RowPtr->SetIsDragDrop(true);
	}

	DecoratorWidget = SNew(SBorder)
		.Padding(8.f)
		.BorderImage(FEditorStyle::GetBrush("Graph.ConnectorFeedback.Border"))
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(DecoratorText, STextBlock)
				.Text(NSLOCTEXT("ArrayDragDrop", "PlaceRowHere", "Place Row Here"))
			]
		];

	Construct();
}

void FArrayRowDragDropOp::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);

	TSharedPtr<SDetailSingleItemRow> RowPtr = nullptr;
	if (Row.IsValid())
	{
		RowPtr = Row.Pin();
		// reset value
		RowPtr->SetIsDragDrop(false);
	}
}