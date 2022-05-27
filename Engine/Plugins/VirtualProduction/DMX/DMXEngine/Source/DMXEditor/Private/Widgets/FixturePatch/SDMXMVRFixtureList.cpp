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

	/** Generates warning texts. Returns a map of those Items that need a warning set along with the Warning Text */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GenerateWarningTexts() const
	{
		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> WarningTextMap;

		return WarningTextMap;
	}

	/** Generates warning texts. Returns a map of those Items that need a warning set along with the Error Text */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GenerateErrorTexts() const
	{
		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> UnitNumberConflicts = GetUnitNumberConflicts();
		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> ChannelConflicts = GetChannelConflicts();

		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> AccumulatedConflicts = UnitNumberConflicts;
		for (const TTuple<TSharedPtr<FDMXMVRFixtureListItem>, FText>& ChannelConflict : ChannelConflicts)
		{
			if (AccumulatedConflicts.Contains(ChannelConflict.Key))
			{
				const FText LineTerminator = FText::FromString(LINE_TERMINATOR);
				const FText AccumulatedErrorText = FText::Format(FText::FromString(TEXT("{0}{1}{2}{3}")), AccumulatedConflicts[ChannelConflict.Key], LineTerminator, LineTerminator, ChannelConflict.Value);
				AccumulatedConflicts[ChannelConflict.Key] = AccumulatedErrorText;
			}
			else
			{
				AccumulatedConflicts.Add(ChannelConflict.Key, ChannelConflict.Value);
			}
		}

		return AccumulatedConflicts;
	}

private:
	/** The patch of an item */
	struct FItemPatch
	{
		FItemPatch(const TSharedPtr<FDMXMVRFixtureListItem>& InItem)
			: Item(InItem)
		{
			Universe = Item->GetUniverse();
			AddressRange = TRange<int32>(Item->GetAddress(), Item->GetAddress() + Item->GetNumChannels());
		}

		FText GetConfictsWith(const FItemPatch& Other) const
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

			const FText UnitNumberText = MakeBeautifulItemText(Item);
			const FText OtherUnitNumberText = MakeBeautifulItemText(Other.Item);
			if (AddressRange.GetLowerBound() == Other.AddressRange.GetLowerBound() &&
				Item->GetFixtureType() == Other.Item->GetFixtureType())
			{
				// Modes confict
				check(Item->GetModeIndex() != Other.Item->GetModeIndex());
				return FText::Format(LOCTEXT("ModeConflict", "Using same Address and Fixture Type as Fixture {1}, but Modes differ."), UnitNumberText, OtherUnitNumberText);
			}
			else if (AddressRange.GetLowerBound() == Other.AddressRange.GetLowerBound())
			{
				// Fixture Types conflict
				check(Item->GetFixtureType() != Other.Item->GetFixtureType());
				return FText::Format(LOCTEXT("FixtureTypeConflict", "Using same Address as Fixture {1}, but Fixture Types differ."), UnitNumberText, OtherUnitNumberText);
			}
			else
			{
				// Addresses conflict
				return FText::Format(LOCTEXT("AddressConflict", "Overlapping Addresses with Fixture {1}"), UnitNumberText, OtherUnitNumberText);
			}
		}

		FORCEINLINE const TSharedPtr<FDMXMVRFixtureListItem>& GetItem() const { return Item; };


	private:
		int32 Universe = -1;
		TRange<int32> AddressRange;

		TSharedPtr<FDMXMVRFixtureListItem> Item;
	};

	/** Returns an Map of Items to Channel conflict Texts */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GetChannelConflicts() const
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
				const FText ConflictText = ItemPatch.GetConfictsWith(Other);
				if (!ConflictText.IsEmpty())
				{
					if (ItemToConflictMap.Contains(ItemPatch.GetItem()))
					{
						FText AppendConflictText = FText::Format(FText::FromString(TEXT("{0}{1}{2}")), ItemToConflictMap[ItemPatch.GetItem()], FText::FromString(FString(LINE_TERMINATOR)), ConflictText);
						ItemToConflictMap[ItemPatch.GetItem()] = AppendConflictText;
					}
					else
					{
						ItemToConflictMap.Add(ItemPatch.GetItem(), ConflictText);
					}
				}
			}
		}

		return ItemToConflictMap;
	}

	/** Returns an Map of Items to Unit Number conflict Texts */
	TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> GetUnitNumberConflicts() const
	{
		TMap<int32, TArray<TSharedPtr<FDMXMVRFixtureListItem>>> UnitNumberMap;
		UnitNumberMap.Reserve(Items.Num());
		for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : Items)
		{
			UnitNumberMap.FindOrAdd(Item->GetUnitNumber()).Add(Item);
		}
		TArray<TArray<TSharedPtr<FDMXMVRFixtureListItem>>> UnitNumberConflicts;
		UnitNumberMap.GenerateValueArray(UnitNumberConflicts);
		UnitNumberConflicts.RemoveAll([](const TArray<TSharedPtr<FDMXMVRFixtureListItem>>& ConflictingItems)
			{
				return ConflictingItems.Num() < 2;
			});
		
		TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> ItemToConflictMap;
		for (TArray<TSharedPtr<FDMXMVRFixtureListItem>>& ConflictingItems : UnitNumberConflicts)
		{
			ConflictingItems.Sort([](const TSharedPtr<FDMXMVRFixtureListItem>& ItemA, const TSharedPtr<FDMXMVRFixtureListItem>& ItemB)
				{
					return ItemA->GetUnitNumber() < ItemB->GetUnitNumber();
				});

			check(ConflictingItems.Num() > 0);
			FText ConflictText = FText::Format(LOCTEXT("BaseUnitNumberConflictText", "Ambiguous IDs in {0}"), MakeBeautifulItemText(ConflictingItems[0]));
			for (int32 ConflictingItemIndex = 1; ConflictingItemIndex < ConflictingItems.Num(); ConflictingItemIndex++)
			{
				ConflictText = FText::Format(LOCTEXT("AppendUnitNumberConflictText", "{0}, {1}"), ConflictText, MakeBeautifulItemText(ConflictingItems[ConflictingItemIndex]));
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
const FName FDMXMVRFixtureListCollumnIDs::UnitNumber = "UnitNumber";
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

	const TMap<TSharedPtr<FDMXMVRFixtureListItem>, FText> ErrorTextMap = StatusTextGenerator.GenerateErrorTexts();
	for (const TTuple<TSharedPtr<FDMXMVRFixtureListItem>, FText>& ItemToErrorTextPair : ErrorTextMap)
	{
		ItemToErrorTextPair.Key->WarningStatusText = ItemToErrorTextPair.Value;
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
 
	TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> FixturePatches;
	const TArray<TSharedPtr<FDMXMVRFixtureListItem>> SelectedItems = ListView->GetSelectedItems();
	for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : SelectedItems)
	{
		if (UDMXEntityFixturePatch* FixturePatch = Item->GetFixturePatch())
		{
			FixturePatches.AddUnique(FixturePatch);
		}
	}

	FixturePatchSharedData->SelectFixturePatches(FixturePatches);

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
		.ColumnId(FDMXMVRFixtureListCollumnIDs::UnitNumber)
		.SortMode(this, &SDMXMVRFixtureList::GetColumnSortMode, FDMXMVRFixtureListCollumnIDs::UnitNumber)
		.OnSort(this, &SDMXMVRFixtureList::SortByColumnID)
		.DefaultLabel(LOCTEXT("UnitNumberColumnLabel", "ID"))
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
			if (Column.ColumnId == FDMXMVRFixtureListCollumnIDs::UnitNumber)
			{
				EditorSettings->MVRFixtureListSettings.UnitNumberColumnWidth = Column.Width.Get();
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
		const float UnitNumberColumnWidth = EditorSettings->MVRFixtureListSettings.UnitNumberColumnWidth;
		if (UnitNumberColumnWidth > 20.f)
		{
			HeaderRow->SetColumnWidth(FDMXMVRFixtureListCollumnIDs::UnitNumber, UnitNumberColumnWidth);
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

	if (ColumnId == FDMXMVRFixtureListCollumnIDs::UnitNumber)
	{
		if (InSortMode == EColumnSortMode::Descending)
		{
			ListSource.Sort([](const TSharedPtr<FDMXMVRFixtureListItem>& ItemA, const TSharedPtr<FDMXMVRFixtureListItem>& ItemB)
				{
					return !(ItemA->GetUnitNumber() <= ItemB->GetUnitNumber());
				});
		}
		else if (InSortMode == EColumnSortMode::Ascending)
		{
			ListSource.Sort([](const TSharedPtr<FDMXMVRFixtureListItem>& ItemA, const TSharedPtr<FDMXMVRFixtureListItem>& ItemB)
				{
					return !(ItemA->GetUnitNumber() >= ItemB->GetUnitNumber());
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
		MenuBuilder.BeginSection("BasicOperations");
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
