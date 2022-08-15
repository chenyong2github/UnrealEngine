// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXMVRFixtureList.h"

#include "DMXEditor.h"
#include "DMXEditorSettings.h"
#include "DMXEditorUtils.h"
#include "DMXFixturePatchSharedData.h"
#include "Commands/DMXEditorCommands.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXLibrary.h"
#include "Widgets/FixturePatch/DMXMVRFixtureListItem.h"
#include "Widgets/FixturePatch/SDMXMVRFixtureListRow.h"
#include "Widgets/FixturePatch/SDMXMVRFixtureListToolbar.h"

#include "ScopedTransaction.h"
#include "TimerManager.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"


#define LOCTEXT_NAMESPACE "SDMXMVRFixtureList"

/** Helper to generate Status Text for MVR Fixture List Items */
class FDMXMVRFixtureListStatusTextGenerator
{
public:
	FDMXMVRFixtureListStatusTextGenerator(const TArray<TSharedPtr<FDMXMVRFixtureListItem>>& InItems)
		: Items(InItems)
	{}

	/** Generates warning texts. Returns a map of those Items that need a warning set along with the warning Text */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GenerateWarningTexts() const
	{
		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> AccumulatedConflicts;

		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> FixtureTypeIssues = GetFixtureTypeIssues();
		AppendConflictTexts(FixtureTypeIssues, AccumulatedConflicts);

		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> FixtureIDIssues = GetFixtureIDIssues();
		AppendConflictTexts(FixtureIDIssues, AccumulatedConflicts);

		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> FixtureIDConflicts = GetFixtureIDConflicts();
		AppendConflictTexts(FixtureIDConflicts, AccumulatedConflicts);

		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> ChannelExcessConflicts = GetChannelExcessConflicts();
		AppendConflictTexts(ChannelExcessConflicts, AccumulatedConflicts);

		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> ChannelOverlapConflicts = GetChannelOverlapConflicts();
		AppendConflictTexts(ChannelOverlapConflicts, AccumulatedConflicts);

		return AccumulatedConflicts;
	}

private:
	void AppendConflictTexts(const TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText>& InItemToConflictTextMap, TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText>& InOutConflictTexts) const
	{
		for (const TTuple<TSharedPtr<FDMXMVRFixtureListItem>, FText>& ItemToConflictTextPair : InItemToConflictTextMap)
		{
			if (InOutConflictTexts.Contains(ItemToConflictTextPair.Key))
			{
				const FText LineTerminator = FText::FromString(LINE_TERMINATOR);
				const FText AccumulatedErrorText = FText::Format(FText::FromString(TEXT("{0}{1}{2}{3}")), InOutConflictTexts[ItemToConflictTextPair.Key], LineTerminator, LineTerminator, ItemToConflictTextPair.Value);
				InOutConflictTexts[ItemToConflictTextPair.Key] = AccumulatedErrorText;
			}
			else
			{
				InOutConflictTexts.Add(ItemToConflictTextPair);
			}
		}

	}

	/** The patch of an item. Useful to Get Conflicts with Other */
	struct FItemPatch
	{
		FItemPatch(const TSharedPtr<FDMXMVRFixtureListItem>& InItem)
			: Item(InItem)
		{
			Universe = Item->GetUniverse();
			AddressRange = TRange<int32>(Item->GetAddress(), Item->GetAddress() + Item->GetNumChannels());
		}

		/** Returns a conflict text if this item conflicts with Other */
		FText GetConfictsWithOther(const FItemPatch& Other) const
		{
			// No conflict with self
			if (Other.Item == Item)
			{
				return FText::GetEmpty();
			}

			// No conflict with the same patch
			if (Item->GetFixturePatch() == Other.Item->GetFixturePatch())
			{
				return FText::GetEmpty();
			}

			// No conflict if not in the same universe
			if (Other.Universe != Universe)
			{
				return FText::GetEmpty();
			}

			// No conflict if channels don't overlap
			if (!AddressRange.Overlaps(Other.AddressRange))
			{
				return FText::GetEmpty();
			}

			// No conflict if patches are functionally equal
			if (AddressRange.GetLowerBound() == Other.AddressRange.GetLowerBound() &&
				Item->GetFixtureType() == Other.Item->GetFixtureType() &&
				Item->GetModeIndex() == Other.Item->GetModeIndex())
			{
				return FText::GetEmpty();
			}

			const FText FixtureIDText = MakeBeautifulItemText(Item);
			const FText OtherFixtureIDText = MakeBeautifulItemText(Other.Item);
			if (AddressRange.GetLowerBound() == Other.AddressRange.GetLowerBound() &&
				Item->GetFixtureType() == Other.Item->GetFixtureType())
			{
				// Modes confict
				check(Item->GetModeIndex() != Other.Item->GetModeIndex());
				return FText::Format(LOCTEXT("ModeConflict", "Uses same Address and Fixture Type as Fixture {1}, but Modes differ."), FixtureIDText, OtherFixtureIDText);
			}
			else if (AddressRange.GetLowerBound() == Other.AddressRange.GetLowerBound())
			{
				// Fixture Types conflict
				check(Item->GetFixtureType() != Other.Item->GetFixtureType());
				return FText::Format(LOCTEXT("FixtureTypeConflict", "Uses same Address as Fixture {1}, but Fixture Types differ."), FixtureIDText, OtherFixtureIDText);
			}
			else
			{
				// Addresses conflict
				return FText::Format(LOCTEXT("AddressConflict", "Overlaps Addresses with Fixture {1}"), FixtureIDText, OtherFixtureIDText);
			}
		}

		FORCEINLINE const TSharedPtr<FDMXMVRFixtureListItem>& GetItem() const { return Item; };

	private:
		int32 Universe = -1;
		TRange<int32> AddressRange;

		TSharedPtr<FDMXMVRFixtureListItem> Item;
	};

	/** Returns a Map of Items to Channels that have Fixture Types with issues set */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GetFixtureTypeIssues() const
	{
		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> ItemToIssueMap;
		for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : Items)
		{
			if (!Item->GetFixtureType())
			{
				const FText IssueText = LOCTEXT("NoFixtureTypeIssue", "No Fixture Type selected.");
				ItemToIssueMap.Add(Item, IssueText);
			}
			else if (Item->GetFixtureType()->Modes.IsEmpty())
			{
				const FText IssueText = LOCTEXT("NoModesIssue", "Fixture Type has no Modes defined.");
				ItemToIssueMap.Add(Item, IssueText);
			}
			else if (Item->GetFixtureType() && Item->GetModeIndex() == INDEX_NONE)
			{
				const FText IssueText = FText::Format(LOCTEXT("FixtureTypeWithoutModeIssue", "Fixture Type '{0}' does not specify any Modes."), FText::FromString(Item->GetFixtureType()->GetName()));
				ItemToIssueMap.Add(Item, IssueText);
			}
		}

		return ItemToIssueMap;
	}

	/** Returns a Map of Items to Channels exceeding the DMX address range Texts */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GetChannelExcessConflicts() const
	{
		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> ItemToConflictMap;
		for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : Items)
		{
			const int32 EndingAddress = Item->GetAddress() + Item->GetNumChannels() - 1;
			if (Item->GetAddress() < 1 &&
				EndingAddress > DMX_MAX_ADDRESS)
			{
				const FText ConflictText = FText::Format(LOCTEXT("ChannelExceedsMinAndMaxChannelConflict", "Exceeds available DMX Address range. Staring Address is {0} but min Address is 1. Ending Address is {1} but max Address is 512."), Item->GetAddress(), EndingAddress);
				ItemToConflictMap.Add(Item, ConflictText);
			}
			else if (Item->GetAddress() < 1)
			{
				const FText ConflictText = FText::Format(LOCTEXT("ChannelExceedsMinChannelNumberConflict", "Exceeds available DMX Address range. Staring Address is {0} but min Address is 1."), Item->GetAddress());
				ItemToConflictMap.Add(Item, ConflictText);
			}
			else if (EndingAddress > DMX_MAX_ADDRESS)
			{
				const FText ConflictText = FText::Format(LOCTEXT("ChannelExeedsMaxChannelNumberConflict", "Exceeds available DMX Address range. Ending Address is {0} but max Address is 512."), EndingAddress);
				ItemToConflictMap.Add(Item, ConflictText);
			}			
		}

		return ItemToConflictMap;
	}

	/** Returns a Map of Items to overlapping Channel conflict Texts */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GetChannelOverlapConflicts() const
	{
		TArray<FItemPatch> ItemPatches;
		ItemPatches.Reserve(Items.Num());
		for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : Items)
		{
			FItemPatch ItemPatch(Item);
			ItemPatches.Add(ItemPatch);
		}

		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> ItemToConflictMap;
		for (const FItemPatch& ItemPatch : ItemPatches)
		{
			for (const FItemPatch& Other : ItemPatches)
			{
				const FText ConflictWithOtherText = ItemPatch.GetConfictsWithOther(Other);
				if (!ConflictWithOtherText.IsEmpty())
				{
					if (ItemToConflictMap.Contains(ItemPatch.GetItem()))
					{
						FText AppendConflictText = FText::Format(FText::FromString(TEXT("{0}{1}{2}")), ItemToConflictMap[ItemPatch.GetItem()], FText::FromString(FString(LINE_TERMINATOR)), ConflictWithOtherText);
						ItemToConflictMap[ItemPatch.GetItem()] = AppendConflictText;
					}
					else
					{
						ItemToConflictMap.Add(ItemPatch.GetItem(), ConflictWithOtherText);
					}
				}
			}
		}

		return ItemToConflictMap;
	}

	/** Returns an Map of Items to Fixture IDs issues Texts */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GetFixtureIDIssues() const
	{
		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> Result;
		for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : Items)
		{
			int32 FixtureIDNumerical;
			if (!LexTryParseString(FixtureIDNumerical, *Item->GetFixtureID()))
			{
				Result.Add(Item, LOCTEXT("FixtureIDNotNumericalIssueText", "FID has to be a number."));
			}
		}
		return Result;
	}

	/** Returns an Map of Items to Fixture IDs conflict Texts */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GetFixtureIDConflicts() const
	{
		TMap<FString, TArray<TSharedPtr<FDMXMVRFixtureListItem>>> FixtureIDMap;
		FixtureIDMap.Reserve(Items.Num());
		for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : Items)
		{
			FixtureIDMap.FindOrAdd(Item->GetFixtureID()).Add(Item);
		}
		TArray<TArray<TSharedPtr<FDMXMVRFixtureListItem>>> FixtureIDConflicts;
		FixtureIDMap.GenerateValueArray(FixtureIDConflicts);
		FixtureIDConflicts.RemoveAll([](const TArray<TSharedPtr<FDMXMVRFixtureListItem>>& ConflictingItems)
			{
				return ConflictingItems.Num() < 2;
			});
		
		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> ItemToConflictMap;
		for (TArray<TSharedPtr<FDMXMVRFixtureListItem>>& ConflictingItems : FixtureIDConflicts)
		{
			ConflictingItems.Sort([](const TSharedPtr<FDMXMVRFixtureListItem>& ItemA, const TSharedPtr<FDMXMVRFixtureListItem>& ItemB)
				{
					return ItemA->GetFixtureID() < ItemB->GetFixtureID();
				});

			check(ConflictingItems.Num() > 0);
			FText ConflictText = FText::Format(LOCTEXT("BaseFixtureIDConflictText", "Ambiguous FIDs in {0}"), MakeBeautifulItemText(ConflictingItems[0]));
			for (int32 ConflictingItemIndex = 1; ConflictingItemIndex < ConflictingItems.Num(); ConflictingItemIndex++)
			{
				ConflictText = FText::Format(LOCTEXT("AppendFixtureIDConflictText", "{0}, {1}"), ConflictText, MakeBeautifulItemText(ConflictingItems[ConflictingItemIndex]));
			}
			
			for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : ConflictingItems)
			{
				ItemToConflictMap.Add(Item, ConflictText);
			}
		}

		return ItemToConflictMap;
	}

	static FText MakeBeautifulItemText(const TSharedPtr<FDMXMVRFixtureListItem>& Item)
	{
		const FString AddressesString = FString::FromInt(Item->GetUniverse()) + TEXT(".") + FString::FromInt(Item->GetAddress());
		const FString ItemNameString = TEXT("'") + Item->GetMVRFixtureName() + TEXT("'");
		const FString BeautifulItemString = ItemNameString + TEXT(" (") + AddressesString + TEXT(")");;
		return FText::FromString(BeautifulItemString);
	}

	/** The items the class handles */
	TArray<TSharedPtr<FDMXMVRFixtureListItem>> Items;
};


const FName FDMXMVRFixtureListCollumnIDs::Status = "Status";
const FName FDMXMVRFixtureListCollumnIDs::FixturePatchName = "FixturePatchName";
const FName FDMXMVRFixtureListCollumnIDs::FixtureID = "FixtureID";
const FName FDMXMVRFixtureListCollumnIDs::MVRFixtureName = "MVRFixtureName";
const FName FDMXMVRFixtureListCollumnIDs::FixtureType = "FixtureType";
const FName FDMXMVRFixtureListCollumnIDs::Mode = "Mode";
const FName FDMXMVRFixtureListCollumnIDs::Patch = "Patch";

const FString SDMXMVRFixtureList::ClipboardCopyMVRFixtureHeader = FString(TEXT("UE::DMX::SDMXMVRFIXTURELIST::COPY_MVRFIXTUREEVENT"));

SDMXMVRFixtureList::SDMXMVRFixtureList()
	: SortMode(EColumnSortMode::Ascending)
	, SortedByColumnID(FDMXMVRFixtureListCollumnIDs::Patch)
{}

SDMXMVRFixtureList::~SDMXMVRFixtureList()
{
	SaveHeaderRowSettings();
}

void SDMXMVRFixtureList::PostUndo(bool bSuccess)
{
	RequestListRefresh();
}

void SDMXMVRFixtureList::PostRedo(bool bSuccess)
{
	RequestListRefresh();
}

void SDMXMVRFixtureList::Construct(const FArguments& InArgs, TWeakPtr<FDMXEditor> InDMXEditor)
{
	if (!InDMXEditor.IsValid())
	{
		return;
	}
	
	WeakDMXEditor = InDMXEditor;
	FixturePatchSharedData = InDMXEditor.Pin()->GetFixturePatchSharedData();

	// Handle Entity changes
	UDMXEntityFixturePatch::GetOnFixturePatchChanged().AddSP(this, &SDMXMVRFixtureList::OnFixturePatchChanged);
	UDMXEntityFixtureType::GetOnFixtureTypeChanged().AddSP(this, &SDMXMVRFixtureList::OnFixtureTypeChanged);

	// Handle Shared Data selection changes
	FixturePatchSharedData->OnFixturePatchSelectionChanged.AddSP(this, &SDMXMVRFixtureList::OnFixturePatchSharedDataSelectedFixturePatches);
	FixturePatchSharedData->OnUniverseSelectionChanged.AddSP(this, &SDMXMVRFixtureList::OnFixturePatchSharedDataSelectedUniverse);

	static const FTableViewStyle TableViewStyle = FAppStyle::Get().GetWidgetStyle<FTableViewStyle>("TreeView");

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			SAssignNew(Toolbar, SDMXMVRFixtureListToolbar, WeakDMXEditor)
			.OnSearchChanged(this, &SDMXMVRFixtureList::OnSearchChanged)
		]

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.FillHeight(1.f)
		[
			SAssignNew(ListView, FDMXMVRFixtureListType)
			.ListViewStyle(&TableViewStyle)
			.HeaderRow(GenerateHeaderRow())
			.ItemHeight(40.0f)
			.ListItemsSource(&ListSource)
			.OnGenerateRow(this, &SDMXMVRFixtureList::OnGenerateRow)
			.OnSelectionChanged(this, &SDMXMVRFixtureList::OnSelectionChanged)
			.OnContextMenuOpening(this, &SDMXMVRFixtureList::OnContextMenuOpening)
			.ReturnFocusToSelection(false)
		]
	];

	RegisterCommands();
	RefreshList();

	// Make an initial selection, as if the user clicked it
	if (ListSource.Num() > 0)
	{
		ListView->SetSelection(ListSource[0], ESelectInfo::OnMouseClick);
	}
}

void SDMXMVRFixtureList::RequestListRefresh()
{
	if (RequestListRefreshTimerHandle.IsValid())
	{
		return;
	}

	RequestListRefreshTimerHandle =
		GEditor->GetTimerManager()->SetTimerForNextTick(
			FTimerDelegate::CreateLambda([this]()
				{
					RefreshList();
				})
		);
}

void SDMXMVRFixtureList::EnterFixturePatchNameEditingMode()
{
	if (ListView.IsValid())
	{
		const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();
		if (SelectedItems.Num() == 0)
		{
			const TSharedPtr<SDMXMVRFixtureListRow>* SelectedRowPtr = Rows.FindByPredicate([&SelectedItems](const TSharedPtr<SDMXMVRFixtureListRow>& Row)
				{
					return Row->GetItem() == SelectedItems[0];
				});
			if (SelectedRowPtr)
			{
				(*SelectedRowPtr)->EnterFixturePatchNameEditingMode();
			}
		}
	}
}

FReply SDMXMVRFixtureList::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SDMXMVRFixtureList::OnSearchChanged()
{
	RequestListRefresh();
}

void SDMXMVRFixtureList::RefreshList()
{	
	RequestListRefreshTimerHandle.Invalidate();

	SaveHeaderRowSettings();

	// Clear cached data
	ListSource.Reset();
	Rows.Reset();

	TSharedPtr<FDMXEditor> DMXEditor = WeakDMXEditor.Pin();
	if (!DMXEditor.IsValid())
	{
		return;
	}

	UDMXLibrary* DMXLibrary = DMXEditor->GetDMXLibrary();
	if (!IsValid(DMXLibrary))
	{
		return;
	}

	const TArray<UDMXEntityFixturePatch*> FixturePatches = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	for (UDMXEntityFixturePatch* FixturePatch : FixturePatches)
	{
		const FGuid& MVRFixtureUUID = FixturePatch->GetMVRFixtureUUID();
		const TSharedRef<FDMXMVRFixtureListItem> NewItem = MakeShared<FDMXMVRFixtureListItem>(DMXEditor.ToSharedRef(), MVRFixtureUUID);

		ListSource.Add(NewItem);
	}

	// Generate status texts
	GenereateStatusText();

	// Apply search filters. Relies on up-to-date status to find conflicts.
	if (Toolbar.IsValid())
	{
		ListSource = Toolbar->FilterItems(ListSource);
	}
	
	// Update and sort the list and its widgets
	ListView->RequestListRefresh();

	SortByColumnID(EColumnSortPriority::Max, FDMXMVRFixtureListCollumnIDs::Patch, EColumnSortMode::Ascending);

	AdoptSelectionFromFixturePatchSharedData();
}

void SDMXMVRFixtureList::GenereateStatusText()
{
	for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : ListSource)
	{
		Item->WarningStatusText = FText::GetEmpty();
		Item->ErrorStatusText = FText::GetEmpty();
	}

	FDMXMVRFixtureListStatusTextGenerator StatusTextGenerator(ListSource);

	const TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> WarningTextMap = StatusTextGenerator.GenerateWarningTexts();
	for (const TTuple<TSharedPtr<FDMXMVRFixtureListItem>, FText>& ItemToWarningTextPair : WarningTextMap)
	{
		ItemToWarningTextPair.Key->WarningStatusText = ItemToWarningTextPair.Value;
	}
}

TSharedRef<ITableRow> SDMXMVRFixtureList::OnGenerateRow(TSharedPtr<FDMXMVRFixtureListItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	const TSharedRef<SDMXMVRFixtureListRow> NewRow =
		SNew(SDMXMVRFixtureListRow, OwnerTable, InItem.ToSharedRef())
		.OnRowRequestsListRefresh(this, &SDMXMVRFixtureList::RequestListRefresh)
		.OnRowRequestsStatusRefresh(this, &SDMXMVRFixtureList::GenereateStatusText)
		.IsSelected_Lambda([this, InItem]()
			{
				return ListView->IsItemSelected(InItem);
			});

	Rows.Add(NewRow);
	return NewRow;
}

void SDMXMVRFixtureList::OnSelectionChanged(TSharedPtr<FDMXMVRFixtureListItem> InItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
 
	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();
	if (SelectedItems.Num() > 0)
	{
		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> FixturePatchesToSelect;
		for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : SelectedItems)
		{
			if (UDMXEntityFixturePatch* FixturePatch = Item->GetFixturePatch())
			{
				FixturePatchesToSelect.AddUnique(FixturePatch);
			}
		}

		FixturePatchSharedData->SelectFixturePatches(FixturePatchesToSelect);

		if (SelectedItems.Num() > 0)
		{
			const int32 SelectedUniverse = FixturePatchSharedData->GetSelectedUniverse();
			const int32 UniverseOfFirstItem = SelectedItems[0]->GetUniverse();
			if (SelectedUniverse != UniverseOfFirstItem)
			{
				FixturePatchSharedData->SelectUniverse(UniverseOfFirstItem);
			}
		}
	}
	else
	{
		// Restore selection when nothing got selected
		const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches = FixturePatchSharedData->GetSelectedFixturePatches();
		TArray<TSharedPtr<FDMXMVRFixtureListItem>> ItemsToSelect;
		for (TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch : SelectedFixturePatches)
		{
			if (FixturePatch.IsValid())
			{
				const TSharedPtr<FDMXMVRFixtureListItem>* ItemPtr = ListSource.FindByPredicate([&FixturePatch](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
					{
						return Item->GetFixturePatch() == FixturePatch;
					});
				if (ItemPtr)
				{
					ItemsToSelect.Add(*ItemPtr);
				}
			}
		}
		ListView->ClearSelection();
		constexpr bool bSelected = true;
		ListView->SetItemSelection(ItemsToSelect, bSelected);
	}
}

void SDMXMVRFixtureList::OnFixturePatchChanged(const UDMXEntityFixturePatch* FixturePatch)
{
	if (!bChangingDMXLibrary)
	{
		RequestListRefresh();
	}
}

void SDMXMVRFixtureList::OnFixtureTypeChanged(const UDMXEntityFixtureType* FixtureType)
{
	if (!bChangingDMXLibrary)
	{
		RequestListRefresh();
	}
}

void SDMXMVRFixtureList::OnFixturePatchSharedDataSelectedFixturePatches()
{
	if (!bChangingDMXLibrary)
	{
		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches = FixturePatchSharedData->GetSelectedFixturePatches();
		SelectedFixturePatches.RemoveAll([](TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch)
			{
				return !FixturePatch.IsValid();
			});

		TArray<TSharedPtr<FDMXMVRFixtureListItem>> NewSelection;
		for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : ListSource)
		{
			if (SelectedFixturePatches.Contains(Item->GetFixturePatch()))
			{
				NewSelection.Add(Item);
			}
		}

		if (NewSelection.Num() > 0)
		{
			ListView->ClearSelection();
			ListView->SetItemSelection(NewSelection, true, ESelectInfo::Direct);

			if (!ListView->IsItemVisible(NewSelection[0]))
			{
				ListView->RequestScrollIntoView(NewSelection[0]);
			}
		}
		else
		{
			ListView->ClearSelection();
		}
	}
}

void SDMXMVRFixtureList::OnFixturePatchSharedDataSelectedUniverse()
{
	if (!bChangingDMXLibrary)
	{
		const int32 SelectedUniverse = FixturePatchSharedData->GetSelectedUniverse();
		const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();
		const bool bUniverseAlreadySelected = SelectedItems.ContainsByPredicate([SelectedUniverse](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
			{
				return Item->GetUniverse() == SelectedUniverse;
			});
		if (bUniverseAlreadySelected)
		{
			return;
		}

		const TSharedPtr<FDMXMVRFixtureListItem>* ItemPtr = ListSource.FindByPredicate([SelectedUniverse](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
			{
				return Item->GetUniverse() == SelectedUniverse;
			});
		if (ItemPtr)
		{
			ListView->RequestScrollIntoView(*ItemPtr);
		}
	}
}

void SDMXMVRFixtureList::AdoptSelectionFromFixturePatchSharedData()
{
	if (!ListView.IsValid())
	{
		return;
	}

	const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches = FixturePatchSharedData->GetSelectedFixturePatches();

	TArray<TSharedPtr<FDMXMVRFixtureListItem>> NewSelection;
	for (const TWeakObjectPtr<UDMXEntityFixturePatch> SelectedFixturePatch : SelectedFixturePatches)
	{
		const TSharedPtr<FDMXMVRFixtureListItem>* SelectedItemPtr = ListSource.FindByPredicate([SelectedFixturePatch](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
			{
				return SelectedFixturePatch.IsValid() && Item->GetFixturePatch() == SelectedFixturePatch;
			});

		if (SelectedItemPtr)
		{
			NewSelection.Add(*SelectedItemPtr);
		}
	}

	if (NewSelection.Num() > 0)
	{
		ListView->ClearSelection();

		constexpr bool bSelected = true;
		ListView->SetItemSelection(NewSelection, bSelected, ESelectInfo::OnMouseClick);
		ListView->RequestScrollIntoView(NewSelection[0]);
	}
	else if(TSharedPtr<FDMXEditor> DMXEditor = WeakDMXEditor.Pin())
	{
		const UDMXLibrary* DMXLibrary = DMXEditor->GetDMXLibrary();
		const TArray<UDMXEntityFixturePatch*> FixturePatches = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
		if (FixturePatches.Num() > 0)
		{
			FixturePatchSharedData->SelectFixturePatch(FixturePatches[0]);
		}
	}
}

void SDMXMVRFixtureList::AutoAssignFixturePatches()
{
	if (FixturePatchSharedData.IsValid())
	{
		TArray<UDMXEntityFixturePatch*> FixturePatchesToAutoAssign;
		const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches = FixturePatchSharedData->GetSelectedFixturePatches();
		for (TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch : SelectedFixturePatches)
		{
			if (FixturePatch.IsValid())
			{
				FixturePatchesToAutoAssign.Add(FixturePatch.Get());
			}
		}

		if (FixturePatchesToAutoAssign.IsEmpty())
		{
			return;
		}

		constexpr bool bAllowDecrementUniverse = false;
		constexpr bool bAllowDecrementChannels = true;
		FDMXEditorUtils::AutoAssignedChannels(bAllowDecrementUniverse, bAllowDecrementChannels, FixturePatchesToAutoAssign);
		FixturePatchSharedData->SelectUniverse(FixturePatchesToAutoAssign[0]->GetUniverseID());

		RequestListRefresh();
	}
}

TSharedRef<SHeaderRow> SDMXMVRFixtureList::GenerateHeaderRow()
{
	const float StatusColumnWidth = FMath::Max(FAppStyle::GetBrush("Icons.Warning")->GetImageSize().X + 6.f, FAppStyle::GetBrush("Icons.Error")->GetImageSize().X + 6.f);

	HeaderRow = SNew(SHeaderRow);
	SHeaderRow::FColumn::FArguments ColumnArgs;

	HeaderRow->AddColumn(
		SHeaderRow::FColumn::FArguments()
		.ColumnId(FDMXMVRFixtureListCollumnIDs::FixturePatchName)
		.DefaultLabel(LOCTEXT("FixturePatchNameColumnLabel", "Fixture Patch"))
		.FillWidth(0.2f)
	);

	HeaderRow->AddColumn(
		SHeaderRow::FColumn::FArguments()
		.ColumnId(FDMXMVRFixtureListCollumnIDs::Status)
		.DefaultLabel(LOCTEXT("StatusColumnLabel", ""))
		.FixedWidth(StatusColumnWidth)
	);

	HeaderRow->AddColumn(
		SHeaderRow::FColumn::FArguments()
		.ColumnId(FDMXMVRFixtureListCollumnIDs::FixtureID)
		.SortMode(this, &SDMXMVRFixtureList::GetColumnSortMode, FDMXMVRFixtureListCollumnIDs::FixtureID)
		.OnSort(this, &SDMXMVRFixtureList::SortByColumnID)
		.DefaultLabel(LOCTEXT("FixtureIDColumnLabel", "FID"))
		.FillWidth(0.1f)
	);

	HeaderRow->AddColumn(
		SHeaderRow::FColumn::FArguments()
		.ColumnId(FDMXMVRFixtureListCollumnIDs::MVRFixtureName)
		.DefaultLabel(LOCTEXT("NameColumnLabel", "Fixture Name"))
		.FillWidth(0.2f)
	);

	HeaderRow->AddColumn(
		SHeaderRow::FColumn::FArguments()
		.ColumnId(FDMXMVRFixtureListCollumnIDs::FixtureType)
		.DefaultLabel(LOCTEXT("FixtureTypeColumnLabel", "FixtureType"))
		.FillWidth(0.2f)
	);

	HeaderRow->AddColumn(
		SHeaderRow::FColumn::FArguments()
		.ColumnId(FDMXMVRFixtureListCollumnIDs::Mode)
		.DefaultLabel(LOCTEXT("ModeColumnLabel", "Mode"))
		.FillWidth(0.2f)
	);

	HeaderRow->AddColumn(
		SHeaderRow::FColumn::FArguments()
		.ColumnId(FDMXMVRFixtureListCollumnIDs::Patch)
		.SortMode(this, &SDMXMVRFixtureList::GetColumnSortMode, FDMXMVRFixtureListCollumnIDs::Patch)
		.OnSort(this, &SDMXMVRFixtureList::SortByColumnID)
		.DefaultLabel(LOCTEXT("PatchColumnLabel", "Patch"))
		.FillWidth(0.1f)
	);

	// Restore user settings
	RestoresHeaderRowSettings();

	return HeaderRow.ToSharedRef();
}

void SDMXMVRFixtureList::SaveHeaderRowSettings()
{
	UDMXEditorSettings* EditorSettings = GetMutableDefault<UDMXEditorSettings>();
	if (HeaderRow.IsValid() && EditorSettings)
	{
		for (const SHeaderRow::FColumn& Column : HeaderRow->GetColumns())
		{
			if (Column.ColumnId == FDMXMVRFixtureListCollumnIDs::FixtureID)
			{
				EditorSettings->MVRFixtureListSettings.FixtureIDColumnWidth = Column.Width.Get();
			}
			if (Column.ColumnId == FDMXMVRFixtureListCollumnIDs::MVRFixtureName)
			{
				EditorSettings->MVRFixtureListSettings.NameColumnWidth = Column.Width.Get();
			}
			else if (Column.ColumnId == FDMXMVRFixtureListCollumnIDs::FixtureType)
			{
				EditorSettings->MVRFixtureListSettings.FixtureTypeColumnWidth = Column.Width.Get();
			}
			else if (Column.ColumnId == FDMXMVRFixtureListCollumnIDs::Mode)
			{
				EditorSettings->MVRFixtureListSettings.ModeColumnWidth = Column.Width.Get();
			}
			else if (Column.ColumnId == FDMXMVRFixtureListCollumnIDs::Patch)
			{
				EditorSettings->MVRFixtureListSettings.PatchColumnWidth = Column.Width.Get();
			}
		}

		EditorSettings->SaveConfig();
	}
}

void SDMXMVRFixtureList::RestoresHeaderRowSettings()
{
	if (const UDMXEditorSettings* EditorSettings = GetDefault<UDMXEditorSettings>())
	{
		const float FixtureIDColumnWidth = EditorSettings->MVRFixtureListSettings.FixtureIDColumnWidth;
		if (FixtureIDColumnWidth > 20.f)
		{
			HeaderRow->SetColumnWidth(FDMXMVRFixtureListCollumnIDs::FixtureID, FixtureIDColumnWidth);
		}

		const float NameColumnWidth = EditorSettings->MVRFixtureListSettings.NameColumnWidth;
		if (NameColumnWidth > 20.f)
		{
			HeaderRow->SetColumnWidth(FDMXMVRFixtureListCollumnIDs::MVRFixtureName, NameColumnWidth);
		}

		const float FixtureTypeColumnWidth = EditorSettings->MVRFixtureListSettings.FixtureTypeColumnWidth;
		if (FixtureTypeColumnWidth > 20.f)
		{
			HeaderRow->SetColumnWidth(FDMXMVRFixtureListCollumnIDs::FixtureType, FixtureTypeColumnWidth);
		}

		const float ModeColumnWidth = EditorSettings->MVRFixtureListSettings.ModeColumnWidth;
		if (ModeColumnWidth > 20.f)
		{
			HeaderRow->SetColumnWidth(FDMXMVRFixtureListCollumnIDs::Mode, ModeColumnWidth);
		}

		const float PatchColumnWidth = EditorSettings->MVRFixtureListSettings.PatchColumnWidth;
		if (PatchColumnWidth > 20.f)
		{
			HeaderRow->SetColumnWidth(FDMXMVRFixtureListCollumnIDs::Patch, PatchColumnWidth);
		}
	}
}

EColumnSortMode::Type SDMXMVRFixtureList::GetColumnSortMode(const FName ColumnId) const
{
	if (SortedByColumnID != ColumnId)
	{
		return EColumnSortMode::None;
	}

	return SortMode;
}

void SDMXMVRFixtureList::SortByColumnID(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode)
{
	SortMode = InSortMode;
	SortedByColumnID = ColumnId;

	if (ColumnId == FDMXMVRFixtureListCollumnIDs::FixtureID)
	{
		if (InSortMode == EColumnSortMode::Descending)
		{
			ListSource.Sort([](const TSharedPtr<FDMXMVRFixtureListItem>& ItemA, const TSharedPtr<FDMXMVRFixtureListItem>& ItemB)
				{
					return !(ItemA->GetFixtureID() <= ItemB->GetFixtureID());
				});
		}
		else if (InSortMode == EColumnSortMode::Ascending)
		{
			ListSource.Sort([](const TSharedPtr<FDMXMVRFixtureListItem>& ItemA, const TSharedPtr<FDMXMVRFixtureListItem>& ItemB)
				{
					return !(ItemA->GetFixtureID() >= ItemB->GetFixtureID());
				});
		}
	}
	else if(ColumnId == FDMXMVRFixtureListCollumnIDs::Patch)
	{
		if (InSortMode == EColumnSortMode::Descending)
		{
			ListSource.StableSort([](const TSharedPtr<FDMXMVRFixtureListItem>& ItemA, const TSharedPtr<FDMXMVRFixtureListItem>& ItemB)
				{
					UDMXEntityFixturePatch* FixturePatchA = ItemA->GetFixturePatch();
					UDMXEntityFixturePatch* FixturePatchB = ItemB->GetFixturePatch();

					const bool bIsUniverseIDLarger = ItemA->GetUniverse() > ItemB->GetUniverse();
					const bool bIsSameUniverse = ItemA->GetUniverse() == ItemB->GetUniverse();
					const bool bAreAddressesLarger = ItemA->GetAddress() > ItemB->GetAddress();
					return bIsUniverseIDLarger || (bIsSameUniverse && bAreAddressesLarger);
				});
		}
		else if (InSortMode == EColumnSortMode::Ascending)
		{
			ListSource.StableSort([](const TSharedPtr<FDMXMVRFixtureListItem>& ItemA, const TSharedPtr<FDMXMVRFixtureListItem>& ItemB)
				{
					UDMXEntityFixturePatch* FixturePatchA = ItemA->GetFixturePatch();
					UDMXEntityFixturePatch* FixturePatchB = ItemB->GetFixturePatch();
					const bool bIsUniverseIDSmaller = ItemA->GetUniverse() < ItemB->GetUniverse();
					const bool bIsSameUniverse = ItemA->GetUniverse() == ItemB->GetUniverse();
					const bool bAreAddressesSmaller = ItemA->GetAddress() < ItemB->GetAddress();
					return bIsUniverseIDSmaller || (bIsSameUniverse && bAreAddressesSmaller);
				});
		}
	}

	ListView->RequestListRefresh();
}

TSharedPtr<SWidget> SDMXMVRFixtureList::OnContextMenuOpening()
{
	if (!ListView.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	const bool bCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bCloseWindowAfterMenuSelection, CommandList);

	if (ListView->GetNumItemsSelected() > 0)
	{
		// Auto Assign Section
		MenuBuilder.BeginSection("AutoAssignSection", LOCTEXT("AutoAssignSection", "Auto-Assign"));
		{
			// Auto Assign Entry
			const FUIAction Action(FExecuteAction::CreateSP(this, &SDMXMVRFixtureList::AutoAssignFixturePatches));

			const FText AutoAssignText = LOCTEXT("AutoAssignContextMenuEntry", "Auto-Assign Selection");
			const TSharedRef<SWidget> Widget =
				SNew(STextBlock)
				.Text(AutoAssignText);

			MenuBuilder.AddMenuEntry(Action, Widget);
			MenuBuilder.EndSection();
		}

		// Basic Operations Section
		MenuBuilder.BeginSection("BasicOperationsSection", LOCTEXT("BasicOperationsSection", "Basic Operations"));
		{
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void SDMXMVRFixtureList::RegisterCommands()
{
	if (CommandList.IsValid())
	{
		return;
	}

	// listen to common editor shortcuts for copy/paste etc
	CommandList = MakeShared<FUICommandList>();
	CommandList->MapAction(FGenericCommands::Get().Cut, 
		FUIAction(
			FExecuteAction::CreateSP(this, &SDMXMVRFixtureList::OnCutSelectedItems),
			FCanExecuteAction::CreateSP(this, &SDMXMVRFixtureList::CanCutItems)
		)
	);
	CommandList->MapAction(FGenericCommands::Get().Copy,
		FUIAction(
			FExecuteAction::CreateSP(this, &SDMXMVRFixtureList::OnCopySelectedItems),
			FCanExecuteAction::CreateSP(this, &SDMXMVRFixtureList::CanCopyItems)
		)
	);
	CommandList->MapAction(FGenericCommands::Get().Paste,
		FUIAction(
			FExecuteAction::CreateSP(this, &SDMXMVRFixtureList::OnPasteItems),
			FCanExecuteAction::CreateSP(this, &SDMXMVRFixtureList::CanPasteItems)
		)
	);
	CommandList->MapAction(FGenericCommands::Get().Duplicate,
		FUIAction(
			FExecuteAction::CreateSP(this, &SDMXMVRFixtureList::OnDuplicateItems),
			FCanExecuteAction::CreateSP(this, &SDMXMVRFixtureList::CanDuplicateItems)
		)
	);
	CommandList->MapAction(FGenericCommands::Get().Delete,
		FUIAction(
			FExecuteAction::CreateSP(this, &SDMXMVRFixtureList::OnDeleteItems),
			FCanExecuteAction::CreateSP(this, &SDMXMVRFixtureList::CanDeleteItems),
			EUIActionRepeatMode::RepeatEnabled
		)
	);
}

bool SDMXMVRFixtureList::CanCutItems() const
{
	return CanCopyItems() && CanDeleteItems();
}

void SDMXMVRFixtureList::OnCutSelectedItems()
{
	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();

	const FScopedTransaction Transaction(SelectedItems.Num() > 1 ? LOCTEXT("CutMVRFixtures", "Cut Fixtures") : LOCTEXT("CutMVRFixture", "Cut Fixture"));

	OnCopySelectedItems();
	OnDeleteItems();
}

bool SDMXMVRFixtureList::CanCopyItems() const
{
	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();
	return SelectedItems.Num() > 0;
}

void SDMXMVRFixtureList::OnCopySelectedItems()
{
	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();

	FString SelectedMVRUUIDsString(ClipboardCopyMVRFixtureHeader);
	for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : SelectedItems)
	{
		SelectedMVRUUIDsString += Item->GetMVRUUID().ToString() + FString(LINE_TERMINATOR);
	}

	FPlatformApplicationMisc::ClipboardCopy(*SelectedMVRUUIDsString);
}

bool SDMXMVRFixtureList::CanPasteItems() const
{
	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();
	return SelectedItems.Num() == 1;
}

void SDMXMVRFixtureList::OnPasteItems()
{
	TGuardValue<bool>(bChangingDMXLibrary, true);

	FString ClipboardString;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardString);

	if (ClipboardString.RemoveFromStart(ClipboardCopyMVRFixtureHeader, ESearchCase::CaseSensitive))
	{
		TArray<FString> UUIDStrings;
		ClipboardString.ParseIntoArray(UUIDStrings, LINE_TERMINATOR);

		TArray<FGuid> UUIDs;
		for (const FString& UUIDString : UUIDStrings)
		{
			FGuid UUID;
			if (FGuid::Parse(UUIDString, UUID))
			{
				UUIDs.Add(UUID);
			}
		}

		TArray<TSharedPtr<FDMXMVRFixtureListItem>> ItemsToPaste;
		for (const FGuid& UUID : UUIDs)
		{
			const TSharedPtr<FDMXMVRFixtureListItem>* ItemPtr = ListSource.FindByPredicate([UUID](const TSharedPtr<FDMXMVRFixtureListItem>& Item)
				{
					return UUID == Item->GetMVRUUID();
				});

			if (ItemPtr)
			{
				ItemsToPaste.Add(*ItemPtr);
			}
		}

		const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();
		if (SelectedItems.Num() == 1) // Should always be one given CanPasteItems
		{
			FDMXMVRFixtureListItem::PasteItemsOntoItem(WeakDMXEditor, SelectedItems[0], ItemsToPaste);
		}

		RequestListRefresh();
		AdoptSelectionFromFixturePatchSharedData();
	}
}

bool SDMXMVRFixtureList::CanDuplicateItems() const
{
	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();
	return SelectedItems.Num() > 0;
}

void SDMXMVRFixtureList::OnDuplicateItems()
{
	TGuardValue<bool>(bChangingDMXLibrary, true);

	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();

	FDMXMVRFixtureListItem::DuplicateItems(WeakDMXEditor, SelectedItems);

	RequestListRefresh();
	AdoptSelectionFromFixturePatchSharedData();
}

bool SDMXMVRFixtureList::CanDeleteItems() const
{
	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();
	return SelectedItems.Num() > 0;
}

void SDMXMVRFixtureList::OnDeleteItems()
{
	TGuardValue<bool>(bChangingDMXLibrary, true);

	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();

	// Make a meaningful selection invariant to ordering of the List
	TSharedPtr<FDMXMVRFixtureListItem> NewSelection;
	for (int32 ItemIndex = 0; ItemIndex < ListSource.Num(); ItemIndex++)
	{
		if (SelectedItems.Contains(ListSource[ItemIndex]))
		{
			if (ListSource.IsValidIndex(ItemIndex + 1) && !SelectedItems.Contains(ListSource[ItemIndex + 1]))
			{
				NewSelection = ListSource[ItemIndex + 1];
				break;
			}
			else if (ListSource.IsValidIndex(ItemIndex - 1) && !SelectedItems.Contains(ListSource[ItemIndex - 1]))
			{
				NewSelection = ListSource[ItemIndex - 1];
				break;
			}
		}
	}
	if (NewSelection.IsValid())
	{
		ListView->SetSelection(NewSelection, ESelectInfo::OnMouseClick);
	}

	FDMXMVRFixtureListItem::DeleteItems(SelectedItems);
	RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE
