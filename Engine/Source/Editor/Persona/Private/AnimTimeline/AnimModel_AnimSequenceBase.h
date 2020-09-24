// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimModel.h"
#include "PersonaDelegates.h"
#include "SAnimTimingPanel.h"
#include "EditorUndoClient.h"

class UAnimSequenceBase;
class FAnimTimelineTrack_Notifies;
class FAnimTimelineTrack_Curves;
class FAnimTimelineTrack;
class FAnimTimelineTrack_NotifiesPanel;
enum class EFrameNumberDisplayFormats : uint8;

/** Anim model for an anim sequence base */
class FAnimModel_AnimSequenceBase : public FAnimModel, public FEditorUndoClient
{
public:
	FAnimModel_AnimSequenceBase(const TSharedRef<IPersonaPreviewScene>& InPreviewScene, const TSharedRef<IEditableSkeleton>& InEditableSkeleton, const TSharedRef<FUICommandList>& InCommandList, UAnimSequenceBase* InAnimSequenceBase);

	~FAnimModel_AnimSequenceBase();

	/** FAnimModel interface */
	virtual void RefreshTracks() override;
	virtual UAnimSequenceBase* GetAnimSequenceBase() const override;
	virtual void Initialize() override;
	virtual void UpdateRange() override;

	/** FEditorUndoClient interface */
	virtual void PostUndo(bool bSuccess) override { HandleUndoRedo(); }
	virtual void PostRedo(bool bSuccess) override { HandleUndoRedo(); }

	const TSharedPtr<FAnimTimelineTrack_Notifies>& GetNotifyRoot() const { return NotifyRoot; }

	/** Delegate used to edit curves */
	FOnEditCurves OnEditCurves;

	/** Delegate used to edit curves */
	FOnStopEditingCurves OnStopEditingCurves;

	/** Notify track timing options */
	bool IsNotifiesTimingElementDisplayEnabled(ETimingElementType::Type ElementType) const;
	void ToggleNotifiesTimingElementDisplayEnabled(ETimingElementType::Type ElementType);

	/** 
	 * Clamps the sequence to the specified length 
	 * @return		Whether clamping was/is necessary
	 */
	virtual bool ClampToEndTime(float NewEndTime);
	
	/** Refresh any simple snap times */
	virtual void RefreshSnapTimes();

protected:
	/** Refresh notify tracks */
	void RefreshNotifyTracks();

	/** Refresh curve tracks */
	void RefreshCurveTracks();

private:
	/** UI handlers */
	void EditSelectedCurves();
	bool CanEditSelectedCurves() const;
	void RemoveSelectedCurves();
	void SetDisplayFormat(EFrameNumberDisplayFormats InFormat);
	bool IsDisplayFormatChecked(EFrameNumberDisplayFormats InFormat) const;
	void ToggleDisplayPercentage();
	bool IsDisplayPercentageChecked() const;
	void ToggleDisplaySecondary();
	bool IsDisplaySecondaryChecked() const;
	void HandleUndoRedo();

private:
	/** The anim sequence base we wrap */
	UAnimSequenceBase* AnimSequenceBase;

	/** Root track for notifies */
	TSharedPtr<FAnimTimelineTrack_Notifies> NotifyRoot;

	/** Legacy notify panel track */
	TSharedPtr<FAnimTimelineTrack_NotifiesPanel> NotifyPanel;

	/** Root track for curves */
	TSharedPtr<FAnimTimelineTrack_Curves> CurveRoot;

	/** Root track for additive layers */
	TSharedPtr<FAnimTimelineTrack> AdditiveRoot;

	/** Display flags for notifies track */
	bool NotifiesTimingElementNodeDisplayFlags[ETimingElementType::Max];
};