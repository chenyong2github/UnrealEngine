// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXMVRFixtureListToolbar.h"

#include "DMXEditor.h"
#include "DMXFixturePatchSharedData.h"
#include "DMXMVRFixtureListItem.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Commands/DMXEditorCommands.h"
#include "Widgets/SDMXEntityDropdownMenu.h"

#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "Algo/MaxElement.h"
#include "Internationalization/Regex.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"


#define LOCTEXT_NAMESPACE "SDMXMVRFixtureListToolbar"

namespace UE::DMXRuntime::SDMXMVRFixtureListToolbar::Private
{
	UE_NODISCARD bool ParseUniverse(const FString& InputString, int32& OutUniverse)
	{
		// Try to match addresses formating, e.g. '1.', '1:' etc.
		static const TCHAR* ParamDelimiters[] =
		{
			TEXT("."),
			TEXT(","),
			TEXT(":"),
			TEXT(";")
		};

		TArray<FString> ValueStringArray;
		constexpr bool bParseEmpty = true;
		const FString InputStringWithSpace = InputString + TEXT(" "); // So ValueStringArray will be lenght of 2 if Address is empty, e.g. '0.'
		InputStringWithSpace.ParseIntoArray(ValueStringArray, ParamDelimiters, 4, bParseEmpty);
		if (ValueStringArray.Num() == 2)
		{
			if (LexTryParseString<int32>(OutUniverse, *ValueStringArray[0]))
			{
				return true;
			}
		}

		// Try to match strings starting with Uni, e.g. 'Uni 1', 'Universe 1', 'Universe1'
		if (InputString.StartsWith(TEXT("Uni")))
		{
			const FRegexPattern SequenceOfDigitsPattern(TEXT("^[^\\d]*(\\d+)"));
			FRegexMatcher Regex(SequenceOfDigitsPattern, *InputString);
			if (Regex.FindNext())
			{
				const FString UniverseString = Regex.GetCaptureGroup(1);
				if (LexTryParseString<int32>(OutUniverse, *UniverseString))
				{
					return true;
				}
			}
		}

		OutUniverse = -1;
		return false;
	}

	UE_NODISCARD bool ParseAddress(const FString& InputString, int32& OutAddress)
	{
		// Try to match addresses formating, e.g. '1.1', '1:1' etc.
		static const TCHAR* ParamDelimiters[] =
		{
			TEXT("."),
			TEXT(","),
			TEXT(":"),
			TEXT(";")
		};

		TArray<FString> ValueStringArray;
		constexpr bool bParseEmpty = false;
		InputString.ParseIntoArray(ValueStringArray, ParamDelimiters, 4, bParseEmpty);

		if (ValueStringArray.Num() == 2)
		{
			if (LexTryParseString<int32>(OutAddress, *ValueStringArray[1]))
			{
				return true;
			}
		}

		// Try to match strings starting with Uni Ad, e.g. 'Uni 1 Ad 1', 'Universe 1 Address 1', 'Universe1Address1'
		if (InputString.StartsWith(TEXT("Uni")) &&
			InputString.Contains(TEXT("Ad")))
		{
			const FRegexPattern SequenceOfDigitsPattern(TEXT("^[^\\d]*(\\d+)[^\\d]*(\\d+)"));
			FRegexMatcher Regex(SequenceOfDigitsPattern, *InputString);
			if (Regex.FindNext())
			{
				const FString AddressString = Regex.GetCaptureGroup(2);
				if (LexTryParseString<int32>(OutAddress, *AddressString))
				{
					return true;
				}
			}
		}

		OutAddress = -1;
		return false;
	}

	UE_NODISCARD bool ParseFixtureID(const FString& InputString, int32& OutFixtureID)
	{
		if (LexTryParseString<int32>(OutFixtureID, *InputString))
		{
			return true;
		}

		OutFixtureID = -1;
		return false;
	}
}

void SDMXMVRFixtureListToolbar::Construct(const FArguments& InArgs, TWeakPtr<FDMXEditor> InDMXEditor)
{
	WeakDMXEditor = InDMXEditor;
	OnSearchChanged = InArgs._OnSearchChanged;

	ChildSlot
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.Padding(FMargin(8.f, 8.f))
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f))
			.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
			[
				SNew(SWrapBox)
				.InnerSlotPadding(FVector2D(14.f, 8.f))
				.UseAllottedWidth(true)

				// Add Fixture Button
				+ SWrapBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					GenerateFixtureTypeDropdownMenu()
				]
										
				+ SWrapBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SSeparator)
					.Orientation(EOrientation::Orient_Vertical)
				]

				// Search
				+ SWrapBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SSearchBox)
					.MinDesiredWidth(400.f)
					.OnTextChanged(this, &SDMXMVRFixtureListToolbar::OnSearchTextChanged)
				]
									
				+ SWrapBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SSeparator)
					.Orientation(EOrientation::Orient_Vertical)
				]

				// Show Conflicts Only option
				+ SWrapBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ShowConflictsOnlyCheckBoxLabel", "Show Conflicts only"))
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]

				+ SWrapBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked(false)
					.OnCheckStateChanged(this, &SDMXMVRFixtureListToolbar::OnShowConflictsOnlyCheckStateChanged)
				]
			]
		];
}

TArray<TSharedPtr<FDMXMVRFixtureListItem>> SDMXMVRFixtureListToolbar::FilterItems(const TArray<TSharedPtr<FDMXMVRFixtureListItem>>& Items)
{
	using namespace UE::DMXRuntime::SDMXMVRFixtureListToolbar::Private;

	// Apply 'conflicts only' if enabled
	TArray<TSharedPtr<FDMXMVRFixtureListItem>> Result = Items;
	if (bShowConfictsOnly)
	{
		Result.RemoveAll([](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
			{
				return
					Item->ErrorStatusText.IsEmpty() &&
					Item->WarningStatusText.IsEmpty();
			});
	}

	// Filter and return in order of precendence
	if (SearchString.IsEmpty())
	{
		return Result;
	}

	int32 Universe;
	if (ParseUniverse(SearchString, Universe))
	{
		Result.RemoveAll([Universe](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
			{
				return Item->GetUniverse() != Universe;
			});

		int32 Address;
		if (ParseAddress(SearchString, Address))
		{
			Result.RemoveAll([Address](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
				{
					return Item->GetAddress() != Address;
				});
		}

		return Result;
	}
	
	int32 FixtureIDNumerical;
	if (ParseFixtureID(SearchString, FixtureIDNumerical))
	{
		TArray<TSharedPtr<FDMXMVRFixtureListItem>> FixtureIDsOnlyResult = Result;
		FixtureIDsOnlyResult.RemoveAll([FixtureIDNumerical](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
			{
				int32 OtherFixtureIDNumerical;
				if (ParseFixtureID(Item->GetFixtureID(), OtherFixtureIDNumerical))
				{
					return OtherFixtureIDNumerical != FixtureIDNumerical;
				}
				return true;
			});

		if (FixtureIDsOnlyResult.Num() > 0)
		{
			return FixtureIDsOnlyResult;
		}
	}

	Result.RemoveAll([this](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
		{
			return !Item->GetFixturePatchName().Contains(SearchString);
		});

	return Result;
}

TSharedRef<SWidget> SDMXMVRFixtureListToolbar::GenerateFixtureTypeDropdownMenu()
{
	FText AddButtonLabel = FDMXEditorCommands::Get().AddNewEntityFixturePatch->GetLabel();
	FText AddButtonToolTip = FDMXEditorCommands::Get().AddNewEntityFixturePatch->GetDescription();

	TSharedRef<SComboButton> AddComboButton = 
		SNew(SComboButton)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(0, 1))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Plus"))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(FMargin(2, 0, 2, 0))
				[
					SNew(STextBlock)
					.Text(AddButtonLabel)
				]
			]
			.MenuContent()
			[

				SNew(SVerticalBox)
				
				// Bulk Add Fixture Patches 
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.Padding(8.f, 2.f, 4.f, 2.f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BulkAddPatchesLabel", "Quantity"))
					]

					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.Padding(4.f, 2.f, 4.f, 2.f)
					[
						SNew(SEditableTextBox)
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
									OutErrorMessage = LOCTEXT("BulkAddPatchesBadString", "Needs a numeric value > 0");
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
									constexpr int32 MaxNumPatchesToBulkAdd = 512;
									NumFixturePatchesToAdd = FMath::Min(Value, MaxNumPatchesToBulkAdd);
								}
							})
					]
				]

				// Fixture Type Selection
				+ SVerticalBox::Slot()
				[
					SAssignNew(FixtureTypeDropdownMenu, SDMXEntityDropdownMenu<UDMXEntityFixtureType>)
					.DMXEditor(WeakDMXEditor)
					.OnEntitySelected(this, &SDMXMVRFixtureListToolbar::OnAddNewMVRFixtureClicked)
				]
			]
			.IsFocusable(true)
			.ContentPadding(FMargin(5.0f, 1.0f))
			.ComboButtonStyle(FAppStyle::Get(), "ToolbarComboButton")
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
			.ForegroundColor(FLinearColor::White)
			.ToolTipText(AddButtonToolTip)
			.OnComboBoxOpened(FOnComboBoxOpened::CreateLambda([this]() 
				{ 
					FixtureTypeDropdownMenu->RefreshEntitiesList();
				}));

	FixtureTypeDropdownMenu->SetComboButton(AddComboButton);

	return AddComboButton;
}

void SDMXMVRFixtureListToolbar::OnAddNewMVRFixtureClicked(UDMXEntity* InSelectedFixtureType)
{
	if (!ensureMsgf(InSelectedFixtureType, TEXT("Trying to add Fixture Patches, but selected fixture type is invalid.")))
	{
		return;
	}

	TSharedPtr<FDMXEditor> DMXEditor = WeakDMXEditor.Pin();
	if (!DMXEditor.IsValid())
	{
		return;
	}
	UDMXLibrary* DMXLibrary = DMXEditor->GetDMXLibrary();
	if (!DMXLibrary)
	{
		return;
	}
	UDMXEntityFixtureType* FixtureType = CastChecked<UDMXEntityFixtureType>(InSelectedFixtureType);

	// Find a Universe and Address
	TArray<UDMXEntityFixturePatch*> FixturePatches = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	UDMXEntityFixturePatch* const* LastFixturePatchPtr = Algo::MaxElementBy(FixturePatches, [](const UDMXEntityFixturePatch* FixturePatch)
		{
			return (int64)FixturePatch->GetUniverseID() * DMX_MAX_ADDRESS + FixturePatch->GetStartingChannel();
		});
	int32 Universe = 1;
	int32 Address = 1;
	if (LastFixturePatchPtr && !FixtureType->Modes.IsEmpty())
	{
		const UDMXEntityFixturePatch& LastFixturePatch = **LastFixturePatchPtr;
		if (LastFixturePatch.GetEndingChannel() + FixtureType->Modes[0].ChannelSpan > DMX_MAX_ADDRESS)
		{
			Universe = LastFixturePatch.GetUniverseID() + 1;
			Address = 1;
		}
		else
		{
			Universe = LastFixturePatch.GetUniverseID();
			Address = LastFixturePatch.GetEndingChannel() + 1;
		}
	}

	// Create a new fixture patches
	const FScopedTransaction CreateFixturePatchTransaction(LOCTEXT("CreateFixturePatchTransaction", "Create Fixture Patch"));
	TArray<UDMXEntityFixturePatch*> NewFixturePatches;
	TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> NewWeakFixturePatches;
	DMXLibrary->PreEditChange(UDMXLibrary::StaticClass()->FindPropertyByName(UDMXLibrary::GetEntitiesPropertyName()));
	for (int32 iNumFixturePatchesAdded = 0; iNumFixturePatchesAdded < NumFixturePatchesToAdd; iNumFixturePatchesAdded++)
	{
		FDMXEntityFixturePatchConstructionParams FixturePatchConstructionParams;
		FixturePatchConstructionParams.FixtureTypeRef = FDMXEntityFixtureTypeRef(FixtureType);
		FixturePatchConstructionParams.ActiveMode = 0;
		FixturePatchConstructionParams.UniverseID = Universe;
		FixturePatchConstructionParams.StartingAddress = Address;

		constexpr bool bMarkLibraryDirty = false;
		UDMXEntityFixturePatch* NewFixturePatch = UDMXEntityFixturePatch::CreateFixturePatchInLibrary(FixturePatchConstructionParams, FixtureType->Name, bMarkLibraryDirty);
		NewFixturePatches.Add(NewFixturePatch);
		NewWeakFixturePatches.Add(NewFixturePatch);	

		// Increment Universe and Address in steps by one. This is enough for auto assign while keeping order of the named patches
		if (Address + FixtureType->Modes[0].ChannelSpan > DMX_MAX_ADDRESS)
		{
			Universe++;
			Address = 1; 
		}
		else
		{
			Address++;
		}
	}

	// Auto assign
	constexpr bool bAllowDecrementUniverse = false;
	constexpr bool bAllowDecrementChannels = false;
	FDMXEditorUtils::AutoAssignedChannels(bAllowDecrementUniverse, bAllowDecrementChannels, NewFixturePatches);

	DMXLibrary->PostEditChange();

	DMXEditor->GetFixturePatchSharedData()->SelectFixturePatches(NewWeakFixturePatches);
}

void SDMXMVRFixtureListToolbar::OnSearchTextChanged(const FText& SearchText)
{
	SearchString = SearchText.ToString();
	OnSearchChanged.ExecuteIfBound();
}

void SDMXMVRFixtureListToolbar::OnShowConflictsOnlyCheckStateChanged(const ECheckBoxState NewCheckState)
{
	bShowConfictsOnly = NewCheckState == ECheckBoxState::Checked;
	OnSearchChanged.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE
