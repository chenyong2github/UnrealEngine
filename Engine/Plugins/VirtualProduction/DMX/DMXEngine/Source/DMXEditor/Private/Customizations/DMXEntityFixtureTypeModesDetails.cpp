// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXEntityFixtureTypeModesDetails.h"

#include "DMXEditor.h"
#include "DMXFixtureTypeSharedData.h"
#include "Commands/DMXEditorCommands.h"
#include "Library/DMXEntityFixtureType.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "DetailWidgetRow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "DMXEntityFixtureTypeModesDetails"

class SDMXModeItemListViewBox
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SDMXModeItemListViewBox) { }
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<FDMXEditor>& InDMXEditor, const TSharedPtr<IPropertyHandleArray>& InModesHandleArray);

	/** Request mode lists updates outside the widget */
	void RequestRefresh() { bRefreshRequested = true; }

protected:
	// Begin SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	// End SWidget

private:
	/** Registers commands for handling actions, such as copy/paste */
	void RegisterCommands();

	/** Generates a Table Row of Mode names */
	TSharedRef<ITableRow> GenerateModeNameRow(TSharedPtr<FDMXFixtureModeItem> NewlySelectedItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Callback for when the a mode is selected in the list view */
	void OnListSelectionChanged(TSharedPtr<FDMXFixtureModeItem>, ESelectInfo::Type SelectInfo);

	/** Called when a context menu should be displayed on a list item */
	TSharedPtr<SWidget> OnContextMenuOpening();

	/** 
	 * Rebuilds the list from Modes Arrays 
	 * If bUpdateSelection is true, tries to make a selection
	 */
	void RebuildList(bool bUpdateSelection = true);

	/** Called when num modes changed */
	void OnNumModesChanged();

	/** Cut selected Item(s) */
	void OnCutSelectedItems();
	bool CanCutItems() const;

	/** Copy selected Item(s) */
	void OnCopySelectedItems();
	bool CanCopyItems() const;

	/** Pastes previously copied Item(s) */
	void OnPasteItems();
	bool CanPasteItems() const;

	/** Duplicates the selected Item */
	bool CanDuplicateItems() const;
	void OnDuplicateItems();

	/** Removes existing selected component Items */
	void OnDeleteItems();
	bool CanDeleteItems() const;

	/** Requests a rename on the selected Entity. */
	void OnRenameItem();
	bool CanRenameItem() const;

private:

	/** Command list for handling actions such as copy/paste */
	TSharedPtr<FUICommandList> CommandList;

	/** Shared data for fixture types */
	TSharedPtr<FDMXFixtureTypeSharedData> SharedData;

	/** The list widget where the user can select modes names */
	TSharedPtr<SDMXModeItemListView> ListView;

	/** Modes Property Handle Array */
	TSharedPtr<IPropertyHandleArray> ModesHandleArray;

	/** Array of Mode Items being displayed */
	TArray<TSharedPtr<FDMXFixtureModeItem>> ListSource;

	/** The widget holds list where the user can select mode names */
	TSharedPtr<SDMXModeItemListViewBox> ListViewBox;

	/** Table rows displayed in the list view */
	TArray<TSharedPtr<SDMXNamedTypeRow<FDMXFixtureModeItem>>> TableRows;

	/** Request list update flag */
	bool bRefreshRequested;
};

void SDMXModeItemListViewBox::Construct(const FArguments& InArgs, const TSharedPtr<FDMXEditor>& InDMXEditor, const TSharedPtr<IPropertyHandleArray>& InModesHandleArray)
{
	bRefreshRequested = false;
	ModesHandleArray = InModesHandleArray;
	SharedData = InDMXEditor->GetFixtureTypeSharedData();

	FSimpleDelegate OnNumModesChangedDelegate = FSimpleDelegate::CreateSP(this, &SDMXModeItemListViewBox::OnNumModesChanged);
	ModesHandleArray->SetOnNumElementsChanged(OnNumModesChangedDelegate);

	ChildSlot
		[
			SNew(SBox)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SAssignNew(ListView, SDMXModeItemListView)
				.ItemHeight(40.0f)
				.ListItemsSource(&ListSource)
				.OnGenerateRow(this, &SDMXModeItemListViewBox::GenerateModeNameRow)
				.OnSelectionChanged(this, &SDMXModeItemListViewBox::OnListSelectionChanged)
				.OnContextMenuOpening(this, &SDMXModeItemListViewBox::OnContextMenuOpening)
				.SelectionMode(ESelectionMode::Single)
			]
		];

	RebuildList();

	RegisterCommands();
}

void SDMXModeItemListViewBox::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshRequested)
	{
		RebuildList();

		bRefreshRequested = false;
	}
}

FReply SDMXModeItemListViewBox::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SDMXModeItemListViewBox::RebuildList(bool bKeepSelection /** = true */)
{
	check(SharedData.IsValid());
	check(ModesHandleArray.IsValid());

	TArray<TSharedPtr<FDMXFixtureModeItem>> ModesBeingEdited;
	uint32 NumModes = 0;
	if (ModesHandleArray->GetNumElements(NumModes) == FPropertyAccess::Success)
	{
		for (uint32 IndexOfMode = 0; IndexOfMode < NumModes; IndexOfMode++)
		{
			TSharedRef<IPropertyHandle> ModeHandle = ModesHandleArray->GetElement(IndexOfMode);
			TSharedPtr<IPropertyHandle> NameHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName));
			check(NameHandle.IsValid() && NameHandle->IsValidHandle());

			TSharedPtr<IPropertyHandle> ModeNameHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName));
			check(ModeNameHandle.IsValid() && ModeNameHandle->IsValidHandle());

			TSharedRef<FDMXFixtureModeItem> ModeItem = MakeShared<FDMXFixtureModeItem>(SharedData, ModeNameHandle);
			ModesBeingEdited.Add(ModeItem);
		}
	}

	SharedData->SetModesBeingEdited(ModesBeingEdited);

	ListSource = ModesBeingEdited;

	TableRows.Reset();
	ListView->RebuildList();

	if (bKeepSelection)
	{
		// Adopt selection from mode items
		TArray<TSharedPtr<FDMXFixtureModeItem>> SelectedModes;
		for (const TSharedPtr<FDMXFixtureModeItem>& ModeItem : ListSource)
		{
			if (ModeItem->IsModeSelected())
			{
				SelectedModes.Add(ModeItem);
			}
		}

		if (SelectedModes.Num() == 0 && ListSource.Num() > 0)
		{
			// If nothing is selected, select the first mode instead
			ListView->SetSelection(ListSource[0]);
		}
		else
		{
			ListView->SetItemSelection(SelectedModes, true);
		}
	}
}

void SDMXModeItemListViewBox::OnNumModesChanged()
{
	RebuildList(false);

	// Select last
	if (ListSource.Num() > 0)
	{
		int32 LastIndex = ListSource.Num() - 1;
		ListView->SetSelection(ListSource[LastIndex]);
	}
}

TSharedRef<ITableRow> SDMXModeItemListViewBox::GenerateModeNameRow(TSharedPtr<FDMXFixtureModeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SDMXNamedTypeRow<FDMXFixtureModeItem>> Row =
		SNew(SDMXNamedTypeRow<FDMXFixtureModeItem>, OwnerTable, InItem)
		.NameEmptyError(LOCTEXT("FixtureModeName.ErrorEmpty", "The Mode Name can't be blank."))
		.NameDuplicateError(LOCTEXT("FixtureModeName.ErrorDuplicateModeName", "Mode Name is already in use."));

	// Remeber the row
	TableRows.Add(Row);

	return Row;
}

void SDMXModeItemListViewBox::OnListSelectionChanged(TSharedPtr<FDMXFixtureModeItem> NewlySelectedItem, ESelectInfo::Type SelectInfo)
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureModeItem>> SelectedItems;
	ListView->GetSelectedItems(SelectedItems);

	SharedData->SelectModes(SelectedItems);
}

TSharedPtr<SWidget> SDMXModeItemListViewBox::OnContextMenuOpening()
{
	const bool bCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection("BasicOperations");
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SDMXModeItemListViewBox::RegisterCommands()
{
	// Listen to common editor shortcuts for copy/paste etc
	if (!CommandList.IsValid())
	{
		CommandList = MakeShared<FUICommandList>();

		CommandList->MapAction(FGenericCommands::Get().Cut,
			FUIAction(FExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::OnCutSelectedItems),
				FCanExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::CanCutItems))
			);

		CommandList->MapAction(FGenericCommands::Get().Copy,
			FUIAction(FExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::OnCopySelectedItems),
				FCanExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::CanCopyItems))
			);

		CommandList->MapAction(FGenericCommands::Get().Paste,
			FUIAction(FExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::OnPasteItems),
				FCanExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::CanPasteItems))
			);

		CommandList->MapAction(FGenericCommands::Get().Duplicate,
			FUIAction(FExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::OnDuplicateItems),
				FCanExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::CanDuplicateItems))
			);

		CommandList->MapAction(FGenericCommands::Get().Delete,
			FUIAction(FExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::OnDeleteItems),
				FCanExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::CanDeleteItems))
			);

		CommandList->MapAction(FGenericCommands::Get().Rename,
			FUIAction(FExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::OnRenameItem),
				FCanExecuteAction::CreateSP(this, &SDMXModeItemListViewBox::CanRenameItem))
			);
	}
}

bool SDMXModeItemListViewBox::CanCutItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems == 1;
}

void SDMXModeItemListViewBox::OnCutSelectedItems()
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	const FScopedTransaction Transaction(NumSelectedItems > 1 ? LOCTEXT("CutModes", "Cut Modes") : LOCTEXT("CutMode", "Cut Mode"));

	OnCopySelectedItems();
	OnDeleteItems();
}

bool SDMXModeItemListViewBox::CanCopyItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems > 0;
}

void SDMXModeItemListViewBox::OnCopySelectedItems()
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureModeItem>> SelectedModes;
	ListView->GetSelectedItems(SelectedModes);

	SharedData->CopyModesToClipboard(SelectedModes);
}

bool SDMXModeItemListViewBox::CanPasteItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems > 0;
}

void SDMXModeItemListViewBox::OnPasteItems()
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureModeItem>> SelectedModes;
	ListView->GetSelectedItems(SelectedModes);

	SharedData->PasteClipboardToModes(SelectedModes);
}

bool SDMXModeItemListViewBox::CanDuplicateItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems > 0;
}

void SDMXModeItemListViewBox::OnDuplicateItems()
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureModeItem>> SelectedModes;
	ListView->GetSelectedItems(SelectedModes);

	SharedData->DuplicateModes(SelectedModes);
}

bool SDMXModeItemListViewBox::CanDeleteItems() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems > 0;
}

void SDMXModeItemListViewBox::OnDeleteItems()
{
	check(SharedData.IsValid());

	TArray<TSharedPtr<FDMXFixtureModeItem>> SelectedModes;
	ListView->GetSelectedItems(SelectedModes);

	SharedData->DeleteModes(SelectedModes);
}

bool SDMXModeItemListViewBox::CanRenameItem() const
{
	int32 NumSelectedItems = ListView->GetNumItemsSelected();
	return NumSelectedItems == 1;
}

void SDMXModeItemListViewBox::OnRenameItem()
{
	TArray<TSharedPtr<FDMXFixtureModeItem>> SelectedModes;
	ListView->GetSelectedItems(SelectedModes);
	check(SelectedModes.Num() == 1);

	const TSharedPtr<SDMXNamedTypeRow<FDMXFixtureModeItem>>* RowPtr = TableRows.FindByPredicate([&](TSharedPtr<SDMXNamedTypeRow<FDMXFixtureModeItem>> Row) {
		return Row->GetItem() == SelectedModes[0];
	});

	if (RowPtr)
	{
		(*RowPtr)->EnterEditingMode();
	}
}

void FDMXEntityFixtureTypeModesDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideCategory("Entity Properties");
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXImport));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXCategory));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, bFixtureMatrixEnabled));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, InputModulators));

	if(TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{	
		// Add the 'Add New Mode' Button, even if the details aren't shown
		// Allows us to provide extra info to the UI user when the list isn't shown
		DetailBuilder.EditCategory("Modes")
			.HeaderContent(
				CreateAddModeButton()
			);
		
		PropertyUtilities = DetailBuilder.GetPropertyUtilities();
		check(PropertyUtilities.IsValid());

		SharedData = DMXEditor->GetFixtureTypeSharedData();
		check(SharedData.IsValid());

		TSharedPtr<IPropertyHandle> ModesHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
		check(ModesHandle.IsValid() && ModesHandle->IsValidHandle());
		ModesHandleArray = ModesHandle->AsArray();

		DetailBuilder.EditCategory("Modes")			
			.AddCustomRow(LOCTEXT("Mode", "Mode"))
			.WholeRowContent()			
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)		
			[
				SAssignNew(ListViewBox, SDMXModeItemListViewBox, DMXEditorPtr.Pin(), ModesHandleArray)
			];

		GEditor->RegisterForUndo(this);
	}
}

void FDMXEntityFixtureTypeModesDetails::PostUndo(bool bSuccess)
{
	ListViewBox->RequestRefresh();
}

void FDMXEntityFixtureTypeModesDetails::PostRedo(bool bSuccess)
{
	ListViewBox->RequestRefresh();
}

TSharedRef<SWidget> FDMXEntityFixtureTypeModesDetails::CreateAddModeButton() const
{
	return
		SNew(SBox)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.IsEnabled(this, &FDMXEntityFixtureTypeModesDetails::GetIsAddModeButtonEnabled)
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
				.ForegroundColor(FLinearColor::White)
				.ToolTipText(this, &FDMXEntityFixtureTypeModesDetails::GetAddModeButtonTooltipText)
				.ContentPadding(FMargin(5.0f, 1.0f))
				.OnClicked(this, &FDMXEntityFixtureTypeModesDetails::OnAddModeButtonClicked)
				.Content()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(0, 1))
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("Plus"))
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(FMargin(2, 0, 2, 0))
					[
						SNew(STextBlock)
						.Text(this, &FDMXEntityFixtureTypeModesDetails::GetAddModeButtonText)
					]
				]
			];
}

bool FDMXEntityFixtureTypeModesDetails::GetIsAddModeButtonEnabled() const
{
	check(SharedData.IsValid());
	if (SharedData->CanAddMode())
	{
		return true;
	}
		
	return false;	
}

FText FDMXEntityFixtureTypeModesDetails::GetAddModeButtonTooltipText() const
{	
	check(SharedData.IsValid());
	if (SharedData->CanAddMode())
	{
		return FDMXEditorCommands::Get().AddNewFixtureTypeMode->GetDescription();
	}

	// Omit to inform about the rare case where the asset is deleted.
	return LOCTEXT("AddModeButtonDisabledTooltip", "Please select a single Fixture Type to add a Mode to it.");
}

FReply FDMXEntityFixtureTypeModesDetails::OnAddModeButtonClicked() const
{	
	check(SharedData.IsValid());
	check(SharedData->CanAddMode());
	
	const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("DMXEntityFixtureTypeModesDetails.ModeAdded", "Mode added to Fixture Type"));
	SharedData->AddMode(Transaction);

	return FReply::Handled();
}

FText FDMXEntityFixtureTypeModesDetails::GetAddModeButtonText() const
{
	check(SharedData.IsValid());
	if (SharedData->CanAddMode())
	{
		return FDMXEditorCommands::Get().AddNewFixtureTypeMode->GetLabel();
	}

	// Omit to inform about the rare case where the asset is deleted.
	return LOCTEXT("AddModeButtonDisabled", "Multiple Fixture Types Selected");
}

#undef LOCTEXT_NAMESPACE
