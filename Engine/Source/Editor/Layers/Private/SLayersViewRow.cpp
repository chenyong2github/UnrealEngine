// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLayersViewRow.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#include "EditorStyleSet.h"
#include "SceneOutlinerDragDrop.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "LayersView"

void SLayersViewRow::Construct(const FArguments& InArgs, TSharedRef< FLayerViewModel > InViewModel, TSharedRef< STableViewBase > InOwnerTableView)
{
	ViewModel = InViewModel;

	HighlightText = InArgs._HighlightText;

	SMultiColumnTableRow< TSharedPtr< FLayerViewModel > >::Construct(FSuperRowType::FArguments().OnDragDetected(InArgs._OnDragDetected), InOwnerTableView);
}

SLayersViewRow::~SLayersViewRow()
{
	ViewModel->OnRenamedRequest().Remove(EnterEditingModeDelegateHandle);
}

TSharedRef< SWidget > SLayersViewRow::GenerateWidgetForColumn(const FName& ColumnID)
{
	TSharedPtr< SWidget > TableRowContent;

	if (ColumnID == LayersView::ColumnID_LayerLabel)
	{
		TableRowContent =
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 1.0f, 3.0f, 1.0f)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("Layer.Icon16x")))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]

		+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(InlineTextBlock, SInlineEditableTextBlock)
				.Font(FEditorStyle::GetFontStyle("LayersView.LayerNameFont"))
				.Text(ViewModel.Get(), &FLayerViewModel::GetNameAsText)
				.ColorAndOpacity(this, &SLayersViewRow::GetColorAndOpacity)
				.HighlightText(HighlightText)
				.ToolTipText(LOCTEXT("DoubleClickToolTip", "Double Click to Select All Actors"))
				.OnVerifyTextChanged(this, &SLayersViewRow::OnRenameLayerTextChanged)
				.OnTextCommitted(this, &SLayersViewRow::OnRenameLayerTextCommitted)
				.IsSelected(this, &SLayersViewRow::IsSelectedExclusively)
			]
		;

		EnterEditingModeDelegateHandle = ViewModel->OnRenamedRequest().AddSP(InlineTextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
	}
	else if (ColumnID == LayersView::ColumnID_Visibility)
	{
		TableRowContent =
			SAssignNew(VisibilityButton, SButton)
			.ContentPadding(0)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.OnClicked(this, &SLayersViewRow::OnToggleVisibility)
			.ToolTipText(LOCTEXT("VisibilityButtonToolTip", "Toggle Layer Visibility"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Content()
			[
				SNew(SImage)
				.Image(this, &SLayersViewRow::GetVisibilityBrushForLayer)
			]
		;
	}
	else if (ColumnID == LayersView::ColumnID_ActorsLoading)
	{
		TableRowContent =
			SAssignNew(ActorsLoadingButton, SButton)
			.ContentPadding(0)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.OnClicked(this, &SLayersViewRow::OnToggleActorsLoading)
			.ToolTipText(LOCTEXT("ActorsLoadingButtonToolTip", "Toggle Actors Loading"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Content()
			[
				SNew(SImage)
				.Image(this, &SLayersViewRow::GetActorsLoadingBrushForLayer)
			]
		;
	}
	else
	{
		checkf(false, TEXT("Unknown ColumnID provided to SLayersView"));
	}

	return TableRowContent.ToSharedRef();
}

void SLayersViewRow::OnRenameLayerTextCommitted(const FText& InText, ETextCommit::Type eInCommitType)
{
	if (!InText.IsEmpty())
	{
		ViewModel->RenameTo(*InText.ToString());
	}
}

bool SLayersViewRow::OnRenameLayerTextChanged(const FText& NewText, FText& OutErrorMessage)
{
	FString OutMessage;
	if (!ViewModel->CanRenameTo(*NewText.ToString(), OutMessage))
	{
		OutErrorMessage = FText::FromString(OutMessage);
		return false;
	}

	return true;
}

void SLayersViewRow::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr< FSceneOutlinerDragDropOp > DragOp = DragDropEvent.GetOperationAs< FSceneOutlinerDragDropOp >();
	if (DragOp.IsValid())
	{
		TSharedPtr< FActorDragDropOp > ActorDragOp = DragOp->GetSubOp<FActorDragDropOp>();
		if (ActorDragOp.IsValid())
		{
			ActorDragOp->ResetToDefaultToolTip();
		}
	}
}

FReply SLayersViewRow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr< FSceneOutlinerDragDropOp > DragOp = DragDropEvent.GetOperationAs< FSceneOutlinerDragDropOp >();
	if (DragOp.IsValid())
	{
		TSharedPtr< FActorDragDropOp > ActorDragOp = DragOp->GetSubOp<FActorDragDropOp>();
		if (ActorDragOp.IsValid())
		{
			bool bCanAssign = false;
			FText Message;
			if (ActorDragOp->Actors.Num() > 1)
			{
				bCanAssign = ViewModel->CanAssignActors(ActorDragOp->Actors, OUT Message);
			}
			else
			{
				bCanAssign = ViewModel->CanAssignActor(ActorDragOp->Actors[0], OUT Message);
			}

			if (bCanAssign)
			{
				ActorDragOp->SetToolTip(Message, FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK")));
			}
			else
			{
				ActorDragOp->SetToolTip(Message, FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error")));
			}

			FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SLayersViewRow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr< FSceneOutlinerDragDropOp > DragOp = DragDropEvent.GetOperationAs< FSceneOutlinerDragDropOp >();
	if (DragOp.IsValid())
	{
		TSharedPtr< FActorDragDropOp > ActorDragOp = DragOp->GetSubOp<FActorDragDropOp>();
		if (ActorDragOp.IsValid())
		{
			ViewModel->AddActors(ActorDragOp->Actors);
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FSlateColor SLayersViewRow::GetColorAndOpacity() const
{
	if (!FSlateApplication::Get().IsDragDropping())
	{
		return FSlateColor::UseForeground();
	}

	bool bCanAcceptDrop = false;
	TSharedPtr<FDragDropOperation> DragDropOp = FSlateApplication::Get().GetDragDroppingContent();

	if (DragDropOp.IsValid() && DragDropOp->IsOfType<FSceneOutlinerDragDropOp>())
	{
		TSharedPtr<FSceneOutlinerDragDropOp> DragOp = StaticCastSharedPtr<FSceneOutlinerDragDropOp>(DragDropOp);
		if (DragOp.IsValid())
		{
			TSharedPtr< FActorDragDropOp > ActorDragOp = DragOp->GetSubOp<FActorDragDropOp>();
			if (ActorDragOp.IsValid())
			{
				FText Message;
				bCanAcceptDrop = ViewModel->CanAssignActors(ActorDragOp->Actors, OUT Message);
			}
		}
	}

	return (bCanAcceptDrop) ? FSlateColor::UseForeground() : FLinearColor(0.30f, 0.30f, 0.30f);
}

const FSlateBrush* SLayersViewRow::GetVisibilityBrushForLayer() const
{
	if (ViewModel->IsVisible())
	{
		return IsHovered() ? FEditorStyle::GetBrush("Level.VisibleHighlightIcon16x") :
			FEditorStyle::GetBrush("Level.VisibleIcon16x");
	}
	else
	{
		return IsHovered() ? FEditorStyle::GetBrush("Level.NotVisibleHighlightIcon16x") :
			FEditorStyle::GetBrush("Level.NotVisibleIcon16x");
	}
}

const FSlateBrush* SLayersViewRow::GetActorsLoadingBrushForLayer() const
{
	if (ViewModel->ShouldLoadActors())
	{
		return FEditorStyle::GetBrush("SessionBrowser.StatusRunning");
	}
	else
	{
		return FEditorStyle::GetBrush("SessionBrowser.StatusTimedOut");
	}
}

#undef LOCTEXT_NAMESPACE
