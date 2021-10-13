// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimTimelineTrack_Curves.h"
#include "PersonaUtils.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "AnimSequenceTimelineCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Animation/AnimSequenceBase.h"
#include "Widgets/Input/STextEntryPopup.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"
#include "Animation/AnimMontage.h"
#include "SAnimOutlinerItem.h"
#include "Preferences/PersonaOptions.h"
#include "SListViewSelectorDropdownMenu.h"
#include "Animation/AnimData/AnimDataModel.h"

#define LOCTEXT_NAMESPACE "FAnimTimelineTrack_Notifies"

const float FAnimTimelineTrack_Curves::CurveListPadding = 8.0f;

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
				return FText::Format(LOCTEXT("CurveCountFormat", "({0})"), FText::AsNumber(AnimSequenceBase->GetDataModel()->GetNumberOfFloatCurves())); 
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
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
	IAnimationDataController& Controller = AnimSequenceBase->GetController();
	Controller.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Float);
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
		if(AnimSequenceBase->GetDataModel()->GetNumberOfFloatCurves() > 0)
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

struct FSmartNameSortItemSortOp
{
	bool operator()(const FSmartNameSortItem& A, const FSmartNameSortItem& B) const
	{
		return (A.SmartName.Compare(B.SmartName) < 0);
	}
};

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

		const TArray<FFloatCurve>& FloatCurves = AnimSequenceBase->GetDataModel()->GetFloatCurves();

		for (USkeleton::AnimCurveUID Id : CurveUids)
		{
			FSmartName SmartName;
			if (Mapping->FindSmartNameByUID(Id, SmartName) && !FloatCurves.ContainsByPredicate([SmartName](FFloatCurve& Curve)
			{
				return Curve.Name == SmartName;
			}))
			{
				SmartNameList.Add(FSmartNameSortItem(SmartName.DisplayName, Id));
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

TSharedRef<ITableRow> FAnimTimelineTrack_Curves::GenerateCurveListRow(FCurveListItem InItem, const TSharedRef<STableViewBase>& OwnerList)
{
	return SNew(STableRow<FCurveListItem>, OwnerList)
		.Padding(FMargin(CurveListPadding, 0.0))
		[
			SNew(STextBlock).Text(FText::FromString(InItem.Get()->SmartName.ToString())).HighlightText(SearchText)
		];
}

void FAnimTimelineTrack_Curves::OnTypeSelectionChanged(FCurveListItem Selection, ESelectInfo::Type SelectInfo)
{
	// When the user is navigating, do not act upon the selection change
	if (SelectInfo == ESelectInfo::OnNavigation)
	{
		return;
	}

	if (Selection.IsValid())
	{
		AddVariableCurve(Selection->ID);
		FSlateApplication::Get().DismissAllMenus();
	}
}

void FAnimTimelineTrack_Curves::OnMouseButtonClicked(FCurveListItem Selection)
{
	if (Selection.IsValid())
	{
		AddVariableCurve(Selection->ID);
		FSlateApplication::Get().DismissAllMenus();
	}
}

void FAnimTimelineTrack_Curves::OnCurveFilterTextChanged(const FText& NewText)
{
	SearchText = NewText;
	FilteredCurveItems.Empty();

	GetCurvesMatchingSearch(NewText, CurveItems, FilteredCurveItems);
	CurveListView->RequestListRefresh();

	auto SelectedItems = CurveListView->GetSelectedItems();
	if (FilteredCurveItems.Num() > 0)
	{
		CurveListView->SetSelection(FilteredCurveItems[0], ESelectInfo::OnNavigation);
	}
}

bool FAnimTimelineTrack_Curves::GetCurvesMatchingSearch(const FText& InSearchText, const TArray<FCurveListItem>& UnfilteredList, TArray<FCurveListItem>& OutFilteredList)
{
	// Trim and sanitized the filter text (so that it more likely matches the action descriptions)
	FString TrimmedFilterString = FText::TrimPrecedingAndTrailing(InSearchText).ToString();

	// Tokenize the search box text into a set of terms; all of them must be present to pass the filter
	TArray<FString> FilterTerms;
	TrimmedFilterString.ParseIntoArray(FilterTerms, TEXT(" "), true);

	// Generate a list of sanitized versions of the strings
	TArray<FString> SanitizedFilterTerms;
	for (int32 iFilters = 0; iFilters < FilterTerms.Num(); iFilters++)
	{
		FString EachString = FName::NameToDisplayString(FilterTerms[iFilters], false);
		EachString = EachString.Replace(TEXT(" "), TEXT(""));
		SanitizedFilterTerms.Add(EachString);
	}

	ensure(SanitizedFilterTerms.Num() == FilterTerms.Num());

	bool bReturnVal = false;

	for (auto it = UnfilteredList.CreateConstIterator(); it; ++it)
	{
		FSmartNameSortItem CurItem = *(*it).Get();

		const bool bIsEmptySearch = InSearchText.IsEmpty();
		bool bFilterTextMatches = true;

		if (!bIsEmptySearch)
		{
			const FString DescriptionString = CurItem.SmartName.ToString();
			const FString MangledDescriptionString = DescriptionString.Replace(TEXT(" "), TEXT(""));

			for (int32 FilterIndex = 0; FilterIndex < FilterTerms.Num() && bFilterTextMatches; ++FilterIndex)
			{
				const bool bMatchesTerm = MangledDescriptionString.Contains(FilterTerms[FilterIndex]) || MangledDescriptionString.Contains(SanitizedFilterTerms[FilterIndex]);
				bFilterTextMatches = bFilterTextMatches && bMatchesTerm;
			}
		}
		if (bIsEmptySearch || bFilterTextMatches)
		{
			OutFilteredList.Add(MakeShared<FSmartNameSortItem>(CurItem));
			bReturnVal = true;
		}
	}
	return bReturnVal;
}

void FAnimTimelineTrack_Curves::OnCurveFilterTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo)
{
	if (CommitInfo == ETextCommit::OnEnter)
	{
		auto SelectedItems = CurveListView->GetSelectedItems();
		if (SelectedItems.Num() > 0)
		{
			CurveListView->SetSelection(SelectedItems[0]);
		}
	}
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
	CurveItems.Empty();
	Builder.BeginSection(NAME_None, LOCTEXT("VariableMenu_ListHeading", "Available Names"));

	for (USkeleton::AnimCurveUID Id : CurveUids)
	{
		if (!AnimSequenceBase->GetDataModel()->FindFloatCurve(FAnimationCurveIdentifier(Id, ERawCurveTrackTypes::RCT_Float)))
		{
			FName CurveName;
			if (Mapping->GetName(Id, CurveName))
			{
				CurveItems.Add(MakeShared<FSmartNameSortItem>(FSmartNameSortItem(CurveName, Id)));
			}
		}
	}

	// Build a SearchBox followed by a list of all the available curves
	FilteredCurveItems = CurveItems; 
	TSharedPtr<SSearchBox>	CurveFilterTextBox;
	TSharedPtr<SMenuOwner>	MenuContent;

	SAssignNew(CurveListView, SCurveListView)
		.ListItemsSource(&FilteredCurveItems)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &FAnimTimelineTrack_Curves::GenerateCurveListRow)
		.OnMouseButtonClick(this, &FAnimTimelineTrack_Curves::OnMouseButtonClicked)
		.OnSelectionChanged(this, &FAnimTimelineTrack_Curves::OnTypeSelectionChanged);

	SAssignNew(CurveFilterTextBox, SSearchBox)
		.OnTextChanged(this, &FAnimTimelineTrack_Curves::OnCurveFilterTextChanged)
		.OnTextCommitted(this, &FAnimTimelineTrack_Curves::OnCurveFilterTextCommitted);

	SAssignNew(MenuContent, SMenuOwner)
		[
			SNew(SListViewSelectorDropdownMenu<FCurveListItem>, CurveFilterTextBox, CurveListView)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(CurveListPadding, 2.0))
				[
					CurveFilterTextBox.ToSharedRef()
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				.VAlign(VAlign_Fill)
				.Padding(CurveListPadding, 2.0)
				[
					SNew(SBox)
					.WidthOverride(300.0f)
					.HeightOverride(300.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("Graph.StateNode.Body"))
							.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Input"))
						]
						+SOverlay::Slot()
						[
							SNew(SScrollBox)
							.Orientation(EOrientation::Orient_Vertical)
							+ SScrollBox::Slot()
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Fill)
							[
								CurveListView.ToSharedRef()
							]
						]
					]
				]
			]
		];

	Builder.AddWidget(MenuContent.ToSharedRef(), FText::GetEmpty(),  false);

	Builder.EndSection();
}

void FAnimTimelineTrack_Curves::AddMetadataEntry(USkeleton::AnimCurveUID Uid)
{
	FSmartName NewName;
	UAnimSequenceBase* AnimSequenceBase = GetModel()->GetAnimSequenceBase();
	ensureAlways(AnimSequenceBase->GetSkeleton()->GetSmartNameByUID(USkeleton::AnimCurveMappingName, Uid, NewName));

	IAnimationDataController& Controller = AnimSequenceBase->GetController();
	IAnimationDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("AddCurveMetadata", "Add Curve Metadata"));

	const FAnimationCurveIdentifier MetadataCurveId(NewName, ERawCurveTrackTypes::RCT_Float);
	Controller.AddCurve(MetadataCurveId, AACF_Metadata);
	Controller.SetCurveKeys(MetadataCurveId, { FRichCurveKey(0.f, 1.f) });	
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

			Skeleton->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, FName(*ComittedText.ToString()), NewTrackName);
			if ( NewTrackName.IsValid() )
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

	IAnimationDataController& Controller = AnimSequenceBase->GetController();
	const FAnimationCurveIdentifier FloatCurveId(NewName, ERawCurveTrackTypes::RCT_Float);
	Controller.AddCurve(FloatCurveId);
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