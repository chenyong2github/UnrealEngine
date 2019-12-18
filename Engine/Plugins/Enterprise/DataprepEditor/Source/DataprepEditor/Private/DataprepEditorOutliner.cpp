// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepEditor.h"

#include "ICustomSceneOutliner.h"
#include "ISceneOutlinerColumn.h"
#include "SceneOutlinerModule.h"

#include "Modules/ModuleManager.h"
#include "SceneOutlinerModule.h"
#include "ScopedTransaction.h"
#include "Widgets/SDataprepEditorViewport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "DataprepEditor"

namespace DataprepEditorSceneOutlinerUtils
{
	/** Specifics for the scene outliner */
	using namespace SceneOutliner;

	/**
	 * This struct is used to force the scene outliner to refuse any rename request
	 */
	struct FCanRenameItem : public TTreeItemGetter<bool>
	{
		virtual bool Get(const SceneOutliner::FActorTreeItem& ActorItem) const override { return false; };
		virtual bool Get(const SceneOutliner::FWorldTreeItem& WorldItem) const override { return false; }
		virtual bool Get(const SceneOutliner::FFolderTreeItem& FolderItem) const override { return false; }
		virtual bool Get(const SceneOutliner::FComponentTreeItem& ComponentItem) const override { return false; }
		virtual bool Get(const SceneOutliner::FSubComponentTreeItem& SubComponentItem) const override { return false; }
	};

	/**
	 * Use this struct to match the scene outliers selection to a dataprep editor selection
	 */
	struct FSynchroniseSelectionToSceneOutliner : public TTreeItemGetter<bool>
	{
		FSynchroniseSelectionToSceneOutliner(TSharedRef<FDataprepEditor> InDataprepEditor)
			: DataprepEditorPtr(InDataprepEditor)
		{
		};

		virtual bool Get(const FActorTreeItem& ActorItem) const override
		{
			if (const FDataprepEditor* DataprepEditor = DataprepEditorPtr.Pin().Get())
			{
				return DataprepEditor->GetWorldItemsSelection().Contains(ActorItem.Actor);
			}
			return false;
		}

		virtual bool Get(const FWorldTreeItem& WorldItem) const override
		{
			return false;
		}
		virtual bool Get(const FFolderTreeItem& FolderItem) const override
		{
			return false;
		}
		virtual bool Get(const FComponentTreeItem& ComponentItem) const override
		{
			if (const FDataprepEditor* DataprepEditor = DataprepEditorPtr.Pin().Get())
			{
				return DataprepEditor->GetWorldItemsSelection().Contains(ComponentItem.Component);
			}
			return false;
		}
		virtual bool Get(const FSubComponentTreeItem& SubComponentItem) const override
		{
			// return this for now as it seams that subcomponent Item is broken or doesn't do what want
			return false;
		}

	private:
		TWeakPtr<FDataprepEditor> DataprepEditorPtr;
	};


	/**
	 * Use this struct to get the selection from the scene outliner
	 */
	struct FGetSelectionFromSceneOutliner : public ITreeItemVisitor
	{
		mutable TSet<TWeakObjectPtr<UObject>> Selection;

		virtual void Visit(const FActorTreeItem& ActorItem) const override
		{
			Selection.Add(ActorItem.Actor);
		}

		virtual void Visit(const FWorldTreeItem& WorldItem) const override {}
		virtual void Visit(const FFolderTreeItem& FolderItem) const override {}
		virtual void Visit(const FComponentTreeItem& ComponentItem) const override
		{
			this->Selection.Add(ComponentItem.Component);
		}

		virtual void Visit(const FSubComponentTreeItem& SubComponentItem) const override {}
	};

	struct FGetVisibilityVisitor : TTreeItemGetter<bool>
	{
		/** Map of tree item to visibility */
		mutable TMap<const ITreeItem*, bool> VisibilityInfo;

		bool RecurseChildren(const ITreeItem& Item) const
		{
			if (const bool* Info = VisibilityInfo.Find(&Item))
			{
				return *Info;
			}
			else
			{
				bool bIsVisible = false;
				for (const auto& ChildPtr : Item.GetChildren())
				{
					auto Child = ChildPtr.Pin();
					if (Child.IsValid() && Child->Get(*this))
					{
						bIsVisible = true;
						break;
					}
				}
				VisibilityInfo.Add(&Item, bIsVisible);

				return bIsVisible;
			}
		}

		bool Get(const FActorTreeItem& ActorItem) const
		{
			if (const bool* Info = VisibilityInfo.Find(&ActorItem))
			{
				return *Info;
			}
			else
			{
				const AActor* Actor = ActorItem.Actor.Get();

				const bool bIsVisible = Actor && !Actor->IsTemporarilyHiddenInEditor(true);
				VisibilityInfo.Add(&ActorItem, bIsVisible);

				return bIsVisible;
			}
		}

		bool Get(const FWorldTreeItem& WorldItem) const
		{
			return RecurseChildren(WorldItem);
		}

		bool Get(const FFolderTreeItem& FolderItem) const
		{
			return RecurseChildren(FolderItem);
		}
	};

	struct FSetVisibilityVisitor : IMutableTreeItemVisitor
	{
		/** Whether this item should be visible or not */
		const bool bSetVisibility;
		TWeakPtr<SDataprepEditorViewport> Viewport;

		FSetVisibilityVisitor(bool bInSetVisibility, TWeakPtr<SDataprepEditorViewport> InViewport)
			: bSetVisibility(bInSetVisibility)
			, Viewport(InViewport)
		{
		}

		virtual void Visit(FActorTreeItem& ActorItem) const override
		{
			AActor* Actor = ActorItem.Actor.Get();
			if (Actor)
			{
				// Save the actor to the transaction buffer to support undo/redo, but do
				// not call Modify, as we do not want to dirty the actor's package and
				// we're only editing temporary, transient values
				SaveToTransactionBuffer(Actor, false);
				Actor->SetIsTemporarilyHiddenInEditor(!bSetVisibility);

				Viewport.Pin()->SetActorVisibility(Actor, bSetVisibility);

				// Apply the same visibility to the actors children
				for (auto& ChildPtr : ActorItem.GetChildren())
				{
					auto Child = ChildPtr.Pin();
					if (Child.IsValid())
					{
						FSetVisibilityVisitor Visibility(bSetVisibility, Viewport);
						Child->Visit(Visibility);
					}
				}
			}
		}

		virtual void Visit(FWorldTreeItem& WorldItem) const override
		{
			for (auto& ChildPtr : WorldItem.GetChildren())
			{
				auto Child = ChildPtr.Pin();
				if (Child.IsValid())
				{
					FSetVisibilityVisitor Visibility(bSetVisibility, Viewport);
					Child->Visit(Visibility);
				}
			}
		}

		virtual void Visit(FFolderTreeItem& FolderItem) const override
		{
			for (auto& ChildPtr : FolderItem.GetChildren())
			{
				auto Child = ChildPtr.Pin();
				if (Child.IsValid())
				{
					FSetVisibilityVisitor Visibility(bSetVisibility, Viewport);
					Child->Visit(Visibility);
				}
			}
		}
	};

	class FVisibilityDragDropOp : public FDragDropOperation, public TSharedFromThis<FVisibilityDragDropOp>
	{
	public:

		DRAG_DROP_OPERATOR_TYPE(FVisibilityDragDropOp, FDragDropOperation)

		/** Flag which defines whether to hide destination actors or not */
		bool bHidden;

		/** Undo transaction stolen from the gutter which is kept alive for the duration of the drag */
		TUniquePtr<FScopedTransaction> UndoTransaction;

		/** The widget decorator to use */
		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNullWidget::NullWidget;
		}

		/** Create a new drag and drop operation out of the specified flag */
		static TSharedRef<FVisibilityDragDropOp> New(const bool _bHidden, TUniquePtr<FScopedTransaction>& ScopedTransaction)
		{
			TSharedRef<FVisibilityDragDropOp> Operation = MakeShareable(new FVisibilityDragDropOp);

			Operation->bHidden = _bHidden;
			Operation->UndoTransaction = MoveTemp(ScopedTransaction);

			Operation->Construct();
			return Operation;
		}
	};

	class FPreviewSceneOutlinerGutter : public ISceneOutlinerColumn
	{
	public:
		FPreviewSceneOutlinerGutter(ISceneOutliner& Outliner, TWeakPtr<SDataprepEditorViewport> InViewport)
		{
			WeakSceneViewport = InViewport;
			WeakOutliner = StaticCastSharedRef<ISceneOutliner>(Outliner.AsShared());
		}

		virtual ~FPreviewSceneOutlinerGutter() {}

		static FName GetID() { return FName("PreviewGutter"); }

		virtual void Tick(double InCurrentTime, float InDeltaTime) override
		{
			VisibilityCache.VisibilityInfo.Empty();
		}

		virtual FName GetColumnID() override { return GetID(); }

		virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override
		{
			return SHeaderRow::Column(GetColumnID())
				.FixedWidth(16.f)
				[
					SNew(SSpacer)
				];
		}

		virtual const TSharedRef<SWidget> ConstructRowWidget(FTreeItemRef TreeItem, const STableRow<FTreeItemPtr>& Row) override;

		virtual void SortItems(TArray<FTreeItemPtr>& RootItems, const EColumnSortMode::Type SortMode) const override {}

		/** Check whether the specified item is visible */
		bool IsItemVisible(const ITreeItem& Item) { return Item.Get(VisibilityCache); }

		TWeakPtr<SDataprepEditorViewport> GetViewport() const
		{
			return WeakSceneViewport;
		}

	private:
		/** Weak pointer back to the scene outliner - required for setting visibility on current selection. */
		TWeakPtr<ISceneOutliner> WeakOutliner;

		TWeakPtr<SDataprepEditorViewport> WeakSceneViewport;

		/** Visitor used to get (and cache) visibilty for items. Cahced per-frame to avoid expensive recursion. */
		FGetVisibilityVisitor VisibilityCache;
	};

	/** Widget responsible for managing the visibility for a single actor */
	class SVisibilityWidget : public SImage
	{
	public:
		SLATE_BEGIN_ARGS(SVisibilityWidget) {}
		SLATE_END_ARGS()

		/** Construct this widget */
		void Construct(const FArguments& InArgs, TWeakPtr<FPreviewSceneOutlinerGutter> InWeakColumn, TWeakPtr<ISceneOutliner> InWeakOutliner, TWeakPtr<ITreeItem> InWeakTreeItem)
		{
			WeakTreeItem = InWeakTreeItem;
			WeakOutliner = InWeakOutliner;
			WeakColumn = InWeakColumn;

			SImage::Construct(
				SImage::FArguments()
				.Image(this, &SVisibilityWidget::GetBrush)
			);
		}

	private:

		/** Start a new drag/drop operation for this widget */
		virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				return FReply::Handled().BeginDragDrop(FVisibilityDragDropOp::New(!IsVisible(), UndoTransaction));
			}
			else
			{
				return FReply::Unhandled();
			}
		}

		/** If a visibility drag drop operation has entered this widget, set its actor to the new visibility state */
		virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
		{
			auto VisibilityOp = DragDropEvent.GetOperationAs<FVisibilityDragDropOp>();
			if (VisibilityOp.IsValid())
			{
				SetIsVisible(!VisibilityOp->bHidden);
			}
		}

		FReply HandleClick()
		{
			auto Outliner = WeakOutliner.Pin();
			auto TreeItem = WeakTreeItem.Pin();
			auto Column = WeakColumn.Pin();

			if (!Outliner.IsValid() || !TreeItem.IsValid() || !Column.IsValid())
			{
				return FReply::Unhandled();
			}

			// Open an undo transaction
			UndoTransaction.Reset(new FScopedTransaction(LOCTEXT("SetActorVisibility", "Set Actor Visibility")));

			const auto& Tree = Outliner->GetTree();

			const bool bVisible = !IsVisible();

			// We operate on all the selected items if the specified item is selected
			if (Tree.IsItemSelected(TreeItem.ToSharedRef()))
			{
				const FSetVisibilityVisitor Visitor(bVisible, WeakColumn.Pin()->GetViewport());

				for (auto& SelectedItem : Tree.GetSelectedItems())
				{
					if (IsVisible(SelectedItem, Column) != bVisible)
					{
						SelectedItem->Visit(Visitor);
					}
				}

				GEditor->RedrawAllViewports();
			}
			else
			{
				SetIsVisible(bVisible);
			}

			return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
		}

		virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override
		{
			return HandleClick();
		}

		/** Called when the mouse button is pressed down on this widget */
		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}

			return HandleClick();
		}

		/** Process a mouse up message */
		virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				UndoTransaction.Reset();
				return FReply::Handled();
			}

			return FReply::Unhandled();
		}

		/** Called when this widget had captured the mouse, but that capture has been revoked for some reason. */
		virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
		{
			UndoTransaction.Reset();
		}

		/** Get the brush for this widget */
		const FSlateBrush* GetBrush() const
		{
			if (IsVisible())
			{
				static const FName NAME_VisibleHoveredBrush = TEXT("Level.VisibleHighlightIcon16x");
				static const FName NAME_VisibleNotHoveredBrush = TEXT("Level.VisibleIcon16x");
				return IsHovered() ? FEditorStyle::GetBrush(NAME_VisibleHoveredBrush) :
					FEditorStyle::GetBrush(NAME_VisibleNotHoveredBrush);
			}
			else
			{
				static const FName NAME_NotVisibleHoveredBrush = TEXT("Level.NotVisibleHighlightIcon16x");
				static const FName NAME_NotVisibleNotHoveredBrush = TEXT("Level.NotVisibleIcon16x");
				return IsHovered() ? FEditorStyle::GetBrush(NAME_NotVisibleHoveredBrush) :
					FEditorStyle::GetBrush(NAME_NotVisibleNotHoveredBrush);
			}
		}

		/** Check if the specified item is visible */
		static bool IsVisible(const FTreeItemPtr& Item, const TSharedPtr<FPreviewSceneOutlinerGutter>& Column)
		{
			return Column.IsValid() && Item.IsValid() ? Column->IsItemVisible(*Item) : false;
		}

		/** Check if our wrapped tree item is visible */
		bool IsVisible() const
		{
			return IsVisible(WeakTreeItem.Pin(), WeakColumn.Pin());
		}

		/** Set the actor this widget is responsible for to be hidden or shown */
		void SetIsVisible(const bool bVisible)
		{
			TSharedPtr<ITreeItem> TreeItem = WeakTreeItem.Pin();
			TSharedPtr<ISceneOutliner> Outliner = WeakOutliner.Pin();

			if (TreeItem.IsValid() && Outliner.IsValid() && IsVisible() != bVisible)
			{
				FSetVisibilityVisitor Visitor(bVisible, WeakColumn.Pin()->GetViewport());
				TreeItem->Visit(Visitor);

				Outliner->Refresh();

				GEditor->RedrawAllViewports();
			}
		}

		/** The tree item we relate to */
		TWeakPtr<ITreeItem> WeakTreeItem;

		/** Reference back to the outliner so we can set visibility of a whole selection */
		TWeakPtr<ISceneOutliner> WeakOutliner;

		/** Weak pointer back to the column */
		TWeakPtr<FPreviewSceneOutlinerGutter> WeakColumn;

		/** Scoped undo transaction */
		TUniquePtr<FScopedTransaction> UndoTransaction;
	};

	const TSharedRef<SWidget> FPreviewSceneOutlinerGutter::ConstructRowWidget(FTreeItemRef TreeItem, const STableRow<FTreeItemPtr>& Row)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SVisibilityWidget, SharedThis(this), WeakOutliner, TreeItem)
			];
	}

	/** End of specifics for the scene outliner */
}

void FDataprepEditor::CreateScenePreviewTab()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	SceneOutliner::FInitializationOptions SceneOutlinerOptions;
	SceneOutlinerOptions.SpecifiedWorldToDisplay = PreviewWorld.Get();

	SceneOutliner = SceneOutlinerModule.CreateCustomSceneOutliner(SceneOutlinerOptions);

	// Add our custom visibility gutter

	auto CreateColumn = [this](ISceneOutliner& Outliner)
	{
		return TSharedRef< ISceneOutlinerColumn >(MakeShareable(new DataprepEditorSceneOutlinerUtils::FPreviewSceneOutlinerGutter(Outliner, SceneViewportView.ToSharedRef())));
	};

	SceneOutliner::FColumnInfo ColumnInfo;
	ColumnInfo.Visibility = SceneOutliner::EColumnVisibility::Visible;
	ColumnInfo.PriorityIndex = 0;
	ColumnInfo.Factory.BindLambda([&](ISceneOutliner& Outliner)
	{
		return TSharedRef< ISceneOutlinerColumn >(MakeShareable(new DataprepEditorSceneOutlinerUtils::FPreviewSceneOutlinerGutter(Outliner, SceneViewportView.ToSharedRef())));
	});

	SceneOutliner->AddColumn(DataprepEditorSceneOutlinerUtils::FPreviewSceneOutlinerGutter::GetID(), ColumnInfo);

	// Add the default outliner columns
	const SceneOutliner::FSharedOutlinerData& SharedData = SceneOutliner->GetSharedData();
	for (auto& DefaultColumn : SceneOutlinerModule.DefaultColumnMap)
	{
		if (!DefaultColumn.Value.ValidMode.IsSet() || SharedData.Mode == DefaultColumn.Value.ValidMode.GetValue())
		{
			SceneOutliner->AddColumn(DefaultColumn.Key, DefaultColumn.Value.ColumnInfo);
		}
	}

	SceneOutliner->SetSelectionMode(ESelectionMode::Multi)
		.SetCanRenameItem(MakeUnique<DataprepEditorSceneOutlinerUtils::FCanRenameItem>())
		.SetShouldSelectItemWhenAdded(MakeUnique<DataprepEditorSceneOutlinerUtils::FSynchroniseSelectionToSceneOutliner>(StaticCastSharedRef<FDataprepEditor>(AsShared())))
		.SetShowActorComponents(false)
		.SetShownOnlySelected(false)
		.SetShowOnlyCurrentLevel(false)
		.SetHideTemporaryActors(false);

	SceneOutliner->GetOnItemSelectionChanged().AddSP(this, &FDataprepEditor::OnSceneOutlinerSelectionChanged);

	SAssignNew(ScenePreviewView, SBorder)
		.Padding(2.f)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SceneOutliner.ToSharedRef()
			]
		];
}

void FDataprepEditor::OnSceneOutlinerSelectionChanged(SceneOutliner::FTreeItemPtr ItemPtr, ESelectInfo::Type SelectionMode)
{
	using namespace SceneOutliner;

	DataprepEditorSceneOutlinerUtils::FGetSelectionFromSceneOutliner Visitor;

	for (FTreeItemPtr Item : SceneOutliner->GetTree().GetSelectedItems())
	{
		Item->Visit(Visitor);
	}

	SetWorldObjectsSelection(MoveTemp(Visitor.Selection), EWorldSelectionFrom::SceneOutliner);
}

void FDataprepEditor::SetWorldObjectsSelection(TSet<TWeakObjectPtr<UObject>>&& NewSelection, EWorldSelectionFrom SelectionFrom /* = EWorldSelectionFrom::Unknow */)
{
	WorldItemsSelection.Empty(NewSelection.Num());
	WorldItemsSelection.Append(MoveTemp(NewSelection));

	if (SelectionFrom != EWorldSelectionFrom::SceneOutliner)
	{
		DataprepEditorSceneOutlinerUtils::FSynchroniseSelectionToSceneOutliner Selector(StaticCastSharedRef<FDataprepEditor>(AsShared()));
		SceneOutliner->SetSelection(Selector);
	}

	if (SelectionFrom != EWorldSelectionFrom::Viewport)
	{
		TArray<AActor*> Actors;
		Actors.Reserve(WorldItemsSelection.Num());

		for (TWeakObjectPtr<UObject> ObjectPtr : WorldItemsSelection)
		{
			if (AActor* Actor = Cast<AActor>(ObjectPtr.Get()))
			{
				Actors.Add(Actor);
			}
		}

		SceneViewportView->SelectActors(Actors);
	}

	{
		TSet<UObject*> Objects;
		Objects.Reserve(WorldItemsSelection.Num());
		for (const TWeakObjectPtr<UObject>& ObjectPtr : WorldItemsSelection)
		{
			Objects.Add(ObjectPtr.Get());
		}

		SetDetailsObjects(Objects, false);
	}
}

#undef LOCTEXT_NAMESPACE
