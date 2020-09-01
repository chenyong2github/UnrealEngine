// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimModel_AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "AnimTimelineTrack.h"
#include "AnimTimelineTrack_Notifies.h"
#include "AnimTimelineTrack_NotifiesPanel.h"
#include "AnimTimelineTrack_Curves.h"
#include "AnimTimelineTrack_Curve.h"
#include "AnimTimelineTrack_FloatCurve.h"
#include "AnimTimelineTrack_VectorCurve.h"
#include "AnimTimelineTrack_TransformCurve.h"
#include "AnimSequenceTimelineCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "IAnimationEditor.h"
#include "Preferences/PersonaOptions.h"
#include "FrameNumberDisplayFormat.h"
#include "Framework/Commands/GenericCommands.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimPreviewInstance.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "FAnimModel_AnimSequence"

FAnimModel_AnimSequenceBase::FAnimModel_AnimSequenceBase(const TSharedRef<IPersonaPreviewScene>& InPreviewScene, const TSharedRef<IEditableSkeleton>& InEditableSkeleton, const TSharedRef<FUICommandList>& InCommandList, UAnimSequenceBase* InAnimSequenceBase)
	: FAnimModel(InPreviewScene, InEditableSkeleton, InCommandList)
	, AnimSequenceBase(InAnimSequenceBase)
{
	SnapTypes.Add(FAnimModel::FSnapType::Frames.Type, FAnimModel::FSnapType::Frames);
	SnapTypes.Add(FAnimModel::FSnapType::Notifies.Type, FAnimModel::FSnapType::Notifies);

	UpdateRange();

	// Clear display flags
	for(bool& bElementNodeDisplayFlag : NotifiesTimingElementNodeDisplayFlags)
	{
		bElementNodeDisplayFlag = false;
	}

	AnimSequenceBase->RegisterOnAnimTrackCurvesChanged(UAnimSequenceBase::FOnAnimTrackCurvesChanged::CreateRaw(this, &FAnimModel_AnimSequenceBase::RefreshTracks));
	AnimSequenceBase->RegisterOnNotifyChanged(UAnimSequenceBase::FOnNotifyChanged::CreateRaw(this, &FAnimModel_AnimSequenceBase::RefreshSnapTimes));
	
	if(GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
}

FAnimModel_AnimSequenceBase::~FAnimModel_AnimSequenceBase()
{
	if(GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}

	AnimSequenceBase->UnregisterOnNotifyChanged(this);
	AnimSequenceBase->UnregisterOnAnimTrackCurvesChanged(this);
}

void FAnimModel_AnimSequenceBase::Initialize()
{
	TSharedRef<FUICommandList> CommandList = WeakCommandList.Pin().ToSharedRef();

	const FAnimSequenceTimelineCommands& Commands = FAnimSequenceTimelineCommands::Get();

	CommandList->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateLambda([this]
		{
			SelectedTracks.Array()[0]->RequestRename();
		}),
		FCanExecuteAction::CreateLambda([this]
		{
			return (SelectedTracks.Num() > 0) && (SelectedTracks.Array()[0]->CanRename());
		})
	);

	CommandList->MapAction(
		Commands.EditSelectedCurves,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::EditSelectedCurves),
		FCanExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::CanEditSelectedCurves));

	CommandList->MapAction(
		Commands.RemoveSelectedCurves,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::RemoveSelectedCurves));

	CommandList->MapAction(
		Commands.DisplayFrames,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::SetDisplayFormat, EFrameNumberDisplayFormats::Frames),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FAnimModel_AnimSequenceBase::IsDisplayFormatChecked, EFrameNumberDisplayFormats::Frames));

	CommandList->MapAction(
		Commands.DisplaySeconds,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::SetDisplayFormat, EFrameNumberDisplayFormats::Seconds),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FAnimModel_AnimSequenceBase::IsDisplayFormatChecked, EFrameNumberDisplayFormats::Seconds));

	CommandList->MapAction(
		Commands.DisplayPercentage,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::ToggleDisplayPercentage),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FAnimModel_AnimSequenceBase::IsDisplayPercentageChecked));

	CommandList->MapAction(
		Commands.DisplaySecondaryFormat,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::ToggleDisplaySecondary),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FAnimModel_AnimSequenceBase::IsDisplaySecondaryChecked));

	CommandList->MapAction(
		Commands.SnapToFrames,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::ToggleSnap, FAnimModel::FSnapType::Frames.Type),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FAnimModel_AnimSequenceBase::IsSnapChecked, FAnimModel::FSnapType::Frames.Type), 
		FIsActionButtonVisible::CreateSP(this, &FAnimModel_AnimSequenceBase::IsSnapAvailable, FAnimModel::FSnapType::Frames.Type));

	CommandList->MapAction(
		Commands.SnapToNotifies,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::ToggleSnap, FAnimModel::FSnapType::Notifies.Type),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FAnimModel_AnimSequenceBase::IsSnapChecked, FAnimModel::FSnapType::Notifies.Type), 
		FIsActionButtonVisible::CreateSP(this, &FAnimModel_AnimSequenceBase::IsSnapAvailable, FAnimModel::FSnapType::Notifies.Type));

	CommandList->MapAction(
		Commands.SnapToCompositeSegments,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::ToggleSnap, FAnimModel::FSnapType::CompositeSegment.Type),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FAnimModel_AnimSequenceBase::IsSnapChecked, FAnimModel::FSnapType::CompositeSegment.Type),
		FIsActionButtonVisible::CreateSP(this, &FAnimModel_AnimSequenceBase::IsSnapAvailable, FAnimModel::FSnapType::CompositeSegment.Type));

	CommandList->MapAction(
		Commands.SnapToMontageSections,
		FExecuteAction::CreateSP(this, &FAnimModel_AnimSequenceBase::ToggleSnap, FAnimModel::FSnapType::MontageSection.Type),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FAnimModel_AnimSequenceBase::IsSnapChecked, FAnimModel::FSnapType::MontageSection.Type),
		FIsActionButtonVisible::CreateSP(this, &FAnimModel_AnimSequenceBase::IsSnapAvailable, FAnimModel::FSnapType::MontageSection.Type));
}

void FAnimModel_AnimSequenceBase::RefreshTracks()
{
	ClearTrackSelection();

	// Clear all tracks
	RootTracks.Empty();

	// Add notifies
	RefreshNotifyTracks();

	// Add curves
	RefreshCurveTracks();

	// Snaps
	RefreshSnapTimes();

	// Tell the UI to refresh
	OnTracksChangedDelegate.Broadcast();

	UpdateRange();
}

UAnimSequenceBase* FAnimModel_AnimSequenceBase::GetAnimSequenceBase() const 
{
	return AnimSequenceBase;
}

void FAnimModel_AnimSequenceBase::RefreshNotifyTracks()
{
	AnimSequenceBase->InitializeNotifyTrack();

	if(!NotifyRoot.IsValid())
	{
		// Add a root track for notifies & then the main 'panel' legacy track
		NotifyRoot = MakeShared<FAnimTimelineTrack_Notifies>(SharedThis(this));
	}

	NotifyRoot->ClearChildren();
	RootTracks.Add(NotifyRoot.ToSharedRef());

	if(!NotifyPanel.IsValid())
	{
		NotifyPanel = MakeShared<FAnimTimelineTrack_NotifiesPanel>(SharedThis(this));
		NotifyRoot->SetAnimNotifyPanel(NotifyPanel.ToSharedRef());
	}

	NotifyRoot->AddChild(NotifyPanel.ToSharedRef());
}

void FAnimModel_AnimSequenceBase::RefreshCurveTracks()
{
	if(!CurveRoot.IsValid())
	{
		// Add a root track for curves
		CurveRoot = MakeShared<FAnimTimelineTrack_Curves>(SharedThis(this));
	}

	CurveRoot->ClearChildren();
	RootTracks.Add(CurveRoot.ToSharedRef());

	// Next add a track for each float curve
	for(FFloatCurve& FloatCurve : AnimSequenceBase->RawCurveData.FloatCurves)
	{
		CurveRoot->AddChild(MakeShared<FAnimTimelineTrack_FloatCurve>(FloatCurve, SharedThis(this)));
	}

	UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimSequenceBase);
	if (AnimSequence)
	{
		if(!AdditiveRoot.IsValid())
		{
			// Add a root track for additive layers
			AdditiveRoot = MakeShared<FAnimTimelineTrack>(LOCTEXT("AdditiveLayerTrackList_Title", "Additive Layer Tracks"), LOCTEXT("AdditiveLayerTrackList_Tooltip", "Additive modifications to bone transforms"), SharedThis(this), true);
		}

		AdditiveRoot->ClearChildren();
		RootTracks.Add(AdditiveRoot.ToSharedRef());

		// Next add a track for each transform curve
		for(FTransformCurve& TransformCurve : AnimSequence->RawCurveData.TransformCurves)
		{
			TSharedRef<FAnimTimelineTrack_TransformCurve> TransformCurveTrack = MakeShared<FAnimTimelineTrack_TransformCurve>(TransformCurve, SharedThis(this));
			TransformCurveTrack->SetExpanded(false);
			AdditiveRoot->AddChild(TransformCurveTrack);

			FText TransformName = FAnimTimelineTrack_TransformCurve::GetTransformCurveName(AsShared(), TransformCurve.Name);
			FLinearColor TransformColor = TransformCurve.GetColor();
			FLinearColor XColor = FLinearColor::Red;
			FLinearColor YColor = FLinearColor::Green;
			FLinearColor ZColor = FLinearColor::Blue;
			FText XName = LOCTEXT("VectorXTrackName", "X");
			FText YName = LOCTEXT("VectorYTrackName", "Y");
			FText ZName = LOCTEXT("VectorZTrackName", "Z");
			
			FText VectorFormat = LOCTEXT("TransformVectorFormat", "{0}.{1}");
			FText TranslationName = LOCTEXT("TransformTranslationTrackName", "Translation");
			TSharedRef<FAnimTimelineTrack_VectorCurve> TranslationCurveTrack = MakeShared<FAnimTimelineTrack_VectorCurve>(TransformCurve.TranslationCurve, TransformCurve.Name, 0, ERawCurveTrackTypes::RCT_Transform, TranslationName, FText::Format(VectorFormat, TransformName, TranslationName), TransformColor, SharedThis(this));
			TranslationCurveTrack->SetExpanded(false);
			TransformCurveTrack->AddChild(TranslationCurveTrack);			

			FText ComponentFormat = LOCTEXT("TransformComponentFormat", "{0}.{1}.{2}");
			TranslationCurveTrack->AddChild(MakeShared<FAnimTimelineTrack_Curve>(TransformCurve.TranslationCurve.FloatCurves[0], TransformCurve.Name, 0, ERawCurveTrackTypes::RCT_Transform, XName, FText::Format(ComponentFormat, TransformName, TranslationName, XName), XColor, XColor, SharedThis(this)));
			TranslationCurveTrack->AddChild(MakeShared<FAnimTimelineTrack_Curve>(TransformCurve.TranslationCurve.FloatCurves[1], TransformCurve.Name, 1, ERawCurveTrackTypes::RCT_Transform, YName, FText::Format(ComponentFormat, TransformName, TranslationName, YName), YColor, YColor, SharedThis(this)));
			TranslationCurveTrack->AddChild(MakeShared<FAnimTimelineTrack_Curve>(TransformCurve.TranslationCurve.FloatCurves[2], TransformCurve.Name, 2, ERawCurveTrackTypes::RCT_Transform, ZName, FText::Format(ComponentFormat, TransformName, TranslationName, ZName), ZColor, ZColor, SharedThis(this)));

			FText RollName = LOCTEXT("RotationRollTrackName", "Roll");
			FText PitchName = LOCTEXT("RotationPitchTrackName", "Pitch");
			FText YawName = LOCTEXT("RotationYawTrackName", "Yaw");
			FText RotationName = LOCTEXT("TransformRotationTrackName", "Rotation");
			TSharedRef<FAnimTimelineTrack_VectorCurve> RotationCurveTrack = MakeShared<FAnimTimelineTrack_VectorCurve>(TransformCurve.RotationCurve, TransformCurve.Name, 3, ERawCurveTrackTypes::RCT_Transform, RotationName, FText::Format(VectorFormat, TransformName, RotationName), TransformColor, SharedThis(this));
			RotationCurveTrack->SetExpanded(false);
			TransformCurveTrack->AddChild(RotationCurveTrack);
			RotationCurveTrack->AddChild(MakeShared<FAnimTimelineTrack_Curve>(TransformCurve.RotationCurve.FloatCurves[0], TransformCurve.Name, 3, ERawCurveTrackTypes::RCT_Transform, RollName, FText::Format(ComponentFormat, TransformName, RotationName, RollName), XColor, XColor, SharedThis(this)));
			RotationCurveTrack->AddChild(MakeShared<FAnimTimelineTrack_Curve>(TransformCurve.RotationCurve.FloatCurves[1], TransformCurve.Name, 4, ERawCurveTrackTypes::RCT_Transform, PitchName, FText::Format(ComponentFormat, TransformName, RotationName, PitchName), YColor, YColor, SharedThis(this)));
			RotationCurveTrack->AddChild(MakeShared<FAnimTimelineTrack_Curve>(TransformCurve.RotationCurve.FloatCurves[2], TransformCurve.Name, 5, ERawCurveTrackTypes::RCT_Transform, YawName, FText::Format(ComponentFormat, TransformName, RotationName, YawName), ZColor, ZColor, SharedThis(this)));

			FText ScaleName = LOCTEXT("TransformScaleTrackName", "Scale");
			TSharedRef<FAnimTimelineTrack_VectorCurve> ScaleCurveTrack = MakeShared<FAnimTimelineTrack_VectorCurve>(TransformCurve.ScaleCurve, TransformCurve.Name, 6, ERawCurveTrackTypes::RCT_Transform, ScaleName, FText::Format(VectorFormat, TransformName, ScaleName), TransformColor, SharedThis(this));
			ScaleCurveTrack->SetExpanded(false);
			TransformCurveTrack->AddChild(ScaleCurveTrack);
			ScaleCurveTrack->AddChild(MakeShared<FAnimTimelineTrack_Curve>(TransformCurve.ScaleCurve.FloatCurves[0], TransformCurve.Name, 6, ERawCurveTrackTypes::RCT_Transform, XName, FText::Format(ComponentFormat, TransformName, ScaleName, XName), XColor, XColor, SharedThis(this)));
			ScaleCurveTrack->AddChild(MakeShared<FAnimTimelineTrack_Curve>(TransformCurve.ScaleCurve.FloatCurves[1], TransformCurve.Name, 7, ERawCurveTrackTypes::RCT_Transform, YName, FText::Format(ComponentFormat, TransformName, ScaleName, YName), YColor, YColor, SharedThis(this)));
			ScaleCurveTrack->AddChild(MakeShared<FAnimTimelineTrack_Curve>(TransformCurve.ScaleCurve.FloatCurves[2], TransformCurve.Name, 8, ERawCurveTrackTypes::RCT_Transform, ZName, FText::Format(ComponentFormat, TransformName, ScaleName, ZName), ZColor, ZColor, SharedThis(this)));
		}
	}
}

void FAnimModel_AnimSequenceBase::EditSelectedCurves()
{
	TArray<IAnimationEditor::FCurveEditInfo> EditCurveInfo;
	for(TSharedRef<FAnimTimelineTrack>& SelectedTrack : SelectedTracks)
	{
		if(SelectedTrack->IsA<FAnimTimelineTrack_Curve>())
		{
			TSharedRef<FAnimTimelineTrack_Curve> CurveTrack = StaticCastSharedRef<FAnimTimelineTrack_Curve>(SelectedTrack);
			const TArray<FRichCurve*> Curves = CurveTrack->GetCurves();
			int32 NumCurves = Curves.Num();
			for(int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
			{
				if(CurveTrack->CanEditCurve(CurveIndex))
				{
					FText FullName = CurveTrack->GetFullCurveName(CurveIndex);
					FLinearColor Color = CurveTrack->GetCurveColor(CurveIndex);
					FSmartName Name;
					ERawCurveTrackTypes Type;
					int32 EditCurveIndex;
					CurveTrack->GetCurveEditInfo(CurveIndex, Name, Type, EditCurveIndex);
					FSimpleDelegate OnCurveChanged = FSimpleDelegate::CreateSP(&CurveTrack.Get(), &FAnimTimelineTrack_Curve::HandleCurveChanged);
					EditCurveInfo.AddUnique(IAnimationEditor::FCurveEditInfo(FullName, Color, Name, Type, EditCurveIndex, OnCurveChanged));
				}
			}
		}
	}

	if(EditCurveInfo.Num() > 0)
	{
		OnEditCurves.ExecuteIfBound(AnimSequenceBase, EditCurveInfo, nullptr);
	}
}

bool FAnimModel_AnimSequenceBase::CanEditSelectedCurves() const
{
	for(const TSharedRef<FAnimTimelineTrack>& SelectedTrack : SelectedTracks)
	{
		if(SelectedTrack->IsA<FAnimTimelineTrack_Curve>())
		{
			TSharedRef<FAnimTimelineTrack_Curve> CurveTrack = StaticCastSharedRef<FAnimTimelineTrack_Curve>(SelectedTrack);
			const TArray<FRichCurve*>& Curves = CurveTrack->GetCurves();
			for(int32 CurveIndex = 0; CurveIndex < Curves.Num(); ++CurveIndex)
			{
				if(CurveTrack->CanEditCurve(CurveIndex))
				{
					return true;
				}
			}
		}
	}

	return false;
}

void FAnimModel_AnimSequenceBase::RemoveSelectedCurves()
{
	FScopedTransaction Transaction(LOCTEXT("CurvePanel_RemoveCurves", "Remove Curves"));

	AnimSequenceBase->Modify(true);

	bool bDeletedCurve = false;

	for(TSharedRef<FAnimTimelineTrack>& SelectedTrack : SelectedTracks)
	{
		if(SelectedTrack->IsA<FAnimTimelineTrack_FloatCurve>())
		{
			TSharedRef<FAnimTimelineTrack_FloatCurve> FloatCurveTrack = StaticCastSharedRef<FAnimTimelineTrack_FloatCurve>(SelectedTrack);

			FFloatCurve& FloatCurve = FloatCurveTrack->GetFloatCurve();
			FSmartName CurveName = FloatCurveTrack->GetName();

			if(AnimSequenceBase->RawCurveData.GetCurveData(CurveName.UID))
			{
				FSmartName TrackName;
				if (AnimSequenceBase->GetSkeleton()->GetSmartNameByUID(USkeleton::AnimCurveMappingName, CurveName.UID, TrackName))
				{
					// Stop editing this curve in the external editor window
					FSmartName Name;
					ERawCurveTrackTypes Type;
					int32 CurveEditIndex;
					FloatCurveTrack->GetCurveEditInfo(0, Name, Type, CurveEditIndex);
					IAnimationEditor::FCurveEditInfo EditInfo(Name, Type, CurveEditIndex);
					OnStopEditingCurves.ExecuteIfBound(TArray<IAnimationEditor::FCurveEditInfo>({ EditInfo }));

					AnimSequenceBase->RawCurveData.DeleteCurveData(TrackName);
					bDeletedCurve = true;
				}
			}
		}
		else if(SelectedTrack->IsA<FAnimTimelineTrack_TransformCurve>())
		{
			TSharedRef<FAnimTimelineTrack_TransformCurve> TransformCurveTrack = StaticCastSharedRef<FAnimTimelineTrack_TransformCurve>(SelectedTrack);

			FTransformCurve& TransformCurve = TransformCurveTrack->GetTransformCurve();
			FSmartName CurveName = TransformCurveTrack->GetName();

			if(AnimSequenceBase->RawCurveData.GetCurveData(CurveName.UID, ERawCurveTrackTypes::RCT_Transform))
			{
				FSmartName CurveToDelete;
				if (AnimSequenceBase->GetSkeleton()->GetSmartNameByUID(USkeleton::AnimTrackCurveMappingName, CurveName.UID, CurveToDelete))
				{
					// Stop editing these curves in the external editor window
					TArray<IAnimationEditor::FCurveEditInfo> CurveEditInfo;
					for(int32 CurveIndex = 0; CurveIndex < TransformCurveTrack->GetCurves().Num(); ++CurveIndex)
					{
						FSmartName Name;
						ERawCurveTrackTypes Type;
						int32 CurveEditIndex;
						TransformCurveTrack->GetCurveEditInfo(CurveIndex, Name, Type, CurveEditIndex);
						IAnimationEditor::FCurveEditInfo EditInfo(Name, Type, CurveEditIndex);
						CurveEditInfo.Add(EditInfo);
					}

					OnStopEditingCurves.ExecuteIfBound(CurveEditInfo);

					AnimSequenceBase->RawCurveData.DeleteCurveData(CurveToDelete, ERawCurveTrackTypes::RCT_Transform);

					if(UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimSequenceBase))
					{
						AnimSequence->bNeedsRebake = true;
					}

					bDeletedCurve = true;
				}
			}	
		}
	}

	if(bDeletedCurve)
	{
		AnimSequenceBase->MarkRawDataAsModified();
		AnimSequenceBase->PostEditChange();

		if (GetPreviewScene()->GetPreviewMeshComponent()->PreviewInstance != nullptr)
		{
			GetPreviewScene()->GetPreviewMeshComponent()->PreviewInstance->RefreshCurveBoneControllers();
		}
	}

	RefreshTracks();
}

void FAnimModel_AnimSequenceBase::SetDisplayFormat(EFrameNumberDisplayFormats InFormat)
{
	GetMutableDefault<UPersonaOptions>()->TimelineDisplayFormat = InFormat;
}

bool FAnimModel_AnimSequenceBase::IsDisplayFormatChecked(EFrameNumberDisplayFormats InFormat) const
{
	return GetDefault<UPersonaOptions>()->TimelineDisplayFormat == InFormat;
}

void FAnimModel_AnimSequenceBase::ToggleDisplayPercentage()
{
	GetMutableDefault<UPersonaOptions>()->bTimelineDisplayPercentage = !GetDefault<UPersonaOptions>()->bTimelineDisplayPercentage;
}

bool FAnimModel_AnimSequenceBase::IsDisplayPercentageChecked() const
{
	return GetDefault<UPersonaOptions>()->bTimelineDisplayPercentage;
}

void FAnimModel_AnimSequenceBase::ToggleDisplaySecondary()
{
	GetMutableDefault<UPersonaOptions>()->bTimelineDisplayFormatSecondary = !GetDefault<UPersonaOptions>()->bTimelineDisplayFormatSecondary;
}

bool FAnimModel_AnimSequenceBase::IsDisplaySecondaryChecked() const
{
	return GetDefault<UPersonaOptions>()->bTimelineDisplayFormatSecondary;
}

void FAnimModel_AnimSequenceBase::HandleUndoRedo()
{
	// Close any curves that are no longer editable
	for(FFloatCurve& FloatCurve : AnimSequenceBase->RawCurveData.FloatCurves)
	{
		if(FloatCurve.GetCurveTypeFlag(AACF_Metadata))
		{
			IAnimationEditor::FCurveEditInfo CurveEditInfo(FloatCurve.Name, ERawCurveTrackTypes::RCT_Float, 0);
			OnStopEditingCurves.ExecuteIfBound(TArray<IAnimationEditor::FCurveEditInfo>({ CurveEditInfo }));
		}
	}
}

void FAnimModel_AnimSequenceBase::UpdateRange()
{
	FAnimatedRange OldPlaybackRange = PlaybackRange;

	// update playback range
	PlaybackRange = FAnimatedRange(0.0, (double)AnimSequenceBase->GetPlayLength());

	if (OldPlaybackRange != PlaybackRange)
	{
		// Update view/range if playback range changed
		SetViewRange(PlaybackRange);
	}
}

bool FAnimModel_AnimSequenceBase::IsNotifiesTimingElementDisplayEnabled(ETimingElementType::Type ElementType) const
{
	return NotifiesTimingElementNodeDisplayFlags[ElementType];
}

void FAnimModel_AnimSequenceBase::ToggleNotifiesTimingElementDisplayEnabled(ETimingElementType::Type ElementType)
{
	NotifiesTimingElementNodeDisplayFlags[ElementType] = !NotifiesTimingElementNodeDisplayFlags[ElementType];
}

bool FAnimModel_AnimSequenceBase::ClampToEndTime(float NewEndTime)
{
	float SequenceLength = AnimSequenceBase->GetPlayLength();

	//if we had a valid sequence length before and our new end time is shorter
	//then we need to clamp.
	return (SequenceLength > 0.f && NewEndTime < SequenceLength);
}

void FAnimModel_AnimSequenceBase::RefreshSnapTimes()
{
	SnapTimes.Reset();
	for(const FAnimNotifyEvent& Notify : AnimSequenceBase->Notifies)
	{
		SnapTimes.Emplace(FSnapType::Notifies.Type, (double)Notify.GetTime());
		if(Notify.NotifyStateClass != nullptr)
		{
			SnapTimes.Emplace(FSnapType::Notifies.Type, (double)(Notify.GetTime() + Notify.GetDuration()));
		}
	}
}

#undef LOCTEXT_NAMESPACE