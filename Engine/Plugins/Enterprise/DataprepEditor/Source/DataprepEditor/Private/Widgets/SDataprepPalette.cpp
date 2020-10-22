// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDataprepPalette.h"

// Dataprep includes
#include "DataprepOperation.h"
#include "DataprepEditorUtils.h"
#include "SchemaActions/DataprepAllMenuActionCollector.h"
#include "SchemaActions/DataprepDragDropOp.h"
#include "SchemaActions/DataprepFilterMenuActionCollector.h"
#include "SchemaActions/DataprepOperationMenuActionCollector.h"
#include "SchemaActions/DataprepSchemaAction.h"
#include "SchemaActions/IDataprepMenuActionCollector.h"

// Engine includes
#include "AssetDiscoveryIndicator.h"
#include "AssetRegistryModule.h"
#include "Brushes/SlateColorBrush.h"
#include "EditorFontGlyphs.h"
#include "EdGraph/EdGraphSchema.h"
#include "EditorStyleSet.h"
#include "EditorWidgetsModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Views/SExpanderArrow.h"

#define LOCTEXT_NAMESPACE "SDataprepPalette"

void SDataprepPalette::Construct(const FArguments& InArgs)
{
	// Create the asset discovery indicator
	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::LoadModuleChecked< FEditorWidgetsModule >( "EditorWidgets" );
	TSharedRef< SWidget > AssetDiscoveryIndicator = EditorWidgetsModule.CreateAssetDiscoveryIndicator( EAssetDiscoveryIndicatorScaleMode::Scale_Vertical );

	// Setting the categories text and string
	AllCategory = LOCTEXT("All Category", "All");
	SelectorsCategory = FDataprepFilterMenuActionCollector::FilterCategory;
	OperationsCategory = FDataprepOperationMenuActionCollector::OperationCategory;

	SAssignNew(FilterBox, SSearchBox)
		.OnTextChanged(this, &SDataprepPalette::OnFilterTextChanged);

	this->ChildSlot
	[
		SNew(SVerticalBox)

		// Path and history
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding( 0, 0, 0, 0 )
		[
			SNew( SWrapBox )
			.UseAllottedSize( true )
			.InnerSlotPadding( FVector2D( 5, 2 ) )

			+ SWrapBox::Slot()
			.FillLineWhenSizeLessThan( 600 )
			.FillEmptySpace( true )
			[
				SNew( SHorizontalBox )

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew( SBorder )
					.Padding( FMargin( 3 ) )
					.BorderImage( FEditorStyle::GetBrush( "ContentBrowser.TopBar.GroupBorder" ) )
					[
						SNew( SHorizontalBox )

						// Add New
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign( VAlign_Center )
						.HAlign( HAlign_Left )
						[
							SNew( SComboButton )
							.ComboButtonStyle( FEditorStyle::Get(), "ToolbarComboButton" )
							.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
							.ForegroundColor(FLinearColor::White)
							.ContentPadding(FMargin(6, 2))
							.OnGetMenuContent_Lambda( [this]{ return ConstructAddActionMenu(); } )
							.HasDownArrow(false)
							.ButtonContent()
							[
								SNew( SHorizontalBox )

								// New Icon
								+ SHorizontalBox::Slot()
								.VAlign(VAlign_Center)
								.AutoWidth()
								[
									SNew(STextBlock)
									.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
									.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
									.Text(FEditorFontGlyphs::File)
								]

								// New Text
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(4, 0, 0, 0)
								[
									SNew( STextBlock )
									.TextStyle( FEditorStyle::Get(), "ContentBrowser.TopBar.Font" )
									.Text( LOCTEXT( "AddNewButton", "Add New" ) )
								]

								// Down Arrow
								+ SHorizontalBox::Slot()
								.VAlign(VAlign_Center)
								.AutoWidth()
								.Padding(4, 0, 0, 0)
								[
									SNew(STextBlock)
									.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
									.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
									.Text(FEditorFontGlyphs::Caret_Down)
								]
							]
						]
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(2, 0, 2, 0)
				[
					FilterBox.ToSharedRef()
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0,0,0,0)
		[
			SNew(SBox)
			.HeightOverride(2.0f)
			[
				SNew(SImage)
				.Image(new FSlateColorBrush(FLinearColor( FColor( 34, 34, 34) ) ) )
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0,2,0,0)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
			[
				SNew(SVerticalBox)
				// Content list
				+ SVerticalBox::Slot()
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SAssignNew(GraphActionMenu, SGraphActionMenu)
						.OnGetFilterText(this, &SDataprepPalette::GetFilterText)
						.OnActionDragged( this, &SDataprepPalette::OnActionDragged )
						.OnCreateCustomRowExpander( this, &SDataprepPalette::OnCreateCustomRowExpander )
						.OnCreateWidgetForAction( this, &SDataprepPalette::OnCreateWidgetForAction )
						.OnCollectAllActions( this, &SDataprepPalette::CollectAllActions )
						.OnContextMenuOpening(this, &SDataprepPalette::OnContextMenuOpening)
						.AutoExpandActionMenu( true )
					]
			
					+ SOverlay::Slot()
					.HAlign( HAlign_Fill )
					.VAlign( VAlign_Bottom )
					.Padding(FMargin(24, 0, 24, 0))
					[
						// Asset discovery indicator
						AssetDiscoveryIndicator
					]
				]
			]
		]

	];

	// Register with the Asset Registry to be informed when it is done loading up files and when a file changed (Added/Removed/Renamed).
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked< FAssetRegistryModule >( TEXT("AssetRegistry") );
	AssetRegistryModule.Get().OnFilesLoaded().AddSP( this, &SDataprepPalette::RefreshActionsList, true );
	AssetRegistryModule.Get().OnAssetAdded().AddSP( this, &SDataprepPalette::AddAssetFromAssetRegistry );
	AssetRegistryModule.Get().OnAssetRemoved().AddSP( this, &SDataprepPalette::RemoveAssetFromRegistry );
	AssetRegistryModule.Get().OnAssetRenamed().AddSP( this, &SDataprepPalette::RenameAssetFromRegistry );
}

FText SDataprepPalette::GetFilterText() const
{
	return FilterBox->GetText();
}

void SDataprepPalette::OnFilterTextChanged(const FText& InFilterText)
{
	GraphActionMenu->GenerateFilteredItems(false);
}

TSharedRef<SWidget> SDataprepPalette::ConstructAddActionMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bCloseSelfOnly=*/true);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("DataprepPaletteLabel", "Dataprep Palette"));
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("CreateNewFilterLabel", "Create New Filter"), LOCTEXT("CreateNewFilterTooltip", "Create new user-defined filter"), FSlateIcon(), 
			FUIAction(FExecuteAction::CreateLambda([this]()
			{
				if (FDataprepEditorUtils::CreateUserDefinedFilter())
				{
					RefreshActionsList(true);
				}
			}))
		);
		MenuBuilder.AddMenuEntry(LOCTEXT("CreateNewOperatorLabel", "Create New Operator"), LOCTEXT("CreateNewOperatorTooltip", "Create new user-defined operator"), FSlateIcon(), 
			FUIAction(FExecuteAction::CreateLambda([this]()
			{
				if (FDataprepEditorUtils::CreateUserDefinedOperation())
				{
					RefreshActionsList(true);
				}
			}))
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedPtr<SWidget> SDataprepPalette::OnContextMenuOpening()
{
	TArray<TSharedPtr<FEdGraphSchemaAction> > SelectedActions;
	GraphActionMenu->GetSelectedActions( SelectedActions );
	if ( SelectedActions.Num() != 1 )
	{
		return TSharedPtr<SWidget>();
	}

	TSharedPtr<FDataprepSchemaAction> DataprepSchemaAction = StaticCastSharedPtr<FDataprepSchemaAction>( SelectedActions[0] );
	
	if ( !DataprepSchemaAction || DataprepSchemaAction->GeneratedClassObjectPath.IsEmpty() )
	{
		return TSharedPtr<SWidget>();
	}

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder( bShouldCloseWindowAfterMenuSelection, nullptr );

	MenuBuilder.BeginSection("BasicOperations");
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenInBP", "Open in Blueprint Editor"),
			FText(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([ObjectPath = DataprepSchemaAction->GeneratedClassObjectPath]()
			{
				if ( UObject* Obj = StaticLoadObject( UObject::StaticClass(), nullptr, *ObjectPath ) )
				{
					FKismetEditorUtilities::BringKismetToFocusAttentionOnObject( Obj );
				}
			}))
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SDataprepPalette::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	FDataprepAllMenuActionCollector ActionCollector;
	for ( TSharedPtr< FDataprepSchemaAction > Action : ActionCollector.CollectActions() )
	{
		OutAllActions.AddAction(StaticCastSharedPtr< FEdGraphSchemaAction >(Action));
	}
}

FReply SDataprepPalette::OnActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent)
{
	if (InActions.Num() > 0 && InActions[0].IsValid() )
	{
		if ( InActions[0]->GetTypeId() == FDataprepSchemaAction::StaticGetTypeId() )
		{
			TSharedPtr<FDataprepSchemaAction> InAction = StaticCastSharedPtr<FDataprepSchemaAction>( InActions[0] );
			return FReply::Handled().BeginDragDrop( FDataprepDragDropOp::New( InAction.ToSharedRef() ) );
		}
	}

	return FReply::Unhandled();
}

TSharedRef<SExpanderArrow> SDataprepPalette::OnCreateCustomRowExpander(const FCustomExpanderData& InCustomExpanderData) const
{
	return SNew(SExpanderArrow, InCustomExpanderData.TableRow);
}

void SDataprepPalette::AddAssetFromAssetRegistry(const FAssetData& InAddedAssetData)
{
	RefreshAssetInRegistry( InAddedAssetData );
}

void SDataprepPalette::RemoveAssetFromRegistry(const FAssetData& InRemovedAssetData)
{
	RefreshAssetInRegistry( InRemovedAssetData );
}

void SDataprepPalette::RenameAssetFromRegistry(const FAssetData& InRenamedAssetData, const FString& InNewName)
{
	RefreshAssetInRegistry( InRenamedAssetData );
}

void SDataprepPalette::RefreshAssetInRegistry(const FAssetData& InAssetData)
{
	// Grab the asset generated class, it will be checked for being a dataprep operation
	FAssetDataTagMapSharedView::FFindTagResult GeneratedClassPathPtr = InAssetData.TagsAndValues.FindTag( TEXT("GeneratedClass") );
	if (GeneratedClassPathPtr.IsSet())
	{
		const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath( GeneratedClassPathPtr.GetValue() );
		const FString ClassName = FPackageName::ObjectPathToObjectName( ClassObjectPath );

		TArray<FName> OutAncestorClassNames;

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked< FAssetRegistryModule >( TEXT("AssetRegistry") );
		AssetRegistryModule.Get().GetAncestorClassNames( FName( *ClassName ) , OutAncestorClassNames );
		
		bool bIsTrackedClass = false;
		for ( FName Ancestor : OutAncestorClassNames )
		{
			if ( Ancestor == UDataprepOperation::StaticClass()->GetFName() )
			{
				bIsTrackedClass = true;
				break;
			}
		}

		if ( bIsTrackedClass )
		{
			RefreshActionsList( true );
		}
	}
}

#undef LOCTEXT_NAMESPACE
