// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithImporterModule.h"

#include "ActorFactoryDatasmithScene.h"
#include "DatasmithAssetImportData.h"
#include "DatasmithAssetUserData.h"
#include "DatasmithConsumer.h"
#include "DatasmithContentEditorModule.h"
#include "DatasmithContentEditorStyle.h"
#include "DatasmithCustomAction.h"
#include "DatasmithFileProducer.h"
#include "DatasmithImportFactory.h"
#include "DatasmithImporterEditorSettings.h"
#include "DatasmithImporterHelper.h"
#include "DatasmithScene.h"
#include "DatasmithStaticMeshImporter.h"
#include "DatasmithUtils.h"
#include "ObjectTemplates/DatasmithObjectTemplate.h"
#include "ObjectTemplates/DatasmithStaticMeshTemplate.h"
#include "UI/DatasmithUIManager.h"

#include "AssetToolsModule.h"
#include "ContentBrowserDelegates.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "DataprepAssetInterface.h"
#include "DataprepAssetUserData.h"
#include "DataprepCoreUtils.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorStyleSet.h"
#include "Engine/StaticMesh.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IAssetTools.h"
#include "LevelEditor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "ToolMenu.h"
#include "ToolMenuDelegates.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DatasmithImporter"

/**
 * DatasmithImporter module implementation (private)
 */
class FDatasmithImporterModule : public IDatasmithImporterModule
{
public:
	virtual void StartupModule() override
	{
		UDatasmithFileProducer::LoadDefaultSettings();

		// Disable any UI feature if running in command mode
		if (!IsRunningCommandlet())
		{
			FDatasmithUIManager::Initialize();

			SetupMenuEntry();
			SetupContentBrowserContextMenuExtender();
			SetupLevelEditorContextMenuExtender();

			IDatasmithContentEditorModule& DatasmithContentEditorModule = FModuleManager::LoadModuleChecked< IDatasmithContentEditorModule >( TEXT("DatasmithContentEditor") );
			FOnSpawnDatasmithSceneActors SpawnSceneActorsDelegate = FOnSpawnDatasmithSceneActors::CreateStatic( UActorFactoryDatasmithScene::SpawnRelatedActors );
			SpawnSceneActorsDelegateHandle = SpawnSceneActorsDelegate.GetHandle();

			DatasmithContentEditorModule.RegisterSpawnDatasmithSceneActorsHandler( SpawnSceneActorsDelegate );

			// Register the details customizer
			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( TEXT("PropertyEditor") );
			PropertyModule.RegisterCustomClassLayout( TEXT("DatasmithFileProducer"), FOnGetDetailCustomizationInstance::CreateStatic( &FDatasmithFileProducerDetails::MakeDetails ) );
			PropertyModule.RegisterCustomClassLayout( TEXT("DatasmithDirProducer"), FOnGetDetailCustomizationInstance::CreateStatic( &FDatasmithDirProducerDetails::MakeDetails ) );

			AddDataprepMenuEntryForDatasmithSceneAsset();
		}
	}

	virtual void ShutdownModule() override
	{
		// Disable any UI feature if running in command mode
		if (!IsRunningCommandlet())
		{
			RemoveDataprepMenuEntryForDatasmithSceneAsset();

			if ( SpawnSceneActorsDelegateHandle.IsValid() && FModuleManager::Get().IsModuleLoaded( TEXT("DatasmithContentEditor") ) )
			{
				IDatasmithContentEditorModule& DatasmithContentEditorModule = FModuleManager::GetModuleChecked< IDatasmithContentEditorModule >( TEXT("DatasmithContentEditor") );
				DatasmithContentEditorModule.UnregisterDatasmithImporter(this);
			}

			RemoveLevelEditorContextMenuExtender();
			RemoveContentBrowserContextMenuExtender();

			FDatasmithUIManager::Shutdown();

			// Register the details customizer
			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( TEXT("PropertyEditor") );
			PropertyModule.UnregisterCustomClassLayout( TEXT("DatasmithFileProducer") );
		}
	}

	virtual void ResetOverrides( UObject* Object ) override
	{
		ResetFromTemplates( Object );
	}

	bool IsInOfflineMode() const
	{
		return GetDefault< UDatasmithImporterEditorSettings >() && GetDefault< UDatasmithImporterEditorSettings >()->bOfflineImporter;
	}

private:
	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
	static TSharedRef<FExtender> OnExtendLevelEditorActorSelectionMenu(const TSharedRef<FUICommandList> CommandList, TArray<AActor*> SelectedActors);

	static void PopulateDatasmithActionsMenu( FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets );
	static void PopulateDatasmithActorsMenu( FMenuBuilder& MenuBuilder, TArray< AActor*> SelectedActors );
	static void ExecuteReimportDatasmithMaterials(TArray<FAssetData> AssetData);

	static void DiffAgainstTemplates( UObject* Outer );
	static void ResetFromTemplates( UObject* Outer );

	static void DiffAssetAgainstTemplate( TArray< FAssetData > SelectedAssets );
	static void ResetAssetFromTemplate( TArray< FAssetData > SelectedAssets );

	static void DiffActorAgainstTemplate( TArray< AActor* > SelectedActors );
	static void ResetActorFromTemplate( TArray< AActor* > SelectedActors );

	static void ApplyCustomActionOnAssets(TArray< FAssetData > SelectedAssets, IDatasmithCustomAction* Action);

	void SetupMenuEntry();
	void OnClickedMenuEntry();

	// Add the menu entry for a datasmith asset generated from a dataprep asset
	void AddDataprepMenuEntryForDatasmithSceneAsset();
	void RemoveDataprepMenuEntryForDatasmithSceneAsset();

	void SetupContentBrowserContextMenuExtender();
	void RemoveContentBrowserContextMenuExtender();

	void SetupLevelEditorContextMenuExtender();
	void RemoveLevelEditorContextMenuExtender();

	static TSharedPtr<IDataprepImporterInterface> CreateDatasmithImportHandler();

	FDelegateHandle ContentBrowserExtenderDelegateHandle;
	FDelegateHandle LevelEditorExtenderDelegateHandle;
	FDelegateHandle SpawnSceneActorsDelegateHandle;
	FDelegateHandle CreateDatasmithImportHandlerDelegateHandle;
};

void FDatasmithImporterModule::SetupMenuEntry()
{
	if (!IsRunningCommandlet())
	{
		FDatasmithUIManager::Get().AddMenuEntry(
			TEXT("Import"),
			LOCTEXT("DatasmithImport", "Datasmith"),
			LOCTEXT("DatasmithImportTooltip", "Import Unreal Datasmith file"),
			TEXT("DatasmithImporter/Content/Icons/DatasmithImporterIcon40"),
			FExecuteAction::CreateRaw(this, &FDatasmithImporterModule::OnClickedMenuEntry),
			UDatasmithImportFactory::StaticClass()
		);
	}
}

void FDatasmithImporterModule::OnClickedMenuEntry()
{
	if (!IsRunningCommandlet())
	{
		FDatasmithImporterHelper::Import<UDatasmithImportFactory>();
	}
}

void FDatasmithImporterModule::AddDataprepMenuEntryForDatasmithSceneAsset()
{
	if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.DatasmithScene")))
	{

		FNewToolMenuDelegate DataprepSectionConstructor;
		DataprepSectionConstructor.BindLambda( [](UToolMenu* ToolMenu)
			{
				if ( ToolMenu )
				{
					TArray<TStrongObjectPtr<UDataprepAssetInterface>> DataprepAssetInterfacesPtr;
					if (UContentBrowserAssetContextMenuContext* ContentBrowserMenuContext = ToolMenu->FindContext<UContentBrowserAssetContextMenuContext>())
					{
						if (ContentBrowserMenuContext->CommonClass == UDatasmithScene::StaticClass())
						{
							TArray<UObject*> SelectedObjects = ContentBrowserMenuContext->GetSelectedObjects();
							DataprepAssetInterfacesPtr.Reserve( SelectedObjects.Num() );
							for (UObject* SelectedObject : SelectedObjects)
							{
								if (UDatasmithScene* SelectedDatasmithScene = Cast<UDatasmithScene>(SelectedObject))
								{
									if (UDataprepAssetUserData* DataprepAssetUserData = SelectedDatasmithScene->GetAssetUserData<UDataprepAssetUserData>())
									{
										if (UDataprepAssetInterface* DataprepAsset = DataprepAssetUserData->DataprepAssetPtr.LoadSynchronous())
										{
											if (UDatasmithConsumer* DatasmithConsumer = Cast<UDatasmithConsumer>(DataprepAsset->GetConsumer()))
											{
												if (DatasmithConsumer->DatasmithScene.LoadSynchronous() == SelectedDatasmithScene)
												{
													// A Dataprep asset was found and it will regenerate this scene
													DataprepAssetInterfacesPtr.Emplace( DataprepAsset );
													continue;
												}
											}
										}
									}
								}

								// Couldn't find the dataprep asset for this scene
								return;
							}
						}
					}
					else
					{
						return;
					}


					FToolUIAction UIAction;
					UIAction.ExecuteAction.BindLambda( [DataprepAssetInterfacesPtr](const FToolMenuContext&)
						{
							for ( const TStrongObjectPtr<UDataprepAssetInterface>& DataprepAssetInterfacePtr : DataprepAssetInterfacesPtr )
							{
								FDataprepCoreUtils::ExecuteDataprep( DataprepAssetInterfacePtr.Get()
									, MakeShared<FDataprepCoreUtils::FDataprepLogger>()
									, MakeShared<FDataprepCoreUtils::FDataprepProgressUIReporter>() );
							}
						});

					FToolMenuInsert MenuInsert;
					MenuInsert.Position = EToolMenuInsertType::First;
					FToolMenuSection& Section = ToolMenu->AddSection( TEXT("Dataprep"), LOCTEXT("Dataprep", "Dataprep"), MenuInsert );

					Section.AddMenuEntry(
						TEXT("UpdateDataprepGeneratedAsset"),
						LOCTEXT("UpdateDataprepGeneratedAsset", "Update Datasmith Scene(s)"),
						LOCTEXT("UpdateDataprepGeneratedAssetTooltip", "Update the asset(s) by executing the Dataprep asset(s) that created it."),
						FSlateIcon(),
						UIAction
					);

				}
			});


		FToolMenuSection& Section = Menu->AddDynamicSection(TEXT("Dataprep"), DataprepSectionConstructor);
	}
}

void FDatasmithImporterModule::RemoveDataprepMenuEntryForDatasmithSceneAsset()
{
	if (UToolMenus* Singlethon = UToolMenus::Get())
	{
		if (UToolMenu* Menu = Singlethon->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.DatasmithScene")))
		{
			Menu->RemoveSection(TEXT("Dataprep"));
		}
	}
}

void FDatasmithImporterModule::SetupContentBrowserContextMenuExtender()
{
	if ( !IsRunningCommandlet() )
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>( "ContentBrowser" );
		TArray< FContentBrowserMenuExtender_SelectedAssets >& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();

		CBMenuExtenderDelegates.Add( FContentBrowserMenuExtender_SelectedAssets::CreateStatic( &FDatasmithImporterModule::OnExtendContentBrowserAssetSelectionMenu ) );
		ContentBrowserExtenderDelegateHandle = CBMenuExtenderDelegates.Last().GetHandle();
	}
}

void FDatasmithImporterModule::RemoveContentBrowserContextMenuExtender()
{
	if ( ContentBrowserExtenderDelegateHandle.IsValid() && FModuleManager::Get().IsModuleLoaded( "ContentBrowser" ) )
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>( "ContentBrowser" );
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
		CBMenuExtenderDelegates.RemoveAll(
			[ this ]( const FContentBrowserMenuExtender_SelectedAssets& Delegate )
			{
				return Delegate.GetHandle() == ContentBrowserExtenderDelegateHandle;
			}
		);
	}
}

void FDatasmithImporterModule::SetupLevelEditorContextMenuExtender()
{
	if ( !IsRunningCommandlet() )
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked< FLevelEditorModule >( "LevelEditor" );
		TArray< FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors >& CBMenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();

		CBMenuExtenderDelegates.Add( FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic( &FDatasmithImporterModule::OnExtendLevelEditorActorSelectionMenu ) );
		LevelEditorExtenderDelegateHandle = CBMenuExtenderDelegates.Last().GetHandle();
	}
}

void FDatasmithImporterModule::RemoveLevelEditorContextMenuExtender()
{
	if ( ContentBrowserExtenderDelegateHandle.IsValid() && FModuleManager::Get().IsModuleLoaded( "LevelEditor" ) )
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked< FLevelEditorModule >( "LevelEditor" );
		TArray< FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors >& CBMenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
		CBMenuExtenderDelegates.RemoveAll(
			[ this ]( const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Delegate )
		{
			return Delegate.GetHandle() == LevelEditorExtenderDelegateHandle;
		}
		);
	}
}

TSharedRef<FExtender> FDatasmithImporterModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	// Run through the assets to determine if any meet our criteria
	bool bShouldExtendAssetActions = false;
	for ( const FAssetData& Asset : SelectedAssets )
	{
		if ( Asset.AssetClass == UMaterial::StaticClass()->GetFName() || Asset.AssetClass == UMaterialInstance::StaticClass()->GetFName() ||
			 Asset.AssetClass == UMaterialInstanceConstant::StaticClass()->GetFName() )
		{
			UMaterialInterface* MaterialInterface = Cast< UMaterialInterface >( Asset.GetAsset() ); // Need to load the asset at this point to figure out the type of the AssetImportData

			if ( ( MaterialInterface && MaterialInterface->AssetImportData && MaterialInterface->AssetImportData->IsA< UDatasmithAssetImportData >() ) ||
				 FDatasmithObjectTemplateUtils::HasObjectTemplates( MaterialInterface ) )
			{
				bShouldExtendAssetActions = true;
				break;
			}
		}
		else if ( Asset.AssetClass == UStaticMesh::StaticClass()->GetFName() )
		{
			UStaticMesh* StaticMesh = Cast< UStaticMesh >( Asset.GetAsset() ); // Need to load the asset at this point to figure out the type of the AssetImportData

			if ( StaticMesh && StaticMesh->AssetImportData && StaticMesh->AssetImportData->IsA< UDatasmithAssetImportData >() )
			{
				bShouldExtendAssetActions = true;
				break;
			}
		}
	}

	if ( bShouldExtendAssetActions )
	{
		// Add the Datasmith actions sub-menu extender
		Extender->AddMenuExtension("GetAssetActions", EExtensionHook::After, nullptr, FMenuExtensionDelegate::CreateLambda(
			[SelectedAssets](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.AddSubMenu(
					NSLOCTEXT("DatasmithActions", "ObjectContext_Datasmith", "Datasmith"),
					NSLOCTEXT("DatasmithActions", "ObjectContext_Datasmith", "Datasmith"),
					FNewMenuDelegate::CreateStatic( &FDatasmithImporterModule::PopulateDatasmithActionsMenu, SelectedAssets ),
					false,
					FSlateIcon());
			}));
	}

	return Extender;
}

TSharedRef<FExtender> FDatasmithImporterModule::OnExtendLevelEditorActorSelectionMenu( const TSharedRef<FUICommandList> CommandList, TArray<AActor*> SelectedActors )
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	bool bShouldExtendActorActions = false;

	for ( AActor* Actor : SelectedActors )
	{
		for ( UActorComponent* Component : Actor->GetComponents() )
		{
			if ( FDatasmithObjectTemplateUtils::HasObjectTemplates( Component ) )
			{
				bShouldExtendActorActions = true;
				break;
			}
		}

		if ( bShouldExtendActorActions )
		{
			break;
		}
	}

	if ( bShouldExtendActorActions )
	{
		// Add the Datasmith actions sub-menu extender
		Extender->AddMenuExtension("ActorControl", EExtensionHook::After, nullptr, FMenuExtensionDelegate::CreateLambda(
			[SelectedActors](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.AddSubMenu(
				NSLOCTEXT("DatasmithActions", "ObjectContext_Datasmith", "Datasmith"),
				NSLOCTEXT("DatasmithActions", "ObjectContext_Datasmith", "Datasmith"),
				FNewMenuDelegate::CreateStatic( &FDatasmithImporterModule::PopulateDatasmithActorsMenu, SelectedActors ),
				false,
				FSlateIcon());
		}));
	}

	return Extender;
}

void FDatasmithImporterModule::PopulateDatasmithActionsMenu( FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets )
{
	bool bCanResetOverrides = false;
	bool bCanReimportMaterial = false;

	for ( const FAssetData& Asset : SelectedAssets )
	{
		if ( Asset.AssetClass == UMaterial::StaticClass()->GetFName() || Asset.AssetClass == UMaterialInstance::StaticClass()->GetFName() ||
			 Asset.AssetClass == UMaterialInstanceConstant::StaticClass()->GetFName() )
		{
			bCanResetOverrides = true;

			UMaterialInterface* MaterialInterface = Cast< UMaterialInterface >( Asset.GetAsset() );
			bCanReimportMaterial = ( MaterialInterface && MaterialInterface->AssetImportData && MaterialInterface->AssetImportData->IsA< UDatasmithAssetImportData >() );
		}
		else if ( Asset.AssetClass == UStaticMesh::StaticClass()->GetFName() )
		{
			bCanResetOverrides = true;
		}
	}

	if ( bCanResetOverrides )
	{
		// Add the Datasmith diff sub-menu extender (disabled until we have a proper UI)
		/*MenuBuilder.AddMenuEntry(
			NSLOCTEXT("DatasmithActions", "ObjectContext_DiffDatasmith", "Show Overrides"),
			NSLOCTEXT("DatasmithActions", "ObjectContext_DiffDatasmithTooltip", "Displays which values are currently overriden"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Diff"),
			FUIAction( FExecuteAction::CreateStatic( &FDatasmithImporterModule::DiffAssetAgainstTemplate, SelectedAssets ), FCanExecuteAction() ));*/

		// Add the Datasmith reset sub-menu extender
		MenuBuilder.AddMenuEntry(
			NSLOCTEXT("DatasmithActions", "ObjectContext_ResetDatasmith", "Reset Overrides"),
			NSLOCTEXT("DatasmithActions", "ObjectContext_ResetDatasmithTooltip", "Resets overriden values with the values from Datasmith"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Refresh"),
			FUIAction( FExecuteAction::CreateStatic( &FDatasmithImporterModule::ResetAssetFromTemplate, SelectedAssets ), FCanExecuteAction() ));
	}

	if ( bCanReimportMaterial )
	{
		// Add the reimport Datasmith material sub-menu extender
		MenuBuilder.AddMenuEntry(
			NSLOCTEXT("AssetTypeActions_Material", "ObjectContext_ReimportDatasmithMaterial", "Reimport Material"),
			NSLOCTEXT("AssetTypeActions_Material", "ObjectContext_ReimportDatasmithMaterialTooltip", "Reimports a material using Datasmith"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
			FUIAction( FExecuteAction::CreateStatic( &FDatasmithImporterModule::ExecuteReimportDatasmithMaterials, SelectedAssets ), FCanExecuteAction() ));
	}

	// Add an entry for each applicable custom action
	FDatasmithCustomActionManager ActionsManager;
	for (UDatasmithCustomActionBase* Action : ActionsManager.GetApplicableActions(SelectedAssets))
	{
		if (ensure(Action))
		{
			MenuBuilder.AddMenuEntry(
				Action->GetLabel(),
				Action->GetTooltip(),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
				FUIAction( FExecuteAction::CreateLambda( [=](){ FDatasmithImporterModule::ApplyCustomActionOnAssets(SelectedAssets, Action);} ), FCanExecuteAction() )
			);
		}
	}
}

void FDatasmithImporterModule::PopulateDatasmithActorsMenu( FMenuBuilder& MenuBuilder, TArray< AActor*> SelectedActors )
{
	// Add the Datasmith diff sub-menu extender (disabled until we have a proper UI)
	/*MenuBuilder.AddMenuEntry(
		NSLOCTEXT("DatasmithActions", "ObjectContext_DiffDatasmith", "Show Overrides"),
		NSLOCTEXT("DatasmithActions", "ObjectContext_DiffDatasmithTooltip", "Displays which values are currently overriden"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Diff"),
		FUIAction( FExecuteAction::CreateStatic( &FDatasmithImporterModule::DiffActorAgainstTemplate, SelectedActors ), FCanExecuteAction() ));*/

	// Add the Datasmith reset sub-menu extender
	MenuBuilder.AddMenuEntry(
		NSLOCTEXT("DatasmithActions", "ObjectContext_ResetDatasmith", "Reset Overrides"),
		NSLOCTEXT("DatasmithActions", "ObjectContext_ResetDatasmithTooltip", "Resets overriden values with the values from Datasmith"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Refresh"),
		FUIAction( FExecuteAction::CreateStatic( &FDatasmithImporterModule::ResetActorFromTemplate, SelectedActors ), FCanExecuteAction() ));

	// Add an entry for each applicable custom action
	FDatasmithCustomActionManager ActionsManager;
	for (UDatasmithCustomActionBase* Action : ActionsManager.GetApplicableActions(SelectedActors))
	{
		if (ensure(Action))
		{
			MenuBuilder.AddMenuEntry(
				Action->GetLabel(),
				Action->GetTooltip(),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
				FUIAction( FExecuteAction::CreateLambda( [=]() { Action->ApplyOnActors(SelectedActors); } ) , FCanExecuteAction() )
			);
		}
	}
}

void FDatasmithImporterModule::ExecuteReimportDatasmithMaterials( TArray<FAssetData> SelectedAssets )
{
	if ( UDatasmithImportFactory* DatasmithImportFactory = UDatasmithImportFactory::StaticClass()->GetDefaultObject< UDatasmithImportFactory >() )
	{
		for ( const FAssetData& AssetData : SelectedAssets )
		{
			if ( AssetData.AssetClass == UMaterial::StaticClass()->GetFName()
			  || AssetData.AssetClass == UMaterialInstance::StaticClass()->GetFName()
			  || AssetData.AssetClass == UMaterialInstanceConstant::StaticClass()->GetFName() )
			{
				if ( UObject* AssetToReimport = AssetData.GetAsset() )
				{
					TArray<FString> OutFilenames;
					if (DatasmithImportFactory->CanReimport(AssetToReimport, OutFilenames))
					{
						DatasmithImportFactory->Reimport( AssetToReimport );
					}
				}
			}
		}
	}
}

void FDatasmithImporterModule::DiffAgainstTemplates( UObject* Outer )
{
	TMap< TSubclassOf< UDatasmithObjectTemplate >, UDatasmithObjectTemplate* >* ObjectTemplates = FDatasmithObjectTemplateUtils::FindOrCreateObjectTemplates( Outer );

	if ( !ObjectTemplates )
	{
		return;
	}

	for ( auto It = ObjectTemplates->CreateConstIterator(); It; ++It )
	{
		UDatasmithObjectTemplate* OldTemplate = FDatasmithObjectTemplateUtils::GetObjectTemplate( Outer, It->Key );

		UDatasmithObjectTemplate* NewTemplate = NewObject< UDatasmithObjectTemplate >( GetTransientPackage(), It->Key, NAME_None, RF_Transient );
		NewTemplate->Load( Outer );

		check( OldTemplate != nullptr );
		check( NewTemplate != nullptr );

		// Dump assets to temp text files
		FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");

		FString OldTextFilename = AssetToolsModule.Get().DumpAssetToTempFile( OldTemplate );
		FString NewTextFilename = AssetToolsModule.Get().DumpAssetToTempFile( NewTemplate );
		FString DiffCommand = GetDefault< UEditorLoadingSavingSettings >()->TextDiffToolPath.FilePath;

		AssetToolsModule.Get().CreateDiffProcess( DiffCommand, OldTextFilename, NewTextFilename );
	}
}

void FDatasmithImporterModule::ResetFromTemplates( UObject* Outer )
{
	if ( TMap< TSubclassOf< UDatasmithObjectTemplate >, UDatasmithObjectTemplate* >* ObjectTemplates = FDatasmithObjectTemplateUtils::FindOrCreateObjectTemplates( Outer ) )
	{
		for ( auto It = ObjectTemplates->CreateConstIterator(); It; ++It )
		{
			It->Value->Apply( Outer, true );
		}
	}
}

void FDatasmithImporterModule::DiffAssetAgainstTemplate( TArray< FAssetData > SelectedAssets )
{
	for ( const FAssetData& AssetData : SelectedAssets )
	{
		UStaticMesh* StaticMesh = Cast< UStaticMesh >( AssetData.GetAsset() );

		if ( !StaticMesh )
		{
			continue;
		}

		DiffAgainstTemplates( StaticMesh );
	}
}

void FDatasmithImporterModule::ResetAssetFromTemplate( TArray< FAssetData > SelectedAssets )
{
	for ( const FAssetData& AssetData : SelectedAssets )
	{
		UObject* Asset = AssetData.GetAsset();

		if ( !Asset )
		{
			continue;
		}

		Asset->PreEditChange( nullptr );
		ResetFromTemplates( Asset );
		Asset->PostEditChange();
	}
}

void FDatasmithImporterModule::DiffActorAgainstTemplate( TArray< AActor*> SelectedActors )
{
	for ( AActor* Actor : SelectedActors )
	{
		if ( !Actor )
		{
			continue;
		}

		for ( UActorComponent* Component : Actor->GetComponents() )
		{
			DiffAgainstTemplates( Component );
		}
	}
}

void FDatasmithImporterModule::ResetActorFromTemplate( TArray< AActor*> SelectedActors )
{
	for ( AActor* Actor : SelectedActors )
	{
		if ( !Actor )
		{
			continue;
		}

		Actor->UnregisterAllComponents( true );

		for ( UActorComponent* Component : Actor->GetComponents() )
		{
			ResetFromTemplates( Component );
		}

		Actor->RerunConstructionScripts();
		Actor->RegisterAllComponents();

		GEditor->RedrawAllViewports();
	}
}

void FDatasmithImporterModule::ApplyCustomActionOnAssets(TArray< FAssetData > SelectedAssets, IDatasmithCustomAction* Action)
{
	if (ensure(Action))
	{
		Action->ApplyOnAssets(SelectedAssets);
	}
}

TSharedPtr< IDataprepImporterInterface > FDatasmithImporterModule::CreateDatasmithImportHandler()
{
	return nullptr;
}

IMPLEMENT_MODULE(FDatasmithImporterModule, DatasmithImporter);

#undef LOCTEXT_NAMESPACE
