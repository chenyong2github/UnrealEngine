// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveTableEditor.h"
#include "Widgets/Text/STextBlock.h"
#include "Modules/ModuleManager.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Framework/Layout/Overscroll.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Layout/SScrollBox.h"
#include "SPositiveActionButton.h"
#include "EditorStyleSet.h"
#include "Styling/StyleColors.h"
#include "EditorReimportHandler.h"
#include "CurveTableEditorModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "CurveEditor.h" 
#include "SCurveEditorPanel.h"
#include "Tree/SCurveEditorTree.h"
#include "Tree/SCurveEditorTreeSelect.h"
#include "Tree/SCurveEditorTreePin.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "Tree/CurveEditorTreeFilter.h"

#include "Tree/SCurveEditorTreeTextFilter.h"

#include "RealCurveModel.h"
#include "RichCurveEditorModel.h"

#include "CurveTableEditorCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

 
#define LOCTEXT_NAMESPACE "CurveTableEditor"

const FName FCurveTableEditor::CurveTableTabId("CurveTableEditor_CurveTable");

struct FCurveTableEditorColumnHeaderData
{
	/** Unique ID used to identify this column */
	FName ColumnId;

	/** Display name of this column */
	FText DisplayName;

	/** The calculated width of this column taking into account the cell data for each row */
	float DesiredColumnWidth;

	/** The evaluated key time **/
	float KeyTime;
};

namespace {

		FName MakeUniqueCurveName( UCurveTable* Table )
		{
				check(Table != nullptr);

				int incr = 0;	
				FName TestName = FName("Curve", incr);

				const TMap<FName, FRealCurve*>& RowMap = Table->GetRowMap();

				while (RowMap.Contains(TestName))
				{
						TestName = FName("Curve", ++incr);
				}

				return TestName;
		}
}

/*
* FCurveTableEditorItem
*
*  FCurveTableEditorItem uses and extends the CurveEditorTreeItem to be used in both our TableView and the CurveEditorTree.
*  The added GenerateTableViewCell handles the table columns unknown to the standard CurveEditorTree.
*
*/ 
class FCurveTableEditorItem : public ICurveEditorTreeItem,  public TSharedFromThis<FCurveTableEditorItem>
{

  	struct CachedKeyInfo
  	{
  		CachedKeyInfo(FKeyHandle& InKeyHandle, FText InDisplayValue) :
  		KeyHandle(InKeyHandle)
  		, DisplayValue(InDisplayValue) {}

  		FKeyHandle KeyHandle;

  		FText DisplayValue;	
  	};

  public: 
	FCurveTableEditorItem (const FName& InRowId, FCurveTableEditorHandle InRowHandle, const TArray<FCurveTableEditorColumnHeaderDataPtr>& InColumns)
		: RowId(InRowId)
		, RowHandle(InRowHandle)
		, Columns(InColumns)
	{
		DisplayName = FText::FromName(InRowId);

		CacheKeys();
	}

	TSharedPtr<SWidget> GenerateCurveEditorTreeWidget(const FName& InColumnName, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID, const TSharedRef<ITableRow>& InTableRow) override
	{
		if (InColumnName == ColumnNames.Label)
		{
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.Padding(FMargin(4.f))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(DisplayName)
					.ColorAndOpacity(FSlateColor::UseForeground())
				];
		}
		else if (InColumnName == ColumnNames.SelectHeader)
		{
			return SNew(SCurveEditorTreeSelect, InCurveEditor, InTreeItemID, InTableRow);
		}
		else if (InColumnName == ColumnNames.PinHeader)
		{
			return SNew(SCurveEditorTreePin, InCurveEditor, InTreeItemID, InTableRow);
		}

		return GenerateTableViewCell(InColumnName, InCurveEditor, InTreeItemID, InTableRow);
	}

	TSharedPtr<SWidget> GenerateTableViewCell(const FName& InColumnId, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID, const TSharedRef<ITableRow>& InTableRow)
	{
		if (!RowHandle.HasRichCurves())
		{
			FRealCurve* Curve = RowHandle.GetCurve();
			FKeyHandle& KeyHandle = CellDataMap[InColumnId].KeyHandle;

			return SNew(SNumericEntryBox<float>)
				.EditableTextBoxStyle( &FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("CurveTableEditor.Cell.Text") )
				.Value_Lambda([Curve, KeyHandle] () { return Curve->GetKeyValue(KeyHandle); })
				// .Value_Lambda([this, InColumnId] () { return CellDataF[InColumnId]; })
				.OnValueChanged_Lambda([Curve, KeyHandle] (float NewValue) {Curve->SetKeyValue(KeyHandle, NewValue);})
				.Justification(ETextJustify::Right)
			;
		}
		return SNullWidget::NullWidget;
	}

	void CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels) override
	{
		if (RowHandle.HasRichCurves())
		{
			if (FRichCurve* RichCurve = RowHandle.GetRichCurve())
			{
				const UCurveTable* Table = RowHandle.CurveTable.Get();
				UCurveTable* RawTable = const_cast<UCurveTable*>(Table);

				TUniquePtr<FRichCurveEditorModelRaw> NewCurve = MakeUnique<FRichCurveEditorModelRaw>(RichCurve, RawTable);
				NewCurve->SetShortDisplayName(DisplayName);
				NewCurve->SetColor(FStyleColors::AccentOrange.GetSpecifiedColor());
				OutCurveModels.Add(MoveTemp(NewCurve));
			}
		}
		else
		{
			const UCurveTable* Table = RowHandle.CurveTable.Get();
			UCurveTable* RawTable = const_cast<UCurveTable*>(Table);

			TUniquePtr<FRealCurveModel> NewCurveModel = MakeUnique<FRealCurveModel>(RowHandle.GetCurve(), RawTable);
			NewCurveModel->SetShortDisplayName(DisplayName);

			OutCurveModels.Add(MoveTemp(NewCurveModel));
		}
	}

	bool PassesFilter(const FCurveEditorTreeFilter* InFilter) const override
	{
		if (InFilter->GetType() == ECurveEditorTreeFilterType::Text)
		{
			const FCurveEditorTreeTextFilter* Filter = static_cast<const FCurveEditorTreeTextFilter*>(InFilter);
			for (const FCurveEditorTreeTextFilterTerm& Term : Filter->GetTerms())
			{
				for(const FCurveEditorTreeTextFilterToken& Token : Term.ChildToParentTokens)
				{
					if(Token.Match(*DisplayName.ToString()))
					{
						return true;
					}
				}
			}

			return false;
		}

		return false;
	}

	void CacheKeys()
	{
		if (!RowHandle.HasRichCurves())
		{
			if (FRealCurve* Curve = RowHandle.GetCurve())
			{	
				for (auto Col : Columns)
				{
					FKeyHandle KeyHandle = Curve->FindKey(Col->KeyTime);
					float KeyValue = Curve->GetKeyValue(KeyHandle);

					CellDataMap.Add(Col->ColumnId, CachedKeyInfo(KeyHandle, FText::AsNumber(KeyValue))); 
				}
			}
		}
	}

	/** Unique ID used to identify this row */
	FName RowId;

	/** Display name of this row */
	FText DisplayName;

	/** Array corresponding to each cell in this row */
	TMap<FName, CachedKeyInfo> CellDataMap;

	/** Handle to the row */
	FCurveTableEditorHandle RowHandle;

	/** A Reference to the available columns in the TableView */
	const TArray<FCurveTableEditorColumnHeaderDataPtr>& Columns;
};


void FCurveTableEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_CurveTableEditor", "Curve Table Editor"));

	InTabManager->RegisterTabSpawner( CurveTableTabId, FOnSpawnTab::CreateSP(this, &FCurveTableEditor::SpawnTab_CurveTable) )
		.SetDisplayName( LOCTEXT("CurveTableTab", "Curve Table") )
		.SetGroup( WorkspaceMenuCategory.ToSharedRef() );
}


void FCurveTableEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner( CurveTableTabId );
}


FCurveTableEditor::~FCurveTableEditor()
{
	FReimportManager::Instance()->OnPostReimport().RemoveAll(this);
}


void FCurveTableEditor::InitCurveTableEditor( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UCurveTable* Table )
{
	const TSharedRef< FTabManager::FLayout > StandaloneDefaultLayout = InitCurveTableLayout();

	FAssetEditorToolkit::InitAssetEditor( Mode, InitToolkitHost, FCurveTableEditorModule::CurveTableEditorAppIdentifier, StandaloneDefaultLayout, ShouldCreateDefaultStandaloneMenu(), ShouldCreateDefaultToolbar(), Table );
	
	BindCommands();
	ExtendMenu();
	RegenerateMenusAndToolbars();

	FReimportManager::Instance()->OnPostReimport().AddSP(this, &FCurveTableEditor::OnPostReimport);
}

TSharedRef< FTabManager::FLayout > FCurveTableEditor::InitCurveTableLayout()
{
	return FTabManager::NewLayout("Standalone_CurveTableEditor_Layout_v1.1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->Split
			(
				FTabManager::NewStack()
				->AddTab(CurveTableTabId, ETabState::OpenedTab)
				->SetHideTabWell(true)
			)
		);
}

void FCurveTableEditor::BindCommands()
{
	FCurveTableEditorCommands::Register();

	ToolkitCommands->MapAction(FCurveTableEditorCommands::Get().CurveViewToggle,
		FExecuteAction::CreateSP(this, &FCurveTableEditor::ToggleViewMode),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FCurveTableEditor::IsCurveViewChecked));
}

void FCurveTableEditor::ExtendMenu()
{
	MenuExtender = MakeShareable(new FExtender);

	struct Local
	{
		static void ExtendMenu(FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.BeginSection("CurveTableEditor", LOCTEXT("CurveTableEditor", "Curve Table"));
			{
				MenuBuilder.AddMenuEntry(FCurveTableEditorCommands::Get().CurveViewToggle);
			}
			MenuBuilder.EndSection();
		}
	};

	MenuExtender->AddMenuExtension(
		"WindowLayout",
		EExtensionHook::After,
		GetToolkitCommands(),
		FMenuExtensionDelegate::CreateStatic(&Local::ExtendMenu)
	);

	AddMenuExtender(MenuExtender);

	FCurveTableEditorModule& CurveTableEditorModule = FModuleManager::LoadModuleChecked<FCurveTableEditorModule>("CurveTableEditor");
	AddMenuExtender(CurveTableEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

FName FCurveTableEditor::GetToolkitFName() const
{
	return FName("CurveTableEditor");
}

FText FCurveTableEditor::GetBaseToolkitName() const
{
	return LOCTEXT( "AppLabel", "CurveTable Editor" );
}

FString FCurveTableEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "CurveTable ").ToString();
}

FLinearColor FCurveTableEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.0f, 0.0f, 0.2f, 0.5f );
}

void FCurveTableEditor::PreChange(const UCurveTable* Changed, FCurveTableEditorUtils::ECurveTableChangeInfo Info)
{
}

void FCurveTableEditor::PostChange(const UCurveTable* Changed, FCurveTableEditorUtils::ECurveTableChangeInfo Info)
{
	const UCurveTable* Table = GetCurveTable();
	if (Changed == Table)
	{
		HandlePostChange();
	}
}

const UCurveTable* FCurveTableEditor::GetCurveTable() const
{
	return Cast<const UCurveTable>(GetEditingObject());
}

void FCurveTableEditor::HandlePostChange()
{
	RefreshCachedCurveTable();
}

TSharedRef<SDockTab> FCurveTableEditor::SpawnTab_CurveTable( const FSpawnTabArgs& Args )
{
	check( Args.GetTabId().TabType == CurveTableTabId );

	bUpdatingTableViewSelection = false;

	TSharedRef<SScrollBar> VerticalScrollBar = SNew(SScrollBar)
		.Orientation(Orient_Vertical);

	ColumnNamesHeaderRow = SNew(SHeaderRow)
		.Visibility(this, &FCurveTableEditor::GetTableViewControlsVisibility);

	CurveEditor = MakeShared<FCurveEditor>();

	FCurveEditorInitParams CurveEditorInitParams;
	CurveEditor->InitCurveEditor(CurveEditorInitParams);

	CurveEditorTree = SNew(SCurveEditorTree, CurveEditor.ToSharedRef())
		.OnTreeViewScrolled(this, &FCurveTableEditor::OnCurveTreeViewScrolled);

	TSharedRef<SCurveEditorPanel> CurveEditorPanel = SNew(SCurveEditorPanel, CurveEditor.ToSharedRef());

	TableView = SNew(SListView<FCurveEditorTreeItemID>)
		.ListItemsSource(&EmptyItems)
		.OnListViewScrolled(this, &FCurveTableEditor::OnTableViewScrolled)
		.HeaderRow(ColumnNamesHeaderRow)
		.OnGenerateRow(CurveEditorTree.Get(), &SCurveEditorTree::GenerateRow)
		.ExternalScrollbar(VerticalScrollBar)
		.SelectionMode(ESelectionMode::Multi)
		.OnSelectionChanged_Lambda(
			[this](TListTypeTraits<FCurveEditorTreeItemID>::NullableType InItemID, ESelectInfo::Type Type)
			{
				this->OnTableViewSelectionChanged(InItemID, Type);
			}
		);

	CurveEditor->GetTree()->Events.OnItemsChanged.AddSP(this, &FCurveTableEditor::RefreshTableRows);
	CurveEditor->GetTree()->Events.OnSelectionChanged.AddSP(this, &FCurveTableEditor::RefreshTableRowsSelection);

	ViewMode = GetCurveTable()->HasRichCurves() ? ECurveTableViewMode::CurveTable : ECurveTableViewMode::Grid;

	RefreshCachedCurveTable();

	return SNew(SDockTab)
		.Label( LOCTEXT("CurveTableTitle", "Curve Table") )
		.TabColorScale( GetTabColorScale() )
		[
			SNew(SBorder)
			.Padding(2)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(8, 0))
				[
					MakeToolbar(CurveEditorPanel)
				]

				+SVerticalBox::Slot()
				[
					SNew(SSplitter)
					+SSplitter::Slot()
					.Value(.2)
					[
						SNew(SVerticalBox)
					
						+SVerticalBox::Slot()
						.Padding(0, 0, 0, 1) // adjusting padding so as to line up the rows in the cell view
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(2.f, 0.f, 4.f, 0.0)
							[
								SNew(SPositiveActionButton)
								.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
								.Text(LOCTEXT("Curve", "Curve"))
								.OnClicked(this, &FCurveTableEditor::OnAddCurveClicked)
							]

							+SHorizontalBox::Slot()	
							[
								SNew(SCurveEditorTreeTextFilter, CurveEditor)
							]
						]

						+SVerticalBox::Slot()
						[
							CurveEditorTree.ToSharedRef()
						]

					]
					+SSplitter::Slot()
					[

						SNew(SHorizontalBox)
						.Visibility(this, &FCurveTableEditor::GetTableViewControlsVisibility)

						+SHorizontalBox::Slot()
						[
							SNew(SScrollBox)
							.Orientation(Orient_Horizontal)

							+SScrollBox::Slot()
							[
								TableView.ToSharedRef()
							]
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							VerticalScrollBar
						]
					]

					+SSplitter::Slot()
					[
						SNew(SBox)
						.Visibility(this, &FCurveTableEditor::GetCurveViewControlsVisibility)
						[
							CurveEditorPanel
						]
					]
				]
			]
		];
}

void FCurveTableEditor::RefreshTableRows()
{
	TableView->RequestListRefresh();
}

void FCurveTableEditor::RefreshTableRowsSelection()
{
	if(bUpdatingTableViewSelection == false)
	{
		TGuardValue<bool> SelecitonGuard(bUpdatingTableViewSelection, true);

		TArray<FCurveEditorTreeItemID> CurrentTreeWidgetSelection;
		TableView->GetSelectedItems(CurrentTreeWidgetSelection);
		const TMap<FCurveEditorTreeItemID, ECurveEditorTreeSelectionState>& CurrentCurveEditorTreeSelection = CurveEditor->GetTreeSelection();

		TArray<FCurveEditorTreeItemID> NewTreeWidgetSelection;
		for (const TPair<FCurveEditorTreeItemID, ECurveEditorTreeSelectionState>& CurveEditorTreeSelectionEntry : CurrentCurveEditorTreeSelection)
		{
			if (CurveEditorTreeSelectionEntry.Value != ECurveEditorTreeSelectionState::None)
			{
				NewTreeWidgetSelection.Add(CurveEditorTreeSelectionEntry.Key);
				CurrentTreeWidgetSelection.RemoveSwap(CurveEditorTreeSelectionEntry.Key);
			}
		}

		TableView->SetItemSelection(CurrentTreeWidgetSelection, false, ESelectInfo::Direct);
		TableView->SetItemSelection(NewTreeWidgetSelection, true, ESelectInfo::Direct);
	}
}

void FCurveTableEditor::OnTableViewSelectionChanged(FCurveEditorTreeItemID ItemID, ESelectInfo::Type)
{
	if (bUpdatingTableViewSelection == false)
	{
		TGuardValue<bool> SelecitonGuard(bUpdatingTableViewSelection, true);
		CurveEditor->GetTree()->SetDirectSelection(TableView->GetSelectedItems(), CurveEditor.Get());
	}
}

void FCurveTableEditor::RefreshCachedCurveTable()
{

	// This will trigger to remove any cached widgets in the TableView while we rebuild the model from the source CurveTable
	TableView->SetListItemsSource(EmptyItems);
	
	CurveEditor->RemoveAllTreeItems();

	ColumnNamesHeaderRow->ClearColumns();
	AvailableColumns.Empty();

	const UCurveTable* Table = GetCurveTable();
	if (!Table || Table->GetRowMap().Num() == 0)
	{
		return;
	}

	TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FTextBlockStyle& CellTextStyle = FEditorStyle::GetWidgetStyle<FTextBlockStyle>("DataTableEditor.CellText");
	static const float CellPadding = 10.0f;

	if (Table->HasRichCurves())
	{
		for (const TPair<FName, FRichCurve*>& CurveRow : Table->GetRichCurveRowMap())
		{
			// Setup the CurveEdtiorTree
			const FName& CurveName = CurveRow.Key;
			FCurveEditorTreeItem* TreeItem = CurveEditor->AddTreeItem(FCurveEditorTreeItemID());
			TreeItem->SetStrongItem(MakeShared<FCurveTableEditorItem>(CurveName, FCurveTableEditorHandle(Table, CurveName), AvailableColumns));
		}
	}

	else
	{
		// Find unique column titles and setup columns
		TArray<float> UniqueColumns;
		for (const TPair<FName, FRealCurve*>& CurveRow : Table->GetRowMap())
		{
			FRealCurve* Curve = CurveRow.Value;
			for (auto CurveIt(Curve->GetKeyHandleIterator()); CurveIt; ++CurveIt)
			{
				UniqueColumns.AddUnique(Curve->GetKeyTime(*CurveIt));
			}
		}
		UniqueColumns.Sort();
		for (const float& ColumnTime : UniqueColumns)
		{
			const FText ColumnText = FText::AsNumber(ColumnTime);
			FCurveTableEditorColumnHeaderDataPtr CachedColumnData = MakeShareable(new FCurveTableEditorColumnHeaderData());
			CachedColumnData->ColumnId = *ColumnText.ToString();
			CachedColumnData->DisplayName = ColumnText;
			CachedColumnData->DesiredColumnWidth = FontMeasure->Measure(CachedColumnData->DisplayName, CellTextStyle.Font).X + CellPadding;
			CachedColumnData->KeyTime = ColumnTime;

			AvailableColumns.Add(CachedColumnData);

			ColumnNamesHeaderRow->AddColumn(
				SHeaderRow::Column(CachedColumnData->ColumnId)
				.DefaultLabel(CachedColumnData->DisplayName)
				.FixedWidth(CachedColumnData->DesiredColumnWidth + 50)
				.HAlignHeader(HAlign_Center)
			);
		}

		// Setup the CurveEditorTree 
		for (const TPair<FName, FRealCurve*>& CurveRow : Table->GetRowMap())
		{
			const FName& CurveName = CurveRow.Key;
			FCurveEditorTreeItem* TreeItem = CurveEditor->AddTreeItem(FCurveEditorTreeItemID());
			TSharedPtr<FCurveTableEditorItem> NewItem = MakeShared<FCurveTableEditorItem>(CurveName, FCurveTableEditorHandle(Table, CurveName), AvailableColumns);
			OnColumnsChanged.AddSP(NewItem.ToSharedRef(), &FCurveTableEditorItem::CacheKeys);
			TreeItem->SetStrongItem(NewItem);
		}
	}

	TableView->SetListItemsSource(CurveEditorTree->GetSourceItems());
}

void FCurveTableEditor::OnCurveTreeViewScrolled(double InScrollOffset)
{
	// Synchronize the list views
	TableView->SetScrollOffset(InScrollOffset);
}


void FCurveTableEditor::OnTableViewScrolled(double InScrollOffset)
{
	// Synchronize the list views
	CurveEditorTree->SetScrollOffset(InScrollOffset);
}

void FCurveTableEditor::OnPostReimport(UObject* InObject, bool)
{
	const UCurveTable* Table = GetCurveTable();
	if (Table && Table == InObject)
	{
		RefreshCachedCurveTable();
	}
}

EVisibility FCurveTableEditor::GetTableViewControlsVisibility() const
{
	return ViewMode == ECurveTableViewMode::CurveTable ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility FCurveTableEditor::GetCurveViewControlsVisibility() const
{
	return ViewMode == ECurveTableViewMode::Grid ? EVisibility::Collapsed : EVisibility::Visible;
}

void FCurveTableEditor::ToggleViewMode()
{
	ViewMode = (ViewMode == ECurveTableViewMode::CurveTable) ? ECurveTableViewMode::Grid : ECurveTableViewMode::CurveTable;
}

bool FCurveTableEditor::IsCurveViewChecked() const
{
	return (ViewMode == ECurveTableViewMode::CurveTable);
}

TSharedRef<SWidget> FCurveTableEditor::MakeToolbar(TSharedRef<SCurveEditorPanel>& InEditorPanel)
{
	FToolBarBuilder ToolBarBuilder(InEditorPanel->GetCommands(), FMultiBoxCustomization::None, InEditorPanel->GetToolbarExtender(), true);
	ToolBarBuilder.SetStyle(&FAppStyle::Get(), "Sequencer.ToolBar");
	ToolBarBuilder.BeginSection("Asset");
	ToolBarBuilder.EndSection();
	// We just use all of the extenders as our toolbar, we don't have a need to create a separate toolbar.

	bool HasRichCurves = GetCurveTable()->HasRichCurves();

	return SNew(SHorizontalBox)

	+SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Center)
	.Padding(FMargin(2.0, 4.0, 8.f, 4.f))
	[
		SNew(SSegmentedControl<ECurveTableViewMode>)
		.Visibility(HasRichCurves ? EVisibility::Collapsed : EVisibility::Visible)
		.OnValueChanged_Lambda([this] (ECurveTableViewMode InMode) {if (InMode != GetViewMode()) ToggleViewMode();  } )
		.Value(this, &FCurveTableEditor::GetViewMode)

		+SSegmentedControl<ECurveTableViewMode>::Slot(ECurveTableViewMode::CurveTable)
        .Icon(FAppStyle::Get().GetBrush("CurveTableEditor.CurveView"))

		+SSegmentedControl<ECurveTableViewMode>::Slot(ECurveTableViewMode::Grid)
        .Icon(FAppStyle::Get().GetBrush("CurveTableEditor.TableView"))
	]

	+SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		[
			SNew(SButton)
            .ButtonStyle( &FAppStyle::Get().GetWidgetStyle< FButtonStyle >( "SimpleButton" ) )
			.Visibility(this, &FCurveTableEditor::GetTableViewControlsVisibility)
			.OnClicked(this, &FCurveTableEditor::OnAddNewKeyColumn)
			.ToolTipText(LOCTEXT("CurveTableEditor_AddKeyColumnTooltip", "Append a new column to the curve table.\nEvery Curve or Table Row will have a new key appended."))
			[
                SNew(SImage)
                .ColorAndOpacity(FSlateColor::UseForeground())
                .Image(FAppStyle::Get().GetBrush("Sequencer.KeyTriangle")) 
	        ]
		]
	]

	+SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.Visibility(this, &FCurveTableEditor::GetCurveViewControlsVisibility)
		[
			ToolBarBuilder.MakeWidget()
		]
	];
}

FReply FCurveTableEditor::OnAddCurveClicked()
{
	UCurveTable* Table = Cast<UCurveTable>(GetEditingObject());
	check(Table != nullptr);

	if (Table->HasRichCurves())
	{
		FName NewCurveUnique = MakeUniqueCurveName(Table);
		FRichCurve& NewCurve = Table->AddRichCurve(NewCurveUnique);
		FCurveEditorTreeItem* TreeItem = CurveEditor->AddTreeItem(FCurveEditorTreeItemID());
		TreeItem->SetStrongItem(MakeShared<FCurveTableEditorItem>(NewCurveUnique, FCurveTableEditorHandle(Table, NewCurveUnique), AvailableColumns));
	}
	else
	{
		FName NewCurveUnique = MakeUniqueCurveName(Table);
		FRealCurve& RealCurve = Table->AddSimpleCurve(NewCurveUnique);
		// Also add a default key for each column 
		for (auto Column : AvailableColumns)
		{
			RealCurve.AddKey(Column->KeyTime, 0.0);
		}

		FCurveEditorTreeItem* TreeItem = CurveEditor->AddTreeItem(FCurveEditorTreeItemID());
		TSharedPtr<FCurveTableEditorItem> NewItem = MakeShared<FCurveTableEditorItem>(NewCurveUnique, FCurveTableEditorHandle(Table, NewCurveUnique), AvailableColumns);
		OnColumnsChanged.AddSP(NewItem.ToSharedRef(), &FCurveTableEditorItem::CacheKeys);
		TreeItem->SetStrongItem(NewItem);


	}

	return FReply::Handled();
}

FReply FCurveTableEditor::OnAddNewKeyColumn()
{
	UCurveTable* Table = Cast<UCurveTable>(GetEditingObject());
	check(Table != nullptr);

	if (!Table->HasRichCurves())
	{
		// Compute a new keytime based on the last columns 
		float NewKeyTime = 1.0;
		if (AvailableColumns.Num() > 1)
		{
			float LastKeyTime = AvailableColumns[AvailableColumns.Num() - 1]->KeyTime;
			float PrevKeyTime = AvailableColumns[AvailableColumns.Num() - 2]->KeyTime;
			NewKeyTime = 2.*LastKeyTime - PrevKeyTime;
		}
		else if (AvailableColumns.Num() > 0)
		{
			float LastKeyTime = AvailableColumns[AvailableColumns.Num() - 1]->KeyTime;
			NewKeyTime = LastKeyTime + 1;
		}

		AddNewKeyColumn(NewKeyTime);
	}

	return FReply::Handled();
}

void FCurveTableEditor::AddNewKeyColumn(float NewKeyTime)
{
	UCurveTable* Table = Cast<UCurveTable>(GetEditingObject());
	check(Table != nullptr);

	if (!Table->HasRichCurves())
	{
		// Make sure we don't already have a key at this time

		// 1. Add new keys to every curve
		for (const TPair<FName, FRealCurve*>& CurveRow : Table->GetRowMap())
		{
			FRealCurve* Curve = CurveRow.Value;
			Curve->UpdateOrAddKey(NewKeyTime, Curve->Eval(NewKeyTime));
		}

		// 2. Add Column to our Table
		FCurveTableEditorColumnHeaderDataPtr ColumnData = MakeShareable(new FCurveTableEditorColumnHeaderData());
		const FText ColumnText = FText::AsNumber(NewKeyTime);
		ColumnData->ColumnId = *ColumnText.ToString();
		ColumnData->DisplayName = ColumnText;
		// ColumnData->DesiredColumnWidth = FontMeasure->Measure(ColumnData->DisplayName, CellTextStyle.Font).X + CellPadding;
		ColumnData->KeyTime = NewKeyTime;

		AvailableColumns.Add(ColumnData);

		// 3. Let the CurveTreeItems know they need to recache
		OnColumnsChanged.Broadcast();

		// Add the column to the TableView Header Row
		ColumnNamesHeaderRow->AddColumn(
			SHeaderRow::Column(ColumnData->ColumnId)
			.DefaultLabel(ColumnData->DisplayName)
			.FixedWidth(ColumnData->DesiredColumnWidth + 50)
			.HAlignHeader(HAlign_Center)
		);	
	}
}

#undef LOCTEXT_NAMESPACE
