// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDataprepPalette.h"

// Dataprep includes
#include "DataprepOperation.h"
#include "SchemaActions/DataprepAllMenuActionCollector.h"
#include "SchemaActions/DataprepDragDropOp.h"
#include "SchemaActions/DataprepFilterMenuActionCollector.h"
#include "SchemaActions/DataprepOperationMenuActionCollector.h"
#include "SchemaActions/DataprepSchemaAction.h"
#include "SchemaActions/IDataprepMenuActionCollector.h"

// Engine includes
#include "AssetDiscoveryIndicator.h"
#include "AssetRegistryModule.h"
#include "EdGraph/EdGraphSchema.h"
#include "EditorStyleSet.h"
#include "EditorWidgetsModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWidget.h"
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

	this->ChildSlot
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
						.OnActionDragged( this, &SDataprepPalette::OnActionDragged )
						.OnCreateCustomRowExpander( this, &SDataprepPalette::OnCreateCustomRowExpander )
						.OnCreateWidgetForAction( this, &SDataprepPalette::OnCreateWidgetForAction )
						.OnCollectAllActions( this, &SDataprepPalette::CollectAllActions )
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
		];

	// Register with the Asset Registry to be informed when it is done loading up files and when a file changed (Added/Removed/Renamed).
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked< FAssetRegistryModule >( TEXT("AssetRegistry") );
	AssetRegistryModule.Get().OnFilesLoaded().AddSP( this, &SDataprepPalette::RefreshActionsList, true );
	AssetRegistryModule.Get().OnAssetAdded().AddSP( this, &SDataprepPalette::AddAssetFromAssetRegistry );
	AssetRegistryModule.Get().OnAssetRemoved().AddSP( this, &SDataprepPalette::RemoveAssetFromRegistry );
	AssetRegistryModule.Get().OnAssetRenamed().AddSP( this, &SDataprepPalette::RenameAssetFromRegistry );
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
