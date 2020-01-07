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
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "ComponentReregisterContext.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/Light.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Internationalization/Internationalization.h"
#include "LevelSequence.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
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

		return DatasmithUniqueId.IsEmpty() ? Object->GetName() : DatasmithUniqueId;
	}
}

bool UDatasmithConsumer::Initialize()
{
	FText TaskDescription = LOCTEXT( "DatasmithImportFactory_Initialize", "Preparing world ..." );
	ProgressTaskPtr = MakeUnique< FDataprepWorkReporter >( Context.ProgressReporterPtr, TaskDescription, 3.0f, 1.0f );

	ProgressTaskPtr->ReportNextStep( LOCTEXT( "DatasmithImportFactory_Initialize", "Preparing world ...") );

	UpdateScene();

	MoveLevel();

	UpdateLevel();

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

	if ( !BuildContexts( Context.WorldPtr.Get() ) )
	{
		return false;
	}

	// Check if the finalize should be threated as a reimport
	if (FDatasmithImporterUtils::FindSceneActors( ImportContextPtr->ActorsContext.FinalWorld, ImportContextPtr->SceneAsset).Num() > 0 )
	{
		ADatasmithSceneActor* FoundSceneActor = nullptr;
		for( AActor* Actor : ImportContextPtr->ActorsContext.FinalWorld->GetCurrentLevel()->Actors )
		{
			if( ADatasmithSceneActor* SceneActor = Cast<ADatasmithSceneActor>(Actor) )
			{
				if( SceneActor->Scene == DatasmithScene )
				{
					FoundSceneActor = SceneActor;
					break;
				}
			}
		}

		if( FoundSceneActor == nullptr )
		{
			//Create a new datasmith scene actor in the targeted level
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

	// Store the level name for subsequent call to Run
	LastLevelName = LevelName;

	return true;
}

void UDatasmithConsumer::Reset()
{
	ImportContextPtr.Reset();
	ProgressTaskPtr.Reset();
	UDataprepContentConsumer::Reset();

	// Restore previous current level
	if( PreviousCurrentLevel != nullptr )
	{
		GWorld->SetCurrentLevel( PreviousCurrentLevel );
		PreviousCurrentLevel = nullptr;
	}
}

const FText& UDatasmithConsumer::GetLabel() const
{
	return DatasmithConsumerLabel;
}

const FText& UDatasmithConsumer::GetDescription() const
{
	return DatasmithConsumerDescription;
}

bool UDatasmithConsumer::BuildContexts( UWorld* ImportWorld )
{
	const FString FilePath = FPaths::Combine( FPaths::ProjectIntermediateDir(), ( DatasmithScene->GetName() + TEXT( ".udatasmith" ) ) );

	ImportContextPtr = MakeUnique< FDatasmithImportContext >( FilePath, false, TEXT("DatasmithImport"), LOCTEXT("DatasmithImportFactoryDescription", "Datasmith") );

	// Update import context with consumer's data
	ImportContextPtr->Options->BaseOptions.SceneHandling = EDatasmithImportScene::CurrentLevel;
	ImportContextPtr->SceneAsset = DatasmithScene.Get();
	ImportContextPtr->ActorsContext.ImportWorld = ImportWorld;
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
	ImportContextPtr->ActorsContext.FinalWorld = GWorld;

	// Initialize ActorsContext's UniqueNameProvider with actors in the GWorld not the Import world
	ImportContextPtr->ActorsContext.UniqueNameProvider = FDatasmithActorUniqueLabelProvider();
	ImportContextPtr->ActorsContext.UniqueNameProvider.PopulateLabelFrom( GWorld );

	// Add assets as if they have been imported using the current import context
	DatasmithConsumerUtils::AddAssetsToContext( *ImportContextPtr, Context.Assets );

	// Store IDatasmithScene(Element) in UDatasmithScene
	FDatasmithImporterUtils::SaveDatasmithScene( ImportContextPtr->Scene.ToSharedRef(), ImportContextPtr->SceneAsset );
	return true;
}

ULevel * UDatasmithConsumer::FindLevel( const FString& InLevelName )
{
	UWorld* FinalWorld = GWorld;

	FSoftObjectPath LevelObjectPath( FPaths::Combine( TargetContentFolder, InLevelName ) );
	UObject* Object = LevelObjectPath.ResolveObject();
	ULevel* Level = Cast<ULevel>( Object );

	for( const ULevelStreaming* LevelStreaming : FinalWorld->GetStreamingLevels() )
	{
		if( LevelStreaming->GetWorldAssetPackageName() == LevelObjectPath.ToString() )
		{
			return LevelStreaming->GetLoadedLevel();
		}
	}

	return Level;
}

bool UDatasmithConsumer::SetLevelName( const FString & InLevelName, FText& OutReason )
{
	FString NewLevelName = InLevelName;

	bool bValidLevelName = false;
	OutReason = FText();

	// Check if a new level can be used with the new name and current limitations
	if( !NewLevelName.IsEmpty() && NewLevelName.Compare( TEXT("current"), ESearchCase::IgnoreCase ) != 0 )
	{
		// Sub-level of sub-level is not supported yet
		// #ueent_todo: sub-level of sub-level
		if( InLevelName.Contains( TEXT("/") ) || InLevelName.Contains( TEXT("\\") ))
		{
			OutReason = LOCTEXT( "DatasmithConsumer_SubLevel", "Sub-level of sub-levels is not supported yet" );
		}
		// Try to see if there is any issue to eventually create this level, i.e. name collision
		else if( FindLevel( InLevelName ) == nullptr )
		{
			FSoftObjectPath LevelObjectPath( FPaths::Combine( TargetContentFolder, InLevelName ) );

			if( StaticFindObject( nullptr, ANY_PACKAGE, *LevelObjectPath.ToString(), true) )
			{
				OutReason = LOCTEXT( "DatasmithConsumer_LevelExists", "A object with that name already exists. Please choose another name." );
			}

			// #ueent_todo: Check if persistent level is locked, etc
		}

		// Good to go if no error documented
		bValidLevelName = OutReason.IsEmpty();
	}
	// New name of level is empty or keyword 'current' used
	else if( !LevelName.IsEmpty() )
	{
		NewLevelName = TEXT("");
		bValidLevelName = true;
	}

	if(bValidLevelName)
	{
		Modify();

		LevelName = NewLevelName;

		OnChanged.Broadcast();
	}

	return bValidLevelName;
}

bool UDatasmithConsumer::SetTargetContentFolder(const FString& InTargetContentFolder, FText& OutReason)
{
	if ( Super::SetTargetContentFolder( InTargetContentFolder, OutReason ) )
	{
		UpdateScene();
		return true;
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

	const FText DialogTitle( LOCTEXT( "DatasmithConsumerDlgTitle", "Warning" ) );

	// Warn user if related Datasmith scene is not in package path and force re-creation of Datasmith scene
	FString DatasmithScenePath = FPaths::GetPath( DatasmithScene->GetPathName() );
	if( DatasmithScenePath != TargetContentFolder )
	{
		// Force re-creation of Datasmith scene
		DatasmithScene.Reset();

		FText WarningMessage = FText::Format(LOCTEXT("DatasmithConsumer_NoSceneAsset", "Package path {0} different from path previously used, {1}.\nPrevious content will not be updated."), FText::FromString (TargetContentFolder ), FText::FromString ( DatasmithScenePath ) );
		FMessageDialog::Open(EAppMsgType::Ok, WarningMessage, &DialogTitle );

		UE_LOG( LogDatasmithImport, Warning, TEXT("%s"), *WarningMessage.ToString() );
	}
	// Check if name of owning Dataprep asset has not changed
	else
	{
		const FString DatasmithSceneName = GetOuter()->GetName() + DatasmithSceneSuffix;

		if( DatasmithScene->GetName() != DatasmithSceneName )
		{
			// Force re-creation of Datasmith scene
			DatasmithScene.Reset();
		}
	}

}

void UDatasmithConsumer::MoveLevel()
{
	// Do nothing if this is the First call to Run, DatasmithScene is null and LastLevelName is empty
	// or the re-Run is using the same level
	if( ( !DatasmithScene.IsValid() && LastLevelName.IsEmpty() ) || LastLevelName == LevelName )
	{
		return;
	}

	const FText DialogTitle( LOCTEXT( "DatasmithConsumerDlgTitle", "Warning" ) );

	ULevel* Level = FindLevel( LevelName );
	if( Level == nullptr )
	{
		FText WarningMessage = FText::Format(LOCTEXT("DatasmithConsumer_NoLevel", "Level {0} different from level previously used, {1}.\nPrevious level will not be updated."), FText::FromString( LevelName ), FText::FromString (LastLevelName ) );
		FMessageDialog::Open(EAppMsgType::Ok, WarningMessage, &DialogTitle );

		UE_LOG( LogDatasmithImport, Warning, TEXT("%s"), *WarningMessage.ToString() );

		return;
	}

	// New level exists, search for DatasmithSceneActor associated with this consumer
	ADatasmithSceneActor* FoundSceneActor = nullptr;
	for( AActor* Actor : Level->Actors )
	{
		if( ADatasmithSceneActor* SceneActor = Cast<ADatasmithSceneActor>(Actor) )
		{
			if( SceneActor->Scene == DatasmithScene.Get() )
			{
				FoundSceneActor = SceneActor;
				break;
			}
		}
	}

	if( FoundSceneActor == nullptr )
	{
		FText WarningMessage = FText::Format(LOCTEXT("DatasmithConsumer_NoScene", "Level {0} does not contain main actor from previous execution.\nA new actor will be created."), FText::FromString( LevelName ) );
		FMessageDialog::Open( EAppMsgType::Ok, WarningMessage, &DialogTitle );

		UE_LOG( LogDatasmithImport, Warning, TEXT("%s"), *WarningMessage.ToString() );
	}
}

void UDatasmithConsumer::UpdateLevel()
{
	PreviousCurrentLevel = nullptr;

	if( !LevelName.IsEmpty() )
	{
		UWorld* FinalWorld = GWorld;

		ULevel* Level = FindLevel( LevelName );

		if( Level == nullptr )
		{
			FSoftObjectPath LevelObjectPath( FPaths::Combine( TargetContentFolder, LevelName ) );

			FString PackageFilename;
			FPackageName::TryConvertLongPackageNameToFilename( LevelObjectPath.ToString(), PackageFilename, FPackageName::GetMapPackageExtension() );
			if( ULevelStreaming* LevelStreaming = EditorLevelUtils::CreateNewStreamingLevelForWorld( *GWorld, ULevelStreamingDynamic::StaticClass(), *PackageFilename ) )
			{
				Level = LevelStreaming->GetLoadedLevel();
			}
			else
			{
				FText Message = LOCTEXT( "DatasmithConsumer_UpdateLevel", "Cannot create level..." );
				LogWarning( Message );
				Level = FinalWorld->PersistentLevel;
			}

			check( Level );
		}

		if( Level != FinalWorld->GetCurrentLevel() )
		{
			PreviousCurrentLevel = FinalWorld->GetCurrentLevel();
			FinalWorld->SetCurrentLevel( Level );
		}
	}
}

namespace DatasmithConsumerUtils
{
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
}

#undef LOCTEXT_NAMESPACE
