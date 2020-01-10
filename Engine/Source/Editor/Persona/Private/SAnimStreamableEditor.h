// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SAnimCurvePanel.h"
#include "SAnimEditorBase.h"
#include "Animation/AnimStreamable.h"

class SAnimNotifyPanel;

//////////////////////////////////////////////////////////////////////////
// SAnimCompositeEditor

/** Overall animation composite editing widget. This mostly contains functions for editing the UAnimComposite.

	SAnimCompositeEditor will create the SAnimCompositePanel which is mostly responsible for setting up the UI 
	portion of the composite tool and registering callbacks to the SAnimCompositeEditor to do the actual editing.
	
*/
class SAnimStreamableEditor : public SAnimEditorBase, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SAnimStreamableEditor)
		: _StreamableAnim(NULL)
		{}

		SLATE_ARGUMENT( UAnimStreamable*, StreamableAnim)
		SLATE_EVENT(FOnObjectsSelected, OnObjectsSelected)
		SLATE_EVENT(FOnInvokeTab, OnInvokeTab)

	SLATE_END_ARGS()

private:
	/** Slate editor panels */
	TSharedPtr<class SAnimNotifyPanel> AnimNotifyPanel;
	TSharedPtr<class SAnimCurvePanel>	AnimCurvePanel;

	/** do I have to update the panel **/
	bool bRebuildPanel;
	void RebuildPanel();

	/** Handler for when composite is edited in details view */
	void OnStreamableChange(class UObject *EditorAnimBaseObj, bool bRebuild);

protected:
	virtual void InitDetailsViewEditorObject(class UEditorAnimBaseObj* EdObj) override;

public:
	void Construct(const FArguments& InArgs, const TSharedRef<class IPersonaPreviewScene>& InPreviewScene, const TSharedRef<class IEditableSkeleton>& InEditableSkeleton);

	~SAnimStreamableEditor();

	/** Return the animation composite being edited */
	UAnimStreamable* GetStreamableAnimObj() const { return StreamableAnim; }
	virtual UAnimationAsset* GetEditorObject() const override { return GetStreamableAnimObj(); }

private:
	/** Pointer to the animation composite being edited */
	UAnimStreamable* StreamableAnim;
	
	/** FEditorUndoClient interface */
	virtual void PostUndo( bool bSuccess ) override;
	virtual void PostRedo( bool bSuccess ) override;
	void PostUndoRedo();

	virtual float CalculateSequenceLengthOfEditorObject() const override;

	/** One-off active timer to trigger a panel rebuild */
	EActiveTimerReturnType TriggerRebuildPanel(double InCurrentTime, float InDeltaTime);

	/** Whether the active timer to trigger a panel rebuild is currently registered */
	bool bIsActiveTimerRegistered;

public:

	/** delegate handlers for when the composite is being editor */
	void			PreAnimUpdate();
	void			PostAnimUpdate();

	//~ Begin SAnimEditorBase Interface
	virtual TSharedRef<SWidget> CreateDocumentAnchor() override;
	//~ End SAnimEditorBase Interface
};
