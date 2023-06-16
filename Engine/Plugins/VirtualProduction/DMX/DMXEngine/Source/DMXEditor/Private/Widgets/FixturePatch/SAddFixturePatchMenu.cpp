// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAddFixturePatchMenu.h"

#include "Algo/Copy.h"
#include "Algo/Find.h"
#include "DMXAddFixturePatchMenuData.h"
#include "DMXFixturePatchSharedData.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "FixturePatchAutoAssignUtility.h"
#include "Library/DMXEntityFixtureType.h"
#include "ScopedTransaction.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SDMXEntityDropdownMenu.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"


#define LOCTEXT_NAMESPACE "SAddFixturePatchMenu"

namespace UE::DMXEditor::FixturePatchEditor
{

	SAddFixturePatchMenu::~SAddFixturePatchMenu()
	{
		UDMXAddFixturePatchMenuData* Model = GetMutableDefault<UDMXAddFixturePatchMenuData>();
		Model->SoftFixtureType = WeakFixtureType.IsValid() ? WeakFixtureType.Get() : Model->SoftFixtureType;
		Model->ActiveModeIndex = WeakFixtureType.IsValid() ? ActiveModeIndex : Model->ActiveModeIndex;
		Model->SaveConfig();
	}

	void SAddFixturePatchMenu::Construct(const FArguments& InArgs, TWeakPtr<FDMXEditor> InWeakDMXEditor)
	{
		WeakDMXEditor = InWeakDMXEditor;

		ChildSlot
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
				SNew(SBox)
				.MinDesiredWidth(100.f)
				[
					SAssignNew(FixtureTypeSelector, SDMXEntityPickerButton<UDMXEntityFixtureType>)
					.DMXEditor(WeakDMXEditor)
					.CurrentEntity_Lambda([this] { return WeakFixtureType.Get(); })
					.OnEntitySelected(this, &SAddFixturePatchMenu::OnFixtureTypeSelected)	
				]
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
				]
			]

			// Num Fixture Patches Label
			+ SGridPanel::Slot(0, 2)
			.Padding(4.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.IsEnabled(this, &SAddFixturePatchMenu::HasValidFixtureTypeAndMode)
				.Text(LOCTEXT("NumPatchesLabel", "Num Patches"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			]

			// Num Fixture Patches editable Text Box
			+ SGridPanel::Slot(1, 2)
			.Padding(4.f)
			[
				SNew(SBox)
				.HAlign(HAlign_Left)
				[
					SNew(SEditableTextBox)
					.IsEnabled(this, &SAddFixturePatchMenu::HasValidFixtureTypeAndMode)
					.MinDesiredWidth(60.f)
					.SelectAllTextWhenFocused(true)
					.ClearKeyboardFocusOnCommit(false)
					.SelectAllTextOnCommit(true)
					.Text_Lambda([this]()
						{
							return FText::FromString(FString::FromInt(NumFixturePatchesToAdd));
						})
					.OnVerifyTextChanged_Lambda([](const FText& InNewText, FText& OutErrorMessage)
						{
							int32 Value;
							if (!LexTryParseString<int32>(Value, *InNewText.ToString()) ||
								Value < 1)
							{
								OutErrorMessage = LOCTEXT("BulkAddPatchesBadString", "Numeric values > 0 only");
								return false;
							}
							return true;
						})
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
						{
							int32 Value;
							if (LexTryParseString<int32>(Value, *Text.ToString()) &&
								Value > 0)
							{
								constexpr int32 MaxNumPatchesToBulkAdd = 32768;
								NumFixturePatchesToAdd = FMath::Min(Value, MaxNumPatchesToBulkAdd);
							}
						})
				]
			]

			// Add fixture patches button
			+ SGridPanel::Slot(1, 3)
			.Padding(4.f)
			[
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
				]
			]
		];

		RequestRefresh();
	}

	void SAddFixturePatchMenu::ForceRefresh()
	{
		RequestRefreshModeComboBoxTimerHandle.Invalidate();

		UDMXLibrary* DMXLibrary = WeakDMXEditor.IsValid() ? WeakDMXEditor.Pin()->GetDMXLibrary() : nullptr;
		if (!WeakDMXEditor.IsValid())
		{
			return;
		}
		const TArray<UDMXEntityFixtureType*> FixtureTypes = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixtureType>();

		// Init 
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

	void SAddFixturePatchMenu::RequestRefresh()
	{ 
		if (!RequestRefreshModeComboBoxTimerHandle.IsValid())
		{
			RequestRefreshModeComboBoxTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &SAddFixturePatchMenu::ForceRefresh));
		}
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
		const TSharedPtr<FDMXFixturePatchSharedData> SharedData = DMXEditor->GetFixturePatchSharedData();
		if (!SharedData.IsValid())
		{
			return FReply::Handled();
		}
		UDMXLibrary* DMXLibrary = DMXEditor->GetDMXLibrary();
		if (!DMXLibrary)
		{
			return FReply::Handled();
		}

		// Ensure valid mode
		UDMXAddFixturePatchMenuData* MenuData = GetMutableDefault<UDMXAddFixturePatchMenuData>();
		if (!ensureMsgf(FixtureType->Modes.IsValidIndex(MenuData->ActiveModeIndex), TEXT("Cannot apply mode. Mode index is invalid.")))
		{
			MenuData->ActiveModeIndex = 0;
		}

		// Create a new fixture patches
		const FScopedTransaction CreateFixturePatchTransaction(LOCTEXT("CreateFixturePatchTransaction", "Create Fixture Patch"));
		DMXLibrary->PreEditChange(UDMXLibrary::StaticClass()->FindPropertyByName(UDMXLibrary::GetEntitiesPropertyName()));

		TArray<UDMXEntityFixturePatch*> NewFixturePatches;
		NewFixturePatches.Reserve(NumFixturePatchesToAdd);
		for (uint32 iNumFixturePatchesAdded = 0; iNumFixturePatchesAdded < NumFixturePatchesToAdd; iNumFixturePatchesAdded++)
		{
			FDMXEntityFixturePatchConstructionParams FixturePatchConstructionParams;
			FixturePatchConstructionParams.FixtureTypeRef = FDMXEntityFixtureTypeRef(FixtureType);
			FixturePatchConstructionParams.ActiveMode = MenuData->ActiveModeIndex;
			FixturePatchConstructionParams.UniverseID = SharedData->GetSelectedUniverse();
			FixturePatchConstructionParams.StartingAddress = 1;

			constexpr bool bMarkLibraryDirty = false;
			UDMXEntityFixturePatch* NewFixturePatch = UDMXEntityFixturePatch::CreateFixturePatchInLibrary(FixturePatchConstructionParams, FixtureType->Name, bMarkLibraryDirty);
			NewFixturePatches.Add(NewFixturePatch);
		}

		// Align and Auto assign to the currently selected universe
		using namespace UE::DMXEditor::AutoAssign;
		FAutoAssignUtility::Align(NewFixturePatches);
		const int32 AssignedToUniverse = FAutoAssignUtility::AutoAssign(EAutoAssignMode::SelectedUniverse, DMXEditor.ToSharedRef(), NewFixturePatches);

		DMXLibrary->PostEditChange();

		// Select universe and new fixture patches
		SharedData->SelectUniverse(AssignedToUniverse);

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
