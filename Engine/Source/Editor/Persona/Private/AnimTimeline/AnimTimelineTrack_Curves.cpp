// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimTimelineTrack_Curves.h"
#include "PersonaUtils.h"
#include "Widgets/SBoxPanel.h"
#include "AnimSequenceTimelineCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Animation/AnimSequenceBase.h"
#include "SAnimCurvePanel.h"
#include "Widgets/Input/STextEntryPopup.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"
#include "Animation/AnimMontage.h"
#include "SAnimOutlinerItem.h"
#include "Preferences/PersonaOptions.h"

#define LOCTEXT_NAMESPACE "FAnimTimelineTrack_Notifies"

ANIMTIMELINE_IMPLEMENT_TRACK(FAnimTimelineTrack_Curves);

FAnimTimelineTrack_Curves::FAnimTimelineTrack_Curves(const TSharedRef<FAnimModel>& InModel)
	: FAnimTimelineTrack(LOCTEXT("CurvesRootTrackLabel", "Curves"), LOCTEXT("CurvesRootTrackToolTip", "Curve data contained in this asset"), InModel)
{
}

TSharedRef<SWidget> FAnimTimelineTrack_Curves::GenerateContainerWidgetForOutliner(const TSharedRef<SAnimOutlinerItem>& InRow)
{
	TSharedPtr<SBorder> OuterBorder;
	TSharedPtr<SHorizontalBox> InnerHorizontalBox;
	OutlinerWidget = GenerateStandardOutlinerWidget(InRow, false, OuterBorder, InnerHorizontalBox);

	OuterBorder->SetBorderBackgroundColor(FEditorStyle::GetColor("AnimTimeline.Outliner.HeaderColor"));

	InnerHorizontalBox->AddSlot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(2.0f, 1.0f)
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(&FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("AnimTimeline.Outliner.Label"))
			.Text(this, &FAnimTimelineTrack_Curves::GetLabel)
			.HighlightText(InRow->GetHighlightText())
		];

	InnerHorizontalBox->AddSlot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(5.0f, 1.0f)
		[
			SNew(STextBlock)
			.TextStyle(&FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("TinyText"))
			.Text_Lambda([this]()
			{ 
				UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
				return FText::Format(LOCTEXT("CurveCountFormat", "({0})"), FText::AsNumber(AnimSequenceBase->RawCurveData.FloatCurves.Num())); 
			})
		];

	UAnimMontage* AnimMontage = Cast<UAnimMontage>(GetModel()->GetAnimSequenceBase());
	if(!(AnimMontage && AnimMontage->HasParentAsset()))
	{
		InnerHorizontalBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(OutlinerRightPadding, 1.0f)
			[
				PersonaUtils::MakeTrackButton(LOCTEXT("EditCurvesButtonText", "Curves"), FOnGetContent::CreateSP(this, &FAnimTimelineTrack_Curves::BuildCurvesSubMenu), MakeAttributeSP(this, &FAnimTimelineTrack_Curves::IsHovered))
			];
	}

	return OutlinerWidget.ToSharedRef();
}

void FAnimTimelineTrack_Curves::DeleteAllCurves()
{
	const FScopedTransaction Transaction( LOCTEXT("AnimCurve_RemoveAllCurves", "Remove All Curves") );

	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
	AnimSequenceBase->Modify(true);
	AnimSequenceBase->RawCurveData.DeleteAllCurveData();
	AnimSequenceBase->MarkRawDataAsModified();

	GetModel()->RefreshTracks();
}

TSharedRef<SWidget> FAnimTimelineTrack_Curves::BuildCurvesSubMenu()
{
	FMenuBuilder MenuBuilder(true, GetModel()->GetCommandList());

	MenuBuilder.BeginSection("Curves", LOCTEXT("CurvesMenuSection", "Curves"));
	{
		MenuBuilder.AddSubMenu(
			FAnimSequenceTimelineCommands::Get().AddCurve->GetLabel(),
			FAnimSequenceTimelineCommands::Get().AddCurve->GetDescription(),
			FNewMenuDelegate::CreateSP(this, &FAnimTimelineTrack_Curves::FillVariableCurveMenu)
		);

		MenuBuilder.AddSubMenu(
			FAnimSequenceTimelineCommands::Get().AddMetadata->GetLabel(),
			FAnimSequenceTimelineCommands::Get().AddMetadata->GetDescription(),
			FNewMenuDelegate::CreateSP(this, &FAnimTimelineTrack_Curves::FillMetadataEntryMenu)
		);

		UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
		if(AnimSequenceBase->RawCurveData.FloatCurves.Num() > 0)
		{
			MenuBuilder.AddMenuEntry(
				FAnimSequenceTimelineCommands::Get().RemoveAllCurves->GetLabel(),
				FAnimSequenceTimelineCommands::Get().RemoveAllCurves->GetDescription(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAnimTimelineTrack_Curves::DeleteAllCurves))
			);
		}
	}

	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Options", LOCTEXT("OptionsMenuSection", "Options"));
	{
		MenuBuilder.AddMenuEntry(
			FAnimSequenceTimelineCommands::Get().ShowCurveKeys->GetLabel(),
			FAnimSequenceTimelineCommands::Get().ShowCurveKeys->GetDescription(),
			FAnimSequenceTimelineCommands::Get().ShowCurveKeys->GetIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAnimTimelineTrack_Curves::HandleShowCurvePoints),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &FAnimTimelineTrack_Curves::IsShowCurvePointsEnabled)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FAnimTimelineTrack_Curves::FillMetadataEntryMenu(FMenuBuilder& Builder)
{
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
	USkeleton* CurrentSkeleton = AnimSequenceBase->GetSkeleton();
	check(CurrentSkeleton);

	const FSmartNameMapping* Mapping = CurrentSkeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
	TArray<USkeleton::AnimCurveUID> CurveUids;
	Mapping->FillUidArray(CurveUids);

	Builder.BeginSection(NAME_None, LOCTEXT("MetadataMenu_ListHeading", "Available Names"));
	{
		TArray<FSmartNameSortItem> SmartNameList;

		for (USkeleton::AnimCurveUID Id : CurveUids)
		{
			if (!AnimSequenceBase->RawCurveData.GetCurveData(Id))
			{
				FName CurveName;
				if (Mapping->GetName(Id, CurveName))
				{
					SmartNameList.Add(FSmartNameSortItem(CurveName, Id));
				}
			}
		}

		{
			SmartNameList.Sort(FSmartNameSortItemSortOp());

			for (FSmartNameSortItem SmartNameItem : SmartNameList)
			{
				const FText Description = LOCTEXT("NewMetadataSubMenu_ToolTip", "Add an existing metadata curve");
				const FText Label = FText::FromName(SmartNameItem.SmartName);

				FUIAction UIAction;
				UIAction.ExecuteAction.BindRaw(
					this, &FAnimTimelineTrack_Curves::AddMetadataEntry,
					SmartNameItem.ID);

				Builder.AddMenuEntry(Label, Description, FSlateIcon(), UIAction);
			}
		}
	}
	Builder.EndSection();

	Builder.AddMenuSeparator();

	const FText Description = LOCTEXT("NewMetadataCreateNew_ToolTip", "Create a new metadata entry");
	const FText Label = LOCTEXT("NewMetadataCreateNew_Label","Create New");
	FUIAction UIAction;
	UIAction.ExecuteAction.BindRaw(this, &FAnimTimelineTrack_Curves::CreateNewMetadataEntryClicked);

	Builder.AddMenuEntry(Label, Description, FSlateIcon(), UIAction);
}

void FAnimTimelineTrack_Curves::FillVariableCurveMenu(FMenuBuilder& Builder)
{
	FText Description = LOCTEXT("NewVariableCurveCreateNew_ToolTip", "Create a new variable curve");
	FText Label = LOCTEXT("NewVariableCurveCreateNew_Label", "Create Curve");
	FUIAction UIAction;
	UIAction.ExecuteAction.BindRaw(this, &FAnimTimelineTrack_Curves::CreateNewCurveClicked);

	Builder.AddMenuEntry(Label, Description, FSlateIcon(), UIAction);

	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
	USkeleton* CurrentSkeleton = AnimSequenceBase->GetSkeleton();
	check(CurrentSkeleton);

	const FSmartNameMapping* Mapping = CurrentSkeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
	TArray<USkeleton::AnimCurveUID> CurveUids;
	Mapping->FillUidArray(CurveUids);

	Builder.BeginSection(NAME_None, LOCTEXT("VariableMenu_ListHeading", "Available Names"));
	{
		TArray<FSmartNameSortItem> SmartNameList;

		for (USkeleton::AnimCurveUID Id : CurveUids)
		{
			if (!AnimSequenceBase->RawCurveData.GetCurveData(Id))
			{
				FName CurveName;
				if (Mapping->GetName(Id, CurveName))
				{
					SmartNameList.Add(FSmartNameSortItem(CurveName, Id));
				}
			}
		}

		{
			SmartNameList.Sort(FSmartNameSortItemSortOp());

			for (FSmartNameSortItem SmartNameItem : SmartNameList)
			{
				Description = LOCTEXT("NewVariableSubMenu_ToolTip", "Add an existing variable curve");
				Label = FText::FromName(SmartNameItem.SmartName);

				UIAction.ExecuteAction.BindRaw(
					this, &FAnimTimelineTrack_Curves::AddVariableCurve,
					SmartNameItem.ID);

				Builder.AddMenuEntry(Label, Description, FSlateIcon(), UIAction);
			}
		}
	}
	Builder.EndSection();
}

void FAnimTimelineTrack_Curves::AddMetadataEntry(USkeleton::AnimCurveUID Uid)
{
	FSmartName NewName;
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
	ensureAlways(AnimSequenceBase->GetSkeleton()->GetSmartNameByUID(USkeleton::AnimCurveMappingName, Uid, NewName));

	FScopedTransaction Transaction(LOCTEXT("AddCurveMetadata", "Add Curve Metadata"));

	AnimSequenceBase->Modify(true);

	if(AnimSequenceBase->RawCurveData.AddCurveData(NewName))
	{
		AnimSequenceBase->MarkRawDataAsModified();
		FFloatCurve* Curve = static_cast<FFloatCurve *>(AnimSequenceBase->RawCurveData.GetCurveData(Uid, ERawCurveTrackTypes::RCT_Float));
		Curve->FloatCurve.AddKey(0.0f, 1.0f);
		Curve->SetCurveTypeFlag(AACF_Metadata, true);
		
		GetModel()->RefreshTracks();
	}
}

void FAnimTimelineTrack_Curves::CreateNewMetadataEntryClicked()
{
	TSharedRef<STextEntryPopup> TextEntry =
		SNew(STextEntryPopup)
		.Label(LOCTEXT("NewMetadataCurveEntryLabal", "Metadata Name"))
		.OnTextCommitted(this, &FAnimTimelineTrack_Curves::CreateNewMetadataEntry);

	FSlateApplication& SlateApp = FSlateApplication::Get();
	SlateApp.PushMenu(
		OutlinerWidget.ToSharedRef(),
		FWidgetPath(),
		TextEntry,
		SlateApp.GetCursorPos(),
		FPopupTransitionEffect::TypeInPopup
		);
}

void FAnimTimelineTrack_Curves::CreateNewMetadataEntry(const FText& CommittedText, ETextCommit::Type CommitType)
{
	FSlateApplication::Get().DismissAllMenus();
	if(CommitType == ETextCommit::OnEnter)
	{
		// Add the name to the skeleton and then add the new curve to the sequence
		UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
		USkeleton* Skeleton = AnimSequenceBase->GetSkeleton();
		if(Skeleton && !CommittedText.IsEmpty())
		{
			FSmartName CurveName;

			if(Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName(*CommittedText.ToString()), CurveName))
			{
				AddMetadataEntry(CurveName.UID);
			}
		}
	}
}

void FAnimTimelineTrack_Curves::CreateNewCurveClicked()
{
	TSharedRef<STextEntryPopup> TextEntry =
		SNew(STextEntryPopup)
		.Label(LOCTEXT("NewCurveEntryLabal", "Curve Name"))
		.OnTextCommitted(this, &FAnimTimelineTrack_Curves::CreateTrack);

	FSlateApplication& SlateApp = FSlateApplication::Get();
	SlateApp.PushMenu(
		OutlinerWidget.ToSharedRef(),
		FWidgetPath(),
		TextEntry,
		SlateApp.GetCursorPos(),
		FPopupTransitionEffect::TypeInPopup
		);
}

void FAnimTimelineTrack_Curves::CreateTrack(const FText& ComittedText, ETextCommit::Type CommitInfo)
{
	if ( CommitInfo == ETextCommit::OnEnter )
	{
		UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
		USkeleton* Skeleton = AnimSequenceBase->GetSkeleton();
		if(Skeleton && !ComittedText.IsEmpty())
		{
			const FScopedTransaction Transaction(LOCTEXT("AnimCurve_AddTrack", "Add New Curve"));
			FSmartName NewTrackName;

			if(Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName(*ComittedText.ToString()), NewTrackName))
			{
				AddVariableCurve(NewTrackName.UID);
			}
		}

		FSlateApplication::Get().DismissAllMenus();
	}
}

void FAnimTimelineTrack_Curves::AddVariableCurve(USkeleton::AnimCurveUID CurveUid)
{
	FScopedTransaction Transaction(LOCTEXT("AddCurve", "Add Curve"));

	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
	AnimSequenceBase->Modify();
	
	USkeleton* Skeleton = AnimSequenceBase->GetSkeleton();
	FSmartName NewName;
	ensureAlways(Skeleton->GetSmartNameByUID(USkeleton::AnimCurveMappingName, CurveUid, NewName));
	AnimSequenceBase->RawCurveData.AddCurveData(NewName);
	AnimSequenceBase->MarkRawDataAsModified();

	GetModel()->RefreshTracks();
}

void FAnimTimelineTrack_Curves::HandleShowCurvePoints()
{
	GetMutableDefault<UPersonaOptions>()->bTimelineDisplayCurveKeys = !GetDefault<UPersonaOptions>()->bTimelineDisplayCurveKeys;
}

bool FAnimTimelineTrack_Curves::IsShowCurvePointsEnabled() const
{
	return GetDefault<UPersonaOptions>()->bTimelineDisplayCurveKeys;
}

#undef LOCTEXT_NAMESPACE