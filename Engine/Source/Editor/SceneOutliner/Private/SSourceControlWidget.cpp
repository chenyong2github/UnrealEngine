// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlWidget.h"

void SSourceControlWidget::Construct(const FArguments& InArgs, TSharedPtr<FSceneOutlinerTreeItemSCC> InItemSourceControl)
{
	check(InItemSourceControl.IsValid());

	ItemSourceControl = InItemSourceControl;

	ItemSourceControl->OnSourceControlStateChanged.BindSP(this, &SSourceControlWidget::UpdateSourceControlStateIcon);

	SImage::Construct(
		SImage::FArguments()
		.ColorAndOpacity(this, &SSourceControlWidget::GetForegroundColor)
		.Image(FStyleDefaults::GetNoBrush()));

	UpdateSourceControlStateIcon(ItemSourceControl->GetSourceControlState());
}

FReply SSourceControlWidget::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	check(ItemSourceControl);

	FSourceControlStatePtr SourceControlState = ItemSourceControl->RefreshSourceControlState();
	UpdateSourceControlStateIcon(SourceControlState);
	return FReply::Handled();
}

void SSourceControlWidget::UpdateSourceControlStateIcon(FSourceControlStatePtr SourceControlState)
{
	if(SourceControlState.IsValid())
	{
		FSlateIcon Icon = SourceControlState->GetIcon();
		
		SetFromSlateIcon(Icon);
	}
	else
	{
		SetImage(nullptr);
		RemoveAllLayers();
	}
}
