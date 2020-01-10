// Copyright Epic Games, Inc. All Rights Reserved.


#include "SAnimStreamableEditor.h"
#include "Animation/EditorAnimBaseObj.h"
#include "IDocumentation.h"

#include "SAnimNotifyPanel.h"
#include "Editor.h"

//////////////////////////////////////////////////////////////////////////
// SAnimCompositeEditor

TSharedRef<SWidget> SAnimStreamableEditor::CreateDocumentAnchor()
{
	return IDocumentation::Get()->CreateAnchor(TEXT("Engine/Animation/AnimationComposite")); // UPDATE TO STREAMABLE
}

void SAnimStreamableEditor::Construct(const FArguments& InArgs, const TSharedRef<class IPersonaPreviewScene>& InPreviewScene, const TSharedRef<class IEditableSkeleton>& InEditableSkeleton)
{
	bIsActiveTimerRegistered = false;
	StreamableAnim = InArgs._StreamableAnim;
	check(StreamableAnim);

	SAnimEditorBase::Construct( SAnimEditorBase::FArguments()
		.OnObjectsSelected(InArgs._OnObjectsSelected), 
		InPreviewScene );

	if(GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	/*EditorPanels->AddSlot()
		.AutoHeight()
		.Padding(0, 10)
		[
			SAssignNew( AnimCompositePanel, SAnimCompositePanel )
			.Composite(CompositeObj)
			.CompositeEditor(SharedThis(this))
			.WidgetWidth(S2ColumnWidget::DEFAULT_RIGHT_COLUMN_WIDTH)
			.ViewInputMin(this, &SAnimEditorBase::GetViewMinInput)
			.ViewInputMax(this, &SAnimEditorBase::GetViewMaxInput)
			.OnSetInputViewRange(this, &SAnimEditorBase::SetInputViewRange)
		];*/

	EditorPanels->AddSlot()
		.AutoHeight()
		.Padding(0, 10)
		[
			SAssignNew( AnimNotifyPanel, SAnimNotifyPanel, InEditableSkeleton )
			.Sequence(StreamableAnim)
			.WidgetWidth(S2ColumnWidget::DEFAULT_RIGHT_COLUMN_WIDTH)
			.InputMin(this, &SAnimEditorBase::GetMinInput)
			.InputMax(this, &SAnimEditorBase::GetMaxInput)
			.ViewInputMin(this, &SAnimEditorBase::GetViewMinInput)
			.ViewInputMax(this, &SAnimEditorBase::GetViewMaxInput)
			.OnSetInputViewRange(this, &SAnimEditorBase::SetInputViewRange)
			.OnGetScrubValue(this, &SAnimEditorBase::GetScrubValue)
			.OnSelectionChanged(this, &SAnimEditorBase::OnSelectionChanged)
			.OnInvokeTab(InArgs._OnInvokeTab)
		];

	EditorPanels->AddSlot()
		.AutoHeight()
		.Padding(0, 10)
		[
			SAssignNew( AnimCurvePanel, SAnimCurvePanel, InEditableSkeleton)
			.Sequence(StreamableAnim)
			.WidgetWidth(S2ColumnWidget::DEFAULT_RIGHT_COLUMN_WIDTH)
			.ViewInputMin(this, &SAnimEditorBase::GetViewMinInput)
			.ViewInputMax(this, &SAnimEditorBase::GetViewMaxInput)
			.InputMin(this, &SAnimEditorBase::GetMinInput)
			.InputMax(this, &SAnimEditorBase::GetMaxInput)
			.OnSetInputViewRange(this, &SAnimEditorBase::SetInputViewRange)
			.OnGetScrubValue(this, &SAnimEditorBase::GetScrubValue)
		];
}

SAnimStreamableEditor::~SAnimStreamableEditor()
{
	if(GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SAnimStreamableEditor::PreAnimUpdate()
{
	StreamableAnim->Modify();
}

void SAnimStreamableEditor::PostAnimUpdate()
{
	StreamableAnim->MarkPackageDirty();
	//SortAndUpdateComposite();
}

void SAnimStreamableEditor::RebuildPanel()
{
	//SortAndUpdateComposite();
	//AnimCompositePanel->Update();
}

void SAnimStreamableEditor::OnStreamableChange(class UObject *EditorAnimBaseObj, bool bRebuild)
{
	if (StreamableAnim != nullptr )
	{
		if(bRebuild && !bIsActiveTimerRegistered)
		{
			// sometimes crashes because the timer delay but animation still renders, so invalidating here before calling timer
			bIsActiveTimerRegistered = true;
			RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAnimStreamableEditor::TriggerRebuildPanel));
		} 

		StreamableAnim->MarkPackageDirty();
	}
}

void SAnimStreamableEditor::PostUndo( bool bSuccess )
{
	PostUndoRedo();
}

void SAnimStreamableEditor::PostRedo( bool bSuccess )
{
	PostUndoRedo();
}

void SAnimStreamableEditor::PostUndoRedo()
{
	if (!bIsActiveTimerRegistered)
	{
		bIsActiveTimerRegistered = true;
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAnimStreamableEditor::TriggerRebuildPanel));
	}
}

void SAnimStreamableEditor::InitDetailsViewEditorObject(UEditorAnimBaseObj* EdObj)
{
	EdObj->InitFromAnim(StreamableAnim, FOnAnimObjectChange::CreateSP( SharedThis(this), &SAnimStreamableEditor::OnStreamableChange ));
}

EActiveTimerReturnType SAnimStreamableEditor::TriggerRebuildPanel(double InCurrentTime, float InDeltaTime)
{
	// we should not update any property related within PostEditChange, 
	// so this is deferred to Tick, when it needs to rebuild, just mark it and this will update in the next tick
	RebuildPanel();

	bIsActiveTimerRegistered = false;
	return EActiveTimerReturnType::Stop;
}

float SAnimStreamableEditor::CalculateSequenceLengthOfEditorObject() const
{
	return StreamableAnim->SequenceLength;
}
