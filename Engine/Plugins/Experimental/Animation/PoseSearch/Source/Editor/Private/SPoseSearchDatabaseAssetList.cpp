// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPoseSearchDatabaseAssetList.h"
#include "PoseSearchDatabaseViewModel.h"

#include "PoseSearch/PoseSearch.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimComposite.h"
#include "Animation/BlendSpace.h"

#include "Misc/TransactionObjectEvent.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Misc/FeedbackContext.h"
#include "AssetSelection.h"
#include "ClassIconFinder.h"
#include "DetailColumnSizeData.h"

#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SPositiveActionButton.h"
#include "Styling/AppStyle.h"

#include "ScopedTransaction.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "PoseSearchDatabaseAssetList"

namespace UE::PoseSearch
{
	static constexpr FLinearColor DisabledColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.25f);
	
	FDatabaseAssetTreeNode::FDatabaseAssetTreeNode(
		int32 InSourceAssetIdx,
		ESearchIndexAssetType InSourceAssetType,
		const TSharedRef<FDatabaseViewModel>& InEditorViewModel) 
		: SourceAssetIdx(InSourceAssetIdx)
		, SourceAssetType(InSourceAssetType)
		, EditorViewModel(InEditorViewModel)
	{ }

	TSharedRef<ITableRow> FDatabaseAssetTreeNode::MakeTreeRowWidget(
		const TSharedRef<STableViewBase>& InOwnerTable,
		TSharedRef<FDatabaseAssetTreeNode> InDatabaseAssetNode,
		TSharedRef<FUICommandList> InCommandList,
		TSharedPtr<SDatabaseAssetTree> InHierarchy)
	{
		return SNew(
			SDatabaseAssetListItem, 
			EditorViewModel.Pin().ToSharedRef(), 
			InOwnerTable, 
			InDatabaseAssetNode, 
			InCommandList, 
			InHierarchy);
	}

	bool FDatabaseAssetTreeNode::IsRootMotionEnabled() const
	{
		if (const UPoseSearchDatabase* Database = EditorViewModel.Pin()->GetPoseSearchDatabase())
		{
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetAnimationAssetBase(SourceAssetIdx))
			{
				return DatabaseAnimationAsset->IsRootMotionEnabled();
			}
		}

		return false;
	}

	bool FDatabaseAssetTreeNode::IsLooping() const
	{
		if (const UPoseSearchDatabase* Database = EditorViewModel.Pin()->GetPoseSearchDatabase())
		{
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetAnimationAssetBase(SourceAssetIdx))
			{
				return DatabaseAnimationAsset->IsLooping();
			}
		}

		return false;
	}

	EPoseSearchMirrorOption FDatabaseAssetTreeNode::GetMirrorOption() const
	{
		if (const UPoseSearchDatabase* Database = EditorViewModel.Pin()->GetPoseSearchDatabase())
		{
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetAnimationAssetBase(SourceAssetIdx))
			{
				return DatabaseAnimationAsset->GetMirrorOption();
			}
		}

		return EPoseSearchMirrorOption::Invalid;
	}

	void SDatabaseAssetListItem::Construct(
		const FArguments& InArgs,
		const TSharedRef<FDatabaseViewModel>& InEditorViewModel,
		const TSharedRef<STableViewBase>& OwnerTable,
		TSharedRef<FDatabaseAssetTreeNode> InAssetTreeNode,
		TSharedRef<FUICommandList> InCommandList,
		TSharedPtr<SDatabaseAssetTree> InHierarchy)
	{
		WeakAssetTreeNode = InAssetTreeNode;
		EditorViewModel = InEditorViewModel;
		SkeletonView = InHierarchy;

		if (InAssetTreeNode->SourceAssetType == ESearchIndexAssetType::Invalid)
		{
			ConstructGroupItem(OwnerTable);
		}
		else
		{
			ConstructAssetItem(OwnerTable);
		}
	}

	void SDatabaseAssetListItem::ConstructGroupItem(const TSharedRef<STableViewBase>& OwnerTable)
	{
		STableRow<TSharedPtr<FDatabaseAssetTreeNode>>::ChildSlot
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			GenerateItemWidget()
		];

		STableRow<TSharedPtr<FDatabaseAssetTreeNode>>::ConstructInternal(
			STableRow<TSharedPtr<FDatabaseAssetTreeNode>>::FArguments()
			.Style(FAppStyle::Get(), "DetailsView.TreeView.TableRow")
			.OnCanAcceptDrop(SkeletonView.Pin().Get(), &SDatabaseAssetTree::OnCanAcceptDrop)
			.OnAcceptDrop(SkeletonView.Pin().Get(), &SDatabaseAssetTree::OnAcceptDrop)
			.ShowSelection(true),
			OwnerTable);
	}

	void SDatabaseAssetListItem::ConstructAssetItem(const TSharedRef<STableViewBase>& OwnerTable)
	{
		STableRow<TSharedPtr<FDatabaseAssetTreeNode>>::Construct(
			STableRow<TSharedPtr<FDatabaseAssetTreeNode>>::FArguments()
			.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
			.OnCanAcceptDrop(SkeletonView.Pin().Get(), &SDatabaseAssetTree::OnCanAcceptDrop)
			.OnAcceptDrop(SkeletonView.Pin().Get(), &SDatabaseAssetTree::OnAcceptDrop)
			.ShowWires(false)
			.Content()
			[
				GenerateItemWidget()
			], OwnerTable);
	}

	void SDatabaseAssetListItem::OnAddSequence()
	{
		EditorViewModel.Pin()->AddSequenceToDatabase(nullptr);
		SkeletonView.Pin()->RefreshTreeView(false);
	}

	void SDatabaseAssetListItem::OnAddBlendSpace()
	{
		EditorViewModel.Pin()->AddBlendSpaceToDatabase(nullptr);
		SkeletonView.Pin()->RefreshTreeView(false);
	}

	void SDatabaseAssetListItem::OnAddAnimComposite()
	{
		EditorViewModel.Pin()->AddAnimCompositeToDatabase(nullptr);
		SkeletonView.Pin()->RefreshTreeView(false);
	}

	FText SDatabaseAssetListItem::GetName() const
	{
		TSharedPtr<FDatabaseAssetTreeNode> Node = WeakAssetTreeNode.Pin();
		TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin();

		if (const UPoseSearchDatabase* Database = ViewModel->GetPoseSearchDatabase())
		{
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetAnimationAssetBase(Node->SourceAssetIdx))
			{
				return FText::FromString(DatabaseAnimationAsset->GetName());
			}

			return FText::FromString(Database->GetName());
		}

		return LOCTEXT("None", "None");
	}

	TSharedRef<SWidget> SDatabaseAssetListItem::GenerateItemWidget()
	{
		TSharedPtr<FDatabaseAssetTreeNode> Node = WeakAssetTreeNode.Pin();
		TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin();
		UPoseSearchDatabase* Database = ViewModel->GetPoseSearchDatabase();
		
		TSharedPtr<SWidget> ItemWidget;
		const FDetailColumnSizeData& ColumnSizeData = SkeletonView.Pin()->GetColumnSizeData();
		
		if (Node->SourceAssetType == ESearchIndexAssetType::Invalid)
		{
			// it's a group
			SAssignNew(ItemWidget, SBorder)
			.BorderImage(this, &SDatabaseAssetListItem::GetGroupBackgroundImage)
			.Padding(FMargin(3.0f, 5.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(5.0f)
				.AutoWidth()
				[
					SNew(SExpanderArrow, STableRow<TSharedPtr<FDatabaseAssetTreeNode>>::SharedThis(this))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SRichTextBlock)
					.Text(this, &SDatabaseAssetListItem::GetName)
					.TransformPolicy(ETextTransformPolicy::ToUpper)
					.DecoratorStyleSet(&FAppStyle::Get())
					.TextStyle(FAppStyle::Get(), "DetailsView.CategoryTextStyle")
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.Padding(2, 0, 0, 0)
				[
					GenerateAddButtonWidget()
				]
			];
		}
		else
		{
			// Item Icon
			TSharedPtr<SImage> ItemIconWidget;
			if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetAnimationAssetBase(Node->SourceAssetIdx))
			{
				SAssignNew(ItemIconWidget, SImage)
					.Image(FSlateIconFinder::FindIconBrushForClass(DatabaseAnimationAsset->GetAnimationAssetStaticClass()));
			}

			// Setup table row to display 
			SAssignNew(ItemWidget, SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSplitter)
				.Style(FAppStyle::Get(), "FoliageEditMode.Splitter")
				.PhysicalSplitterHandleSize(1.0f)
				.HitDetectionSplitterHandleSize(5.0f)
				.HighlightedHandleIndex(ColumnSizeData.GetHoveredSplitterIndex())
				.MinimumSlotHeight(0.5f)
				
				// Asset Name with type icon
				+SSplitter::Slot()
				.Value(ColumnSizeData.GetNameColumnWidth())
				.MinSize(0.3f)
				.OnSlotResized(ColumnSizeData.GetOnNameColumnResized())
				[
					SNew(SHorizontalBox)
					.Clipping(EWidgetClipping::ClipToBounds)
					+ SHorizontalBox::Slot()
					.MaxWidth(18)
					.AutoWidth()
					.Padding(0.0f, 0.0f, 5.0f, 0.0f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						ItemIconWidget.ToSharedRef()
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SDatabaseAssetListItem::GetName)
						.ColorAndOpacity(this, &SDatabaseAssetListItem::GetNameTextColorAndOpacity)
					]
				]
				
				// Display information via icons
				+SSplitter::Slot()
				.Value(ColumnSizeData.GetValueColumnWidth())
				.MinSize(0.3f)
				.OnSlotResized(ColumnSizeData.GetOnValueColumnResized())
				[
					// Asset Info.

					// Looping
					SNew(SHorizontalBox)
					.Clipping(EWidgetClipping::ClipToBounds)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 1.0f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Graph.Node.Loop"))
						.ColorAndOpacity(this, &SDatabaseAssetListItem::GetLoopingColorAndOpacity)
						.ToolTipText(this, &SDatabaseAssetListItem::GetLoopingToolTip)
					]

					// Root Motion
					+ SHorizontalBox::Slot()
					.Padding(1.0f, 1.0f)
					.AutoWidth()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("AnimGraph.Attribute.RootMotionDelta.Icon"))
						.ColorAndOpacity(this, &SDatabaseAssetListItem::GetRootMotionColorAndOpacity)
						.ToolTipText(this, &SDatabaseAssetListItem::GetRootMotionOptionToolTip)
					]
					
					// Mirror Type
					+ SHorizontalBox::Slot()
					.Padding(1.0f, 1.0f)
					.AutoWidth()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(this, &SDatabaseAssetListItem::GetMirrorOptionSlateBrush)
						.ToolTipText(this, &SDatabaseAssetListItem::GetMirrorOptionToolTip)
					]
				]
			]
			
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.MaxWidth(18)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.EyeDropper"))
					.Visibility_Raw(this, &SDatabaseAssetListItem::GetSelectedActorIconVisbility)
				]
				+ SHorizontalBox::Slot()
				.MaxWidth(16)
				.Padding(4.0f, 0.0f)
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SDatabaseAssetListItem::GetAssetEnabledChecked)
					.OnCheckStateChanged(const_cast<SDatabaseAssetListItem*>(this), &SDatabaseAssetListItem::OnAssetIsEnabledChanged)
					.ToolTipText(this, &SDatabaseAssetListItem::GetAssetEnabledToolTip)
					.CheckedImage(FAppStyle::Get().GetBrush("Icons.Visible"))
					.CheckedHoveredImage(FAppStyle::Get().GetBrush("Icons.Visible"))
					.CheckedPressedImage(FAppStyle::Get().GetBrush("Icons.Visible"))
					.UncheckedImage(FAppStyle::Get().GetBrush("Icons.Hidden"))
					.UncheckedHoveredImage(FAppStyle::Get().GetBrush("Icons.Hidden"))
					.UncheckedPressedImage(FAppStyle::Get().GetBrush("Icons.Hidden"))
				]
			];
		}

		return ItemWidget.ToSharedRef();
	}

	TSharedRef<SWidget> SDatabaseAssetListItem::GenerateAddButtonWidget()
	{
		FMenuBuilder AddOptions(true, nullptr);

		AddOptions.AddMenuEntry(
			LOCTEXT("AddSequence", "Add Sequence"),
			LOCTEXT("AddSequenceTooltip", "Add new sequence to this group"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SDatabaseAssetListItem::OnAddSequence)),
			NAME_None,
			EUserInterfaceActionType::Button);

		AddOptions.AddMenuEntry(
			LOCTEXT("AddBlendSpaceOption", "Add Blend Space"),
			LOCTEXT("AddBlendSpaceOptionTooltip", "Add new blend space to this group"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SDatabaseAssetListItem::OnAddBlendSpace)),
			NAME_None,
			EUserInterfaceActionType::Button);

		AddOptions.AddMenuEntry(
			LOCTEXT("AnimCompositeOption", "Add Anim Composite"),
			LOCTEXT("AddAnimCompositeToDefaultGroupTooltip", "Add new anim composite to this group"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SDatabaseAssetListItem::OnAddAnimComposite)),
			NAME_None,
			EUserInterfaceActionType::Button);

		TSharedPtr<SComboButton> AddButton;
		SAssignNew(AddButton, SComboButton)
		.ContentPadding(0)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.HasDownArrow(false)
		.ButtonContent()
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
		]
		.MenuContent()
		[
			AddOptions.MakeWidget()
		];

		return AddButton.ToSharedRef();
	}


	const FSlateBrush* SDatabaseAssetListItem::GetGroupBackgroundImage() const
	{
		if (STableRow<TSharedPtr<FDatabaseAssetTreeNode>>::IsHovered())
		{
			return FAppStyle::Get().GetBrush("Brushes.Secondary");
		}
		else
		{
			return FAppStyle::Get().GetBrush("Brushes.Header");
		}
	}

	EVisibility SDatabaseAssetListItem::GetSelectedActorIconVisbility() const
	{
		TSharedPtr<FDatabaseViewModel> ViewModelPtr = EditorViewModel.Pin();
		TSharedPtr<FDatabaseAssetTreeNode> TreeNodePtr = WeakAssetTreeNode.Pin();
		if (const FPoseSearchIndexAsset* SelectedIndexAsset = ViewModelPtr->GetSelectedActorIndexAsset())
		{
			if (TreeNodePtr->SourceAssetType == ESearchIndexAssetType::Sequence &&
				TreeNodePtr->SourceAssetIdx == SelectedIndexAsset->SourceAssetIdx)
			{
				return EVisibility::Visible;
			}
		}

		return EVisibility::Hidden;
	}

	ECheckBoxState SDatabaseAssetListItem::GetAssetEnabledChecked() const
	{
		TSharedPtr<FDatabaseViewModel> ViewModelPtr = EditorViewModel.Pin();
		TSharedPtr<FDatabaseAssetTreeNode> TreeNodePtr = WeakAssetTreeNode.Pin();
		const UPoseSearchDatabase* Database = ViewModelPtr->GetPoseSearchDatabase();

		if (Database->AnimationAssets.IsValidIndex(TreeNodePtr->SourceAssetIdx))
		{
			if (ViewModelPtr->IsEnabled(TreeNodePtr->SourceAssetIdx))
			{
				return ECheckBoxState::Checked;
			}
		}

		return ECheckBoxState::Unchecked;
	}

	void SDatabaseAssetListItem::OnAssetIsEnabledChanged(ECheckBoxState NewCheckboxState)
	{
		const FScopedTransaction Transaction(LOCTEXT("EnableChangedForAssetInPoseSearchDatabase", "Update enabled flag for item from Pose Search Database"));

		const TSharedPtr<FDatabaseViewModel> ViewModelPtr = EditorViewModel.Pin();
		const TSharedPtr<FDatabaseAssetTreeNode> TreeNodePtr = WeakAssetTreeNode.Pin();

		ViewModelPtr->GetPoseSearchDatabase()->Modify();
		
		ViewModelPtr->SetIsEnabled(TreeNodePtr->SourceAssetIdx, NewCheckboxState == ECheckBoxState::Checked ? true : false);

		SkeletonView.Pin()->RefreshTreeView(false, true);
		ViewModelPtr->BuildSearchIndex();
	}

	FSlateColor SDatabaseAssetListItem::GetNameTextColorAndOpacity() const
	{
		return GetAssetEnabledChecked() == ECheckBoxState::Checked ? FLinearColor::White : DisabledColor;
	}

	FSlateColor SDatabaseAssetListItem::GetLoopingColorAndOpacity() const
	{
		const TSharedPtr<FDatabaseAssetTreeNode> Node = WeakAssetTreeNode.Pin();
		return Node->IsLooping() ? FLinearColor::White : DisabledColor;
	}

	FText SDatabaseAssetListItem::GetLoopingToolTip() const
	{
		const TSharedPtr<FDatabaseAssetTreeNode> Node = WeakAssetTreeNode.Pin();
		return Node->IsLooping() ? LOCTEXT("NodeLoopEnabledToolTip", "Looping") : LOCTEXT("NodeLoopDisabledToolTip", "Not looping");
	}

	FSlateColor SDatabaseAssetListItem::GetRootMotionColorAndOpacity() const
	{
		const TSharedPtr<FDatabaseAssetTreeNode> Node = WeakAssetTreeNode.Pin();
		return Node->IsRootMotionEnabled() ? FLinearColor::White : DisabledColor;
	}

	FText SDatabaseAssetListItem::GetRootMotionOptionToolTip() const
	{
		const TSharedPtr<FDatabaseAssetTreeNode> Node = WeakAssetTreeNode.Pin();
		return Node->IsRootMotionEnabled() ? LOCTEXT("NodeRootMotionEnabledToolTip", "Root motion enabled") : LOCTEXT("NodeRootMotionDisabledToolTip", "No root motion enabled");

	}
	const FSlateBrush* SDatabaseAssetListItem::GetMirrorOptionSlateBrush() const
	{
		const TSharedPtr<FDatabaseAssetTreeNode> Node = WeakAssetTreeNode.Pin();

		// TODO: Update icons when appropriate assets become available.
		switch (Node->GetMirrorOption())
		{
			case EPoseSearchMirrorOption::UnmirroredOnly: 
				return FAppStyle::Get().GetBrush("Icons.Minus");
			
			case EPoseSearchMirrorOption::MirroredOnly: 
				return FAppStyle::Get().GetBrush("Icons.Plus");
			
			case EPoseSearchMirrorOption::UnmirroredAndMirrored:
				return FAppStyle::Get().GetBrush("Icons.X");
			
			default:
				return nullptr;
		}
	}

	FText SDatabaseAssetListItem::GetMirrorOptionToolTip() const
	{
		const TSharedPtr<FDatabaseAssetTreeNode> Node = WeakAssetTreeNode.Pin();
		return FText::FromString(LOCTEXT("ToolTipMirrorOption", "Mirror Option: ").ToString() + (Node ? UEnum::GetDisplayValueAsText(Node->GetMirrorOption()).ToString() : LOCTEXT("ToolTipMirrorOption_Invalid", "Invalid").ToString()));
	}
	
	FText SDatabaseAssetListItem::GetAssetEnabledToolTip() const
	{
		if (GetAssetEnabledChecked() == ECheckBoxState::Checked)
		{
			return LOCTEXT("DisableAssetTooltip", "Disable this asset in the Pose Search Database.");
		}
		
		return LOCTEXT("EnableAssetTooltip", "Enable this asset in the Pose Search Database.");
	}

	SDatabaseAssetTree::~SDatabaseAssetTree()
	{
	}

	void SDatabaseAssetTree::Construct(
		const FArguments& InArgs, 
		TSharedRef<FDatabaseViewModel> InEditorViewModel)
	{
		EditorViewModel = InEditorViewModel;
		
		ColumnSizeData.SetValueColumnWidth(0.6f);

		CreateCommandList();

		ChildSlot
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(SPositiveActionButton)
					.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
					.Text(LOCTEXT("AddNew", "Add"))
					.ToolTipText(LOCTEXT("AddNewToolTip", "Add a new Sequence, Blend Space or Group"))
					.OnGetMenuContent(this, &SDatabaseAssetTree::CreateAddNewMenuWidget)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.Padding(2, 0, 0, 0)
				[
					GenerateFilterBoxWidget()
				]
			]
			+SVerticalBox::Slot()
			.Padding(0.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(2.0f)
				.BorderImage(FAppStyle::GetBrush("SCSEditor.TreePanel"))
				[
					SNew(SOverlay)
					+SOverlay::Slot()
					[
						SAssignNew(TreeView, STreeView<TSharedPtr<FDatabaseAssetTreeNode>>)
						.TreeItemsSource(&RootNodes)
						.SelectionMode(ESelectionMode::Multi)
						.OnGenerateRow(this, &SDatabaseAssetTree::MakeTableRowWidget)
						.OnGetChildren(this, &SDatabaseAssetTree::HandleGetChildrenForTree)
						.OnContextMenuOpening(this, &SDatabaseAssetTree::CreateContextMenu)
						.HighlightParentNodesForSelection(false)
						.OnSelectionChanged_Lambda([this](TSharedPtr<FDatabaseAssetTreeNode> Item, ESelectInfo::Type Type)
							{
								TArray<TSharedPtr<FDatabaseAssetTreeNode>> SelectedItems = TreeView->GetSelectedItems();
								OnSelectionChanged.Broadcast(SelectedItems, Type);
							})
						.ItemHeight(24)
					]
					+SOverlay::Slot()
					[
						SAssignNew(TreeViewDragAndDropSuggestion, SVerticalBox)
						+ SVerticalBox::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Drag and drop Animation Sequences, Anim Composites or Blendspaces")))
							.Font(FAppStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
						]
					]
				]
			]
		];

		RefreshTreeView(true);
	}

	FReply SDatabaseAssetTree::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
	{
		FReply Reply = FReply::Unhandled();

		TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

		const bool bValidOperation =
			Operation.IsValid() &&
			(Operation->IsOfType<FExternalDragOperation>() || Operation->IsOfType<FAssetDragDropOp>());
		if (bValidOperation)
		{
			Reply = AssetUtil::CanHandleAssetDrag(DragDropEvent);

			if (!Reply.IsEventHandled())
			{
				if (Operation->IsOfType<FAssetDragDropOp>())
				{
					const TSharedPtr<FAssetDragDropOp> AssetDragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(Operation);

					for (const FAssetData& AssetData : AssetDragDropOp->GetAssets())
					{
						if (UClass* AssetClass = AssetData.GetClass())
						{
							if (AssetClass->IsChildOf(UAnimSequence::StaticClass()) ||
								AssetClass->IsChildOf(UAnimComposite::StaticClass()) ||
								AssetClass->IsChildOf(UBlendSpace::StaticClass()))
							{
								Reply = FReply::Handled();
								break;
							}
						}
					}
				}
			}
		}

		return Reply;
	}

	FReply SDatabaseAssetTree::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
	{
		return OnAcceptDrop(DragDropEvent, EItemDropZone::OntoItem, nullptr);
	}

	FReply SDatabaseAssetTree::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
	{
		if (CommandList->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	bool SDatabaseAssetTree::MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const
	{
		// Ensure that we only react to modifications to the UPosesSearchDatabase.
		if (const TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin())
		{
			if (const UPoseSearchDatabase * Database = ViewModel->GetPoseSearchDatabase())
			{
				for (const TPair<UObject*, FTransactionObjectEvent>& TransactionObjectPair : TransactionObjectContexts)
				{
					const UObject* Object = TransactionObjectPair.Key;
					while (Object != nullptr)
					{
						if (Object == Database)
						{
							return true;
						}

						Object = Object->GetOuter();
					}
				}
			}
		}
		
		return false;
	}

	void SDatabaseAssetTree::PostUndo(bool bSuccess)
	{
		if (bSuccess)
		{
			FinalizeTreeChanges();
		}
	}

	void SDatabaseAssetTree::PostRedo(bool bSuccess)
	{
		if (bSuccess)
		{
			FinalizeTreeChanges();
		}
	}

	void SDatabaseAssetTree::RefreshTreeView(bool bIsInitialSetup, bool bRecoverSelection)
	{
		const TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin();
		if (!ViewModel.IsValid())
		{
			return;
		}

		const TSharedRef<FDatabaseViewModel> ViewModelRef = ViewModel.ToSharedRef();

		// Empty node data.
		RootNodes.Reset();
		AllNodes.Reset();

		const UPoseSearchDatabase* Database = ViewModel->GetPoseSearchDatabase();
		if (!IsValid(Database))
		{
			TreeView->RequestTreeRefresh();
			return;
		}

		// Store selection so we can recover it afterwards (if possible)
		TArray<TSharedPtr<FDatabaseAssetTreeNode>> PreviouslySelectedNodes = TreeView->GetSelectedItems();

		// Rebuild node hierarchy
		{
			// Setup default group node
			{
				const TSharedPtr<FDatabaseAssetTreeNode> DefaultGroupNode = MakeShared<FDatabaseAssetTreeNode>(
					INDEX_NONE,
					ESearchIndexAssetType::Invalid,
					ViewModelRef);
				AllNodes.Add(DefaultGroupNode);
				RootNodes.Add(DefaultGroupNode);
			}
			
			const int32 DefaultGroupIdx = RootNodes.Num() - 1;
			
			auto CreateAssetNode = [this, ViewModelRef](int32 AssetIdx, ESearchIndexAssetType AssetType, int32 GroupIdx)
			{
				// Create sequence node
				const TSharedPtr<FDatabaseAssetTreeNode> SequenceGroupNode = MakeShared<FDatabaseAssetTreeNode>(
					AssetIdx,
					AssetType,
					ViewModelRef);
				const TSharedPtr<FDatabaseAssetTreeNode>& ParentGroupNode = RootNodes[GroupIdx];

				// Setup hierarchy
				SequenceGroupNode->Parent = ParentGroupNode;
				ParentGroupNode->Children.Add(SequenceGroupNode);

				// Keep track of node
				AllNodes.Add(SequenceGroupNode);
			};
			
			// Build an index based off of alphabetical order than iterate the index instead
			TArray<uint32> IndexArray;
			IndexArray.SetNumUninitialized(Database->AnimationAssets.Num());
			for (int32 AnimationAssetIdx = 0; AnimationAssetIdx < Database->AnimationAssets.Num(); ++AnimationAssetIdx)
			{
				IndexArray[AnimationAssetIdx] = AnimationAssetIdx;
			}

			IndexArray.Sort([Database](int32 SequenceIdxA, int32 SequenceIdxB)
			{
				const FPoseSearchDatabaseAnimationAssetBase* A = Database->GetAnimationAssetBase(SequenceIdxA);
				const FPoseSearchDatabaseAnimationAssetBase* B = Database->GetAnimationAssetBase(SequenceIdxB);

				//If its null add it to the end of the list 
				if (!B->GetAnimationAsset())
				{
					return true;
				}

				if (!A->GetAnimationAsset())
				{
					return false;
				}

				const int32 Comparison = A->GetName().Compare(B->GetName());
				return Comparison < 0;
			});

			// create all nodes
			for (int32 AnimationAssetIdx = 0; AnimationAssetIdx < Database->AnimationAssets.Num(); ++AnimationAssetIdx)
			{
				const int32 MappedId = IndexArray[AnimationAssetIdx];

				if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetAnimationAssetBase(MappedId))
				{
					const bool bFiltered = (DatabaseAnimationAsset->GetAnimationAsset() == nullptr || GetAssetFilterString().IsEmpty()) ? false : !DatabaseAnimationAsset->GetName().Contains(GetAssetFilterString());

					if (!bFiltered)
					{
						CreateAssetNode(MappedId, DatabaseAnimationAsset->GetSearchIndexType(), DefaultGroupIdx);
					}
				}
			}

			// Show drag and drop suggestion if tree is empty
			TreeViewDragAndDropSuggestion->SetVisibility(IndexArray.IsEmpty() ? EVisibility::Visible : EVisibility::Hidden);
		}

		// Update tree view
		TreeView->RequestTreeRefresh();

		for (TSharedPtr<FDatabaseAssetTreeNode>& RootNode : RootNodes)
		{
			TreeView->SetItemExpansion(RootNode, true);
		}

		// Handle selection
		if (bRecoverSelection)
		{
			RecoverSelection(PreviouslySelectedNodes);
		}
		else
		{
			TreeView->SetItemSelection(PreviouslySelectedNodes, false, ESelectInfo::Direct);
		}
	}

	TSharedRef<ITableRow> SDatabaseAssetTree::MakeTableRowWidget(
		TSharedPtr<FDatabaseAssetTreeNode> InItem,
		const TSharedRef<STableViewBase>& OwnerTable)
	{
		return InItem->MakeTreeRowWidget(OwnerTable, InItem.ToSharedRef(), CommandList.ToSharedRef(), SharedThis(this));
	}

	void SDatabaseAssetTree::HandleGetChildrenForTree(
		TSharedPtr<FDatabaseAssetTreeNode> InNode,
		TArray<TSharedPtr<FDatabaseAssetTreeNode>>& OutChildren)
	{
		OutChildren = InNode.Get()->Children;
	}

	TOptional<EItemDropZone> SDatabaseAssetTree::OnCanAcceptDrop(
		const FDragDropEvent& DragDropEvent,
		EItemDropZone DropZone,
		TSharedPtr<FDatabaseAssetTreeNode> TargetItem)
	{
		TOptional<EItemDropZone> ReturnedDropZone;

		TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

		const bool bValidOperation = Operation.IsValid() && Operation->IsOfType<FAssetDragDropOp>();
		if (bValidOperation)
		{
			const TSharedPtr<FAssetDragDropOp> AssetDragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(Operation);

			for (const FAssetData& AssetData : AssetDragDropOp->GetAssets())
			{
				if (UClass* AssetClass = AssetData.GetClass())
				{
					if (AssetClass->IsChildOf(UAnimSequence::StaticClass()) ||
						AssetClass->IsChildOf(UAnimComposite::StaticClass()) ||
						AssetClass->IsChildOf(UBlendSpace::StaticClass()))
					{
						ReturnedDropZone = EItemDropZone::OntoItem;
						break;
					}
				}
			}
		}

		return ReturnedDropZone;
	}

	FReply SDatabaseAssetTree::OnAcceptDrop(
		const FDragDropEvent& DragDropEvent,
		EItemDropZone DropZone,
		TSharedPtr<FDatabaseAssetTreeNode> TargetItem)
	{
		const TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

		const bool bValidOperation = Operation.IsValid() && Operation->IsOfType<FAssetDragDropOp>();
		if (!bValidOperation)
		{
			return FReply::Unhandled();
		}

		const TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin();
		if (!ViewModel.IsValid())
		{
			return FReply::Unhandled();
		}

		TArray<FAssetData> DroppedAssetData = AssetUtil::ExtractAssetDataFromDrag(Operation);
		const int32 NumAssets = DroppedAssetData.Num();

		int32 AddedAssets = 0;
		if (NumAssets > 0)
		{
			GWarn->BeginSlowTask(LOCTEXT("LoadingAssets", "Loading Asset(s)"), true);

			{
				const FScopedTransaction Transaction(LOCTEXT("AddSequencesOrBlendspaces", "Add Sequence(s) and/or Blendspace(s) to Pose Search Database"));
				ViewModel->GetPoseSearchDatabase()->Modify();
				
				for (int32 DroppedAssetIdx = 0; DroppedAssetIdx < NumAssets; ++DroppedAssetIdx)
				{
					const FAssetData& AssetData = DroppedAssetData[DroppedAssetIdx];

					if (!AssetData.IsAssetLoaded())
					{
						GWarn->StatusUpdate(
							DroppedAssetIdx,
							NumAssets,
							FText::Format(
								LOCTEXT("LoadingAsset", "Loading Asset {0}"),
								FText::FromName(AssetData.AssetName)));
					}

					UClass* AssetClass = AssetData.GetClass();
					UObject* Asset = AssetData.GetAsset();
					
					if (AssetClass->IsChildOf(UAnimSequence::StaticClass()))
					{
						ViewModel->AddSequenceToDatabase(Cast<UAnimSequence>(Asset));
						++AddedAssets;
					}
					if (AssetClass->IsChildOf(UAnimComposite::StaticClass()))
					{
						ViewModel->AddAnimCompositeToDatabase(Cast<UAnimComposite>(Asset));
						++AddedAssets;
					}
					else if (AssetClass->IsChildOf(UBlendSpace::StaticClass()))
					{
						ViewModel->AddBlendSpaceToDatabase(Cast<UBlendSpace>(Asset));
						++AddedAssets;
					}
				}
			}
			
			GWarn->EndSlowTask();
		}

		if (AddedAssets == 0)
		{
			return FReply::Unhandled();
		}

		FinalizeTreeChanges(false);
		return FReply::Handled();
	}

	TSharedRef<SWidget> SDatabaseAssetTree::CreateAddNewMenuWidget()
	{
		FMenuBuilder AddOptions(true, nullptr);

		AddOptions.BeginSection("AddOptions", LOCTEXT("AssetAddOptions", "Assets"));
		{
			AddOptions.AddMenuEntry(
				LOCTEXT("AddSequenceOption", "Sequence"),
				LOCTEXT("AddSequenceOptionTooltip", "Add new sequence to the default group"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SDatabaseAssetTree::OnAddSequence, true)),
				NAME_None,
				EUserInterfaceActionType::Button);

			AddOptions.AddMenuEntry(
				LOCTEXT("BlendSpaceOption", "Blend Space"),
				LOCTEXT("AddBlendSpaceToDefaultGroupTooltip", "Add new blend space to the default group"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SDatabaseAssetTree::OnAddBlendSpace, true)),
				NAME_None,
				EUserInterfaceActionType::Button);

			AddOptions.AddMenuEntry(
				LOCTEXT("AnimCompositeOption", "Anim Composite"),
				LOCTEXT("AddAnimCompositeToDefaultGroupTooltip", "Add new anim composite to the default group"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SDatabaseAssetTree::OnAddAnimComposite, true)),
				NAME_None,
				EUserInterfaceActionType::Button);
		}
		AddOptions.EndSection();

		return AddOptions.MakeWidget();
	}

	TSharedPtr<SWidget> SDatabaseAssetTree::CreateContextMenu()
	{
		const bool CloseAfterSelection = true;
		FMenuBuilder MenuBuilder(CloseAfterSelection, CommandList);

		const TArray<TSharedPtr<FDatabaseAssetTreeNode>> SelectedNodes = TreeView->GetSelectedItems();
		if (!SelectedNodes.IsEmpty())
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("DeleteUngroup", "Delete / Remove"),
				LOCTEXT(
					"DeleteUngroupTooltip", 
					"Deletes groups and ungrouped assets; removes grouped assets from group."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SDatabaseAssetTree::OnDeleteNodes)),
				NAME_None,
				EUserInterfaceActionType::Button);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("Enable", "Enable"),
				LOCTEXT(
					"EnableTooltip",
					"Sets Assets Enabled."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SDatabaseAssetTree::OnEnableNodes)),
				NAME_None,
				EUserInterfaceActionType::Button);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("Disable", "Disable"),
				LOCTEXT(
					"DisableToolTip",
					"Sets Assets Disabled."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SDatabaseAssetTree::OnDisableNodes)),
				NAME_None,
				EUserInterfaceActionType::Button);
		}

		return MenuBuilder.MakeWidget();
	}

	TSharedRef<SWidget> SDatabaseAssetTree::GenerateFilterBoxWidget()
	{
		TSharedPtr<SSearchBox> SearchBox;
		SAssignNew(SearchBox, SSearchBox)
			.MinDesiredWidth(300.0f)
			.InitialText(this, &SDatabaseAssetTree::GetFilterText)
			.ToolTipText(FText::FromString(TEXT("Enter Asset Filter...")))
			.OnTextChanged(this, &SDatabaseAssetTree::OnAssetFilterTextCommitted, ETextCommit::Default)
			.OnTextCommitted(this, &SDatabaseAssetTree::OnAssetFilterTextCommitted);

		return SearchBox.ToSharedRef();
	}


	FText SDatabaseAssetTree::GetFilterText() const
	{
		return FText::FromString(GetAssetFilterString());
	}

	void SDatabaseAssetTree::OnAssetFilterTextCommitted(const FText& InText, ETextCommit::Type CommitInfo)
	{
		SetAssetFilterString(InText.ToString());
		RefreshTreeView(false);
	}


	void SDatabaseAssetTree::OnAddSequence(bool bFinalizeChanges)
	{
		FScopedTransaction Transaction(LOCTEXT("AddSequence", "Add Sequence"));
		const TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin();
		
		ViewModel->GetPoseSearchDatabase()->Modify();
		
		ViewModel->AddSequenceToDatabase(nullptr);

		if (bFinalizeChanges)
		{
			FinalizeTreeChanges();
		}
	}

	void SDatabaseAssetTree::OnAddBlendSpace(bool bFinalizeChanges)
	{
		FScopedTransaction Transaction(LOCTEXT("AddBlendSpaceTransaction", "Add Blend Space"));
		const TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin();
		
		ViewModel->GetPoseSearchDatabase()->Modify();
		
		ViewModel->AddBlendSpaceToDatabase(nullptr);

		if (bFinalizeChanges)
		{
			FinalizeTreeChanges();
		}
	}

	void SDatabaseAssetTree::OnAddAnimComposite(bool bFinalizeChanges)
	{
		FScopedTransaction Transaction(LOCTEXT("AddAnimCompositeTransaction", "Add Anim Composite"));

		EditorViewModel.Pin()->AddAnimCompositeToDatabase(nullptr);

		if (bFinalizeChanges)
		{
			FinalizeTreeChanges();
		}
	}

	void SDatabaseAssetTree::OnDeleteAsset(TSharedPtr<FDatabaseAssetTreeNode> Node, bool bFinalizeChanges)
	{
		FScopedTransaction Transaction(LOCTEXT("DeleteAsset", "Delete Asset"));
		const TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin();
		
		ViewModel->GetPoseSearchDatabase()->Modify();
		ViewModel->DeleteFromDatabase(Node->SourceAssetIdx);

		if (bFinalizeChanges)
		{
			FinalizeTreeChanges();
		}
	}

	void SDatabaseAssetTree::RegisterOnSelectionChanged(const FOnSelectionChanged& Delegate)
	{
		OnSelectionChanged.Add(Delegate);
	}

	void SDatabaseAssetTree::UnregisterOnSelectionChanged(void* Unregister)
	{
		OnSelectionChanged.RemoveAll(Unregister);
	}

	void SDatabaseAssetTree::RecoverSelection(const TArray<TSharedPtr<FDatabaseAssetTreeNode>>& PreviouslySelectedNodes)
	{
		TArray<TSharedPtr<FDatabaseAssetTreeNode>> NewSelectedNodes;

		for (const TSharedPtr<FDatabaseAssetTreeNode>& Node : AllNodes)
		{
			const bool bFoundNode = PreviouslySelectedNodes.ContainsByPredicate(
				[Node](const TSharedPtr<FDatabaseAssetTreeNode>& PrevSelectedNode)
			{
				return
					PrevSelectedNode->SourceAssetType == Node->SourceAssetType &&
					PrevSelectedNode->SourceAssetIdx == Node->SourceAssetIdx;
			});

			if (bFoundNode)
			{
				NewSelectedNodes.Add(Node);
			}
		}

		// @todo: investigate if we should call a TreeView->ClearSelection() before TreeView->SetItemSelection
		TreeView->SetItemSelection(NewSelectedNodes, true, ESelectInfo::Direct);
	}

	void SDatabaseAssetTree::CreateCommandList()
	{
		CommandList = MakeShared<FUICommandList>();

		CommandList->MapAction(
			FGenericCommands::Get().Delete,
			FUIAction(
				FExecuteAction::CreateSP(this, &SDatabaseAssetTree::OnDeleteNodes),
				FCanExecuteAction::CreateSP(this, &SDatabaseAssetTree::CanDeleteNodes)));
	}

	bool SDatabaseAssetTree::CanDeleteNodes() const
	{
		TArray<TSharedPtr<FDatabaseAssetTreeNode>> SelectedNodes = TreeView->GetSelectedItems();
		for (TSharedPtr<FDatabaseAssetTreeNode> SelectedNode : SelectedNodes)
		{
			if (SelectedNode->SourceAssetType != ESearchIndexAssetType::Invalid ||
				SelectedNode->SourceAssetIdx != INDEX_NONE)
			{
				return true;
			}
		}

		return false;
	}

	void SDatabaseAssetTree::OnDeleteNodes()
	{
		TArray<TSharedPtr<FDatabaseAssetTreeNode>> SelectedNodes = TreeView->GetSelectedItems();
		if (!SelectedNodes.IsEmpty())
		{
			const FScopedTransaction Transaction(LOCTEXT("DeletePoseSearchDatabaseNodes", "Delete selected items from Pose Search Database"));
			const TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin();

			ViewModel->GetPoseSearchDatabase()->Modify();
			
			SelectedNodes.Sort(
				[](const TSharedPtr<FDatabaseAssetTreeNode>& A, const TSharedPtr<FDatabaseAssetTreeNode>& B)
			{
				if (A->SourceAssetType != ESearchIndexAssetType::Invalid &&
					B->SourceAssetType == ESearchIndexAssetType::Invalid)
				{
					return true;
				}
				if (B->SourceAssetType != ESearchIndexAssetType::Invalid &&
					A->SourceAssetType == ESearchIndexAssetType::Invalid)
				{
					return false;
				}
				return B->SourceAssetIdx < A->SourceAssetIdx;
			});

			for (TSharedPtr<FDatabaseAssetTreeNode> SelectedNode : SelectedNodes)
			{
				if (SelectedNode->SourceAssetType != ESearchIndexAssetType::Invalid)
				{
					OnDeleteAsset(SelectedNode, false);
				}
			}
			
			FinalizeTreeChanges();
		}
	}

	void SDatabaseAssetTree::EnableSelectedNodes(bool bIsEnabled)
	{
		TArray<TSharedPtr<FDatabaseAssetTreeNode>> SelectedNodes = TreeView->GetSelectedItems();
		if (!SelectedNodes.IsEmpty())
		{
			const FText TransactionName = bIsEnabled ? LOCTEXT("EnablePoseSearchDatabaseNodes", "Enable selected items from Pose Search Database") : LOCTEXT("DisablePoseSearchDatabaseNodes", "Disable selected items from Pose Search Database");
			const FScopedTransaction Transaction(TransactionName);
			const TSharedPtr<FDatabaseViewModel> ViewModel = EditorViewModel.Pin();

			ViewModel->GetPoseSearchDatabase()->Modify();

			for (TSharedPtr<FDatabaseAssetTreeNode> SelectedNode : SelectedNodes)
			{
				ViewModel->SetIsEnabled(SelectedNode->SourceAssetIdx, bIsEnabled);
			}
		
			FinalizeTreeChanges();
		}
	}

	void SDatabaseAssetTree::FinalizeTreeChanges(bool bRecoverSelection)
	{
		RefreshTreeView(false, bRecoverSelection);
		EditorViewModel.Pin()->BuildSearchIndex();
	}
}

#undef LOCTEXT_NAMESPACE
