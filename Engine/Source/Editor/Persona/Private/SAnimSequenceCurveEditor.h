// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAnimSequenceCurveEditor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "EditorUndoClient.h"

class FCurveEditor;
class UAnimSequenceBase;
class ITimeSliderController;
class SCurveEditorTree;
class IPersonaPreviewScene;
class SCurveEditorPanel;
class FTabManager;

class SAnimSequenceCurveEditor : public IAnimSequenceCurveEditor, public FEditorUndoClient
{
	SLATE_BEGIN_ARGS(SAnimSequenceCurveEditor) {}

	SLATE_ARGUMENT(TSharedPtr<ITimeSliderController>, ExternalTimeSliderController)

	SLATE_ARGUMENT(TSharedPtr<FTabManager>, TabManager)

	SLATE_END_ARGS()

	SAnimSequenceCurveEditor();
	~SAnimSequenceCurveEditor();

	void Construct(const FArguments& InArgs, const TSharedRef<IPersonaPreviewScene>& InPreviewScene, UAnimSequenceBase* InAnimSequence);

	/** IAnimSequenceCurveEditor interface */
	virtual void ResetCurves() override;
	virtual void AddCurve(const FText& InCurveDisplayName, const FLinearColor& InCurveColor, const FSmartName& InName, ERawCurveTrackTypes InType, int32 InCurveIndex, FSimpleDelegate InOnCurveModified) override;
	virtual void RemoveCurve(const FSmartName& InName, ERawCurveTrackTypes InType, int32 InCurveIndex) override;
	virtual void ZoomToFit() override;
	
	virtual void PostUndo(bool bSuccess) override { PostUndoRedo(); }
	virtual void PostRedo(bool bSuccess) override { PostUndoRedo(); }

private:
	// Build the toolbar for this curve editor
	TSharedRef<SWidget> MakeToolbar(TSharedRef<SCurveEditorPanel> InEditorPanel);

	// Handle undo/redo to check underlying curve data is still valid
	void PostUndoRedo();

private:
	/** The actual curve editor */
	TSharedPtr<FCurveEditor> CurveEditor;

	/** The search widget for filtering curves in the Curve Editor tree. */
	TSharedPtr<SWidget> CurveEditorSearchBox;

	/** The anim sequence we are editing */
	UAnimSequenceBase* AnimSequence;

	/** The tree widget int he curve editor */
	TSharedPtr<SCurveEditorTree> CurveEditorTree;
};