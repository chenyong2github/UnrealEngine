// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimTimelineTrack_TransformCurve.h"
#include "Animation/AnimSequenceBase.h"
#include "Widgets/Layout/SBorder.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AnimSequenceTimelineCommands.h"
#include "ScopedTransaction.h"
#include "Animation/AnimSequence.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimPreviewInstance.h"
#include "AnimModel_AnimSequenceBase.h"

#define LOCTEXT_NAMESPACE "FAnimTimelineTrack_TransformCurve"

ANIMTIMELINE_IMPLEMENT_TRACK(FAnimTimelineTrack_TransformCurve);

FAnimTimelineTrack_TransformCurve::FAnimTimelineTrack_TransformCurve(FTransformCurve& InCurve, const TSharedRef<FAnimModel>& InModel)
	: FAnimTimelineTrack_Curve(FAnimTimelineTrack_TransformCurve::GetTransformCurveName(InModel, InCurve.Name), FAnimTimelineTrack_TransformCurve::GetTransformCurveName(InModel, InCurve.Name), InCurve.GetColor(), InCurve.GetColor(), InModel)
	, TransformCurve(InCurve)
	, CurveName(InCurve.Name)
{
	Curves.Add(&InCurve.TranslationCurve.FloatCurves[0]);
	Curves.Add(&InCurve.TranslationCurve.FloatCurves[1]);
	Curves.Add(&InCurve.TranslationCurve.FloatCurves[2]);
	Curves.Add(&InCurve.RotationCurve.FloatCurves[0]);
	Curves.Add(&InCurve.RotationCurve.FloatCurves[1]);
	Curves.Add(&InCurve.RotationCurve.FloatCurves[2]);
	Curves.Add(&InCurve.ScaleCurve.FloatCurves[0]);
	Curves.Add(&InCurve.ScaleCurve.FloatCurves[1]);
	Curves.Add(&InCurve.ScaleCurve.FloatCurves[2]);
}

FLinearColor FAnimTimelineTrack_TransformCurve::GetCurveColor(int32 InCurveIndex) const
{
	static const FLinearColor Colors[3] =
	{
		FLinearColor::Red,
		FLinearColor::Green,
		FLinearColor::Blue,
	};

	return Colors[InCurveIndex % 3];
}

FText FAnimTimelineTrack_TransformCurve::GetFullCurveName(int32 InCurveIndex) const 
{
	check(InCurveIndex >= 0 && InCurveIndex < 9);

	static const FText TrackNames[9] =
	{
		LOCTEXT("TranslationXTrackName", "Translation.X"),
		LOCTEXT("TranslationYTrackName", "Translation.Y"),
		LOCTEXT("TranslationZTrackName", "Translation.Z"),
		LOCTEXT("RotationRollTrackName", "Rotation.Roll"),
		LOCTEXT("RotationPitchTrackName", "Rotation.Pitch"),
		LOCTEXT("RotationYawTrackName", "Rotation.Yaw"),
		LOCTEXT("ScaleXTrackName", "Scale.X"),
		LOCTEXT("ScaleYTrackName", "Scale.Y"),
		LOCTEXT("ScaleZTrackName", "Scale.Z"),
	};
			
	return FText::Format(LOCTEXT("TransformVectorFormat", "{0}.{1}"), FullCurveName, TrackNames[InCurveIndex]);
}

FText FAnimTimelineTrack_TransformCurve::GetTransformCurveName(const TSharedRef<FAnimModel>& InModel, const FSmartName& InSmartName)
{
	const FSmartNameMapping* NameMapping = InModel->GetAnimSequenceBase()->GetSkeleton()->GetSmartNameContainer(USkeleton::AnimTrackCurveMappingName);
	if(NameMapping)
	{
		FName CurveName;
		if(NameMapping->GetName(InSmartName.UID, CurveName))
		{
			return FText::FromName(CurveName);
		}
	}

	return FText::FromName(InSmartName.DisplayName);
}

TSharedRef<SWidget> FAnimTimelineTrack_TransformCurve::BuildCurveTrackMenu()
{
	FMenuBuilder MenuBuilder(true, GetModel()->GetCommandList());

	MenuBuilder.BeginSection("Curve", LOCTEXT("CurveMenuSection", "Curve"));
	{
			MenuBuilder.AddMenuEntry(
				FAnimSequenceTimelineCommands::Get().EditCurve->GetLabel(),
				FAnimSequenceTimelineCommands::Get().EditCurve->GetDescription(),
				FAnimSequenceTimelineCommands::Get().EditCurve->GetIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAnimTimelineTrack_TransformCurve::HendleEditCurve)));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("RemoveTrack", "Remove Track"),
			LOCTEXT("RemoveTrackTooltip", "Remove this track"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FAnimTimelineTrack_TransformCurve::DeleteTrack)));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("TrackEnabled", "Enabled"),
			LOCTEXT("TrackEnabledTooltip", "Enable/disable this track"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAnimTimelineTrack_TransformCurve::ToggleEnabled),
				FCanExecuteAction(), 
				FIsActionChecked::CreateSP(this, &FAnimTimelineTrack_TransformCurve::IsEnabled)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
			);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FAnimTimelineTrack_TransformCurve::DeleteTrack()
{
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
	TSharedRef<FAnimModel_AnimSequenceBase> BaseModel = StaticCastSharedRef<FAnimModel_AnimSequenceBase>(GetModel());

	if(AnimSequenceBase->RawCurveData.GetCurveData(TransformCurve.Name.UID, ERawCurveTrackTypes::RCT_Transform))
	{
		const FScopedTransaction Transaction(LOCTEXT("AnimCurve_DeleteTrack", "Delete Curve"));
		FSmartName CurveToDelete;
		if (AnimSequenceBase->GetSkeleton()->GetSmartNameByUID(USkeleton::AnimTrackCurveMappingName, TransformCurve.Name.UID, CurveToDelete))
		{
			// Stop editing these curves in the external editor window
			TArray<IAnimationEditor::FCurveEditInfo> CurveEditInfo;
			for(int32 CurveIndex = 0; CurveIndex < Curves.Num(); ++CurveIndex)
			{
				FSmartName Name;
				ERawCurveTrackTypes Type;
				int32 CurveEditIndex;
				GetCurveEditInfo(CurveIndex, Name, Type, CurveEditIndex);
				IAnimationEditor::FCurveEditInfo EditInfo(Name, Type, CurveEditIndex);
				CurveEditInfo.Add(EditInfo);
			}

			BaseModel->OnStopEditingCurves.ExecuteIfBound(CurveEditInfo);

			AnimSequenceBase->Modify();
			AnimSequenceBase->RawCurveData.DeleteCurveData(CurveToDelete, ERawCurveTrackTypes::RCT_Transform);

			if(UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimSequenceBase))
			{
				AnimSequence->bNeedsRebake = true;
			}

			GetModel()->RefreshTracks();

			if (GetModel()->GetPreviewScene()->GetPreviewMeshComponent()->PreviewInstance != nullptr)
			{
				GetModel()->GetPreviewScene()->GetPreviewMeshComponent()->PreviewInstance->RefreshCurveBoneControllers();
			}
		}
	}
}

bool FAnimTimelineTrack_TransformCurve::IsEnabled() const
{
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
	FAnimCurveBase* Curve = AnimSequenceBase->RawCurveData.GetCurveData(TransformCurve.Name.UID, ERawCurveTrackTypes::RCT_Transform);
	return Curve && !Curve->GetCurveTypeFlag(AACF_Disabled);
}

void FAnimTimelineTrack_TransformCurve::ToggleEnabled()
{
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();

	FAnimCurveBase* Curve = AnimSequenceBase->RawCurveData.GetCurveData(TransformCurve.Name.UID, ERawCurveTrackTypes::RCT_Transform);
	if (Curve)
	{
		bool bEnabled = !Curve->GetCurveTypeFlag(AACF_Disabled);

		const FScopedTransaction Transaction(bEnabled ? LOCTEXT("AnimCurve_DisableTrack", "Disable track") : LOCTEXT("AnimCurve_EnableTrack", "Enable track"));
		AnimSequenceBase->Modify();

		Curve->SetCurveTypeFlag(AACF_Disabled, bEnabled);

		if(UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimSequenceBase))
		{
			AnimSequence->bNeedsRebake = true;
		}

		// need to update curves, otherwise they're not disabled
		if (GetModel()->GetPreviewScene()->GetPreviewMeshComponent()->PreviewInstance != nullptr)
		{
			GetModel()->GetPreviewScene()->GetPreviewMeshComponent()->PreviewInstance->RefreshCurveBoneControllers();
		}
	}
}

void FAnimTimelineTrack_TransformCurve::GetCurveEditInfo(int32 InCurveIndex, FSmartName& OutName, ERawCurveTrackTypes& OutType, int32& OutCurveIndex) const
{
	OutName = TransformCurve.Name;
	OutType = ERawCurveTrackTypes::RCT_Transform;
	OutCurveIndex = InCurveIndex;
}

#undef LOCTEXT_NAMESPACE