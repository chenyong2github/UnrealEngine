// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimTimelineTrack_FloatCurve.h"
#include "CurveEditor.h"
#include "Animation/AnimSequenceBase.h"
#include "Widgets/Layout/SBorder.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AnimSequenceTimelineCommands.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Animation/Skeleton.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Animation/AnimMontage.h"
#include "Widgets/Colors/SColorBlock.h"
#include "PersonaUtils.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "AnimModel_AnimSequenceBase.h"
#include "SAnimOutlinerItem.h"

#define LOCTEXT_NAMESPACE "FAnimTimelineTrack_FloatCurve"

ANIMTIMELINE_IMPLEMENT_TRACK(FAnimTimelineTrack_FloatCurve);

FAnimTimelineTrack_FloatCurve::FAnimTimelineTrack_FloatCurve(FFloatCurve& InCurve, const TSharedRef<FAnimModel>& InModel)
	: FAnimTimelineTrack_Curve(InCurve.FloatCurve, InCurve.Name, 0, ERawCurveTrackTypes::RCT_Float, FText::FromName(InCurve.Name.DisplayName), FText::FromName(InCurve.Name.DisplayName), InCurve.GetColor(), InCurve.GetColor(), InModel)
	, FloatCurve(InCurve)
	, CurveName(InCurve.Name)
{
	SetHeight(32.0f);
}

TSharedRef<SWidget> FAnimTimelineTrack_FloatCurve::MakeTimelineWidgetContainer()
{
	TSharedRef<SWidget> CurveWidget = MakeCurveWidget();

	// zoom to fit now we have a view
	CurveEditor->ZoomToFit(EAxisList::Y);

	auto ColorLambda = [this]()
	{
		if(GetModel()->IsTrackSelected(AsShared()))
		{
			return FEditorStyle::GetSlateColor("SelectionColor").GetSpecifiedColor().CopyWithNewOpacity(0.75f);
		}
		else
		{
			return FloatCurve.GetCurveTypeFlag(AACF_Metadata) ? FloatCurve.GetColor().Desaturate(0.25f) : FloatCurve.GetColor().Desaturate(0.75f); 
		}
	};

	return
		SAssignNew(TimelineWidgetContainer, SBorder)
		.Padding(0.0f)
		.BorderImage_Lambda([this](){ return FloatCurve.GetCurveTypeFlag(AACF_Metadata) ? FEditorStyle::GetBrush("Sequencer.Section.SelectedSectionOverlay") : FEditorStyle::GetBrush("AnimTimeline.Outliner.DefaultBorder"); })
		.BorderBackgroundColor_Lambda(ColorLambda)
		[
			CurveWidget
		];
}

TSharedRef<SWidget> FAnimTimelineTrack_FloatCurve::GenerateContainerWidgetForOutliner(const TSharedRef<SAnimOutlinerItem>& InRow)
{
	TSharedPtr<SBorder> OuterBorder;
	TSharedPtr<SHorizontalBox> InnerHorizontalBox;
	TSharedRef<SWidget> OutlinerWidget = GenerateStandardOutlinerWidget(InRow, false, OuterBorder, InnerHorizontalBox);


	UAnimMontage* AnimMontage = Cast<UAnimMontage>(GetModel()->GetAnimSequenceBase());
	bool bChildAnimMontage = AnimMontage && AnimMontage->HasParentAsset();

	InnerHorizontalBox->AddSlot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(2.0f, 1.0f)
		.FillWidth(1.0f)
		[
			SAssignNew(EditableTextLabel, SInlineEditableTextBlock)
			.IsReadOnly(bChildAnimMontage)
			.Text(this, &FAnimTimelineTrack_FloatCurve::GetLabel)
			.IsSelected_Lambda([this](){ return GetModel()->IsTrackSelected(SharedThis(this)); })
			.OnTextCommitted(this, &FAnimTimelineTrack_FloatCurve::OnCommitCurveName)
			.HighlightText(InRow->GetHighlightText())
		];

	if(!bChildAnimMontage)
	{
		OuterBorder->SetOnMouseDoubleClick(FPointerEventHandler::CreateSP(this, &FAnimTimelineTrack_FloatCurve::HandleDoubleClicked));
		AddCurveTrackButton(InnerHorizontalBox);
	}

	return OutlinerWidget;
}

TSharedRef<SWidget> FAnimTimelineTrack_FloatCurve::BuildCurveTrackMenu()
{
	FMenuBuilder MenuBuilder(true, GetModel()->GetCommandList());

	bool bIsMetadata = FloatCurve.GetCurveTypeFlag(AACF_Metadata);

	MenuBuilder.BeginSection("Curve", bIsMetadata ? LOCTEXT("CurveMetadataMenuSection", "Curve Metadata") : LOCTEXT("CurveMenuSection", "Curve"));
	{
		if(bIsMetadata)
		{
			MenuBuilder.AddMenuEntry(
				FAnimSequenceTimelineCommands::Get().ConvertMetaDataToCurve->GetLabel(),
				FAnimSequenceTimelineCommands::Get().ConvertMetaDataToCurve->GetDescription(),
				FAnimSequenceTimelineCommands::Get().ConvertMetaDataToCurve->GetIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAnimTimelineTrack_FloatCurve::ConvertMetaDataToCurve)
				)
			);
		}
		else
		{
			MenuBuilder.AddMenuEntry(
				FAnimSequenceTimelineCommands::Get().EditCurve->GetLabel(),
				FAnimSequenceTimelineCommands::Get().EditCurve->GetDescription(),
				FAnimSequenceTimelineCommands::Get().EditCurve->GetIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAnimTimelineTrack_FloatCurve::HendleEditCurve)));

			MenuBuilder.AddMenuEntry(
				FAnimSequenceTimelineCommands::Get().ConvertCurveToMetaData->GetLabel(),
				FAnimSequenceTimelineCommands::Get().ConvertCurveToMetaData->GetDescription(),
				FAnimSequenceTimelineCommands::Get().ConvertCurveToMetaData->GetIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAnimTimelineTrack_FloatCurve::ConvertCurveToMetaData)
				)
			);
		}

		MenuBuilder.AddMenuEntry(
			FAnimSequenceTimelineCommands::Get().RemoveCurve->GetLabel(),
			FAnimSequenceTimelineCommands::Get().RemoveCurve->GetDescription(),
			FAnimSequenceTimelineCommands::Get().RemoveCurve->GetIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAnimTimelineTrack_FloatCurve::RemoveCurve)
			)
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FAnimTimelineTrack_FloatCurve::ConvertCurveToMetaData()
{
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();

	// Stop editing this curve in the external editor window
	IAnimationEditor::FCurveEditInfo EditInfo(CurveName, ERawCurveTrackTypes::RCT_Float, 0);
	StaticCastSharedRef<FAnimModel_AnimSequenceBase>(GetModel())->OnStopEditingCurves.ExecuteIfBound(TArray<IAnimationEditor::FCurveEditInfo>({ EditInfo }));

	FScopedTransaction Transaction(LOCTEXT("CurvePanel_ConvertCurveToMetaData", "Convert curve to metadata"));
	AnimSequenceBase->Modify(true);
	FloatCurve.SetCurveTypeFlag(AACF_Metadata, true);

	// We're moving to a metadata curve, we need to clear out the keys.
	FloatCurve.FloatCurve.Reset();
	FloatCurve.FloatCurve.AddKey(0.0f, 1.0f);

	ZoomToFit();
}

void FAnimTimelineTrack_FloatCurve::ConvertMetaDataToCurve()
{
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();

	FScopedTransaction Transaction(LOCTEXT("CurvePanel_ConvertMetaDataToCurve", "Convert metadata to curve"));
	AnimSequenceBase->Modify(true);
	FloatCurve.SetCurveTypeFlag(AACF_Metadata, false);
}

void FAnimTimelineTrack_FloatCurve::RemoveCurve()
{
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();

	FScopedTransaction Transaction(LOCTEXT("CurvePanel_RemoveCurve", "Remove Curve"));
	
	if(AnimSequenceBase->RawCurveData.GetCurveData(FloatCurve.Name.UID))
	{
		FSmartName TrackName;
		if (AnimSequenceBase->GetSkeleton()->GetSmartNameByUID(USkeleton::AnimCurveMappingName, FloatCurve.Name.UID, TrackName))
		{
			// Stop editing this curve in the external editor window
			IAnimationEditor::FCurveEditInfo EditInfo(CurveName, ERawCurveTrackTypes::RCT_Float, 0);
			StaticCastSharedRef<FAnimModel_AnimSequenceBase>(GetModel())->OnStopEditingCurves.ExecuteIfBound(TArray<IAnimationEditor::FCurveEditInfo>({ EditInfo }));

			AnimSequenceBase->Modify(true);
			AnimSequenceBase->RawCurveData.DeleteCurveData(TrackName);
			AnimSequenceBase->MarkRawDataAsModified();
			AnimSequenceBase->PostEditChange();
			
			GetModel()->RefreshTracks();
		}
	}
}

void FAnimTimelineTrack_FloatCurve::OnCommitCurveName(const FText& InText, ETextCommit::Type CommitInfo)
{
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();

	if (USkeleton* Skeleton = AnimSequenceBase->GetSkeleton())
	{
		// only do this if the name isn't same
		FText CurrentCurveName = GetLabel();
		if (!CurrentCurveName.EqualToCaseIgnored(InText))
		{
			// Stop editing this curve in the external editor window
			IAnimationEditor::FCurveEditInfo EditInfo(CurveName, ERawCurveTrackTypes::RCT_Float, 0);
			StaticCastSharedRef<FAnimModel_AnimSequenceBase>(GetModel())->OnStopEditingCurves.ExecuteIfBound(TArray<IAnimationEditor::FCurveEditInfo>({ EditInfo }));

			// Check that the name doesn't already exist
			const FName RequestedName = FName(*InText.ToString());

			const FSmartNameMapping* NameMapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);

			FScopedTransaction Transaction(LOCTEXT("CurveEditor_RenameCurve", "Rename Curve"));

			AnimSequenceBase->Modify();

			FSmartName NewSmartName;
			if (NameMapping->FindSmartName(RequestedName, NewSmartName))
			{
				// Already in use in this sequence, and if it's not my UID
				if (NewSmartName.UID != FloatCurve.Name.UID && AnimSequenceBase->RawCurveData.GetCurveData(NewSmartName.UID) != nullptr)
				{
					Transaction.Cancel();

					FFormatNamedArguments Args;
					Args.Add(TEXT("InvalidName"), FText::FromName(RequestedName));
					FNotificationInfo Info(FText::Format(LOCTEXT("AnimCurveRenamedInUse", "The name \"{InvalidName}\" is already used."), Args));

					Info.bUseLargeFont = false;
					Info.ExpireDuration = 5.0f;

					TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
					if (Notification.IsValid())
					{
						Notification->SetCompletionState(SNotificationItem::CS_Fail);
					}
					return;
				}
			}
			else
			{
				if(!Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, RequestedName, NewSmartName))
				{
					Transaction.Cancel();
					FNotificationInfo Info(LOCTEXT("AnimCurveRenamedError", "Failed to rename curve smart name, check the log for errors."));

					Info.bUseLargeFont = false;
					Info.ExpireDuration = 5.0f;

					TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
					if (Notification.IsValid())
					{
						Notification->SetCompletionState(SNotificationItem::CS_Fail);
					}
					return;
				}
			}

			FloatCurve.Name.UID = NewSmartName.UID;
			FloatCurve.Name.DisplayName = NewSmartName.DisplayName;

			CurveName = FloatCurve.Name;
			FullCurveName = FText::FromName(FloatCurve.Name.DisplayName);
		}
	}
}

FText FAnimTimelineTrack_FloatCurve::GetLabel() const
{
	return FAnimTimelineTrack_FloatCurve::GetFloatCurveName(GetModel(), FloatCurve.Name);
}

FText FAnimTimelineTrack_FloatCurve::GetFloatCurveName(const TSharedRef<FAnimModel>& InModel, const FSmartName& InSmartName)
{
	const FSmartNameMapping* NameMapping = InModel->GetAnimSequenceBase()->GetSkeleton()->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
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

bool FAnimTimelineTrack_FloatCurve::CanEditCurve(int32 InCurveIndex) const
{
	return !FloatCurve.GetCurveTypeFlag(AACF_Metadata);
}

void FAnimTimelineTrack_FloatCurve::RequestRename()
{
	if(EditableTextLabel.IsValid())
	{
		EditableTextLabel->EnterEditingMode();
	}
}

void FAnimTimelineTrack_FloatCurve::AddCurveTrackButton(TSharedPtr<SHorizontalBox> InnerHorizontalBox)
{
	InnerHorizontalBox->AddSlot()
	.AutoWidth()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Center)
	.Padding(0.0f, 1.0f)
	[
		PersonaUtils::MakeTrackButton(LOCTEXT("EditCurveButtonText", "Curve"), FOnGetContent::CreateSP(this, &FAnimTimelineTrack_FloatCurve::BuildCurveTrackMenu), MakeAttributeSP(this, &FAnimTimelineTrack_FloatCurve::IsHovered))
	];

	auto GetValue = [this]()
	{
		return FloatCurve.Color;
	};

	auto SetValue = [this](FLinearColor InNewColor)
	{
		UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
		FScopedTransaction Transaction(LOCTEXT("SetCurveColor", "Set Curve Color"));
		AnimSequenceBase->Modify(true);
		FloatCurve.Color = InNewColor;

		Color = InNewColor;

		// Set display curves too
		for(const TPair<FCurveModelID, TUniquePtr<FCurveModel>>& CurvePair : CurveEditor->GetCurves())
		{
			CurvePair.Value->SetColor(InNewColor);
		}
	};

	auto OnGetMenuContent = [GetValue, SetValue]()
	{
		// Open a color picker
		return SNew(SColorPicker)
			.TargetColorAttribute_Lambda(GetValue)
			.UseAlpha(false)
			.DisplayInlineVersion(true)
			.OnColorCommitted_Lambda(SetValue);
	};

	InnerHorizontalBox->AddSlot()
	.AutoWidth()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Fill)
	.Padding(2.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SComboButton)
		.ToolTipText(LOCTEXT("EditCurveColor", "Edit Curve Color"))
		.ContentPadding(0.0f)
		.HasDownArrow(false)
		.ButtonStyle(FEditorStyle::Get(), "Sequencer.AnimationOutliner.ColorStrip")
		.OnGetMenuContent_Lambda(OnGetMenuContent)
		.CollapseMenuOnParentFocus(true)
		.VAlign(VAlign_Fill)
		.ButtonContent()
		[
			SNew(SColorBlock)
			.Color_Lambda(GetValue)
			.ShowBackgroundForAlpha(false)
			.IgnoreAlpha(true)
			.Size(FVector2D(OutlinerRightPadding - 2.0f, OutlinerRightPadding))
		]
	];
}

FLinearColor FAnimTimelineTrack_FloatCurve::GetCurveColor(int32 InCurveIndex) const
{ 
	return FloatCurve.Color; 
}

void FAnimTimelineTrack_FloatCurve::GetCurveEditInfo(int32 InCurveIndex, FSmartName& OutName, ERawCurveTrackTypes& OutType, int32& OutCurveIndex) const
{
	OutName = CurveName;
	OutType = ERawCurveTrackTypes::RCT_Float;
	OutCurveIndex = InCurveIndex;
}

#undef LOCTEXT_NAMESPACE