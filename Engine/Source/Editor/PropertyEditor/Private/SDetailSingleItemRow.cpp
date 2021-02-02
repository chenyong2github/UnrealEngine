// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDetailSingleItemRow.h"

#include "DetailGroup.h"
#include "DetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailKeyframeHandler.h"
#include "IDetailPropertyExtensionHandler.h"
#include "ObjectPropertyNode.h"
#include "PropertyEditorConstants.h"
#include "PropertyEditorModule.h"
#include "PropertyHandleImpl.h"
#include "SConstrainedBox.h"
#include "SDetailExpanderArrow.h"
#include "SDetailRowIndent.h"
#include "SResetToDefaultPropertyEditor.h"

#include "HAL/PlatformApplicationMisc.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"

namespace DetailWidgetConstants
{
	const FMargin LeftRowPadding( 20.0f, 2.5f, 10.0f, 2.5f );
	const FMargin RightRowPadding( 12.0f, 2.5f, 2.0f, 2.5f );
}

namespace SDetailSingleItemRow_Helper
{
	//Get the node item number in case it is expand we have to recursively count all expanded children
	void RecursivelyGetItemShow(TSharedRef<FDetailTreeNode> ParentItem, int32& ItemShowNum)
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

			IDetailsViewPrivate* DetailsView = OwnerTreeNode.Pin()->GetDetailsView();
			TSharedPtr<IPropertyHandle> SwappingHandle = PropertyEditorHelpers::GetPropertyHandle(SwappingPropertyNode.ToSharedRef(), DetailsView->GetNotifyHook(), DetailsView->GetPropertyUtilities());
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
	if (!ArrayDropOp.IsValid())
	{
		return FReply::Unhandled();
	}

	TSharedPtr<SDetailSingleItemRow> RowPtr = ArrayDropOp->Row.Pin();
	if (!RowPtr.IsValid())
	{
		return FReply::Unhandled();
	}

	if (!CheckValidDrop(RowPtr))
	{
		return FReply::Unhandled();
	}

	IDetailsViewPrivate* DetailsView = OwnerTreeNode.Pin()->GetDetailsView();

	TSharedPtr<FPropertyNode> SwappingPropertyNode = RowPtr->SwappablePropertyNode;
	TSharedPtr<IPropertyHandle> SwappingHandle = PropertyEditorHelpers::GetPropertyHandle(SwappingPropertyNode.ToSharedRef(), DetailsView->GetNotifyHook(), DetailsView->GetPropertyUtilities());
	TSharedPtr<IPropertyHandleArray> ParentHandle = SwappingHandle->GetParentHandle()->AsArray();
	int32 OriginalIndex = SwappingPropertyNode->GetArrayIndex();
	int32 NewIndex = SwappablePropertyNode->GetArrayIndex();

	// Need to swap the moving and target expansion states before saving
	bool bOriginalSwappableExpansion = SwappablePropertyNode->HasNodeFlags(EPropertyNodeFlags::Expanded) != 0;
	bool bOriginalSwappingExpansion = SwappingPropertyNode->HasNodeFlags(EPropertyNodeFlags::Expanded) != 0;
	SwappablePropertyNode->SetNodeFlags(EPropertyNodeFlags::Expanded, bOriginalSwappingExpansion);
	SwappingPropertyNode->SetNodeFlags(EPropertyNodeFlags::Expanded, bOriginalSwappableExpansion);

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

	return FReply::Handled();
}

TOptional<EItemDropZone> SDetailSingleItemRow::OnArrayCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr< FDetailTreeNode > Type)
{
	TSharedPtr<FArrayRowDragDropOp> ArrayDropOp = DragDropEvent.GetOperationAs< FArrayRowDragDropOp >();
	if (!ArrayDropOp.IsValid())
	{
		return TOptional<EItemDropZone>();
	}

	TSharedPtr<SDetailSingleItemRow> RowPtr = ArrayDropOp->Row.Pin();
	if (!RowPtr.IsValid())
	{
		return TOptional<EItemDropZone>();
	}

	bool IsValidDrop = CheckValidDrop(RowPtr);
	if (!IsValidDrop)
	{
		bIsHoveredDragTarget = false;
	}

	ArrayDropOp->IsValidTarget = IsValidDrop;

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
		TSharedPtr<FDetailTreeNode> OwnerTreeNodePtr = OwnerTreeNode.Pin();
		if (OwnerTreeNodePtr.IsValid())
		{
			IDetailsViewPrivate* DetailsView = OwnerTreeNodePtr->GetDetailsView();
			if (DetailsView != nullptr)
			{
				Handle = PropertyEditorHelpers::GetPropertyHandle(PropertyNode.ToSharedRef(), DetailsView->GetNotifyHook(), DetailsView->GetPropertyUtilities());
			}
		}
	}
	else if (WidgetRow.PropertyHandles.Num() > 0)
	{
		// @todo: Handle more than 1 property handle?
		Handle = WidgetRow.PropertyHandles[0];
	}

	return Handle;
}

bool SDetailSingleItemRow::UpdateResetToDefault()
{
	TSharedPtr<IPropertyHandle> PropertyHandle = GetPropertyHandle();
	if (PropertyHandle.IsValid())
	{
		if (PropertyHandle->HasMetaData("NoResetToDefault") || PropertyHandle->GetInstanceMetaData("NoResetToDefault"))
		{
			return false;
		}
	}

	if (WidgetRow.CustomResetToDefault.IsSet())
	{
		return WidgetRow.CustomResetToDefault.GetValue().IsResetToDefaultVisible(PropertyHandle);
	}
	else if (PropertyHandle.IsValid())
	{
		return PropertyHandle->CanResetToDefault();
	}

	return false;

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
			WidgetRow = Customization->GetWidgetRow();

			TSharedPtr<SWidget> NameWidget = WidgetRow.NameWidget.Widget;

			TSharedPtr<SWidget> ValueWidget =
				SNew(SConstrainedBox)
				.MinWidth(WidgetRow.ValueWidget.MinWidth)
				.MaxWidth(WidgetRow.ValueWidget.MaxWidth)
				[
					WidgetRow.ValueWidget.Widget
				];

			TAttribute<bool> IsEnabledAttribute;
			if (WidgetRow.IsEnabledAttr.IsSet() || WidgetRow.IsEnabledAttr.IsBound())
			{
				TAttribute<bool> RowEnabledAttr = WidgetRow.IsEnabledAttr;
				TAttribute<bool> PropertyEnabledAttr = InOwnerTreeNode->IsPropertyEditingEnabled();
				IsEnabledAttribute = TAttribute<bool>::Create(
					[RowEnabledAttr, PropertyEnabledAttr]()
					{
						return RowEnabledAttr.Get() && PropertyEnabledAttr.Get();
					});
			}
			else
			{
				IsEnabledAttribute = InOwnerTreeNode->IsPropertyEditingEnabled();
			}

			NameWidget->SetEnabled(IsEnabledAttribute);
			ValueWidget->SetEnabled(IsEnabledAttribute);

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
				.Padding(6.0f, 0.0f, 6.0f, 0.0f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SConstrainedBox)
					.MinWidth(20)
					[
						SNew(SEditConditionWidget)
						.EditConditionValue(WidgetRow.EditConditionValue)
						.OnEditConditionValueChanged(WidgetRow.OnEditConditionValueChanged)
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
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(this, &SDetailSingleItemRow::GetInnerBackgroundColor)
				.Padding(0)
				[
					SAssignNew(InnerSplitter, SSplitter)
					.Style(FEditorStyle::Get(), "DetailsView.Splitter")
					.PhysicalSplitterHandleSize(1.0f)
					.HitDetectionSplitterHandleSize(5.0f)
					.HighlightedHandleIndex(ColumnSizeData.HoveredSplitterIndex)
					.OnHandleHovered(ColumnSizeData.OnSplitterHandleHovered)
				]
			];

			// create Name column:
			// | Left  | Name | Value | Right |
			TSharedRef<SHorizontalBox> NameColumnBox = SNew(SHorizontalBox)
				.Clipping(EWidgetClipping::OnDemand);

			// indentation and expander arrow
			NameColumnBox->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Fill)
				.Padding(0)
				.AutoWidth()
				[
					SNew(SDetailRowIndent, SharedThis(this))
				];

			NameColumnBox->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(5,0,0,0)
				.AutoWidth()
				[
					SNew(SDetailExpanderArrow, SharedThis(this))
				];
			
			TSharedPtr<FPropertyNode> PropertyNode = Customization->GetPropertyNode();
			if (PropertyNode.IsValid())
			{
				if (PropertyNode->IsReorderable())
				{
					TSharedPtr<SDetailSingleItemRow> InRow = SharedThis(this);
					TSharedRef<SWidget> ArrayHandle = PropertyEditorHelpers::MakePropertyReorderHandle(PropertyNode.ToSharedRef(), InRow);
					ArrayHandle->SetEnabled(IsEnabledAttribute);
					ArrayHandle->SetVisibility(TAttribute<EVisibility>::Create([this]() 
					{
						return this->IsHovered() ? EVisibility::Visible : EVisibility::Hidden;
					}));

					NameColumnBox->AddSlot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						.Padding(0)
						.AutoWidth()
						[
							ArrayHandle
						];

					SwappablePropertyNode = PropertyNode;
				}
				
				if (PropertyNode->IsReorderable() || 
					(CastField<FArrayProperty>(PropertyNode->GetProperty()) != nullptr && 
					CastField<FObjectProperty>(CastField<FArrayProperty>(PropertyNode->GetProperty())->Inner) != nullptr)) // Is an object array
				{
					ArrayDragDelegate = FOnTableRowDragEnter::CreateSP(this, &SDetailSingleItemRow::OnArrayDragEnter);
					ArrayDragLeaveDelegate = FOnTableRowDragLeave::CreateSP(this, &SDetailSingleItemRow::OnArrayDragLeave);
					ArrayDropDelegate = FOnTableRowDrop::CreateSP(this, &SDetailSingleItemRow::OnArrayHeaderDrop);
					ArrayAcceptDropDelegate = FOnCanAcceptDrop::CreateSP(this, &SDetailSingleItemRow::OnArrayCanAcceptDrop);
				}
			}

			const bool bIsReorderable = PropertyNode.IsValid() && PropertyNode->IsReorderable();
			auto GetLeftRowPadding = [this, bIsReorderable]()
			{
				FMargin Padding = DetailWidgetConstants::LeftRowPadding;
				Padding.Left -= bIsReorderable ? 16 : 0;
				return Padding;
			};

			if (bHasMultipleColumns)
			{
				NameColumnBox->AddSlot()
					.HAlign(WidgetRow.NameWidget.HorizontalAlignment)
					.VAlign(WidgetRow.NameWidget.VerticalAlignment)
					.Padding(TAttribute<FMargin>::Create(GetLeftRowPadding))
					[
						NameWidget.ToSharedRef()
					];
		
				InnerSplitter->AddSlot()
					.Value(ColumnSizeData.NameColumnWidth)
					.OnSlotResized(ColumnSizeData.OnNameColumnResized)
					[
						NameColumnBox
					];

				TSharedRef<SWidget> ExtensionWidget = CreateExtensionWidget();
				ExtensionWidget->SetEnabled(IsEnabledAttribute);

				// create Value column:
				// | Left  | Name | Value | Right |
				InnerSplitter->AddSlot()
					.Value(ColumnSizeData.ValueColumnWidth)
					.OnSlotResized(ColumnSizeData.OnValueColumnResized) 
					[
						SNew(SHorizontalBox)
						.Clipping(EWidgetClipping::OnDemand)
						+ SHorizontalBox::Slot()
						.HAlign(WidgetRow.ValueWidget.HorizontalAlignment)
						.VAlign(WidgetRow.ValueWidget.VerticalAlignment)
						.Padding(DetailWidgetConstants::RightRowPadding)
						[
							ValueWidget.ToSharedRef()
						]
						// extension widget
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.Padding(5,0,0,0)
						.AutoWidth()
						[
							ExtensionWidget
						]
					];
			}
			else
			{
				NameColumnBox->SetEnabled(IsEnabledAttribute);
				NameColumnBox->AddSlot()
					.HAlign(WidgetRow.WholeRowWidget.HorizontalAlignment)
					.VAlign(WidgetRow.WholeRowWidget.VerticalAlignment)
					.Padding(TAttribute<FMargin>::Create(GetLeftRowPadding))
					[
						WidgetRow.WholeRowWidget.Widget
					];

				InnerSplitter->AddSlot()
					[
						NameColumnBox
					];
			}

			TArray<FPropertyRowExtensionButton> ExtensionButtons;

			FPropertyRowExtensionButton& ResetToDefault = ExtensionButtons.AddDefaulted_GetRef();
			ResetToDefault.Label = NSLOCTEXT("PropertyEditor", "ResetToDefault", "Reset to Default");
			ResetToDefault.UIAction = FUIAction(
				FExecuteAction::CreateSP(this, &SDetailSingleItemRow::OnResetToDefaultClicked),
				FCanExecuteAction::CreateSP(this, &SDetailSingleItemRow::IsResetToDefaultEnabled)
			);

			// We could just collapse the Reset to Default button by setting the FIsActionButtonVisible delegate,
			// but this would cause the reset to defaults not to reserve space in the toolbar and not be aligned across all rows.
			// Instead, we show an empty icon and tooltip and disable the button.
			static FSlateIcon EnabledResetToDefaultIcon(FAppStyle::Get().GetStyleSetName(), "PropertyWindow.DiffersFromDefault");
			static FSlateIcon DisabledResetToDefaultIcon(FAppStyle::Get().GetStyleSetName(), "NoBrush");
			ResetToDefault.Icon = TAttribute<FSlateIcon>::Create([this]()
			{
				return IsResetToDefaultEnabled() ? 
					EnabledResetToDefaultIcon :
					DisabledResetToDefaultIcon;
			});
			ResetToDefault.ToolTip = TAttribute<FText>::Create([this]() 
			{
				return IsResetToDefaultEnabled() ?
					NSLOCTEXT("PropertyEditor", "ResetToDefaultToolTip", "Reset this property to its default value.") :
					FText::GetEmpty();
			});

			CreateGlobalExtensionWidgets(ExtensionButtons);

			FSlimHorizontalToolBarBuilder ToolbarBuilder(TSharedPtr<FUICommandList>(), FMultiBoxCustomization::None);
			ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);
			ToolbarBuilder.SetStyle(&FAppStyle::Get(), "DetailsView.ExtensionToolBar");

			for (const FPropertyRowExtensionButton& Extension : ExtensionButtons)
			{
				ToolbarBuilder.AddToolBarButton(Extension.UIAction, NAME_None, Extension.Label, Extension.ToolTip, Extension.Icon);
			}

			TSharedRef<SHorizontalBox> RightColumnBox = SNew(SHorizontalBox);
			RightColumnBox->AddSlot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.Padding(5,0,5,0)
				[
					ToolbarBuilder.MakeWidget()
				];

			OuterSplitter->AddSlot()
				.Value(ColumnSizeData.RightColumnWidth)
				.OnSlotResized(ColumnSizeData.OnRightColumnResized)
				.MinSize(50)
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

	TWeakPtr<STableViewBase> OwnerTableViewWeak = InOwnerTableView;
	auto GetScrollbarWellBrush = [this, OwnerTableViewWeak]()
	{
		return SDetailTableRowBase::IsScrollBarVisible(OwnerTableViewWeak) ?
			FAppStyle::Get().GetBrush("DetailsView.GridLine") : 
			FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle");
	};

	auto GetScrollbarWellTint = [this, OwnerTableViewWeak]()
	{
		return SDetailTableRowBase::IsScrollBarVisible(OwnerTableViewWeak) ?
			FSlateColor(EStyleColor::White) : 
			this->GetInnerBackgroundColor();
	};

	this->ChildSlot
	[
		SNew( SBorder )
		.BorderImage(FAppStyle::Get().GetBrush( "DetailsView.GridLine"))
		.Padding(FMargin(0,0,0,1))
		[
			SNew( SHorizontalBox )
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				SNew( SBorder )
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(this, &SDetailSingleItemRow::GetOuterBackgroundColor)
				.Padding(0)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					Widget
				]
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Fill)
			.AutoWidth()
			[
				SNew(SBorder)
				.BorderImage_Lambda(GetScrollbarWellBrush)
				.BorderBackgroundColor_Lambda(GetScrollbarWellTint)
				.Padding(FMargin(0, 0, SDetailTableRowBase::ScrollBarPadding, 0))
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

bool SDetailSingleItemRow::IsResetToDefaultEnabled() const
{
	return bCachedResetToDefaultEnabled;
}

void SDetailSingleItemRow::OnResetToDefaultClicked() const
{
	TSharedPtr<IPropertyHandle> PropertyHandle = GetPropertyHandle();
	if (WidgetRow.CustomResetToDefault.IsSet())
	{
		WidgetRow.CustomResetToDefault.GetValue().OnResetToDefaultClicked(PropertyHandle);
	}
	else if (PropertyHandle.IsValid())
	{
		PropertyHandle->ResetToDefault();
	}
}

/** Get the background color of the outer part of the row, which contains the edit condition and extension widgets. */
FSlateColor SDetailSingleItemRow::GetOuterBackgroundColor() const
{
	if (IsHighlighted())
	{
		return FAppStyle::Get().GetSlateColor("Colors.Panel");
	}
	else if (bIsDragDropObject)
	{
		return FAppStyle::Get().GetSlateColor("Colors.Panel");
	}
	else if (IsHovered())
	{
		if (bIsHoveredDragTarget)
		{
			return FAppStyle::Get().GetSlateColor("Colors.Panel");
		}
		else
		{
			return FAppStyle::Get().GetSlateColor("Colors.Header");
		}
	}
	
	return FAppStyle::Get().GetSlateColor("Colors.Panel");
}

/** Get the background color of the inner part of the row, which contains the name and value widgets. */
FSlateColor SDetailSingleItemRow::GetInnerBackgroundColor() const
{
	if (IsHovered() && !bIsHoveredDragTarget)
	{
		return FAppStyle::Get().GetSlateColor("Colors.Header");
	}

	if (bIsHoveredDragTarget)
	{
		return FAppStyle::Get().GetSlateColor("Colors.Hover2");
	}

	if (bIsDragDropObject)
	{
		return FAppStyle::Get().GetSlateColor("Colors.Hover");
	}

	int32 IndentLevel = GetIndentLevelForBackgroundColor();
	return PropertyEditorConstants::GetRowBackgroundColor(IndentLevel);
}

bool SDetailSingleItemRow::OnContextMenuOpening(FMenuBuilder& MenuBuilder)
{
	const bool bIsCopyPasteBound = WidgetRow.IsCopyPasteBound();

	FUIAction CopyAction;
	FUIAction PasteAction;

	if (bIsCopyPasteBound)
	{
		CopyAction = WidgetRow.CopyMenuAction;
		PasteAction = WidgetRow.PasteMenuAction;
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

	if (OwnerTreeNode.Pin()->GetDetailsView()->IsFavoritingEnabled())
	{
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
	}

	if (WidgetRow.CustomMenuItems.Num() > 0)
	{
		// Hide separator line if it only contains the SearchWidget, making the next 2 elements the top of the list
		if (MenuBuilder.GetMultiBox()->GetBlocks().Num() > 1)
		{
			MenuBuilder.AddMenuSeparator();
		}

		for (const FDetailWidgetRow::FCustomMenuData& CustomMenuData : WidgetRow.CustomMenuItems)
		{
			// Add the menu entry
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
			IDetailsViewPrivate* DetailsView = OwnerTreeNode.Pin()->GetDetailsView();
			TSharedPtr<IPropertyHandle> Handle = PropertyEditorHelpers::GetPropertyHandle(PropertyNode.ToSharedRef(), DetailsView->GetNotifyHook(), DetailsView->GetPropertyUtilities());

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
			FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "PasteProperty", "Paste Property"));

			IDetailsViewPrivate* DetailsView = OwnerTreeNode.Pin()->GetDetailsView();
			TSharedPtr<IPropertyHandle> Handle = PropertyEditorHelpers::GetPropertyHandle(PropertyNode.ToSharedRef(), DetailsView->GetNotifyHook(), DetailsView->GetPropertyUtilities());

			Handle->SetValueFromFormattedString(ClipboardContent);

			FPropertyValueImpl::RebuildInstancedProperties(Handle, PropertyNode.Get());

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
	if (OwnerTreeNode.IsValid())
	{
		FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	}

	return !ClipboardContent.IsEmpty();
}

TSharedRef<SWidget> SDetailSingleItemRow::CreateExtensionWidget() const
{
	TSharedPtr<SWidget> ExtensionWidget = SNullWidget::NullWidget;

	TSharedPtr<FDetailTreeNode> OwnerTreeNodePinned = OwnerTreeNode.Pin();
	if (OwnerTreeNodePinned.IsValid())
	{ 
		IDetailsViewPrivate* DetailsView = OwnerTreeNodePinned->GetDetailsView();
		TSharedPtr<IDetailPropertyExtensionHandler> ExtensionHandler = DetailsView->GetExtensionHandler();
		if (Customization->HasPropertyNode() && ExtensionHandler.IsValid())
		{
			TSharedPtr<IPropertyHandle> Handle = PropertyEditorHelpers::GetPropertyHandle(Customization->GetPropertyNode().ToSharedRef(), nullptr, nullptr);
			const UClass* ObjectClass = Handle->GetOuterBaseClass();
			if (Handle->IsValidHandle() && ObjectClass && ExtensionHandler->IsPropertyExtendable(ObjectClass, *Handle))
			{
				FDetailLayoutBuilderImpl& DetailLayout = OwnerTreeNodePinned->GetParentCategory()->GetParentLayoutImpl();
				ExtensionWidget = ExtensionHandler->GenerateExtensionWidget(DetailLayout, ObjectClass, Handle);
			}
		}
	}

	return ExtensionWidget.ToSharedRef();
}

void SDetailSingleItemRow::OnFavoriteMenuToggle()
{
	if (!Customization->GetPropertyNode().IsValid() || !Customization->GetPropertyNode()->CanDisplayFavorite())
	{
		return; 
	}

	bool bToggled = !Customization->GetPropertyNode()->IsFavorite();
	Customization->GetPropertyNode()->SetFavorite(bToggled);

	TSharedPtr<FDetailTreeNode> OwnerTreeNodePinned = OwnerTreeNode.Pin();
	if (!OwnerTreeNodePinned.IsValid())
	{
		return;
	}

	// Calculate the scrolling offset (by item) to make sure the mouse stay over the same property
	int32 ExpandSize = 0;
	if (OwnerTreeNodePinned->ShouldBeExpanded())
	{
		SDetailSingleItemRow_Helper::RecursivelyGetItemShow(OwnerTreeNodePinned.ToSharedRef(), ExpandSize);
	}
	else
	{
		// if the item is not expand count is 1
		ExpandSize = 1;
	}

	// Apply the calculated offset
	IDetailsViewPrivate* DetailsView = OwnerTreeNodePinned->GetDetailsView();
	DetailsView->MoveScrollOffset(bToggled ? ExpandSize : -ExpandSize);

	// Refresh the tree
	DetailsView->ForceRefresh();
}

void SDetailSingleItemRow::CreateGlobalExtensionWidgets(TArray<FPropertyRowExtensionButton>& OutExtensions) const
{
	// fetch global extension widgets 
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FOnGenerateGlobalRowExtensionArgs Args;
	Args.OwnerTreeNode = OwnerTreeNode;

	if (Customization->HasPropertyNode())
	{
		Args.PropertyHandle = PropertyEditorHelpers::GetPropertyHandle(Customization->GetPropertyNode().ToSharedRef(), nullptr, nullptr);
	}

	PropertyEditorModule.GetGlobalRowExtensionDelegate().Broadcast(Args, OutExtensions);
}

bool SDetailSingleItemRow::IsHighlighted() const
{
	TSharedPtr<FDetailTreeNode> OwnerTreeNodePtr = OwnerTreeNode.Pin();
	return OwnerTreeNodePtr.IsValid() ? OwnerTreeNodePtr->IsHighlighted() : false;
}

void SDetailSingleItemRow::SetIsDragDrop(bool bInIsDragDrop)
{
	bIsDragDropObject = bInIsDragDrop;
}

void SDetailSingleItemRow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	bCachedResetToDefaultEnabled = UpdateResetToDefault();
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

FText FArrayRowDragDropOp::GetDecoratorText() const
{
	if (IsValidTarget)
	{
		return NSLOCTEXT("ArrayDragDrop", "PlaceRowHere", "Place Row Here");
	}
	else
	{
		return NSLOCTEXT("ArrayDragDrop", "CannotPlaceRowHere", "Cannot Place Row Here");
	}
}

const FSlateBrush* FArrayRowDragDropOp::GetDecoratorIcon() const
{
	if (IsValidTarget)
	{
		return FEditorStyle::GetBrush("Graph.ConnectorFeedback.OK");
	}
	else 
	{
		return FEditorStyle::GetBrush("Graph.ConnectorFeedback.Error");
	}
}

FArrayRowDragDropOp::FArrayRowDragDropOp(TSharedPtr<SDetailSingleItemRow> InRow)
{
	IsValidTarget = false;

	check(InRow.IsValid());
	Row = InRow;

	// mark row as being used for drag and drop
	InRow->SetIsDragDrop(true);

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
				SNew(SImage)
				.Image_Raw(this, &FArrayRowDragDropOp::GetDecoratorIcon)
			]
			+ SHorizontalBox::Slot()
			.Padding(5,0,0,0)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Raw(this, &FArrayRowDragDropOp::GetDecoratorText)
			]
		];

	Construct();
}

void FArrayRowDragDropOp::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);

	TSharedPtr<SDetailSingleItemRow> RowPtr = Row.Pin();
	if (RowPtr.IsValid())
	{
		// reset value
		RowPtr->SetIsDragDrop(false);
	}
}