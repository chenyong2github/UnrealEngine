// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserTableEditor.h"

#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SComboButton.h"
#include "SAssetDropTarget.h"
#include "SClassViewer.h"
#include "SourceCodeNavigation.h"
#include "Chooser.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "ClassViewerFilter.h"
#include "IPropertyAccessEditor.h"
#include "LandscapeRender.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "ObjectChooser_Asset.h"
#include "ObjectChooserClassFilter.h"
#include "ObjectChooserWidgetFactories.h"
#include "GraphEditorSettings.h"
#include "IDetailCustomization.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "ChooserEditor"

namespace UE::ChooserEditor
{

const FName FChooserTableEditor::ToolkitFName( TEXT( "ChooserTableEditor" ) );
const FName FChooserTableEditor::PropertiesTabId( TEXT( "ChooserEditor_Properties" ) );
const FName FChooserTableEditor::TableTabId( TEXT( "ChooserEditor_Table" ) );

void FChooserTableEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_ChooserTableEditor", "Chooser Table Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner( PropertiesTabId, FOnSpawnTab::CreateSP(this, &FChooserTableEditor::SpawnPropertiesTab) )
		.SetDisplayName( LOCTEXT("PropertiesTab", "Details") )
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon("EditorStyle", "LevelEditor.Tabs.Details"));
	
	InTabManager->RegisterTabSpawner( TableTabId, FOnSpawnTab::CreateSP(this, &FChooserTableEditor::SpawnTableTab) )
		.SetDisplayName( LOCTEXT("TableTab", "Chooser Table") )
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon("EditorStyle", "LevelEditor.Tabs.Details"));
}

void FChooserTableEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner( TableTabId );
	InTabManager->UnregisterTabSpawner( PropertiesTabId );
}

const FName FChooserTableEditor::ChooserEditorAppIdentifier( TEXT( "ChooserEditorApp" ) );

FChooserTableEditor::~FChooserTableEditor()
{
	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
	
	DetailsView.Reset();
}


void FChooserTableEditor::InitEditor( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit, FGetDetailsViewObjects GetDetailsViewObjects )
{
	EditingObjects = ObjectsToEdit;
	FCoreUObjectDelegates::OnObjectsReplaced.AddSP(this, &FChooserTableEditor::OnObjectsReplaced);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>( "PropertyEditor" );
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NotifyHook = this;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditorModule.CreateDetailView( DetailsViewArgs );
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout( "Standalone_ChooserTableEditor_Layout_v1" )
	->AddArea
	(
		FTabManager::NewPrimaryArea() ->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewSplitter()
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.7)
				->AddTab( TableTabId, ETabState::OpenedTab )
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.3)
				->AddTab( PropertiesTabId, ETabState::OpenedTab )
			)
		)
	);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor( Mode, InitToolkitHost, FChooserTableEditor::ChooserEditorAppIdentifier, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectsToEdit );

	RegenerateMenusAndToolbars();

	SelectRootProperties();
}

FName FChooserTableEditor::GetToolkitFName() const
{
	return ToolkitFName;
}

FText FChooserTableEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Chooser Table Editor");
}

void FChooserTableEditor::PostUndo(bool bSuccess)
{
	UpdateTableColumns();
	UpdateTableRows();
}
	
void FChooserTableEditor::PostRedo(bool bSuccess)
{
	UpdateTableColumns();
	UpdateTableRows();
}

	
void FChooserTableEditor::NotifyPreChange(FProperty* PropertyAboutToChange)
{
}

void FChooserTableEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (PropertyThatChanged->GetNameCPP() == GET_MEMBER_NAME_STRING_CHECKED(UChooserTable, OutputObjectType))
	{
		// rebuild all result widgets
		UpdateTableRows();
	}
	if (PropertyThatChanged->GetNameCPP() == GET_MEMBER_NAME_STRING_CHECKED(UChooserTable, ContextObjectType))
	{
		// rebuild all column header widgets
		UpdateTableColumns();
	}
}

	
FText FChooserTableEditor::GetToolkitName() const
{
	const TArray<UObject*>& EditingObjs = GetEditingObjects();

	check( EditingObjs.Num() > 0 );

	FFormatNamedArguments Args;
	Args.Add( TEXT("ToolkitName"), GetBaseToolkitName() );

	if( EditingObjs.Num() == 1 )
	{
		const UObject* EditingObject = EditingObjs[ 0 ];
		return FText::FromString(EditingObject->GetName());
	}
	else
	{
		UClass* SharedBaseClass = nullptr;
		for( int32 x = 0; x < EditingObjs.Num(); ++x )
		{
			UObject* Obj = EditingObjs[ x ];
			check( Obj );

			UClass* ObjClass = Cast<UClass>(Obj);
			if (ObjClass == nullptr)
			{
				ObjClass = Obj->GetClass();
			}
			check( ObjClass );

			// Initialize with the class of the first object we encounter.
			if( SharedBaseClass == nullptr )
			{
				SharedBaseClass = ObjClass;
			}

			// If we've encountered an object that's not a subclass of the current best baseclass,
			// climb up a step in the class hierarchy.
			while( !ObjClass->IsChildOf( SharedBaseClass ) )
			{
				SharedBaseClass = SharedBaseClass->GetSuperClass();
			}
		}

		check(SharedBaseClass);

		Args.Add( TEXT("NumberOfObjects"), EditingObjs.Num() );
		Args.Add( TEXT("ClassName"), FText::FromString( SharedBaseClass->GetName() ) );
		return FText::Format( LOCTEXT("ToolkitTitle_EditingMultiple", "{NumberOfObjects} {ClassName} - {ToolkitName}"), Args );
	}
}

FText FChooserTableEditor::GetToolkitToolTipText() const
{
	const TArray<UObject*>& EditingObjs = GetEditingObjects();

	check( EditingObjs.Num() > 0 );

	FFormatNamedArguments Args;
	Args.Add( TEXT("ToolkitName"), GetBaseToolkitName() );

	if( EditingObjs.Num() == 1 )
	{
		const UObject* EditingObject = EditingObjs[ 0 ];
		return FAssetEditorToolkit::GetToolTipTextForObject(EditingObject);
	}
	else
	{
		UClass* SharedBaseClass = NULL;
		for( int32 x = 0; x < EditingObjs.Num(); ++x )
		{
			UObject* Obj = EditingObjs[ x ];
			check( Obj );

			UClass* ObjClass = Cast<UClass>(Obj);
			if (ObjClass == nullptr)
			{
				ObjClass = Obj->GetClass();
			}
			check( ObjClass );

			// Initialize with the class of the first object we encounter.
			if( SharedBaseClass == nullptr )
			{
				SharedBaseClass = ObjClass;
			}

			// If we've encountered an object that's not a subclass of the current best baseclass,
			// climb up a step in the class hierarchy.
			while( !ObjClass->IsChildOf( SharedBaseClass ) )
			{
				SharedBaseClass = SharedBaseClass->GetSuperClass();
			}
		}

		check(SharedBaseClass);

		Args.Add( TEXT("NumberOfObjects"), EditingObjs.Num() );
		Args.Add( TEXT("ClassName"), FText::FromString( SharedBaseClass->GetName() ) );
		return FText::Format( LOCTEXT("ToolkitTitle_EditingMultipleToolTip", "{NumberOfObjects} {ClassName} - {ToolkitName}"), Args );
	}
}

FLinearColor FChooserTableEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.5f, 0.0f, 0.0f, 0.5f );
}

void FChooserTableEditor::SetPropertyVisibilityDelegate(FIsPropertyVisible InVisibilityDelegate)
{
	DetailsView->SetIsPropertyVisibleDelegate(InVisibilityDelegate);
	DetailsView->ForceRefresh();
}

void FChooserTableEditor::SetPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled InPropertyEditingDelegate)
{
	DetailsView->SetIsPropertyEditingEnabledDelegate(InPropertyEditingDelegate);
	DetailsView->ForceRefresh();
}

TSharedRef<SDockTab> FChooserTableEditor::SpawnPropertiesTab( const FSpawnTabArgs& Args )
{
	check( Args.GetTabId() == PropertiesTabId );

	return SNew(SDockTab)
		.Label( LOCTEXT("GenericDetailsTitle", "Details") )
		.TabColorScale( GetTabColorScale() )
		.OnCanCloseTab_Lambda([]() { return false; })
		[
			DetailsView.ToSharedRef()
		];
}


class FChooserRowDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FWidgetTemplateDragDropOp, FDecoratedDragDropOp)

	FChooserTableEditor* ChooserEditor;
	uint32 RowIndex;

	/** Constructs the drag drop operation */
	static TSharedRef<FChooserRowDragDropOp> New(FChooserTableEditor* InEditor, uint32 InRowIndex)
	{
		TSharedRef<FChooserRowDragDropOp> Operation = MakeShareable(new FChooserRowDragDropOp());
		Operation->ChooserEditor = InEditor;
		Operation->RowIndex = InRowIndex;
		Operation->DefaultHoverText = LOCTEXT("Chooser Row", "Chooser Row");
		FObjectChooserWidgetFactories::ConvertToText(InEditor->GetChooser()->Results[InRowIndex].GetObject(), Operation->DefaultHoverText);
		Operation->CurrentHoverText = Operation->DefaultHoverText;
			
		Operation->Construct();
	
		return Operation;
	};
};

class SChooserRowHandle : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChooserRowHandle)
	{}
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_ARGUMENT(FChooserTableEditor*, ChooserEditor)
	SLATE_ARGUMENT(uint32, RowIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChooserEditor = InArgs._ChooserEditor;
		RowIndex = InArgs._RowIndex;

		ChildSlot
		[
			SNew(SBox) .Padding(0.0f) .HAlign(HAlign_Center) .VAlign(VAlign_Center) .WidthOverride(16.0f)
			[
				SNew(SImage)
				.Image(FCoreStyle::Get().GetBrush("VerticalBoxDragIndicatorShort"))
			]
		];
	}

	FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	};

	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		TSharedRef<FChooserRowDragDropOp> DragDropOp = FChooserRowDragDropOp::New(ChooserEditor, RowIndex);
		return FReply::Handled().BeginDragDrop(DragDropOp);
	}

private:
	FChooserTableEditor* ChooserEditor = nullptr;
	uint32 RowIndex;
};


class SChooserTableRow : public SMultiColumnTableRow<TSharedPtr<FChooserTableEditor::FChooserTableRow>>
{
public:
	SLATE_BEGIN_ARGS(SChooserTableRow) {}
		/** The list item for this row */
		SLATE_ARGUMENT(TSharedPtr<FChooserTableEditor::FChooserTableRow>, Entry)
		SLATE_ARGUMENT(UChooserTable*, Chooser)
		SLATE_ARGUMENT(FChooserTableEditor*, Editor)
	SLATE_END_ARGS()


	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		RowIndex = Args._Entry;
		Chooser = Args._Chooser;
		Editor = Args._Editor;

		SMultiColumnTableRow<TSharedPtr<FChooserTableEditor::FChooserTableRow>>::Construct(
			FSuperRowType::FArguments(),
			OwnerTableView
		);
	}

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the list view. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		static FName Result = "Result";
		static FName Handles = "Handles";
		
		if (RowIndex->RowIndex < Chooser->Results.Num())
		{
			if (ColumnName == Handles && RowIndex->RowIndex < Chooser->Results.Num())
			{
				// row drag handle
				
				return SNew(SChooserRowHandle).ChooserEditor(Editor).RowIndex(RowIndex->RowIndex);
			}
			else if (ColumnName == Result) 
			{
				UObject* RowValue = Chooser->Results[RowIndex->RowIndex].GetObject();
				TSharedPtr<SWidget> ResultWidget = FObjectChooserWidgetFactories::CreateWidget(UObjectChooser::StaticClass(), RowValue,Chooser->ContextObjectType,
				FOnClassPicked::CreateLambda([this, RowIndex=RowIndex->RowIndex](UClass* ChosenClass)
				{
					const FScopedTransaction Transaction(LOCTEXT("Change Row Result Type", "Change Row Result Type"));
					UObject* RowValue = NewObject<UObject>(Chooser, ChosenClass, NAME_None, RF_Transactional);
					Chooser->Modify(true);
					Chooser->Results[RowIndex] = RowValue;
					FObjectChooserWidgetFactories::CreateWidget(UObjectChooser::StaticClass(), RowValue, Chooser->ContextObjectType, FOnClassPicked(), &CacheBorder);
				}),
				&CacheBorder
				);
				return ResultWidget.ToSharedRef();
			}
			else
			{
				const int ColumnIndex = ColumnName.GetNumber() - 1;
				if (ColumnIndex < Chooser->Columns.Num() && ColumnIndex >=0)
				{
					TScriptInterface<IChooserColumn>& Column = Chooser->Columns[ColumnIndex];
					TSharedPtr<SWidget> ColumnWidget;
					UClass* ColumnClass = Column.GetObject()->GetClass();
					while (ColumnClass && !ColumnWidget.IsValid())
					{
						if (auto Creator = FChooserTableEditor::ColumnWidgetCreators.Find(ColumnClass))
						{
							ColumnWidget = (*Creator)(Column.GetObject(), RowIndex->RowIndex);
							break;
						}
						ColumnClass = ColumnClass->GetSuperClass();
					}
					
					if (ColumnWidget.IsValid())
					{
						return ColumnWidget.ToSharedRef();
					}
				}
			}
		}
		else if (RowIndex->RowIndex == Chooser->Results.Num())
        {
			// on the row past the end, show an Add button in the result column
			if (ColumnName == Result)
			{
				return Editor->GetCreateRowComboButton().ToSharedRef();
			}
		}
		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FChooserTableEditor::FChooserTableRow> RowIndex;
	UChooserTable* Chooser;
	FChooserTableEditor* Editor;
	TSharedPtr<SBorder> CacheBorder;
};


TSharedRef<ITableRow> FChooserTableEditor::GenerateTableRow(TSharedPtr<FChooserTableRow> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
	
	return SNew(SChooserTableRow, OwnerTable)
		.Entry(InItem).Chooser(Chooser).Editor(this);
}

FReply FChooserTableEditor::SelectRootProperties()
{
	if( DetailsView.IsValid() )
	{
		// Make sure details window is pointing to our object
		DetailsView->SetObjects( EditingObjects );
	}

	return FReply::Handled();
}


void FChooserTableEditor::UpdateTableColumns()
{
	UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
	
	HeaderRow->ClearColumns();
	
	HeaderRow->AddColumn(SHeaderRow::Column("Handles")
					.ManualWidth(30)
					.HeaderContent()
					[					
						SNew(SButton).OnClicked_Raw(this, &FChooserTableEditor::SelectRootProperties)
					]);
	
	HeaderRow->AddColumn(SHeaderRow::Column("Result")
					.DefaultLabel(LOCTEXT("ResultColumnName", "Result"))
					.ManualWidth(300));

	FName ColumnId("ChooserColumn", 1);
	int NumColumns = Chooser->Columns.Num();	
	for(int ColumnIndex = 0; ColumnIndex < NumColumns; ColumnIndex++)
	{
		TScriptInterface<IChooserColumn>& Column = Chooser->Columns[ColumnIndex];

		HeaderRow->AddColumn(SHeaderRow::FColumn::FArguments()
			.ColumnId(ColumnId)
			.ManualWidth(200)
			.OnGetMenuContent_Lambda([this, &Column, Chooser, ColumnIndex, ColumnId]()
			{
				UChooserColumnMenuContext* MenuContext = NewObject<UChooserColumnMenuContext>();
				MenuContext->Editor = this;
                MenuContext->Chooser = Chooser;
                MenuContext->ColumnIndex = ColumnIndex;

				FMenuBuilder MenuBuilder(true,nullptr);

				MenuBuilder.AddMenuEntry(LOCTEXT("Column Properties","Properties"),LOCTEXT("Delete Column ToolTip", "Remove this column and all its data from the table"),FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this,ColumnIndex, ColumnId, &Column]()
						{
							UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
							DetailsView->SetObject(Chooser->Columns[ColumnIndex].GetObject());
							SelectedColumn = ColumnId;
						})
						)
					);

				MenuBuilder.AddMenuEntry(LOCTEXT("Delete Column","Delete"),LOCTEXT("Delete Column ToolTip", "Remove this column and all its data from the table"),FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this,ColumnIndex, &Column]()
						{
							const FScopedTransaction Transaction(LOCTEXT("Delete Column Transaction", "Delete Column"));
							UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
							Chooser->Modify(true);
							Chooser->Columns.RemoveAt(ColumnIndex);
							UpdateTableColumns();
						})
						));
				
					FClassViewerInitializationOptions Options;
					Options.ClassFilters.Add(MakeShared<FInterfaceClassFilter>(Column->GetInputValueInterface()));

				MenuBuilder.AddSubMenu(LOCTEXT("Input Type", "Input Type"),
					LOCTEXT("InputTypeToolTip", "Change input parameter type"),
					FNewMenuDelegate::CreateLambda([this, &Column](FMenuBuilder& Builder)
					{
						FClassViewerInitializationOptions Options;
						Options.ClassFilters.Add(MakeShared<FInterfaceClassFilter>(Column->GetInputValueInterface()));
						
						// Add class filter for columns here
						TSharedRef<SWidget> Widget = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(Options, FOnClassPicked::CreateLambda([this, &Column](UClass* ChosenClass)
						{
							const FScopedTransaction Transaction(LOCTEXT("SetColumnInputType", "Set Column Input Type"));
							UObject* Value = NewObject<UObject>(Column.GetObject(), ChosenClass, NAME_None, RF_Transactional);
							Column.GetObject()->Modify(true);
							Column->SetInputValue(Value);
							UpdateTableColumns();
							UpdateTableRows();
						}));
						
						Builder.AddWidget(Widget, FText());
					}));
				
				return MenuBuilder.MakeWidget();
				
			})
			.HeaderContent()
			[
				SNew(SBorder)
				.VAlign(VAlign_Center)
				.Padding(3)
				.BorderBackgroundColor_Lambda([this, ColumnId] ()
				{
					// unclear why this color is coming out much darker
					return SelectedColumn == ColumnId ? FSlateColor(FColor(0x00, 0x70, 0xe0, 0xFF)) : FSlateColor(FLinearColor(0.05f,0.05f,0.05f));
				})
				.OnMouseButtonDown_Lambda([this, ColumnIndex, ColumnId](	const FGeometry&, const FPointerEvent& PointerEvent)
				{
					UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
					TableView->ClearSelection();
					
					if (PointerEvent.GetEffectingButton() == EKeys::LeftMouseButton )
					{
						DetailsView->SetObject(Chooser->Columns[ColumnIndex].GetObject());
						SelectedColumn = ColumnId;
						return FReply::Handled();
					}
					else 
					{
						SelectedColumn = ColumnId;
						
						return FReply::Handled();
					}
				})
				[
					FObjectChooserWidgetFactories::CreateWidget(Chooser->Columns[ColumnIndex]->GetInputValue(), Chooser->ContextObjectType).ToSharedRef()
				]
				
			]);
		
		ColumnId.SetNumber(ColumnId.GetNumber() + 1);
	}

	HeaderRow->AddColumn( SHeaderRow::FColumn::FArguments()
		.ColumnId("Add")
		.FillWidth(1.0)
		.HeaderContent( )
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().MaxWidth(120)
			[
				CreateColumnComboButton.ToSharedRef()
			]
		]
		);

}

TSharedRef<SDockTab> FChooserTableEditor::SpawnTableTab( const FSpawnTabArgs& Args )
{
	check( Args.GetTabId() == TableTabId );

	UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
	
	// + button to create new columns
	
	CreateColumnComboButton = SNew(SComboButton).OnGetMenuContent_Lambda([this]()
	{
		FClassViewerInitializationOptions Options;
		Options.ClassFilters.Add(MakeShared<FInterfaceClassFilter>(UChooserColumn::StaticClass()));
		
		// Add class filter for columns here
		TSharedRef<SWidget> Widget = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(Options, FOnClassPicked::CreateLambda([this](UClass* ChosenClass)
		{
			CreateColumnComboButton->SetIsOpen(false);
			UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
			const FScopedTransaction Transaction(LOCTEXT("Add Column Transaction", "Add Column"));
			Chooser->Modify(true);
			UObject* ColumnObject = NewObject<UObject>(Chooser, ChosenClass, NAME_None, RF_Transactional);
			Chooser->Columns.Add(TScriptInterface<IChooserColumn>(ColumnObject));
			UpdateTableColumns();
			UpdateTableRows();
			SelectedColumn = "ChooserColumn";
			SelectedColumn.SetNumber(Chooser->Columns.Num());
			DetailsView->SetObject(ColumnObject);
		}));
		return Widget;
	})
	.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
	.ButtonContent()
	[
		SNew(STextBlock).Text(LOCTEXT("AddColumn", "+ Add Column"))
	];


	CreateRowComboButton = SNew(SComboButton)
		.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
		.ButtonContent()
		[
			SNew(STextBlock).Text(LOCTEXT("AddRow", "+ Add Row"))
		]
		.OnGetMenuContent_Lambda([this]()
		{
			FClassViewerInitializationOptions Options;
			Options.ClassFilters.Add(MakeShared<FInterfaceClassFilter>(UObjectChooser::StaticClass()) );
			
			// Add class filter for columns here
			TSharedRef<SWidget> Widget = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(Options,
				FOnClassPicked::CreateLambda([this](UClass* ChosenClass)
				{
					CreateRowComboButton->SetIsOpen(false);
					UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
					const FScopedTransaction Transaction(LOCTEXT("Add Row Transaction", "Add Row"));
					Chooser->Modify(true);
					UObject* ResultObject = NewObject<UObject>(Chooser, ChosenClass, NAME_None, RF_Transactional);
					Chooser->Results.Add(TScriptInterface<IObjectChooser>(ResultObject));
					UpdateTableRows();
				}));
			return Widget;
		});

	HeaderRow = SNew(SHeaderRow);

	UpdateTableRows();
	UpdateTableColumns();

	TableView = SNew(SListView<TSharedPtr<FChooserTableRow>>)
    			.ListItemsSource(&TableRows)
				.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Event)
				{
					if (Event.GetKey() == EKeys::Delete)
					{
						const FScopedTransaction Transaction(LOCTEXT("Delete Row Transaction", "Delete Row"));
						UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
						Chooser->Modify(true);
						// delete selected rows.
						TArray<uint32> RowsToDelete;
						for(auto& SelectedRow:SelectedRows)
						{
							RowsToDelete.Add(SelectedRow->Row);
						}
						// sort indices in reverse
						RowsToDelete.Sort([](int32 A, int32 B){ return A>B; });
						for(uint32 RowIndex : RowsToDelete)
						{
							Chooser->Results.RemoveAt(RowIndex);
						}

						for(auto& Column:Chooser->Columns)
						{
							Column.GetInterface()->DeleteRows(RowsToDelete);
						}

						UpdateTableRows();
						
						return FReply::Handled();
					}
					return FReply::Unhandled();
				}
				)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FChooserTableRow> SelectedItem,  ESelectInfo::Type SelectInfo)
				{
					SelectedColumn = "None";
					if (SelectedItem)
					{
						SelectedRows.SetNum(0);
						UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
						// Get the list of objects to edit the details of
						TObjectPtr<UChooserRowDetails> Selection = NewObject<UChooserRowDetails>();
						Selection->Chooser = Chooser;
						Selection->Row = SelectedItem->RowIndex;
						Selection->SetFlags(RF_Transactional); // for undo?
						SelectedRows.Add(Selection);
											
						TArray<UObject*> DetailsObjects;
						for(auto& Item : SelectedRows)
						{
							DetailsObjects.Add(Item.Get());
						}

						if( DetailsView.IsValid() )
						{
							// Make sure details window is pointing to our object
							DetailsView->SetObjects( DetailsObjects );
						}
					}
				})
    			.OnGenerateRow_Raw(this, &FChooserTableEditor::GenerateTableRow)
				.HeaderRow(HeaderRow);
	
	return SNew(SDockTab)
		.Label( LOCTEXT("ChooserTableTitle", "Chooser Table") )
		.TabColorScale( GetTabColorScale() )
		.OnCanCloseTab_Lambda([]() { return false; })
		[
			TableView.ToSharedRef()
		];
}

void FChooserTableEditor::UpdateTableRows()
{
	UChooserTable* Chooser = Cast<UChooserTable>(EditingObjects[0]);
	int32 OldNum = TableRows.Num();
	int32 NewNum = Chooser->Results.Num();

	// Sync the TableRows array which drives the ui table to match the number of results.
	// Add 1 at the end, for the "Add Row" control
	TableRows.SetNum(NewNum + 1);
	
	for(int32 i = OldNum; i < NewNum+1; i++)
	{
		TableRows[i] = MakeShared<FChooserTableRow>(i);
	}

	// Make sure each column has the same number of row datas as there are results
	for(auto& Column : Chooser->Columns)
	{
		if (IChooserColumn* ColumnInterface = Column.GetInterface())
		{
			ColumnInterface->SetNumRows(NewNum);
		}
	}

	if (TableView.IsValid())
	{
		TableView->RebuildList();
	}
}

void FChooserTableEditor::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	bool bChangedAny = false;

	// Refresh our details view if one of the objects replaced was in the map. This gets called before the reinstance GC fixup, so we might as well fixup EditingObjects now too
	for (int32 i = 0; i < EditingObjects.Num(); i++)
	{
		UObject* SourceObject = EditingObjects[i];
		UObject* ReplacedObject = ReplacementMap.FindRef(SourceObject);

		if (ReplacedObject && ReplacedObject != SourceObject)
		{
			EditingObjects[i] = ReplacedObject;
			bChangedAny = true;
		}
	}

	if (bChangedAny)
	{
		DetailsView->SetObjects(EditingObjects);
	}
}

FString FChooserTableEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Chooser Table Asset ").ToString();
}

TSharedRef<FChooserTableEditor> FChooserTableEditor::CreateEditor( const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UObject* ObjectToEdit, FGetDetailsViewObjects GetDetailsViewObjects )
{
	TSharedRef< FChooserTableEditor > NewEditor( new FChooserTableEditor() );

	TArray<UObject*> ObjectsToEdit;
	ObjectsToEdit.Add( ObjectToEdit );
	NewEditor->InitEditor( Mode, InitToolkitHost, ObjectsToEdit, GetDetailsViewObjects );

	return NewEditor;
}

TSharedRef<FChooserTableEditor> FChooserTableEditor::CreateEditor( const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit, FGetDetailsViewObjects GetDetailsViewObjects )
{
	TSharedRef< FChooserTableEditor > NewEditor( new FChooserTableEditor() );
	NewEditor->InitEditor( Mode, InitToolkitHost, ObjectsToEdit, GetDetailsViewObjects );
	return NewEditor;
}

void FChooserTableEditor::SelectColumn(int Index)
{
	UChooserTable* Chooser = GetChooser();
	if (Index < Chooser->Columns.Num())
	{
		SelectedColumn = "Column";
		SelectedColumn.SetNumber(Index);
		DetailsView->SetObject(Chooser->Columns[Index].GetObject());
	}
}
	
void FChooserTableEditor::DeleteColumn(int Index)
{
	UChooserTable* Chooser = GetChooser();
	if (Index < Chooser->Columns.Num())
	{
		Chooser->Columns.RemoveAt(Index);
		UpdateTableColumns();
	}
}

/// Result widgets

TMap<const UClass*, TFunction<TSharedRef<SWidget> (UObject* Column, int Row)>> FChooserTableEditor::ColumnWidgetCreators;

TSharedRef<SWidget> CreateAssetWidget(UObject* Object, UClass* ContextClass)
{
	UObjectChooser_Asset* DIAsset = Cast<UObjectChooser_Asset>(Object);

	UObject* Asset = DIAsset->Asset;

	UChooserTable* Chooser = Object->GetTypedOuter<UChooserTable>();
	
	return SNew(SObjectPropertyEntryBox)
		.AllowedClass((Chooser!=nullptr && Chooser->OutputObjectType!=nullptr) ? Chooser->OutputObjectType.Get() : UObject::StaticClass())
		.ObjectPath_Lambda([DIAsset](){ return DIAsset->Asset ? DIAsset->Asset.GetPath() : "";})
		.OnObjectChanged_Lambda([DIAsset](const FAssetData& AssetData)
		{
			const FScopedTransaction Transaction(LOCTEXT("Edit Asset", "Edit Asset"));
			DIAsset->Modify(true);
			DIAsset->Asset = AssetData.GetAsset();
		});
}

TSharedRef<SWidget> CreateEvaluateChooserWidget(UObject* Object, UClass* ContextObject)
{
	UObjectChooser_EvaluateChooser* EvaluateChooser = Cast<UObjectChooser_EvaluateChooser>(Object);
	
	return SNew(SObjectPropertyEntryBox)
		.AllowedClass(UChooserTable::StaticClass())
		.ObjectPath_Lambda([EvaluateChooser](){ return EvaluateChooser->Chooser ? EvaluateChooser->Chooser.GetPath() : "";})
		.OnObjectChanged_Lambda([EvaluateChooser](const FAssetData& AssetData){ EvaluateChooser->Chooser = Cast<UChooserTable>(AssetData.GetAsset()); });
}

void ConvertToText_Asset(const UObject* Object, FText& OutText)
{
	const UObjectChooser_Asset* AssetInterface = Cast<UObjectChooser_Asset>(Object);
	if (AssetInterface->Asset == nullptr)
	{
		OutText = LOCTEXT("NoChooser", "[No Chooser]");
	}
	else
	{
		OutText = FText::FromString(AssetInterface->Asset->GetName());
	}
}

void ConvertToText_EvaluateChooser(const UObject* Object, FText& OutText)
{
	const UObjectChooser_EvaluateChooser* EvaluateChooser = Cast<UObjectChooser_EvaluateChooser>(Object);
	if (EvaluateChooser->Chooser == nullptr)
	{
		OutText = LOCTEXT("NoChooser", "[No Chooser]");
	}
	else
	{
		OutText = FText::FromString(EvaluateChooser->Chooser->GetName());
	}
}

TSharedRef<SWidget> CreateObjectWidget(UObject* Object, UClass* ContextClass)
{
	FText ObjectText = FText::FromString(Object->GetName());
	FObjectChooserWidgetFactories::ConvertToText(Object, ObjectText);
	
	return SNew (STextBlock).Text(ObjectText); // could make this use Text_Lambda, to have it update correctly?
}


class FChooserRowDetails : public IDetailCustomization
{
public:
	FChooserRowDetails() {};
	virtual ~FChooserRowDetails() override {};

	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable( new FChooserRowDetails() );
	}

	// IDetailCustomization interface
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
};

// Make the details panel show the values for the selected row, showing each column value
void FChooserRowDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	UChooserRowDetails* Row = Cast<UChooserRowDetails>(Objects[0]);
	UChooserTable* Chooser = Row->Chooser;
	
	if (Chooser->Results.Num() > Row->Row)
	{
		IDetailCategoryBuilder& PropertiesCategory = DetailBuilder.EditCategory("Row Properties");

		TSharedPtr<IPropertyHandle> ChooserProperty = DetailBuilder.GetProperty("Chooser", Row->StaticClass());
		DetailBuilder.HideProperty(ChooserProperty);
	
		TSharedPtr<IPropertyHandle> ResultsArrayProperty = ChooserProperty->GetChildHandle("Results");
		TSharedPtr<IPropertyHandle> CurrentResultProperty = ResultsArrayProperty->AsArray()->GetElement(Row->Row);
		IDetailPropertyRow& NewResultProperty = PropertiesCategory.AddProperty(CurrentResultProperty);
		NewResultProperty.DisplayName(LOCTEXT("ResultColumnName","Result"));
		NewResultProperty.ShowPropertyButtons(false); // hide array add button
		NewResultProperty.ShouldAutoExpand(true);
	
		for(int ColumnIndex=0; ColumnIndex<Chooser->Columns.Num(); ColumnIndex++)
		{
			TScriptInterface<IChooserColumn>& Column = Chooser->Columns[ColumnIndex];
			UObject* ColumnObject = Chooser->Columns[ColumnIndex].GetObject();
			
			TSharedPtr<IPropertyHandle> ColumnDataProperty =  DetailBuilder.AddObjectPropertyData({ColumnObject}, "RowValues");
			uint32 NumElements = 0;
			ColumnDataProperty->AsArray()->GetNumElements(NumElements);
			if (Row->Row < (int)NumElements)
			{
				TSharedRef<IPropertyHandle> CellData = ColumnDataProperty->AsArray()->GetElement(Row->Row);
	
				IDetailPropertyRow& NewColumnProperty = PropertiesCategory.AddProperty(CellData);
				FText DisplayName = LOCTEXT("No Input Value", "No Input Value");
				if (UObject* InputValue = Column.GetInterface()->GetInputValue())
				{
					FObjectChooserWidgetFactories::ConvertToText(InputValue, DisplayName);
				}
				NewColumnProperty.DisplayName(DisplayName);
				NewColumnProperty.ShowPropertyButtons(false); // hide array add button
				NewColumnProperty.ShouldAutoExpand(true);
			}
		}
	}
}

void FChooserTableEditor::RegisterWidgets()
{
	FObjectChooserWidgetFactories::ChooserTextConverter.Add(UObjectChooser_Asset::StaticClass(), ConvertToText_Asset);
	FObjectChooserWidgetFactories::ChooserTextConverter.Add(UObjectChooser_EvaluateChooser::StaticClass(), ConvertToText_EvaluateChooser);
	
	FObjectChooserWidgetFactories::ChooserWidgetCreators.Add(UObject::StaticClass(), CreateObjectWidget);
	FObjectChooserWidgetFactories::ChooserWidgetCreators.Add(UObjectChooser_Asset::StaticClass(), CreateAssetWidget);
	FObjectChooserWidgetFactories::ChooserWidgetCreators.Add(UObjectChooser_EvaluateChooser::StaticClass(), CreateEvaluateChooserWidget);
	

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("ChooserRowDetails", FOnGetDetailCustomizationInstance::CreateStatic(&FChooserRowDetails::MakeInstance));	
}
}

#undef LOCTEXT_NAMESPACE
