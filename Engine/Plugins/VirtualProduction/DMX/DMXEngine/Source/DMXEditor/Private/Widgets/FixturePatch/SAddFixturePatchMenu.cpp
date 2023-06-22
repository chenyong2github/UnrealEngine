// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAddFixturePatchMenu.h"

#include "Algo/Copy.h"
#include "Algo/Find.h"
#include "DMXAddFixturePatchMenuData.h"
#include "DMXEditor.h"
#include "DMXFixturePatchSharedData.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "FixturePatchAutoAssignUtility.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXLibrary.h"
#include "ScopedTransaction.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SDMXEntityDropdownMenu.h"
#include "Widgets/Input/SEditableTextBox.h"


#define LOCTEXT_NAMESPACE "SAddFixturePatchMenu"

namespace UE::DMXEditor::FixturePatchEditor
{
	SAddFixturePatchMenu::~SAddFixturePatchMenu()
	{
		SaveConfig();
	}

	void SAddFixturePatchMenu::Construct(const FArguments& InArgs, TWeakPtr<FDMXEditor> InWeakDMXEditor)
	{
		WeakDMXEditor = InWeakDMXEditor;

		SharedData = WeakDMXEditor.IsValid() ? WeakDMXEditor.Pin()->GetFixturePatchSharedData() : nullptr;
		UDMXLibrary* DMXLibrary = WeakDMXEditor.IsValid() ? WeakDMXEditor.Pin()->GetDMXLibrary() : nullptr;
		if (!SharedData.IsValid() || !DMXLibrary)
		{
			return;
		}

		const TArray<UDMXEntityFixtureType*> FixtureTypes = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixtureType>();
		UDMXAddFixturePatchMenuData* MenuData = GetMutableDefault<UDMXAddFixturePatchMenuData>();
		UDMXEntityFixtureType* const* SelectedFixtureTypePtr = Algo::FindByPredicate(FixtureTypes, [MenuData](const UDMXEntityFixtureType* FixtureType)
			{
				return FixtureType == MenuData->SoftFixtureType;
			});
		if (!SelectedFixtureTypePtr)
		{
			SelectedFixtureTypePtr = FixtureTypes.IsEmpty() ? nullptr : &FixtureTypes[0];
		}
		WeakFixtureType = SelectedFixtureTypePtr ? *SelectedFixtureTypePtr : nullptr;
		ActiveModeIndex = WeakFixtureType.IsValid() && WeakFixtureType->Modes.IsValidIndex(ActiveModeIndex) ? ActiveModeIndex : 0;

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SGridPanel)
				.FillColumn(1, 1.f)

				// Select Fixture Type
				+ SGridPanel::Slot(0, 0)
				.Padding(4.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SelectFixtureTypeLabel", "Fixture Type"))
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]

				+ SGridPanel::Slot(1, 0)
				.Padding(4.f)
				.VAlign(VAlign_Center)
				[
					MakeFixtureTypeSelectWidget()
				]

				// Select Mode
				+ SGridPanel::Slot(0, 1)
				.Padding(4.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.IsEnabled(this, &SAddFixturePatchMenu::HasValidFixtureTypeAndMode)
					.Text(LOCTEXT("SelectModeLabel", "Mode"))
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]
				+ SGridPanel::Slot(1, 1)
				.Padding(4.f)
				[
					MakeModeSelectWidget()
				]

				// Universe label 
				+ SGridPanel::Slot(0, 2)
				.Padding(4.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.IsEnabled(this, &SAddFixturePatchMenu::HasValidFixtureTypeAndMode)
					.Text(LOCTEXT("UniverseDotChannelLabel", "Universe.Channel"))
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]

				// Select universe and channel 
				+ SGridPanel::Slot(1, 2)
				.Padding(4.f)
				.VAlign(VAlign_Center)
				[
					MakeUniverseChannelSelectWidget()
				]

				// Num Fixture Patches Label
				+ SGridPanel::Slot(0, 3)
				.Padding(4.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.IsEnabled(this, &SAddFixturePatchMenu::HasValidFixtureTypeAndMode)
					.Text(LOCTEXT("NumPatchesLabel", "Num Patches"))
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]

				// Num Fixture Patches editable Text Box
				+ SGridPanel::Slot(1, 3)
				.Padding(4.f)
				[
					MakeNumFixturePatchesEditableTextBox()
				]

				// Add fixture patches button
				+ SGridPanel::Slot(1, 4)
				.Padding(4.f)
				[
					MakeAddFixturePatchesButton()
				]
			]
		];

		RequestRefresh();

		UDMXLibrary::GetOnEntitiesAdded().AddSP(this, &SAddFixturePatchMenu::OnEntityAddedOrRemoved);
		UDMXLibrary::GetOnEntitiesRemoved().AddSP(this, &SAddFixturePatchMenu::OnEntityAddedOrRemoved);
	}

	void SAddFixturePatchMenu::RequestRefresh()
	{
		RequestRefreshModeComboBoxTimerHandle.Invalidate();

		if (!RequestRefreshModeComboBoxTimerHandle.IsValid())
		{
			RequestRefreshModeComboBoxTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &SAddFixturePatchMenu::ForceRefresh));
		}
	}

	void SAddFixturePatchMenu::SaveConfig()
	{
		UDMXAddFixturePatchMenuData* Data = GetMutableDefault<UDMXAddFixturePatchMenuData>();
		Data->SoftFixtureType = WeakFixtureType.Get();
		Data->ActiveModeIndex = ActiveModeIndex;
		Data->SaveConfig();
	}

	TSharedRef<SWidget> SAddFixturePatchMenu::MakeFixtureTypeSelectWidget()
	{
		return
			SNew(SBox)
			.MinDesiredWidth(100.f)
			[
				SAssignNew(FixtureTypeSelector, SDMXEntityPickerButton<UDMXEntityFixtureType>)
				.DMXEditor(WeakDMXEditor)
				.CurrentEntity_Lambda([this] { return WeakFixtureType.Get(); })
				.OnEntitySelected(this, &SAddFixturePatchMenu::OnFixtureTypeSelected)
			];
	}

	TSharedRef<SWidget> SAddFixturePatchMenu::MakeModeSelectWidget()
	{
		return
			SAssignNew(ModeComboBox, SComboBox<TSharedPtr<uint32>>)
			.IsEnabled(this, &SAddFixturePatchMenu::HasValidFixtureTypeAndMode)
			.OptionsSource(&ModeSources)
			.OnGenerateWidget(this, &SAddFixturePatchMenu::GenerateModeComboBoxEntry)
			.OnSelectionChanged(this, &SAddFixturePatchMenu::OnModeSelected)
			.InitiallySelectedItem(0)
			[
				SNew(STextBlock)
				.MinDesiredWidth(50.0f)
				.Text(this, &SAddFixturePatchMenu::GetActiveModeText)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			];
	}

	TSharedRef<SWidget> SAddFixturePatchMenu::MakeUniverseChannelSelectWidget()
	{
		return
			SNew(SBox)
			.HAlign(HAlign_Left)
			[
				SNew(SEditableTextBox)
				.IsEnabled(this, &SAddFixturePatchMenu::HasValidFixtureTypeAndMode)
				.MinDesiredWidth(60.f)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.SelectAllTextWhenFocused(true)
				.ClearKeyboardFocusOnCommit(true)
				.Text(this, &SAddFixturePatchMenu::GetUniverseChannelText)
				.OnVerifyTextChanged(this, &SAddFixturePatchMenu::OnVerifyUniverseChannelText)
				.OnTextCommitted(this, &SAddFixturePatchMenu::OnUniverseChannelTextCommitted)
			];
	}

	TSharedRef<SWidget> SAddFixturePatchMenu::MakeNumFixturePatchesEditableTextBox()
	{
		return
			SNew(SBox)
			.HAlign(HAlign_Left)
			[
				SNew(SEditableTextBox)
				.IsEnabled(this, &SAddFixturePatchMenu::HasValidFixtureTypeAndMode)
				.MinDesiredWidth(60.f)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.SelectAllTextWhenFocused(true)
				.ClearKeyboardFocusOnCommit(true)
				.Text_Lambda([this]()
					{
						return FText::FromString(FString::FromInt(NumFixturePatchesToAdd));
					})
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
					{
						int32 Value;
						if (LexTryParseString<int32>(Value, *Text.ToString()) &&
							Value > 0)
						{
							constexpr int32 MaxNumFixturePatchesToAdd = 16384;
							NumFixturePatchesToAdd = FMath::Clamp(Value, 1, MaxNumFixturePatchesToAdd);
						}

						// Add fixture patches if enter is pressed
						if (CommitType == ETextCommit::OnEnter)
						{
							OnAddFixturePatchButtonClicked();
						}
					})
			];
	}

	TSharedRef<SWidget> SAddFixturePatchMenu::MakeAddFixturePatchesButton()
	{
		return
			SNew(SBox)
			.HAlign(HAlign_Right)
			.MinDesiredWidth(120.f)
			[
				SNew(SButton)
				.IsEnabled(this, &SAddFixturePatchMenu::HasValidFixtureTypeAndMode)
				.ContentPadding(FMargin(4.0f, 4.0f))
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.ForegroundColor(FLinearColor::White)
				.OnClicked(this, &SAddFixturePatchMenu::OnAddFixturePatchButtonClicked)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
						{
							return FText::Format(LOCTEXT("AddFixturePatchButtonText", "Add Fixture {0}|plural(one=Patch, other=Patches)"), NumFixturePatchesToAdd);
						})
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
				]
			];
	}

	TSharedRef<SWidget> SAddFixturePatchMenu::GenerateModeComboBoxEntry(const TSharedPtr<uint32> InModeIndex) const
	{
		UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(WeakFixtureType);
		if (!FixtureType)
		{
			return SNullWidget::NullWidget;
		}

		const TArray<FDMXFixtureMode>& Modes = FixtureType->Modes;

		return
			SNew(STextBlock)
			.MinDesiredWidth(50.0f)
			.Text_Lambda([&Modes, ModeIndex = *InModeIndex, this]()
				{
					const UDMXAddFixturePatchMenuData* MenuData = GetDefault<UDMXAddFixturePatchMenuData>();
					if (Modes.IsValidIndex(ModeIndex))
					{
						return FText::FromString(Modes[ModeIndex].ModeName);
					}
					else
					{
						return LOCTEXT("NoModeAvailableText", "No Mode available");
					}
				})
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
	}

	void SAddFixturePatchMenu::ForceRefresh()
	{
		RequestRefreshModeComboBoxTimerHandle.Invalidate();


		if (!WeakFixtureType.IsValid() && WeakDMXEditor.IsValid())
		{
			if (UDMXLibrary* DMXLibrary = WeakDMXEditor.Pin()->GetDMXLibrary())
			{
				const TArray<UDMXEntityFixtureType*> FixtureTypes = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixtureType>();
				WeakFixtureType = FixtureTypes.IsEmpty() ? nullptr : FixtureTypes[0];
			}
		}

		ModeSources.Reset();
		if (UDMXEntityFixtureType* FixtureType = WeakFixtureType.Get())
		{
			for (int32 ModeIndex = 0; ModeIndex < FixtureType->Modes.Num(); ModeIndex++)
			{
				ModeSources.Add(MakeShared<uint32>(ModeIndex));
			}
		}

		ModeComboBox->RefreshOptions();
	}

	void SAddFixturePatchMenu::OnEntityAddedOrRemoved(UDMXLibrary* DMXLibrary, TArray<UDMXEntity*> Entities)
	{
		RequestRefresh();
	}

	void SAddFixturePatchMenu::OnFixtureTypeSelected(UDMXEntity* InSelectedFixtureType)
	{
		WeakFixtureType = Cast<UDMXEntityFixtureType>(InSelectedFixtureType);

		RequestRefresh();
	}

	void SAddFixturePatchMenu::OnModeSelected(TSharedPtr<uint32> InSelectedMode, ESelectInfo::Type SelectInfo)
	{
		ActiveModeIndex = InSelectedMode.IsValid() ? *InSelectedMode : 0;
	}

	FText SAddFixturePatchMenu::GetUniverseChannelText() const
	{
		if (!Universe.IsSet() && !Channel.IsSet())
		{
			const int32 SelectedUniverse = SharedData->GetSelectedUniverse();
			return FText::FromString(FString::FromInt(SelectedUniverse) + TEXT(".") + FString::FromInt(1));
		}
		else
		{
			const FString UnivereString = Universe.IsSet() ? FString::FromInt(Universe.GetValue()) : TEXT("");
			const FString ChannelString = Channel.IsSet() ? FString::FromInt(Channel.GetValue()) : TEXT("");
			const FString Separator = Universe.IsSet() && Channel.IsSet() ? TEXT(".") : TEXT("");

			return FText::FromString(UnivereString + Separator + ChannelString);
		}
	}

	bool SAddFixturePatchMenu::OnVerifyUniverseChannelText(const FText& InNewText, FText& OutErrorMessage)
	{
		const FString String = InNewText.ToString();
		int32 IndexOfDot = INDEX_NONE;
		InNewText.ToString().FindChar('.', IndexOfDot);
		if (IndexOfDot == INDEX_NONE && !String.IsNumeric())
		{
			OutErrorMessage = LOCTEXT("InvalidUniverseInfo", "Universe, for example '3' or Universe.Channel, for example '4.5'");
			return false;
		}
		return true;
	}

	void SAddFixturePatchMenu::OnUniverseChannelTextCommitted(const FText& Text, ETextCommit::Type CommitType)
	{
		static const TCHAR* Delimiter[] = { TEXT("."), TEXT(","), TEXT(":"), TEXT(" ") };
		TArray<FString> Substrings;
		constexpr bool bCullEmpty = true;
		Text.ToString().ParseIntoArray(Substrings, Delimiter, 4, bCullEmpty);

		int32 NewUniverse;
		if (Substrings.IsValidIndex(0) &&
			LexTryParseString(NewUniverse, *Substrings[0]))
		{
			Universe = NewUniverse;
			SharedData->SelectUniverse(NewUniverse);
		}
		else
		{
			Universe.Reset();
		}

		int32 NewChannel;
		if (Substrings.IsValidIndex(1) &&
			LexTryParseString(NewChannel, *Substrings[1]))
		{
			Channel = NewChannel;
		}
		else
		{
			Channel.Reset();
		}

		// Add fixture patches if enter is pressed
		if (CommitType == ETextCommit::OnEnter)
		{
			OnAddFixturePatchButtonClicked();
		}
	}

	FReply SAddFixturePatchMenu::OnAddFixturePatchButtonClicked()
	{
		FSlateApplication::Get().DismissAllMenus();

		UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(WeakFixtureType);
		if (!FixtureType || FixtureType->Modes.IsEmpty())
		{
			return FReply::Handled();
		}

		const TSharedPtr<FDMXEditor> DMXEditor = WeakDMXEditor.Pin();
		if (!DMXEditor.IsValid())
		{
			return FReply::Handled();
		}

		UDMXLibrary* DMXLibrary = DMXEditor->GetDMXLibrary();
		if (!DMXLibrary)
		{
			return FReply::Handled();
		}

		// Ensure valid mode
		if (!ensureMsgf(FixtureType->Modes.IsValidIndex(ActiveModeIndex), TEXT("Cannot apply mode. Mode index is invalid.")))
		{
			return FReply::Handled();
		}

		// Create new fixture patches
		const FScopedTransaction CreateFixturePatchTransaction(LOCTEXT("CreateFixturePatchTransaction", "Create Fixture Patch"));
		DMXLibrary->PreEditChange(UDMXLibrary::StaticClass()->FindPropertyByName(UDMXLibrary::GetEntitiesPropertyName()));

		const int32 PatchToUniverse = Universe.IsSet() ? Universe.GetValue() : SharedData->GetSelectedUniverse();

		TArray<UDMXEntityFixturePatch*> NewFixturePatches;
		NewFixturePatches.Reserve(NumFixturePatchesToAdd);
		for (uint32 iNumFixturePatchesAdded = 0; iNumFixturePatchesAdded < NumFixturePatchesToAdd; iNumFixturePatchesAdded++)
		{
			FDMXEntityFixturePatchConstructionParams FixturePatchConstructionParams;
			FixturePatchConstructionParams.FixtureTypeRef = FDMXEntityFixtureTypeRef(FixtureType);
			FixturePatchConstructionParams.ActiveMode = ActiveModeIndex;
			FixturePatchConstructionParams.UniverseID = PatchToUniverse;
			FixturePatchConstructionParams.StartingAddress = Channel.IsSet() ? Channel.GetValue() : 1;

			constexpr bool bMarkLibraryDirty = false;
			UDMXEntityFixturePatch* NewFixturePatch = UDMXEntityFixturePatch::CreateFixturePatchInLibrary(FixturePatchConstructionParams, FixtureType->Name, bMarkLibraryDirty);
			NewFixturePatches.Add(NewFixturePatch);
		}

		// Align
		using namespace UE::DMXEditor::AutoAssign;
		FAutoAssignUtility::Align(NewFixturePatches);

		DMXLibrary->PostEditChange();

		// Select universe and new fixture patches
		SharedData->SelectUniverse(PatchToUniverse);

		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> NewWeakFixturePatches;
		NewWeakFixturePatches.Reserve(NumFixturePatchesToAdd);
		Algo::Copy(NewFixturePatches, NewWeakFixturePatches);
		SharedData->SelectFixturePatches(NewWeakFixturePatches);

		return FReply::Handled();
	}

	FText SAddFixturePatchMenu::GetActiveModeText() const
	{
		UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(WeakFixtureType);
		if (!FixtureType)
		{
			return FText::GetEmpty();
		}

		const TArray<FDMXFixtureMode>& Modes = FixtureType->Modes;
		const UDMXAddFixturePatchMenuData* MenuData = GetMutableDefault<UDMXAddFixturePatchMenuData>();
		if (Modes.Num() > 0 && Modes.IsValidIndex(MenuData->ActiveModeIndex))
		{
			return FText::FromString(Modes[MenuData->ActiveModeIndex].ModeName);
		}
		else if (Modes.IsEmpty())
		{
			return LOCTEXT("NoModeAvailableComboButtonText", "No Modes in Fixture Type");
		}
		else
		{
			return LOCTEXT("NoFixtureTypeSelectedComboButtonText", "No Fixture Type selected");
		}
	}

	bool SAddFixturePatchMenu::HasValidFixtureTypeAndMode() const
	{
		return WeakFixtureType.IsValid() && !WeakFixtureType->Modes.IsEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
