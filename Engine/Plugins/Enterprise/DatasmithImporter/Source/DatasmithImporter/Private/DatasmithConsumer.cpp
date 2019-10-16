// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "ComponentReregisterContext.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
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

#define LOCTEXT_NAMESPACE "DatasmithConsumer"

const FText DatasmithConsumerLabel( LOCTEXT( "DatasmithConsumerLabel", "Datasmith writer" ) );
const FText DatasmithConsumerDescription( LOCTEXT( "DatasmithConsumerDesc", "Writes data prep world's current level and assets to current level" ) );

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

	FString GetObjectTag(UObject* Object)
	{
		const FString ObjectPath = FPaths::Combine( Object->GetOutermost()->GetName(), Object->GetName() );
		return FMD5::HashBytes( reinterpret_cast<const uint8*>(*ObjectPath), ObjectPath.Len() * sizeof(TCHAR) );
	}
}

bool UDatasmithConsumer::Initialize()
{
	FText TaskDescription = LOCTEXT( "DatasmithImportFactory_Initialize", "Preparing world ..." );
	ProgressTaskPtr = MakeUnique< FDataprepWorkReporter >( Context.ProgressReporterPtr, TaskDescription, 3.0f, 1.0f );

	ProgressTaskPtr->ReportNextStep( LOCTEXT( "DatasmithImportFactory_Initialize", "Preparing world ...") );

	MoveAssets();

	MoveLevel();

	UpdateLevel();

	UPackage* ParentPackage = CreatePackage( nullptr, *TargetContentFolder );
	ParentPackage->FullyLoad();

	// Check if the Datasmith scene is not already in memory
	if ( !DatasmithScene.IsValid() )
	{
		FName DatasmithSceneName = MakeUniqueObjectName( ParentPackage, UDatasmithScene::StaticClass(), *( GetName() + TEXT("_DS") ) );

		UPackage* Package = CreatePackage( nullptr, *FPaths::Combine( ParentPackage->GetPathName(), DatasmithSceneName.ToString() ) );
		Package->FullyLoad();

		DatasmithScene = NewObject< UDatasmithScene >( Package, DatasmithSceneName, GetFlags() | RF_Public | RF_Standalone | RF_Transactional );
		check( DatasmithScene.IsValid() );

		FAssetRegistryModule::AssetCreated( DatasmithScene.Get() );
		DatasmithScene->MarkPackageDirty();

		DatasmithScene->AssetImportData = NewObject< UDatasmithSceneImportData >( DatasmithScene.Get(), UDatasmithSceneImportData::StaticClass() );
		check( DatasmithScene->AssetImportData );
	}

	// #ueent_todo: Find out necessity of namespace for uniqueness of asset's and actor's names
	if ( !BuildContexts( Context.WorldPtr.Get() ) )
	{
		// #ueent_todo: Provide details of why initialization failed
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

	// Store package path and level name for subsequent call to Run
	LastPackagePath = TargetContentFolder;
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
	UDatasmithSceneImportData* ImportData = Cast< UDatasmithSceneImportData >( DatasmithScene->AssetImportData );

	const FString FilePath = FPaths::Combine( FPaths::ProjectIntermediateDir(), ( DatasmithScene->GetName() + TEXT( ".udatasmith" ) ) );

	ImportContextPtr = MakeUnique< FDatasmithImportContext >( FilePath, false, TEXT("DatasmithImport"), LOCTEXT("DatasmithImportFactoryDescription", "Datasmith") );

	// Update import context with consumer's data
	ImportContextPtr->Options->BaseOptions = ImportData->BaseOptions;
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

	// Store IDatasmithScene(Element) in UDatasmithScene
	FDatasmithImporterUtils::SaveDatasmithScene( ImportContextPtr->Scene.ToSharedRef(), ImportContextPtr->SceneAsset );

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

	// Copy BaseOptions from ImportContext
	ImportData->BaseOptions.AssetOptions.PackagePath = ImportContextPtr->Options->BaseOptions.AssetOptions.PackagePath;

	// Add assets as if they have been imported using the current import context
	DatasmithConsumerUtils::AddAssetsToContext( *ImportContextPtr, Context.Assets );

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

void UDatasmithConsumer::MoveAssets()
{
	// Do nothing if this is the First call to Run, DatasmithScene is null and LastPackagePath is empty
	// or the re-Run is using the same package path
	if( ( !DatasmithScene.IsValid() && LastPackagePath.IsEmpty() ) || LastPackagePath == TargetContentFolder )
	{
		return;
	}

	const FText DialogTitle( LOCTEXT( "DatasmithConsumerDlgTitle", "Warning" ) );

	// Warn user if related Datasmith scene is not in package path and force re-creation of Datasmith scene
	if( DatasmithScene.IsValid() && !DatasmithScene->GetPathName().StartsWith( TargetContentFolder ) )
	{
		FText WarningMessage = FText::Format(LOCTEXT("DatasmithConsumer_NoSceneAsset", "Package path {0} different from path previously used, {1}.\nPrevious content will not be updated."), FText::FromString (TargetContentFolder ), FText::FromString ( LastPackagePath ) );
		FMessageDialog::Open(EAppMsgType::Ok, WarningMessage, &DialogTitle );

		UE_LOG( LogDatasmithImport, Warning, TEXT("%s"), *WarningMessage.ToString() );

		// Force re-creation of Datasmith scene
		DatasmithScene.Reset();
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
				// #ueent_todo: Warn user that level could not be created
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

			// #ueent_todo: is there more to add to the actor element?
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

		// #ueent_todo: Find a better way to filter those out.
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
		for(TWeakObjectPtr<UObject>& AssetPtr : Assets)
		{
			if( UObject* Asset = AssetPtr.Get() )
			{
				FString AssetTag = DatasmithConsumerUtils::GetObjectTag( Asset );

				if(UTexture* Texture = Cast<UTexture>(Asset))
				{
					TSharedRef< IDatasmithTextureElement > TextureElement = FDatasmithSceneFactory::CreateTexture( *AssetTag );
					ImportContext.ImportedTextures.Add( TextureElement, Texture );
				}
				else if(UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Asset))
				{
					TSharedRef< IDatasmithMaterialElement > MaterialElement = FDatasmithSceneFactory::CreateMaterial( *AssetTag );
					ImportContext.ImportedMaterials.Add( MaterialElement, MaterialInstance );

					if ( UMaterialInterface* MaterialParent = MaterialInstance->Parent )
					{
						FString MaterialInstancePath = MaterialInstance->GetOutermost()->GetName();
						FString ParentPath = MaterialParent->GetOutermost()->GetName();

						// Add parent material to ImportedParentMaterials if applicable
						if ( ParentPath.StartsWith( MaterialInstancePath ) )
						{
							// #ueent_todo : Do we want to compute the hash of the material and check its existence?
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
					ImportContext.ImportedStaticMeshes.Add( MeshElement, StaticMesh );
				}
				else if(ULevelSequence* LevelSequence = Cast<ULevelSequence>(Asset))
				{
					TSharedRef< IDatasmithLevelSequenceElement > LevelSequenceElement = FDatasmithSceneFactory::CreateLevelSequence( *AssetTag );
					ImportContext.ImportedLevelSequences.Add( LevelSequenceElement, LevelSequence );
				}
				else if(ULevelVariantSets* LevelVariantSets = Cast<ULevelVariantSets>(Asset))
				{
					TSharedRef< IDatasmithLevelVariantSetsElement > LevelVariantSetsElement = FDatasmithSceneFactory::CreateLevelVariantSets( *AssetTag );
					ImportContext.ImportedLevelVariantSets.Add( LevelVariantSetsElement, LevelVariantSets );
				}
				// #ueent_todo: Add support for assets which are not of the classes above
			}
		}

		// Second take care UMaterial objects which are not referenced by a UmaterialInstance one
		for( TWeakObjectPtr<UObject>& AssetPtr : Assets )
		{
			if( UMaterial* Material = Cast<UMaterial>( AssetPtr.Get() ) )
			{
				if( !ParentMaterials.Contains( Material ) )
				{
					FString AssetTag = DatasmithConsumerUtils::GetObjectTag( Material );
					TSharedRef< IDatasmithMaterialElement > MaterialElement = FDatasmithSceneFactory::CreateMaterial( *AssetTag );
					ImportContext.ImportedMaterials.Add( MaterialElement, Material );
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
