// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "RenderPage/RenderPageCollection.h"
#include "Styling/AppStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"


class SCheckBox;
class SSearchBox;
class URenderPage;
class URenderPagesMoviePipelineRenderJob;

namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	DECLARE_DELEGATE_RetVal_TwoParams(FText, FOnRenderPagesPageListEditableTextBlockTextCommitted, const FText&, ETextCommit::Type);


	/**
	 * A widget with which the user can see and modify the list of pages the render page collection contains.
	 */
	class SRenderPagesPageList : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesPageList) {}
		SLATE_END_ARGS()

		virtual void Tick(const FGeometry&, const double, const float) override;
		void Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor);

		/** Refreshes the content of this widget. */
		void Refresh();

		/** Refreshes the state of the header is-page-enabled checkbox. */
		void RefreshHeaderEnabledCheckbox();

	private:
		/** Gets called when a page is created. */
		void OnRenderPageCreated(URenderPage* Page);

		/** Gets called when the header is-page-enabled checkbox is toggled. */
		void OnHeaderCheckboxToggled(ECheckBoxState State);

		/** Gets the desired state of the header is-page-enabled checkbox. */
		ECheckBoxState GetDesiredHeaderEnabledCheckboxState();

		/** Adds the render status column to the page list. */
		void AddRenderStatusColumn();

		/** Removes the render status column from the page list. */
		void RemoveRenderStatusColumn();

	private:
		void OnBatchRenderingStarted(URenderPagesMoviePipelineRenderJob* RenderJob) { Refresh(); }
		void OnBatchRenderingFinished(URenderPagesMoviePipelineRenderJob* RenderJob) { Refresh(); }
		void OnSearchBarTextChanged(const FText& Text) { Refresh(); }

	private:
		/** Callback for generating a row widget in the session tree view. */
		TSharedRef<ITableRow> HandlePagesCollectionGenerateRow(URenderPage* Item, const TSharedRef<STableViewBase>& OwnerTable);

		/** Callback for session tree view selection changes. */
		void HandlePagesCollectionSelectionChanged(URenderPage* Item, ESelectInfo::Type SelectInfo);

	private:
		/** A reference to the BP Editor that owns this collection. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** The render page collection that is shown in the UI. */
		TWeakObjectPtr<URenderPageCollection> RenderPagesCollectionWeakPtr;

		/** The render pages that are shown in the UI. */
		TArray<URenderPage*> RenderPages;

		/** The widget that lists the render pages. */
		TSharedPtr<SListView<URenderPage*>> RenderPageListWidget;

		/** The search bar widget. */
		TSharedPtr<SSearchBox> RenderPagesSearchBox;

		/** The header checkbox for the enable/disable column. */
		TSharedPtr<SCheckBox> RenderPageEnabledHeaderCheckbox;
	};


	/**
	 * The widget that represents a single render page (a single row).
	 */
	class SRenderPagesPageListTableRow : public SMultiColumnTableRow<URenderPage*>
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesPageListTableRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakPtr<IRenderPageCollectionEditor> InBlueprintEditor, URenderPage* InRenderPage, const TSharedPtr<SRenderPagesPageList>& InPageListWidget);
		TOptional<EItemDropZone> OnCanAcceptDrop(const FDragDropEvent& Event, EItemDropZone InItemDropZone, URenderPage* Page);
		FReply OnAcceptDrop(const FDragDropEvent& Event, EItemDropZone InItemDropZone, URenderPage* Page);
		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;
		FText GetRenderStatusText() const;

	private:
		/** A reference to the BP Editor that owns this collection. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** A reference to the render page model. */
		TObjectPtr<URenderPage> RenderPage;

		/** A reference to the page list (the parent widget). */
		TSharedPtr<SRenderPagesPageList> PageListWidget;
	};


	/**
	 * The class that makes it possible to drag and drop render pages (allowing the user to reorganize the render pages list).
	 */
	class FRenderPagesPageListTableRowDragDropOp final : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FRenderPagesPageListTableRowDragDropOp, FDragDropOperation)

		using WidgetType = SRenderPagesPageListTableRow;
		using HeldItemType = TObjectPtr<URenderPage>;

		FRenderPagesPageListTableRowDragDropOp(const TSharedPtr<WidgetType> InWidget, const HeldItemType InPage)
			: Page(InPage)
		{
			DecoratorWidget = SNew(SBorder)
				.Padding(0.f)
				.BorderImage(FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.Border"))
				.Content()
				[
					InWidget.ToSharedRef()
				];
		}

		HeldItemType GetPage() const
		{
			return Page;
		}

		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return DecoratorWidget;
		}

	private:
		/** The held item. */
		HeldItemType Page;

		/** Holds the displayed widget. */
		TSharedPtr<SWidget> DecoratorWidget;
	};
}
