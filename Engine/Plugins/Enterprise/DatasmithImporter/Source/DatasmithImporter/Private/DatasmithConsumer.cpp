// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithConsumer.h"

#include "DatasmithActorImporter.h"
#include "DatasmithAreaLightActor.h"
#include "DatasmithAssetImportData.h"
#include "DatasmithAssetUserData.h"
#include "DatasmithContentEditorModule.h"
#include "DatasmithContentBlueprintLibrary.h"
#include "DatasmithScene.h"
#include "DatasmithSceneActor.h"
#include "DatasmithSceneFactory.h"
#include "IDatasmithSceneElements.h"
#include "DatasmithStaticMeshImporter.h"
#include "DataprepAssetUserData.h"
#include "DataprepAssetInterface.h"
#include "LevelVariantSets.h"
#include "ObjectTemplates/DatasmithActorTemplate.h"
#include "ObjectTemplates/DatasmithAreaLightActorTemplate.h"
#include "ObjectTemplates/DatasmithCineCameraComponentTemplate.h"
#include "ObjectTemplates/DatasmithLightComponentTemplate.h"
#include "ObjectTemplates/DatasmithMaterialInstanceTemplate.h"
#include "ObjectTemplates/DatasmithPointLightComponentTemplate.h"
#include "ObjectTemplates/DatasmithObjectTemplate.h"
#include "ObjectTemplates/DatasmithSceneComponentTemplate.h"
#include "ObjectTemplates/DatasmithStaticMeshTemplate.h"
#include "ObjectTemplates/DatasmithSkyLightComponentTemplate.h"
#include "ObjectTemplates/DatasmithStaticMeshComponentTemplate.h"
#include "Utility/DatasmithImporterUtils.h"

#include "Algo/Count.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "ComponentReregisterContext.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Engine/Light.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Factories/WorldFactory.h"
#include "FileHelpers.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IAssetTools.h"
#include "Internationalization/Internationalization.h"
#include "LevelSequence.h"
#include "LevelUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Guid.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DatasmithConsumer"

const FText DatasmithConsumerLabel( LOCTEXT( "DatasmithConsumerLabel", "Datasmith writer" ) );
const FText DatasmithConsumerDescription( LOCTEXT( "DatasmithConsumerDesc", "Writes data prep world's current level and assets to current level" ) );

const TCHAR* DatasmithSceneSuffix = TEXT("_Scene");

namespace DatasmithConsumerUtils
{
	/** Helper to generate actor element from a scene actor */
	void ConvertSceneActorsToActors( FDatasmithImportContext& ImportContext );

	/** Helper to pre-build all static meshes from the array of assets passed to a consumer */
	void AddAssetsToContext( FDatasmithImportContext& ImportContext, TArray< TWeakObjectPtr< UObject > >& Assets );

	FString GetObjectUniqueId(UObject* Object)
	{
		UDatasmithContentBlueprintLibrary* DatasmithContentLibrary = Cast< UDatasmithContentBlueprintLibrary >( UDatasmithContentBlueprintLibrary::StaticClass()->GetDefaultObject() );
		FString DatasmithUniqueId = DatasmithContentLibrary->GetDatasmithUserDataValueForKey( Object, UDatasmithAssetUserData::UniqueIdMetaDataKey );

		return DatasmithUniqueId.Len() == 0 ? Object->GetName() : DatasmithUniqueId;
	}

	void SaveMap(UWorld* WorldToSave);

	const FString& GetMarker(UObject* Object, const FString& Name);

	void SetMarker(UObject* Object, const FString& Name, const FString& Value);

	void MoveActorsToLevel(const TArray<AActor*>& ActorsToMove, ULevel* DestLevel, TMap< FName, TSoftObjectPtr< AActor > >& ActorsMap, bool bDuplicate);

	template<class AssetClass>
	void SetMarker(const TMap<FName, TSoftObjectPtr< AssetClass >>& AssetMap, const FString& Name, const FString& Value)
	{
		for(const TPair<FName, TSoftObjectPtr< AssetClass >>& Entry : AssetMap)
		{
			if(UObject* Asset = Entry.Value.Get())
			{
				SetMarker(Asset, Name, Value);
			}
		}
	}

	template<class AssetClass>
	void CollectAssetsToSave(TMap<FName, TSoftObjectPtr< AssetClass >>& AssetMap, TArray<UPackage*>& OutPackages)
	{
		if(AssetMap.Num() > 0)
		{
			OutPackages.Reserve(OutPackages.Num() + AssetMap.Num());

			for(TPair<FName, TSoftObjectPtr< AssetClass >>& Entry : AssetMap)
			{
				if(UObject* Asset = Entry.Value.Get())
				{
					OutPackages.Add(Asset->GetOutermost());
				}
			}
		}
	}

	template<class AssetClass>
	void ApplyFolderDirective(TMap<FName, TSoftObjectPtr< AssetClass >>& AssetMap, const FString& RootPackagePath, TFunction<void(ELogVerbosity::Type, FText)> ReportCallback)
	{
		auto CanMoveAsset = [&ReportCallback](UObject* Source, UObject* Target) -> bool
		{
			// Overwrite existing owned asset with the new one
			if( GetMarker(Source, UDatasmithConsumer::ConsumerMarkerID) == GetMarker(Target, UDatasmithConsumer::ConsumerMarkerID))
			{
				TArray<UObject*> ObjectsToReplace(&Target, 1);
				ObjectTools::ForceReplaceReferences( Source, ObjectsToReplace );

				Target->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);

				return true;
			}

			if(Source->GetClass() != Target->GetClass())
			{
				const FText AssetName = FText::FromString( Source->GetName() );
				const FText AssetFolder = FText::FromString( FPaths::GetPath(Source->GetPathName()) );
				const FText Message = FText::Format( LOCTEXT( "FolderDirective_ClassIssue", "Cannot move {0} to {1}. An asset with same name but different class exists"), AssetName, AssetFolder );
				ReportCallback(ELogVerbosity::Error, Message);
			}
			else
			{
				const FText AssetName = FText::FromString( Source->GetName() );
				const FText AssetFolder = FText::FromString( FPaths::GetPath(Source->GetPathName()) );
				const FText Message = FText::Format( LOCTEXT( "FolderDirective_Overwrite", "Cannot move {0} to {1}. An asset with same name and same class exists"), AssetName, AssetFolder );
				ReportCallback(ELogVerbosity::Error, Message);
			}

			return false;
		};

		TArray<UPackage*> PackagesToCheck;
		TMap<FSoftObjectPath, FSoftObjectPath> AssetRedirectorMap;

		for(TPair<FName, TSoftObjectPtr< AssetClass >>& Entry : AssetMap)
		{
			if(UObject* Asset = Entry.Value.Get())
			{
				const FString& OutputFolder = GetMarker(Asset, UDataprepContentConsumer::RelativeOutput);
				if(OutputFolder.Len() > 0)
				{
					FString SourcePackagePath = Entry.Value->GetOuter()->GetPathName();
					FString TargetPackagePath = FPaths::Combine(RootPackagePath, OutputFolder, Asset->GetName());
					
					FString PackageFilename;
					FPackageName::TryConvertLongPackageNameToFilename( TargetPackagePath, PackageFilename, FPackageName::GetAssetPackageExtension() );

					if( SourcePackagePath != TargetPackagePath)
					{
						bool bCanMove = true;

						FString TargetAssetFullPath = TargetPackagePath + "." + Asset->GetName();
						if(UObject* MemoryObject = FSoftObjectPath(TargetAssetFullPath).ResolveObject())
						{
							bCanMove = CanMoveAsset( Asset, MemoryObject);
						}
						else if(FPaths::FileExists(PackageFilename))
						{
							bCanMove = CanMoveAsset( Asset, FSoftObjectPath(TargetAssetFullPath).TryLoad());
						}

						if(bCanMove)
						{
							FSoftObjectPath& SoftObjectPathRef = AssetRedirectorMap.Emplace( Asset );

							UPackage* Package = CreatePackage(nullptr, *TargetPackagePath);
							Package->FullyLoad();

							Asset->Rename(nullptr, Package, REN_DontCreateRedirectors | REN_NonTransactional);
							Entry.Value = Asset;

							SoftObjectPathRef = Asset;
							PackagesToCheck.Add(Package);
						}
					}
				}
			}
		}

		if(AssetRedirectorMap.Num() > 0)
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			AssetTools.RenameReferencingSoftObjectPaths(PackagesToCheck, AssetRedirectorMap);
		}
	}
}

const FString UDatasmithConsumer::ConsumerMarkerID = TEXT("DatasmithConsumer_UniqueID");

UDatasmithConsumer::UDatasmithConsumer()
	: DatasmithScene(nullptr)
	, WorkingWorld(nullptr)
	, PrimaryLevel(nullptr)
{
	if(!HasAnyFlags(RF_NeedLoad|RF_ClassDefaultObject))
	{
		UniqueID = FGuid::NewGuid().ToString(EGuidFormats::Short);
	}
}

void UDatasmithConsumer::PostLoad()
{
	UDataprepContentConsumer::PostLoad();

	// Update UniqueID for previous version of the consumer
	if(HasAnyFlags(RF_WasLoaded))
	{
		bool bMarkDirty = false;
		if(UniqueID.Len() == 0)
		{
			UniqueID = FGuid::NewGuid().ToString(EGuidFormats::Short);
			bMarkDirty = true;
		}

		if(LevelName.Len() == 0)
		{
			LevelName = GetOuter()->GetName() + TEXT("_Map");
		}

		if(OutputLevelSoftObject.GetAssetPathString().Len() == 0)
		{
			OutputLevelSoftObject = FSoftObjectPath(FPaths::Combine(TargetContentFolder, LevelName) + "." + LevelName);

			bMarkDirty = true;
		}

		if(bMarkDirty)
		{
			const FText AssetName = FText::FromString( GetOuter()->GetName() );
			const FText WarningMessage = FText::Format( LOCTEXT( "DataprepConsumerOldVersion", "{0} is from an old version and has been updated. Please save asset to complete update."), AssetName );
			const FText NotificationText = FText::Format( LOCTEXT( "DataprepConsumerOldVersionNotif", "{0} is from an old version and has been updated."), AssetName );
			LogWarning(WarningMessage);
			//DataprepCorePrivateUtils::LogMessage( EMessageSeverity::Warning, WarningMessage, NotificationText );

			GetOutermost()->SetDirtyFlag(true);
		}
	}
}

void UDatasmithConsumer::PostInitProperties()
{
	UDataprepContentConsumer::PostInitProperties();

	if(!HasAnyFlags(RF_NeedLoad|RF_ClassDefaultObject))
	{
		if(LevelName.Len() == 0)
		{
			LevelName = GetOuter()->GetName() + TEXT("_Map");
		}

		OutputLevelSoftObject = FSoftObjectPath(FPaths::Combine( TargetContentFolder, LevelName) + TEXT(".") + LevelName);
	}
}

bool UDatasmithConsumer::Initialize()
{
	FText TaskDescription = LOCTEXT( "DatasmithImportFactory_Initialize", "Preparing world ..." );
	ProgressTaskPtr = MakeUnique< FDataprepWorkReporter >( Context.ProgressReporterPtr, TaskDescription, 3.0f, 1.0f );

	ProgressTaskPtr->ReportNextStep( LOCTEXT( "DatasmithImportFactory_Initialize", "Preparing world ...") );

	if(!CheckOutputDirectives())
	{
		return false;
	}

	UpdateScene();

	UPackage* ParentPackage = CreatePackage( nullptr, *GetTargetPackagePath() );
	ParentPackage->FullyLoad();

	// Re-create the DatasmithScene if it is invalid
	if ( !DatasmithScene.IsValid() )
	{

		FString DatasmithSceneName = GetOuter()->GetName() + DatasmithSceneSuffix;

		UPackage* Package = CreatePackage( nullptr, *FPaths::Combine( ParentPackage->GetPathName(), DatasmithSceneName ) );
		Package->FullyLoad();

		if( UObject* ExistingObject = StaticFindObject( nullptr, Package, *DatasmithSceneName, true ) )
		{
			bool bDatasmithSceneFound = false;

			// Check to see if existing scene is not from same Dataprep asset
			if( UDatasmithScene* ExistingDatasmithScene = Cast<UDatasmithScene>( ExistingObject ) )
			{
				if ( ExistingDatasmithScene->GetClass()->ImplementsInterface( UInterface_AssetUserData::StaticClass() ) )
				{
					if ( IInterface_AssetUserData* AssetUserDataInterface = Cast< IInterface_AssetUserData >( ExistingDatasmithScene ) )
					{
						if( UDataprepAssetUserData* DataprepAssetUserData = AssetUserDataInterface->GetAssetUserData< UDataprepAssetUserData >() )
						{
							UDataprepAssetInterface* DataprepAssetInterface = Cast< UDataprepAssetInterface >( GetOuter() );
							check( DataprepAssetInterface );

							if( DataprepAssetUserData->DataprepAssetPtr == DataprepAssetInterface )
							{
								DatasmithScene.Reset();
								DatasmithScene = ExistingDatasmithScene;
								Package = nullptr;
								bDatasmithSceneFound = true;
							}
						}
					}
				}
			}

			if( !bDatasmithSceneFound )
			{
				DatasmithSceneName = MakeUniqueObjectName( ParentPackage, UDatasmithScene::StaticClass(), *DatasmithSceneName ).ToString();
				Package = CreatePackage( nullptr, *FPaths::Combine( ParentPackage->GetPathName(), DatasmithSceneName ) );
				Package->FullyLoad();
			}
		}

		if(Package != nullptr)
		{
			DatasmithScene = NewObject< UDatasmithScene >( Package, *DatasmithSceneName, GetFlags() | RF_Standalone | RF_Public | RF_Transactional );
		}
		check( DatasmithScene.IsValid() );

		DatasmithScene->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated( DatasmithScene.Get() );

		DatasmithScene->AssetImportData = NewObject< UDatasmithSceneImportData >( DatasmithScene.Get(), UDatasmithSceneImportData::StaticClass() );
		check( DatasmithScene->AssetImportData );

		// Store a Dataprep asset pointer into the scene asset in order to be able to later re-execute the dataprep pipeline
		if ( DatasmithScene->GetClass()->ImplementsInterface(UInterface_AssetUserData::StaticClass()) )
		{
			if ( IInterface_AssetUserData* AssetUserDataInterface = Cast< IInterface_AssetUserData >( DatasmithScene.Get() ) )
			{
				UDataprepAssetUserData* DataprepAssetUserData = AssetUserDataInterface->GetAssetUserData< UDataprepAssetUserData >();

				if ( !DataprepAssetUserData )
				{
					EObjectFlags Flags = RF_Public;
					DataprepAssetUserData = NewObject< UDataprepAssetUserData >( DatasmithScene.Get(), NAME_None, Flags );
					AssetUserDataInterface->AddAssetUserData( DataprepAssetUserData );
				}

				UDataprepAssetInterface* DataprepAssetInterface = Cast< UDataprepAssetInterface >( GetOuter() );
				check( DataprepAssetInterface );

				DataprepAssetUserData->DataprepAssetPtr = DataprepAssetInterface;
			}
		}
	}

	CreateWorld();

	if ( !BuildContexts() )
	{
		return false;
	}

	// Check if the finalize should be threated as a reimport
	TArray< ADatasmithSceneActor* > SceneActors = FDatasmithImporterUtils::FindSceneActors( ImportContextPtr->ActorsContext.FinalWorld, ImportContextPtr->SceneAsset);
	if (SceneActors.Num() > 0 )
	{
		ADatasmithSceneActor* FoundSceneActor = nullptr;
		for( ADatasmithSceneActor* SceneActor : SceneActors )
		{
			if( SceneActor && SceneActor->Scene == DatasmithScene )
			{
				FoundSceneActor = SceneActor;
				break;
			}
		}

		if( FoundSceneActor == nullptr )
		{
			//Create a new Datasmith scene actor in the targeted level
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Template = ImportContextPtr->ActorsContext.ImportSceneActor;
			ADatasmithSceneActor* DestinationSceneActor = Cast< ADatasmithSceneActor >(ImportContextPtr->ActorsContext.FinalWorld->SpawnActor< ADatasmithSceneActor >(SpawnParameters));

			// Name new destination ADatasmithSceneActor to the DatasmithScene's name
			DestinationSceneActor->SetActorLabel( ImportContextPtr->Scene->GetName() );
			DestinationSceneActor->MarkPackageDirty();
			DestinationSceneActor->RelatedActors.Reset();
		}

		ImportContextPtr->bIsAReimport = true;
		ImportContextPtr->Options->ReimportOptions.bRespawnDeletedActors = false;
		ImportContextPtr->Options->ReimportOptions.bUpdateActors = true;
		ImportContextPtr->Options->UpdateNotDisplayedConfig( true );
	}

	return true;
}

// Inspired from FDataprepDatasmithImporter::FinalizeAssets
bool UDatasmithConsumer::Run()
{
	// Pre-build static meshes
	ProgressTaskPtr->ReportNextStep( LOCTEXT( "DatasmithImportFactory_PreBuild", "Pre-building assets ...") );
	FDatasmithStaticMeshImporter::PreBuildStaticMeshes( *ImportContextPtr );

	// No need to have a valid set of assets.
	// All assets have been added to the AssetContext in UDatasmithConsumer::BuildContexts
	ProgressTaskPtr->ReportNextStep( LOCTEXT( "DatasmithImportFactory_Finalize", "Finalizing commit ...") );
	FDatasmithImporter::FinalizeImport( *ImportContextPtr, TSet<UObject*>() );

	// Apply UDataprepConsumerUserData directives for assets
	UDatasmithScene* SceneAsset = ImportContextPtr->SceneAsset;

	TFunction<void(ELogVerbosity::Type, FText)> ReportFunc = [&](ELogVerbosity::Type Verbosity, FText Message)
	{
		switch(Verbosity)
		{
			case ELogVerbosity::Warning:
			LogWarning(Message);
			break;

			case ELogVerbosity::Error:
			LogError(Message);
			break;

			default:
			LogInfo(Message);
			break;
		}
	};

	DatasmithConsumerUtils::SetMarker(SceneAsset->Textures, UDatasmithConsumer::ConsumerMarkerID, UniqueID);
	DatasmithConsumerUtils::ApplyFolderDirective(SceneAsset->Textures, TargetContentFolder, ReportFunc );

	DatasmithConsumerUtils::SetMarker(SceneAsset->StaticMeshes, UDatasmithConsumer::ConsumerMarkerID, UniqueID);
	DatasmithConsumerUtils::ApplyFolderDirective(SceneAsset->StaticMeshes, TargetContentFolder, ReportFunc );

	DatasmithConsumerUtils::SetMarker(SceneAsset->Materials, UDatasmithConsumer::ConsumerMarkerID, UniqueID);
	DatasmithConsumerUtils::ApplyFolderDirective(SceneAsset->Materials, TargetContentFolder, ReportFunc );

	DatasmithConsumerUtils::SetMarker(SceneAsset->MaterialFunctions, UDatasmithConsumer::ConsumerMarkerID, UniqueID);
	DatasmithConsumerUtils::ApplyFolderDirective(SceneAsset->MaterialFunctions, TargetContentFolder, ReportFunc );

	DatasmithConsumerUtils::SetMarker(SceneAsset->LevelSequences, UDatasmithConsumer::ConsumerMarkerID, UniqueID);
	DatasmithConsumerUtils::ApplyFolderDirective(SceneAsset->LevelSequences, TargetContentFolder, ReportFunc );

	DatasmithConsumerUtils::SetMarker(SceneAsset->LevelVariantSets, UDatasmithConsumer::ConsumerMarkerID, UniqueID);
	DatasmithConsumerUtils::ApplyFolderDirective(SceneAsset->LevelVariantSets, TargetContentFolder, ReportFunc );

	// Apply UDataprepConsumerUserData directives for actors
	ApplySubLevelDirective();

	return FinalizeRun();
}

bool UDatasmithConsumer::FinalizeRun()
{
	UDatasmithScene* SceneAsset = ImportContextPtr->SceneAsset;

	// Save all assets
	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(DatasmithScene->GetOutermost());

	DatasmithConsumerUtils::CollectAssetsToSave(SceneAsset->Textures, PackagesToSave);
	DatasmithConsumerUtils::CollectAssetsToSave(SceneAsset->MaterialFunctions, PackagesToSave);
	DatasmithConsumerUtils::CollectAssetsToSave(SceneAsset->Materials, PackagesToSave);
	DatasmithConsumerUtils::CollectAssetsToSave(SceneAsset->StaticMeshes, PackagesToSave);
	DatasmithConsumerUtils::CollectAssetsToSave(SceneAsset->LevelSequences, PackagesToSave);
	DatasmithConsumerUtils::CollectAssetsToSave(SceneAsset->LevelVariantSets, PackagesToSave);

	const bool bCheckDirty = false;
	const bool bPromptToSave = false;
	const FEditorFileUtils::EPromptReturnCode Return = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);

	// Save secondary levels
	const TArray<ULevel*>& Levels = WorkingWorld->GetLevels();

	for(ULevel* Level : Levels)
	{
		if(Level)
		{
			UWorld* World = Cast<UWorld>(Level->GetOuter());

			if(World != WorkingWorld.Get())
			{
				DatasmithConsumerUtils::SaveMap(World);
			}
		}
	}

	// Save primary level now
	WorkingWorld->PersistentLevel = PrimaryLevel;
	DatasmithConsumerUtils::SaveMap(WorkingWorld.Get());

	return true;
}


bool UDatasmithConsumer::CreateWorld()
{
	ensure(!WorkingWorld.IsValid());

	UWorld* World = Cast<UWorld>(OutputLevelSoftObject.TryLoad());
	if(World)
	{
		World->SetFlags(RF_Public | RF_Transactional |  RF_Standalone);

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(World->WorldType);
		WorldContext.SetCurrentWorld(World);

		// Load all the secondary levels of the world.
		World->LoadSecondaryLevels(true);

		// Check that all secondary levels have been added to world
		for(ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if(ULevel* SecondaryLevel = StreamingLevel->GetLoadedLevel())
			{
				if(!World->ContainsLevel(SecondaryLevel))
				{
					World->AddLevel(SecondaryLevel);
				}
			}
		}
	}
	else
	{
		UPackage* Package = CreatePackage(nullptr, *OutputLevelSoftObject.GetLongPackageName());
		Package->FullyLoad();
		Package->SetFlags(RF_Public);

		World = NewObject< UWorld >(Package, *OutputLevelSoftObject.GetAssetName(), RF_Public | RF_Transactional |  RF_Standalone);
		World->WorldType = EWorldType::Inactive;

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(World->WorldType);
		WorldContext.SetCurrentWorld(World);

		World->InitializeNewWorld(UWorld::InitializationValues()
			.AllowAudioPlayback(false)
			.CreateAISystem(false)
			.CreateNavigation(false)
			.CreatePhysicsScene(false)
			.RequiresHitProxies(false)
			.ShouldSimulatePhysics(false)
			.SetTransactional(false));
	}

	if(World == nullptr)
	{
		ensure(false);
		return false;
	}

	TArray<ULevel*> Levels = World->GetLevels();

	// Find level associated with this consumer
	PrimaryLevel = nullptr;
	for(ULevel* Level : Levels)
	{
		if(Level && Level->GetOuter()->GetName() == LevelName)
		{
			PrimaryLevel = Level;
			break;
		}
	}
	ensure(PrimaryLevel);

	DatasmithConsumerUtils::SetMarker(PrimaryLevel, ConsumerMarkerID, UniqueID);
	PrimaryLevel->bIsVisible = true;

	// If there is more than one level, move all actors of the world to the main level.
	// The call to FinalizeRun will take care of redistributing to the sub-levels if applicable
	if(Levels.Num() > 1)
	{
		// Get the ADatasmithSceneActor of the world if it exists
		TArray< ADatasmithSceneActor* > SceneActors = FDatasmithImporterUtils::FindSceneActors( World, DatasmithScene.Get());
		TMap< FName, TSoftObjectPtr< AActor > > EmptyRelatedActors;
		TMap< FName, TSoftObjectPtr< AActor > >& RelatedActors = SceneActors.Num() > 0 ? SceneActors[0]->RelatedActors : EmptyRelatedActors;

		for(ULevel* Level : Levels)
		{
			if(Level != PrimaryLevel)
			{
				Level->bIsVisible = true;

				// Collect actors to copy to primary level
				TArray<AActor*> ActorsToCopy;
				for(AActor* Actor : Level->Actors)
				{
					if(Actor && Actor->GetRootComponent() && !Actor->IsA<AWorldSettings>() && !Actor->IsA<APhysicsVolume>() && !Actor->IsA<ABrush>() )
					{
						ActorsToCopy.Add(Actor);
					}
				}

				DatasmithConsumerUtils::MoveActorsToLevel( ActorsToCopy, PrimaryLevel, RelatedActors, true);

				World->RemoveLevel(Level);

				// Make sure the world and package are properly discarded
				UPackage* LevelPackage = Level->GetOutermost();
				UWorld* LevelWorld = Cast<UWorld>(Level->GetOuter());

				// Move the world to the transient package
				LevelWorld->Rename(nullptr, GetTransientPackage(), REN_NonTransactional | REN_DontCreateRedirectors);

				// Empty the world from all its content
				LevelWorld->DestroyWorld( true );
				GEngine->DestroyWorldContext( LevelWorld );

				// Indicates world is good for garbage collect
				LevelWorld->ClearFlags(RF_Standalone | RF_Public | RF_Transactional);
				LevelWorld->SetFlags(RF_Transient);
				LevelWorld->MarkPendingKill();

				// Indicates level's package is good for garbage collect
				LevelPackage->SetDirtyFlag(false);
				LevelPackage->ClearFlags(RF_Standalone | RF_Public | RF_Transactional);
				LevelPackage->SetFlags(RF_Transient);
				LevelPackage->MarkPendingKill();
			}
		}

		World->ClearStreamingLevels();

		// Collect garbage to clear out the discarded world(s) and level(s) 
		CollectGarbage( GARBAGE_COLLECTION_KEEPFLAGS );
	}

	World->PersistentLevel = PrimaryLevel;
	World->SetCurrentLevel(PrimaryLevel);

	WorkingWorld = TStrongObjectPtr<UWorld>(World);

	return true;
}

void UDatasmithConsumer::ClearWorld()
{
	if(WorkingWorld.IsValid())
	{
		UWorld* WorldToDelete = WorkingWorld.Get();
		WorkingWorld.Reset();

		WorldToDelete->PersistentLevel = PrimaryLevel;
		WorldToDelete->SetCurrentLevel(PrimaryLevel);

		PrimaryLevel = nullptr;

		WorldToDelete->DestroyWorld( true );
		GEngine->DestroyWorldContext( WorldToDelete );

		// Collect garbage to clear out the destroyed level
		CollectGarbage( GARBAGE_COLLECTION_KEEPFLAGS );
	}
}

void UDatasmithConsumer::Reset()
{
	ImportContextPtr.Reset();
	ProgressTaskPtr.Reset();
	UDataprepContentConsumer::Reset();

	ClearWorld();
}

const FText& UDatasmithConsumer::GetLabel() const
{
	return DatasmithConsumerLabel;
}

const FText& UDatasmithConsumer::GetDescription() const
{
	return DatasmithConsumerDescription;
}

bool UDatasmithConsumer::BuildContexts()
{
	const FString FilePath = FPaths::Combine( FPaths::ProjectIntermediateDir(), ( DatasmithScene->GetName() + TEXT( ".udatasmith" ) ) );

	ImportContextPtr = MakeUnique< FDatasmithImportContext >( FilePath, false, TEXT("DatasmithImport"), LOCTEXT("DatasmithImportFactoryDescription", "Datasmith") );

	// Update import context with consumer's data
	ImportContextPtr->Options->BaseOptions.SceneHandling = EDatasmithImportScene::CurrentLevel;
	ImportContextPtr->SceneAsset = DatasmithScene.Get();
	ImportContextPtr->ActorsContext.ImportWorld = Context.WorldPtr.Get();
	ImportContextPtr->Scene = FDatasmithSceneFactory::CreateScene( *DatasmithScene->GetName() );
	ImportContextPtr->SceneName = ImportContextPtr->Scene->GetName();

	// Convert all incoming Datasmith scene actors as regular actors
	DatasmithConsumerUtils::ConvertSceneActorsToActors( *ImportContextPtr );

	// Recreate scene graph from actors in world
	ImportContextPtr->Scene->SetHost( TEXT( "DatasmithConsumer" ) );

	TArray<AActor*> RootActors;
	ImportContextPtr->ActorsContext.ImportSceneActor->GetAttachedActors( RootActors );
	FDatasmithImporterUtils::FillSceneElement( ImportContextPtr->Scene, RootActors );

	// Initialize context
	FString SceneOuterPath = DatasmithScene->GetOutermost()->GetName();
	FString RootPath = FPackageName::GetLongPackagePath( SceneOuterPath );

	if ( Algo::Count( RootPath, TEXT('/') ) > 1 )
	{
		// Remove the scene folder as it shouldn't be considered in the import path
		RootPath.Split( TEXT("/"), &RootPath, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd );
	}

	FPaths::NormalizeDirectoryName( RootPath );

	if ( !ImportContextPtr->Init( ImportContextPtr->Scene.ToSharedRef(), RootPath, RF_Public | RF_Standalone | RF_Transactional, GWarn, TSharedPtr< FJsonObject >(), true ) )
	{
		FText Message = LOCTEXT( "DatasmithConsumer_BuildContexts", "Initialization of consumer failed" );
		LogError( Message );
		return false;
	}

	// Set the feedback context
	ImportContextPtr->FeedbackContext = Context.ProgressReporterPtr ? Context.ProgressReporterPtr->GetFeedbackContext() : nullptr;

	// Update ImportContext's package data
	ImportContextPtr->AssetsContext.RootFolderPath = TargetContentFolder;
	ImportContextPtr->AssetsContext.TransientFolderPath = Context.TransientContentFolder;

	ImportContextPtr->AssetsContext.StaticMeshesFinalPackage.Reset();
	ImportContextPtr->AssetsContext.MaterialsFinalPackage.Reset();
	ImportContextPtr->AssetsContext.TexturesFinalPackage.Reset();
	ImportContextPtr->AssetsContext.LightPackage.Reset();
	ImportContextPtr->AssetsContext.LevelSequencesFinalPackage.Reset();
	ImportContextPtr->AssetsContext.LevelVariantSetsFinalPackage.Reset();

	ImportContextPtr->AssetsContext.StaticMeshesImportPackage.Reset();
	ImportContextPtr->AssetsContext.TexturesImportPackage.Reset();
	ImportContextPtr->AssetsContext.MaterialsImportPackage.Reset();
	ImportContextPtr->AssetsContext.MasterMaterialsImportPackage.Reset();
	ImportContextPtr->AssetsContext.MaterialFunctionsImportPackage.Reset();
	ImportContextPtr->AssetsContext.LevelSequencesImportPackage.Reset();
	ImportContextPtr->AssetsContext.LevelVariantSetsImportPackage.Reset();

	// Set the destination world as the one in the level editor
	ImportContextPtr->ActorsContext.FinalWorld = WorkingWorld.Get();

	// Initialize ActorsContext's UniqueNameProvider with actors in the GWorld not the Import world
	ImportContextPtr->ActorsContext.UniqueNameProvider = FDatasmithActorUniqueLabelProvider();
	ImportContextPtr->ActorsContext.UniqueNameProvider.PopulateLabelFrom( ImportContextPtr->ActorsContext.FinalWorld );

	// Add assets as if they have been imported using the current import context
	DatasmithConsumerUtils::AddAssetsToContext( *ImportContextPtr, Context.Assets );

	// Store IDatasmithScene(Element) in UDatasmithScene
	FDatasmithImporterUtils::SaveDatasmithScene( ImportContextPtr->Scene.ToSharedRef(), ImportContextPtr->SceneAsset );
	return true;
}

bool UDatasmithConsumer::SetLevelNameImplementation(const FString& InLevelName, FText& OutReason, const bool bIsAutomated)
{
	if(InLevelName.Len() == 0)
	{
		OutReason = LOCTEXT( "DatasmithConsumer_NameEmpty", "The level name is empty. Please enter a valid name." );
		return false;
	}

	if(!CanCreateLevel(TargetContentFolder, InLevelName, !bIsAutomated && !IsRunningCommandlet()))
	{
		return false;
	}

	if(SetOutputLevel(InLevelName))
	{
		Modify();

		LevelName = InLevelName;

		OnChanged.Broadcast();

		return true;
	}

	// Warn user new name has not been set
	OutReason = FText::Format( LOCTEXT("DatasmithConsumer_BadLevelName", "Cannot create level named {0}."), FText::FromString( InLevelName ) );

	return false;
}

bool UDatasmithConsumer::CanCreateLevel(const FString& RequestedFolder, const FString& RequestedName, const bool bShowDialog)
{
	FSoftObjectPath ObjectPath(FPaths::Combine(RequestedFolder, RequestedName) + TEXT(".") + RequestedName);

	FString PackageFilename;
	FPackageName::TryConvertLongPackageNameToFilename( ObjectPath.GetLongPackageName(), PackageFilename, FPackageName::GetMapPackageExtension() );

	if(FPaths::FileExists(PackageFilename))
	{
		if(UWorld* World = Cast<UWorld>(ObjectPath.TryLoad()))
		{
			if(DatasmithConsumerUtils::GetMarker(World->PersistentLevel, ConsumerMarkerID) != UniqueID)
			{
				if(bShowDialog)
				{
					const FTextFormat Format(LOCTEXT("DatasmithConsumer_SetTargetContentFolder_Overwrite_Dlg", "Level {0} already exists in {1}.\n\nDo you want to overwrite it?"));
					const FText WarningMessage = FText::Format( Format, FText::FromString(RequestedName), FText::FromString(RequestedFolder));
					const FText DialogTitle( LOCTEXT("DatasmithConsumer_Overwrite_DlgTitle", "Warning - Level already exists") );

					if(FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage, &DialogTitle) != EAppReturnType::Yes)
					{
						return false;
					}
				}
				else
				{
					const FTextFormat Format(LOCTEXT("DatasmithConsumer_SetTargetContentFolder_Overwrite", "Level {0} already exists in {1}.It will be overwritten."));
					const FText WarningMessage = FText::Format( Format, FText::FromString(RequestedName), FText::FromString(RequestedFolder));
					LogWarning(WarningMessage);
				}
			}
		}
	}
	else if(FDatasmithImporterUtils::CanCreateAsset(ObjectPath.GetAssetPathString(), UWorld::StaticClass()) != FDatasmithImporterUtils::EAssetCreationStatus::CS_CanCreate)
	{
		const FTextFormat Format(LOCTEXT("DatasmithConsumer_SetTargetContentFolder_CantCreate_Dlg", "Cannot create level {0} in folder {1}."));
		const FText Message = FText::Format( Format, FText::FromString(RequestedName), FText::FromString(RequestedFolder));

		if(bShowDialog)
		{
			const FText DialogTitle( LOCTEXT("DatasmithConsumer_CantCreate_DlgTitle", "Warning - Cannot create level") );

			FMessageDialog::Open(EAppMsgType::Ok, Message, &DialogTitle);

			return false;
		}
		else
		{
			LogError(Message);
		}
	}

	return true;
}

bool UDatasmithConsumer::SetTargetContentFolderImplementation(const FString& InTargetContentFolder, FText& OutFailureReason, const bool bIsAutomated)
{
	if(!CanCreateLevel(InTargetContentFolder, LevelName, !bIsAutomated && !IsRunningCommandlet()))
	{
		return false;
	}

	if ( Super::SetTargetContentFolderImplementation( InTargetContentFolder, OutFailureReason, bIsAutomated ) )
	{
		// Inform user if related Datasmith scene is not in package path and force re-creation of Datasmith scene
		const FText Message = FText::Format( LOCTEXT("DatasmithConsumer_SetTargetContentFolder", "Package path {0} different from path previously used. Previous content will not be updated."), FText::FromString( TargetContentFolder ) );
		LogInfo(Message);

		DatasmithScene.Reset();

		return SetOutputLevel( LevelName );
	}

	return false;
}

void UDatasmithConsumer::UpdateScene()
{
	// Do nothing if this is the First call to Run, DatasmithScene is null
	if( !DatasmithScene.IsValid() )
	{
		return;
	}

	// Check if name of owning Dataprep asset has not changed
	const FString DatasmithSceneName = GetOuter()->GetName() + DatasmithSceneSuffix;
	if( DatasmithScene->GetName() != DatasmithSceneName )
	{
		// Force re-creation of Datasmith scene
		DatasmithScene.Reset();
	}
}

bool UDatasmithConsumer::SetOutputLevel(const FString& InLevelName)
{
	if(InLevelName.Len() > 0)
	{
		Modify();

		OutputLevelSoftObject = FSoftObjectPath(FPaths::Combine( TargetContentFolder, InLevelName) + TEXT(".") + InLevelName);

		MarkPackageDirty();

		OnChanged.Broadcast();

		return true;
	}

	return false;
}

ULevel* UDatasmithConsumer::FindOrAddLevel(const FString& InLevelName)
{
	FString LevelPackageName = FPaths::Combine(TargetContentFolder, InLevelName);

	if(ULevelStreaming* StreamingLevel = FLevelUtils::FindStreamingLevel(WorkingWorld.Get(), *LevelPackageName))
	{
		return StreamingLevel->GetLoadedLevel();
	}

	ULevel* CurrentLevel = WorkingWorld->PersistentLevel;

	// This level has not been added yet
	FString PackageFilename;
	FPackageName::TryConvertLongPackageNameToFilename( LevelPackageName, PackageFilename, FPackageName::GetMapPackageExtension() );

	bool bCleanLevel = false;

	ULevelStreaming* StreamingLevel = nullptr;
	if(FPaths::FileExists(PackageFilename))
	{
		FTransform LevelTransform;
		StreamingLevel = UEditorLevelUtils::AddLevelToWorld(WorkingWorld.Get(), *LevelPackageName, ULevelStreamingAlwaysLoaded::StaticClass(), LevelTransform);
		ensure(StreamingLevel);

		WorkingWorld->LoadSecondaryLevels();
		ensure(StreamingLevel->GetLoadedLevel());

		bCleanLevel = true;
	}
	else
	{
		StreamingLevel = EditorLevelUtils::CreateNewStreamingLevelForWorld( *WorkingWorld, ULevelStreamingAlwaysLoaded::StaticClass(), *PackageFilename );
		ensure(StreamingLevel);
	}

	WorkingWorld->PersistentLevel = CurrentLevel;
	WorkingWorld->SetCurrentLevel(CurrentLevel);

	// Mark level as generated by this consumer
	if(StreamingLevel)
	{
		ULevel* NewLevel = StreamingLevel->GetLoadedLevel();

		DatasmithConsumerUtils::SetMarker(NewLevel, ConsumerMarkerID, UniqueID);

		if(bCleanLevel)
		{
			// Clean up the level if it contains actors from previous execution
			UWorld* LevelWorld = Cast<UWorld>(NewLevel->GetOuter());

			TArray<AActor*> LevelActors = NewLevel->Actors;
			for(AActor* Actor : LevelActors)
			{

				if(Actor && Actor->GetRootComponent() && !Actor->IsA<AWorldSettings>() && !Actor->IsA<APhysicsVolume>() && !Actor->IsA<ABrush>() )
				{
					LevelWorld->DestroyActor(Actor, true);
					Actor->UObject::Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
				}
			}

			LevelWorld->CleanupWorld(false, false);
			LevelWorld->CleanupActors();

			DatasmithConsumerUtils::SaveMap(LevelWorld);
		}

		WorkingWorld->AddLevel(NewLevel);

		return NewLevel;
	}

	return nullptr;
}

bool UDatasmithConsumer::CheckOutputDirectives()
{
	auto CanCreateAsset = [](const FString& AssetPathName,const UClass* AssetClass)
	{
		return FDatasmithImporterUtils::CanCreateAsset(AssetPathName, AssetClass) == FDatasmithImporterUtils::EAssetCreationStatus::CS_CanCreate;
	};

	const bool bShowDialog = !Context.bSilentMode && !IsRunningCommandlet();

	// Collect garbage to clear out the destroyed level
	CollectGarbage( GARBAGE_COLLECTION_KEEPFLAGS );

	// Collect all sub-levels to be created
	TSet<FString> LevelsToCreate;
	LevelsToCreate.Add(LevelName);

	for(AActor* Actor : Context.WorldPtr->GetCurrentLevel()->Actors)
	{
		if(Actor)
		{
			const FString& OutputLevelName = DatasmithConsumerUtils::GetMarker(Actor->GetRootComponent(), UDataprepContentConsumer::RelativeOutput);
			if(OutputLevelName.Len() > 0)
			{
				LevelsToCreate.Add(OutputLevelName);
			}
		}
	}

	bool bCannotCreateAsset = false;

	// check if any of the levels to create or update is not opened in the level editor
	TArray<FString> OpenedLevels;
	UWorld* GlobalWorld = GWorld;
	{
		TArray<ULevel*> WorldLevels = GlobalWorld->GetLevels();
		for(ULevel* Level : WorldLevels)
		{
			if(Level)
			{
				const FString WorldLevelName = Level->GetOuter()->GetName();
				if(LevelsToCreate.Contains(WorldLevelName))
				{
					OpenedLevels.Add(WorldLevelName);
				}
			}
		}
	}

	for(const FString& LevelToCreate : LevelsToCreate)
	{
		FSoftObjectPath AssetSoftObjectPath(FPaths::Combine(TargetContentFolder, LevelToCreate) + "." + LevelToCreate);

		if(!CanCreateAsset(AssetSoftObjectPath.GetAssetPathString(), UWorld::StaticClass()))
		{
			const FTextFormat TextFormat(LOCTEXT( "DatasmithConsumer_CannotCreateAsset", "Cannot create asset {0}. Commit will be aborted" ));
			const FText Message = FText::Format( TextFormat, FText::FromString(AssetSoftObjectPath.GetAssetPathString()) );
			LogError(Message);

			bCannotCreateAsset = true;
		}
		// Check if umap file does not already exist. If so, user will be asked if he/she wants to overwrite it
		else
		{
			FString PackageFilename;
			FPackageName::TryConvertLongPackageNameToFilename( AssetSoftObjectPath.GetLongPackageName(), PackageFilename, FPackageName::GetMapPackageExtension() );
			if(FPaths::FileExists(PackageFilename))
			{
				const FTextFormat TextFormat(LOCTEXT( "DatasmithConsumer_UMapAlreadyExists", "Level {0} already exists" ));
				const FText Message = FText::Format( TextFormat, FText::FromString(LevelToCreate) );
				LogInfo(Message);
			}
		}
	}

	// Abort commit if any level is opened
	if(OpenedLevels.Num() > 0)
	{
		FText OpenedLevelsText;
		if(OpenedLevels.Num() == 1)
		{
			const FTextFormat TextFormat(LOCTEXT( "DatasmithConsumer_OneLevelOpened", "level {0} is opened" ));
			OpenedLevelsText = FText::Format( TextFormat, FText::FromString(OpenedLevels[0]) );
		}
		else
		{
			const FString Separator(TEXT(", "));
			FString OpenedLevelListString = OpenedLevels[0];
			for(int32 Index = 1; Index < OpenedLevels.Num(); ++Index)
			{
				OpenedLevelListString += Separator + OpenedLevels[Index];
			}

			const FTextFormat TextFormat(LOCTEXT( "DatasmithConsumer_MultipleLevelOpened", "levels {0} are opened"));
			OpenedLevelsText = FText::Format( TextFormat, FText::FromString(OpenedLevelListString) );
		}

		const FText Message = FText::Format( LOCTEXT( "DatasmithConsumer_OpenAbortCommit", "Cannot proceed with commit because {0}.\nPlease close any editor using this level and commit again" ), OpenedLevelsText );
		if(bShowDialog)
		{
			const FText Title( LOCTEXT( "DatasmithConsumer_OpenAbortCommitTitle", "Main level is opened" ) );
			FMessageDialog::Open( EAppMsgType::Ok, Message, &Title );
		}
		else
		{
			LogError(Message);
		}

		return false;
	}
	
	for(const TWeakObjectPtr< UObject >& AssetPtr : Context.Assets)
	{
		if(UObject* Asset = AssetPtr.Get())
		{
			const FString& OutputFolder = DatasmithConsumerUtils::GetMarker(Asset, UDataprepContentConsumer::RelativeOutput);
			if(OutputFolder.Len() > 0)
			{
				FString AssetName = Asset->GetName();
				FSoftObjectPath AssetSoftObjectPath(FPaths::Combine(TargetContentFolder, OutputFolder, AssetName) + "." + AssetName);

				if(Asset->GetPathName() != AssetSoftObjectPath.GetLongPackageName())
				{
					if(!CanCreateAsset(AssetSoftObjectPath.GetAssetPathString(), Asset->GetClass()))
					{
						const FTextFormat TextFormat(LOCTEXT( "DatasmithConsumer_CannotCreateAsset", "Cannot create asset {0}. Commit will be aborted" ));
						const FText Message = FText::Format( TextFormat, FText::FromString(AssetSoftObjectPath.GetAssetPathString()) );
						LogError(Message);

						bCannotCreateAsset = true;
					}
				}
			}
		}
	}

	if(bCannotCreateAsset)
	{

		const FText Message = LOCTEXT( "DatasmithConsumer_CreateAbortCommit", "Cannot proceed with commit because some assets and/or levels cannot be created.\nCheck your log for details, fix all issues and commit again" );
		
		if(bShowDialog)
		{
			const FText Title( LOCTEXT( "DatasmithConsumer_CreateAbortCommitTitle", "Cannot create some assets" ) );
			FMessageDialog::Open( EAppMsgType::Ok, Message, &Title );
		}
		else
		{
			LogError(Message);
		}

		return false;
	}

	return true;
}

void UDatasmithConsumer::ApplySubLevelDirective()
{
	TMap< FName, TSoftObjectPtr< AActor > >& RelatedActors = ImportContextPtr->ActorsContext.CurrentTargetedScene->RelatedActors;

	TMap<FString, ULevel*> LevelMap;
	TMap<ULevel*, TArray<AActor*>> ActorsToMove;

	LevelMap.Add(LevelName, PrimaryLevel);
	ActorsToMove.Add(PrimaryLevel);

	for(TPair< FName, TSoftObjectPtr< AActor > >& Entry : RelatedActors)
	{
		if(AActor* Actor = Entry.Value.Get())
		{
			ULevel* TargetLevel = PrimaryLevel;

			const FString& OutputDirectiveName = DatasmithConsumerUtils::GetMarker(Actor->GetRootComponent(), UDataprepContentConsumer::RelativeOutput);
			if(OutputDirectiveName.Len() > 0 && OutputDirectiveName != LevelName)
			{
				ULevel* Level = nullptr;
				if(ULevel** OutputLevelPtr = LevelMap.Find(OutputDirectiveName))
				{
					Level = *OutputLevelPtr;
				}
				else
				{
					Level = FindOrAddLevel(OutputDirectiveName);

					if(Level)
					{
						// Tag new level as owned by consumer
						LevelMap.Add(OutputDirectiveName, Level);
						DatasmithConsumerUtils::SetMarker(Level, ConsumerMarkerID, UniqueID);
					}
					else
					{
						FText Message = LOCTEXT( "DatasmithConsumer_ApplySubLevelDirective", "Cannot create level..." );
						LogWarning( Message );
					}
				}

				if(Level)
				{
					TargetLevel = Level;
				}

			}

			if(Actor->GetLevel() != TargetLevel)
			{
				ActorsToMove.FindOrAdd(TargetLevel).Add(Actor);
			}
		}
	}

	for(TPair<ULevel*, TArray<AActor*>> Entry : ActorsToMove)
	{
		DatasmithConsumerUtils::MoveActorsToLevel( Entry.Value, Entry.Key, RelatedActors, false);
	}
}

namespace DatasmithConsumerUtils
{
	const FString& GetMarker(UObject* Object, const FString& Name)
	{
		if ( IInterface_AssetUserData* AssetUserDataInterface = Cast< IInterface_AssetUserData >( Object ) )
		{
			UDataprepConsumerUserData* DataprepContentUserData = AssetUserDataInterface->GetAssetUserData< UDataprepConsumerUserData >();

			if ( DataprepContentUserData )
			{
				return DataprepContentUserData->GetMarker(Name);
			}
		}

		static FString NullString;

		return NullString;
	}

	void SetMarker(UObject* Object, const FString& Name, const FString& Value)
	{
		if ( IInterface_AssetUserData* AssetUserDataInterface = Cast< IInterface_AssetUserData >( Object ) )
		{
			UDataprepConsumerUserData* DataprepContentUserData = AssetUserDataInterface->GetAssetUserData< UDataprepConsumerUserData >();

			if ( !DataprepContentUserData )
			{
				EObjectFlags Flags = RF_Public;
				DataprepContentUserData = NewObject< UDataprepConsumerUserData >( Object, NAME_None, Flags );
				AssetUserDataInterface->AddAssetUserData( DataprepContentUserData );
			}

			return DataprepContentUserData->AddMarker(Name, Value);
		}
	}

	void ConvertSceneActorsToActors( FDatasmithImportContext& ImportContext )
	{
		UWorld* ImportWorld = ImportContext.ActorsContext.ImportWorld;

		// Find all ADatasmithSceneActor in the world
		TArray< ADatasmithSceneActor* > SceneActorsToConvert;
		TArray<AActor*> Actors( ImportWorld->GetCurrentLevel()->Actors );
		for( AActor* Actor : Actors )
		{
			if( ADatasmithSceneActor* ImportSceneActor = Cast<ADatasmithSceneActor>( Actor ) )
			{
				SceneActorsToConvert.Add( ImportSceneActor );
			}
		}

		// Create the import scene actor for the import context
		ADatasmithSceneActor* RootSceneActor = FDatasmithImporterUtils::CreateImportSceneActor( ImportContext, FTransform::Identity );
		if (RootSceneActor == nullptr)
		{
			return;
		}
		RootSceneActor->Scene = ImportContext.SceneAsset;

		ImportContext.ActorsContext.ImportSceneActor = RootSceneActor;

		// Add existing scene actors as regular actors
		TMap< FName, TSoftObjectPtr< AActor > >& RelatedActors = RootSceneActor->RelatedActors;
		RelatedActors.Reserve( ImportWorld->GetCurrentLevel()->Actors.Num() );

		USceneComponent* NewSceneActorRootComponent = RootSceneActor->GetRootComponent();
		ImportContext.Hierarchy.Push( NewSceneActorRootComponent );

		TArray<AActor*> ActorsToVisit;

		for( ADatasmithSceneActor* SceneActor : SceneActorsToConvert )
		{
			// Create AActor to replace scene actor
			const FString SceneActorName = SceneActor->GetName();
			const FString SceneActorLabel = SceneActor->GetActorLabel();
			SceneActor->Rename( nullptr, nullptr, REN_DontCreateRedirectors | REN_NonTransactional );

			// Use actor's label instead of name.
			// Rationale: Datasmith scene actors are created with the same name and label and their name can change when calling SetLabel.
			TSharedRef< IDatasmithActorElement > RootActorElement = FDatasmithSceneFactory::CreateActor( *SceneActorLabel );
			RootActorElement->SetLabel( *SceneActorLabel );

			AActor* Actor = FDatasmithActorImporter::ImportBaseActor( ImportContext, RootActorElement );
			check( Actor && Actor->GetRootComponent());

			FDatasmithImporter::ImportMetaDataForObject( ImportContext, RootActorElement, Actor );


			// Copy the transforms
			USceneComponent* ActorRootComponent = Actor->GetRootComponent();
			check( ActorRootComponent );

			USceneComponent* SceneActorRootComponent = SceneActor->GetRootComponent();

			ActorRootComponent->SetRelativeTransform( SceneActorRootComponent->GetRelativeTransform() );
			ActorRootComponent->SetComponentToWorld( SceneActorRootComponent->GetComponentToWorld() );

			// Reparent children of root scene actor to new root actor
			TArray<USceneComponent*> AttachedChildren;
			SceneActor->GetRootComponent()->GetChildrenComponents(false, AttachedChildren);

			for(USceneComponent* SceneComponent : AttachedChildren)
			{
				SceneComponent->AttachToComponent( ActorRootComponent, FAttachmentTransformRules::KeepRelativeTransform );
			}

			// Attach new actor to root scene actor
			ActorRootComponent->AttachToComponent( NewSceneActorRootComponent, FAttachmentTransformRules::KeepRelativeTransform );

			// Copy AssetUserData - it is done by known classes but should be improved
			if ( IInterface_AssetUserData* SourceAssetUserDataInterface = Cast< IInterface_AssetUserData >( SceneActorRootComponent ) )
			{
				if(IInterface_AssetUserData* TargetAssetUserDataInterface = Cast< IInterface_AssetUserData >(ActorRootComponent))
				{
					if(UAssetUserData* SourceDatasmithUserData = SourceAssetUserDataInterface->GetAssetUserDataOfClass(UDatasmithAssetUserData::StaticClass()))
					{
						UAssetUserData* TargetDatasmithUserData = DuplicateObject<UAssetUserData>(SourceDatasmithUserData, ActorRootComponent);
						TargetAssetUserDataInterface->AddAssetUserData(TargetDatasmithUserData);
					}

					if(UAssetUserData* SourceConsumerUserData = SourceAssetUserDataInterface->GetAssetUserDataOfClass(UDataprepConsumerUserData::StaticClass()))
					{
						UAssetUserData* TargetConsumerUserData = DuplicateObject<UAssetUserData>(SourceConsumerUserData, ActorRootComponent);
						TargetAssetUserDataInterface->AddAssetUserData(TargetConsumerUserData);
					}
				}
			}


			// Delete root scene actor since it is not needed anymore
			ImportWorld->DestroyActor( SceneActor, false, true );
			SceneActor->UnregisterAllComponents();

			SceneActor->Rename( nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional );

			Actor->RegisterAllComponents();

			// Append children of actor to be later added as related actors
			TArray<AActor*> Children;
			Actor->GetAttachedActors( Children );

			ActorsToVisit.Append( Children );
		}

		// Recursively add all children of previous scene actors as related to new scene actor
		while ( ActorsToVisit.Num() > 0)
		{
			AActor* VisitedActor = ActorsToVisit.Pop();
			if (VisitedActor == nullptr)
			{
				continue;
			}

			// Add visited actor as actor related to scene actor
			RelatedActors.Add( FName( *GetObjectUniqueId( VisitedActor ) ), VisitedActor );

			// Continue with children
			TArray<AActor*> Children;
			VisitedActor->GetAttachedActors( Children );

			ActorsToVisit.Append( Children );
		}

		auto IsUnregisteredActor = [&](AActor* Actor)
		{
			// Skip non-imported actors
			if( Actor == RootSceneActor || Actor == nullptr || Actor->GetRootComponent() == nullptr || Actor->IsA<AWorldSettings>() || Actor->IsA<APhysicsVolume>() || Actor->IsA<ABrush>() )
			{
				return false;
			}

			// Skip actor which we have already processed
			return RelatedActors.Find( *GetObjectUniqueId( Actor ) ) ? false : true;
		};

		// Find remaining root actors (non scene actors)
		TArray< AActor* > RootActors;
		for( AActor* Actor : ImportWorld->GetCurrentLevel()->Actors )
		{
			if( IsUnregisteredActor( Actor ) )
			{
				// Find root actor
				AActor* RootActor = Actor;

				while( RootActor->GetAttachParentActor() != nullptr )
				{
					RootActor = RootActor->GetAttachParentActor();
				}

				// Attach root actor to root scene actor
				RootActor->GetRootComponent()->AttachToComponent( NewSceneActorRootComponent, FAttachmentTransformRules::KeepRelativeTransform );

				// Add root actor and its children as related to new scene actor
				ActorsToVisit.Add( RootActor );

				while ( ActorsToVisit.Num() > 0)
				{
					AActor* VisitedActor = ActorsToVisit.Pop();
					if (VisitedActor == nullptr)
					{
						continue;
					}

					// Add visited actor as actor related to scene actor
					RelatedActors.Add( FName( *GetObjectUniqueId( VisitedActor ) ), VisitedActor );

					// Continue with children
					TArray<AActor*> Children;
					VisitedActor->GetAttachedActors( Children );

					ActorsToVisit.Append( Children );
				}
			}
		}
	}

	void AddAssetsToContext(FDatasmithImportContext& ImportContext, TArray<TWeakObjectPtr<UObject>>& Assets)
	{
		// Addition is done in 2 passes to properly collect UMaterial objects referenced by UMaterialInstance ones
		// Templates are added to assets which have not been created through Datasmith

		// Add template and Datasmith unique Id to source object
		auto AddTemplate = [](UClass* TemplateClass, UObject* Source)
		{
			UDatasmithObjectTemplate* DatasmithTemplate = NewObject< UDatasmithObjectTemplate >( Source, TemplateClass );
			DatasmithTemplate->Load( Source );
			FDatasmithObjectTemplateUtils::SetObjectTemplate( Source, DatasmithTemplate );

			UDatasmithAssetUserData::SetDatasmithUserDataValueForKey(Source, UDatasmithAssetUserData::UniqueIdMetaDataKey, Source->GetName() );
		};

		// First skip UMaterial objects which are not referenced by a UmaterialInstance one
		int32 MaterialCount = 0;
		TSet< UMaterialInterface* > ParentMaterials;
		TSet< UMaterialFunctionInterface* > MaterialFunctions;
		for(TWeakObjectPtr<UObject>& AssetPtr : Assets)
		{
			if( UObject* Asset = AssetPtr.Get() )
			{
				FString AssetTag = FDatasmithImporterUtils::GetDatasmithElementIdString( Asset );

				if(UTexture* Texture = Cast<UTexture>(Asset))
				{
					TSharedRef< IDatasmithTextureElement > TextureElement = FDatasmithSceneFactory::CreateTexture( *AssetTag );
					TextureElement->SetLabel( *Texture->GetName() );

					ImportContext.ImportedTextures.Add( TextureElement, Texture );
					ImportContext.Scene->AddTexture( TextureElement );
				}
				else if(UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Asset))
				{
					TSharedRef< IDatasmithBaseMaterialElement > MaterialElement = FDatasmithSceneFactory::CreateMaterial( *AssetTag );
					MaterialElement->SetLabel( *MaterialInstance->GetName() );

					if (UMaterial* SourceMaterial = Cast< UMaterial >(MaterialInstance))
					{
						MaterialElement = StaticCastSharedRef< IDatasmithBaseMaterialElement >( FDatasmithSceneFactory::CreateUEPbrMaterial( *AssetTag ) );
						MaterialElement->SetLabel( *MaterialInstance->GetName() );
					}

					if ( UMaterialInterface* MaterialParent = MaterialInstance->Parent )
					{
						FString MaterialInstancePath = MaterialInstance->GetOutermost()->GetName();
						FString ParentPath = MaterialParent->GetOutermost()->GetName();

						// Add parent material to ImportedParentMaterials if applicable
						if ( ParentPath.StartsWith( MaterialInstancePath ) )
						{
							ImportContext.ImportedParentMaterials.Add( MaterialCount, MaterialParent );
							MaterialCount++;

							ParentMaterials.Add( MaterialParent );
						}
					}

					if(UMaterialInstanceConstant* MaterialInstanceConstant = Cast<UMaterialInstanceConstant>(MaterialInstance))
					{
						if(!FDatasmithObjectTemplateUtils::GetObjectTemplate<UDatasmithMaterialInstanceTemplate>( MaterialInstanceConstant ))
						{
							AddTemplate( UDatasmithMaterialInstanceTemplate::StaticClass(), MaterialInstanceConstant );
						}
					}

					ImportContext.ImportedMaterials.Add( MaterialElement, MaterialInstance );
					ImportContext.Scene->AddMaterial( MaterialElement );
				}
				else if(UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
				{
					// Clean up static meshes which have incomplete render data.
					if(StaticMesh->RenderData.IsValid() && !StaticMesh->RenderData->IsInitialized())
					{
						StaticMesh->RenderData.Reset();
					}

					if(FDatasmithObjectTemplateUtils::GetObjectTemplate<UDatasmithStaticMeshTemplate>( StaticMesh ) == nullptr)
					{
						AddTemplate( UDatasmithStaticMeshTemplate::StaticClass(), StaticMesh );
					}

					TSharedRef< IDatasmithMeshElement > MeshElement = FDatasmithSceneFactory::CreateMesh( *AssetTag );
					MeshElement->SetLabel( *StaticMesh->GetName() );


					for(int32 Index = 0; Index < StaticMesh->GetNumSections( 0 ); ++Index)
					{
						const FString MaterialTag = FDatasmithImporterUtils::GetDatasmithElementIdString( StaticMesh->GetMaterial( Index ) );
						MeshElement->SetMaterial( *MaterialTag, Index );
					}

					ImportContext.ImportedStaticMeshes.Add( MeshElement, StaticMesh );
					ImportContext.Scene->AddMesh( MeshElement );
				}
				else if(ULevelSequence* LevelSequence = Cast<ULevelSequence>(Asset))
				{
					TSharedRef< IDatasmithLevelSequenceElement > LevelSequenceElement = FDatasmithSceneFactory::CreateLevelSequence( *AssetTag );
					LevelSequenceElement->SetLabel( *LevelSequence->GetName() );

					ImportContext.ImportedLevelSequences.Add( LevelSequenceElement, LevelSequence );
					ImportContext.Scene->AddLevelSequence( LevelSequenceElement );
				}
				else if(ULevelVariantSets* LevelVariantSets = Cast<ULevelVariantSets>(Asset))
				{
					TSharedRef< IDatasmithLevelVariantSetsElement > LevelVariantSetsElement = FDatasmithSceneFactory::CreateLevelVariantSets( *AssetTag );
					LevelVariantSetsElement->SetLabel( *LevelVariantSets->GetName() );

					ImportContext.ImportedLevelVariantSets.Add( LevelVariantSetsElement, LevelVariantSets );
					ImportContext.Scene->AddLevelVariantSets( LevelVariantSetsElement );
				}
				// #ueent_todo: Add support for assets which are not of the classes above
			}
		}

		// Second take care UMaterial objects which are not referenced by a UmaterialInstance one
		for( TWeakObjectPtr<UObject>& AssetPtr : Assets )
		{
			UObject* AssetObject = AssetPtr.Get();
			if( UMaterial* Material = Cast<UMaterial>( AssetObject ) )
			{
				if( !ParentMaterials.Contains( Material ) )
				{
					FString AssetTag = FDatasmithImporterUtils::GetDatasmithElementIdString( Material );
					TSharedRef< IDatasmithMaterialElement > MaterialElement = FDatasmithSceneFactory::CreateMaterial( *AssetTag );
					MaterialElement->SetLabel( *Material->GetName() );

					ImportContext.ImportedMaterials.Add( MaterialElement, Material );
					ImportContext.Scene->AddMaterial( MaterialElement );
				}
			}
			else if( UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>( AssetObject ) )
			{
				if( !MaterialFunctions.Contains( Cast<UMaterialFunctionInterface>( MaterialFunction ) ) )
				{
					FString AssetTag = FDatasmithImporterUtils::GetDatasmithElementIdString( MaterialFunction );

					TSharedRef< IDatasmithUEPbrMaterialElement > UEPbrMaterialFunctionElement = FDatasmithSceneFactory::CreateUEPbrMaterial( *AssetTag );

					UEPbrMaterialFunctionElement->SetLabel( *MaterialFunction->GetName() );
					UEPbrMaterialFunctionElement->SetMaterialFunctionOnly( true );

					TSharedRef< IDatasmithBaseMaterialElement > BaseMaterialElement = StaticCastSharedRef< IDatasmithBaseMaterialElement >( UEPbrMaterialFunctionElement );

					ImportContext.ImportedMaterialFunctions.Add( BaseMaterialElement, MaterialFunction );
					ImportContext.ImportedMaterialFunctionsByName.Add( BaseMaterialElement->GetName(), BaseMaterialElement );

					ImportContext.Scene->AddMaterial( BaseMaterialElement );
				}
			}
		}
	}

	void SaveMap(UWorld* WorldToSave)
	{
		const bool bHasStandaloneFlag = WorldToSave->HasAnyFlags(RF_Standalone);
		FSoftObjectPath WorldSoftObject(WorldToSave);

		// Delete map file if it already exists
		FString PackageFilename;
		FPackageName::TryConvertLongPackageNameToFilename( WorldSoftObject.GetLongPackageName(), PackageFilename, FPackageName::GetMapPackageExtension() );

		IFileManager::Get().Delete(*PackageFilename, /*RequireExists=*/ false, /*EvenReadOnly=*/ true, /*Quiet=*/ true);

		// Add RF_Standalone flag to properly save the completed world
		WorldToSave->SetFlags(RF_Standalone);

		UEditorLoadingAndSavingUtils::SaveMap(WorldToSave, WorldSoftObject.GetLongPackageName() );

		// Clear RF_Standalone from flag to properly delete and garbage collect the completed world
		if(!bHasStandaloneFlag)
		{
			WorldToSave->ClearFlags(RF_Standalone);
		}

		WorldToSave->GetOutermost()->SetDirtyFlag(false);
	}

	TArray<AActor*> MoveActorsToLevel(const TArray<AActor*>& ActorsToMove, ULevel* DestLevel, bool bDuplicate)
	{
		if(DestLevel == nullptr || ActorsToMove.Num() == 0)
		{
			return TArray<AActor*>();
		}

		UWorld* OwningWorld = DestLevel->OwningWorld;

		// Backup the current contents of the clipboard string as we'll be using cut/paste features to move actors
		// between levels and this will trample over the clipboard data.
		FString OriginalClipboardContent;
		FPlatformApplicationMisc::ClipboardPaste(OriginalClipboardContent);

		TMap<FSoftObjectPath, FSoftObjectPath> ActorPathMapping;
		GEditor->SelectNone(false, true, false);

		USelection* ActorSelection = GEditor->GetSelectedActors();
		ActorSelection->BeginBatchSelectOperation();
		for (AActor* Actor : ActorsToMove)
		{
			ActorPathMapping.Add(FSoftObjectPath(Actor), FSoftObjectPath());
			GEditor->SelectActor(Actor, true, false);
		}
		ActorSelection->EndBatchSelectOperation(false);

		if(GEditor->GetSelectedActorCount() == 0)
		{
			return TArray<AActor*>();
		}

		// Cache the old level
		ULevel* OldCurrentLevel = OwningWorld->GetCurrentLevel();

		// If we are moving the actors, cut them to remove them from the existing level
		const bool bShoudCut = !bDuplicate;
		const bool bIsMove = bShoudCut;
		GEditor->CopySelectedActorsToClipboard(OwningWorld, bShoudCut, bIsMove, /*bWarnAboutReferences =*/ false);

		UEditorLevelUtils::SetLevelVisibility(DestLevel, true, false);

		// Scope this so that Actors that have been pasted will have their final levels set before doing the actor mapping
		{
			// Set the new level and force it visible while we do the paste
			FLevelPartitionOperationScope LevelPartitionScope(DestLevel);
			OwningWorld->SetCurrentLevel(LevelPartitionScope.GetLevel());

			//const bool bDuplicate = false;
			const bool bOffsetLocations = false;
			const bool bWarnIfHidden = false;
			GEditor->edactPasteSelected(OwningWorld, bDuplicate, bOffsetLocations, bWarnIfHidden);

			// Restore the original current level
			OwningWorld->SetCurrentLevel(OldCurrentLevel);
		}

		TArray<AActor*> NewActors;
		NewActors.Reserve(GEditor->GetSelectedActorCount());

		// Build a remapping of old to new names so we can do a fixup
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			AActor* Actor = static_cast<AActor*>(*It);
			if(!Actor)
			{
				continue;
			}

			NewActors.Add(Actor);
			FSoftObjectPath NewPath = FSoftObjectPath(Actor);

			bool bFoundMatch = false;

			// First try exact match
			for (TPair<FSoftObjectPath, FSoftObjectPath>& Pair : ActorPathMapping)
			{
				if (Pair.Value.IsNull() && NewPath.GetSubPathString() == Pair.Key.GetSubPathString())
				{
					bFoundMatch = true;
					Pair.Value = NewPath;
					break;
				}
			}

			if (!bFoundMatch)
			{
				// Remove numbers from end as it may have had to add some to disambiguate
				FString PartialPath = NewPath.GetSubPathString();
				int32 IgnoreNumber;
				FActorLabelUtilities::SplitActorLabel(PartialPath, IgnoreNumber);

				for (TPair<FSoftObjectPath, FSoftObjectPath>& Pair : ActorPathMapping)
				{
					if (Pair.Value.IsNull())
					{
						FString KeyPartialPath = Pair.Key.GetSubPathString();
						FActorLabelUtilities::SplitActorLabel(KeyPartialPath, IgnoreNumber);
						if (PartialPath == KeyPartialPath)
						{
							bFoundMatch = true;
							Pair.Value = NewPath;
							break;
						}
					}
				}
			}

			if (!bFoundMatch)
			{
				UE_LOG(LogDatasmithImport, Error, TEXT("Cannot find remapping for moved actor ID %s, any soft references pointing to it will be broken!"), *Actor->GetPathName());
			}
		}

		FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		TArray<FAssetRenameData> RenameData;

		for (TPair<FSoftObjectPath, FSoftObjectPath>& Pair : ActorPathMapping)
		{
			if (Pair.Value.IsValid())
			{
				RenameData.Add(FAssetRenameData(Pair.Key, Pair.Value, true));
			}
		}

		if (RenameData.Num() > 0)
		{
			AssetToolsModule.Get().RenameAssets(RenameData);
		}

		// Restore the original clipboard contents
		FPlatformApplicationMisc::ClipboardCopy(*OriginalClipboardContent);

		return NewActors;
	}

	void MoveActorsToLevel(const TArray<AActor*>& ActorsToMove, ULevel* DestLevel, TMap<FName,TSoftObjectPtr<AActor>>& ActorsMap, bool bDuplicate)
	{
		if(ActorsToMove.Num() > 0)
		{
			UWorld* PrevGWorld = GWorld;
			GWorld = DestLevel->OwningWorld;

			// Cache Destination flags
			EObjectFlags DestLevelFlags = DestLevel->GetFlags();
			EObjectFlags DestWorldFlags = DestLevel->GetOuter()->GetFlags();
			EObjectFlags DestPackageFlags = DestLevel->GetOutermost()->GetFlags();

			TArray<AActor*> NewActors = MoveActorsToLevel( ActorsToMove, DestLevel, bDuplicate);
			printf(">>> %d", NewActors.Num());

			GWorld = PrevGWorld;

			// Update map of related actors with new actors
			UDatasmithContentBlueprintLibrary* DatasmithContentLibrary = Cast< UDatasmithContentBlueprintLibrary >( UDatasmithContentBlueprintLibrary::StaticClass()->GetDefaultObject() );

			for(AActor* Actor : DestLevel->Actors)
			{
				const FString DatasmithUniqueId = DatasmithContentLibrary->GetDatasmithUserDataValueForKey( Actor, UDatasmithAssetUserData::UniqueIdMetaDataKey );
				if(DatasmithUniqueId.Len() > 0)
				{
					if(TSoftObjectPtr<AActor>* SoftObjectPtr = ActorsMap.Find(FName(*DatasmithUniqueId)))
					{
						*SoftObjectPtr = Actor;
					}
				}
			}

			// Restore Destination flags
			DestLevel->SetFlags(DestLevelFlags);
			DestLevel->GetOuter()->SetFlags(DestWorldFlags);
			DestLevel->GetOutermost()->SetFlags(DestPackageFlags);
		}
	}
}

#undef LOCTEXT_NAMESPACE
