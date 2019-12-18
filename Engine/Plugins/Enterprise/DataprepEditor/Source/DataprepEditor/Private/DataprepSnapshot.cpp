// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepEditor.h"

#include "DataprepCoreUtils.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorUtils.h"

#include "ActorEditorUtils.h"
#include "AutoReimport/AutoReimportManager.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "Async/ParallelFor.h"
#include "Editor/UnrealEdEngine.h"
#include "EngineUtils.h"
#include "Engine/Texture.h"
#include "Exporters/Exporter.h"
#include "Factories/LevelFactory.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformOutputDevices.h"
#include "GenericPlatform/GenericPlatformTime.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialInstance.h"
#include "MaterialShared.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Settings/LevelEditorMiscSettings.h"
#include "UnrealExporter.h"
#include "UObject/PropertyPortFlags.h"

#define LOCTEXT_NAMESPACE "DataprepEditor"

enum class EDataprepAssetClass : uint8 {
	EDataprep,
	ETexture,
	EMaterialFunction,
	EMaterialFunctionInstance,
	EMaterial,
	EMaterialInstance,
	EStaticMesh,
	EOther,
	EMaxClasses
};

namespace DataprepSnapshotUtil
{
	const TCHAR* SnapshotExtension = TEXT(".dpc");


	/**
	 * Extends FObjectAndNameAsStringProxyArchive to support FLazyObjectPtr.
	 */
	struct FSnapshotCustomArchive : public FObjectAndNameAsStringProxyArchive
	{
		FSnapshotCustomArchive(FArchive& InInnerArchive)
			:	FObjectAndNameAsStringProxyArchive(InInnerArchive, false)
		{
			// Set archive as transacting to persist all data including data in memory
			SetIsTransacting( true );
		}

		virtual FArchive& operator<<(FLazyObjectPtr& Obj) override
		{
			// Copied from FArchiveUObject::SerializeLazyObjectPtr
			// Note that archive is transacting
			if (IsLoading())
			{
				// Reset before serializing to clear the internal weak pointer. 
				Obj.Reset();
			}
			InnerArchive << Obj.GetUniqueID();

			return *this;
		}
	};

	void RemoveSnapshotFiles(const FString& RootDir)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DataprepSnapshotUtil::RemoveSnapshotFiles);

		TArray<FString> FileNames;
		IFileManager::Get().FindFiles( FileNames, *RootDir, SnapshotExtension );
		for ( FString& FileName : FileNames )
		{
			IFileManager::Get().Delete( *FPaths::Combine( RootDir, FileName ), false );
		}

	}

	FString BuildAssetFileName(const FString& RootPath, const FString& AssetPath )
	{
		static FString FileNamePrefix( TEXT("stream_") );

		FString PackageFileName = FileNamePrefix + FString::Printf( TEXT("%08x"), ::GetTypeHash( AssetPath ) );
		return FPaths::ConvertRelativePathToFull( FPaths::Combine( RootPath, PackageFileName ) + SnapshotExtension );
	}

	void WriteSnapshotData(UObject* Object, TArray<uint8>& OutSerializedData)
	{
		// Helper struct to identify dependency of a UObject on other UObject(s) except given one (its outer)
		struct FObjectDependencyAnalyzer : public FArchiveUObject
		{
			FObjectDependencyAnalyzer(UObject* InSourceObject, const TSet<UObject*>& InValidObjects)
				: SourceObject( InSourceObject )
				, ValidObjects( InValidObjects )
			{ }

			virtual FArchive& operator<<(UObject*& Obj) override
			{
				if(Obj != nullptr)
				{
					// Limit serialization to sub-object of source object
					if( Obj == SourceObject->GetOuter() || Obj->IsA<UPackage>() || (Obj->HasAnyFlags(RF_Public) && Obj->GetOuter()->IsA<UPackage>()))
					{
						return FArchiveUObject::operator<<(Obj);
					}
					// Stop serialization when a dependency is found or has been found
					else if( Obj != SourceObject && !DependentObjects.Contains( Obj ) && ValidObjects.Contains( Obj ) )
					{
						DependentObjects.Add( Obj );
					}
				}

				return *this;
			}

			UObject* SourceObject;
			const TSet<UObject*>& ValidObjects;
			TSet<UObject*> DependentObjects;
		};

		FMemoryWriter MemAr(OutSerializedData);
		FSnapshotCustomArchive Ar(MemAr);

		// Collect sub-objects depending on input object including nested objects
		TArray< UObject* > SubObjectsArray;
		GetObjectsWithOuter( Object, SubObjectsArray, /*bIncludeNestedObjects = */ true );

		// Sort array of sub-objects based on their inter-dependency
		{
			// Create and initialize graph of dependency between sub-objects
			TMap< UObject*, TSet<UObject*> > SubObjectDependencyGraph;
			SubObjectDependencyGraph.Reserve( SubObjectsArray.Num() );

			for(UObject* SubObject : SubObjectsArray)
			{
				SubObjectDependencyGraph.Add( SubObject );
			}

			// Build graph of dependency: each entry contains the set of sub-objects to create before itself
			TSet<UObject*> SubObjectsSet(SubObjectsArray);
			for(UObject* SubObject : SubObjectsArray)
			{
				FObjectDependencyAnalyzer Analyzer( SubObject, SubObjectsSet );
				SubObject->Serialize( Analyzer );

				SubObjectDependencyGraph[SubObject].Append(Analyzer.DependentObjects);
			}

			// Sort array of sub-objects: first objects do not depend on ones below
			int32 Count = SubObjectsArray.Num();
			SubObjectsArray.Empty(Count);

			while(Count != SubObjectsArray.Num())
			{
				for(auto& Entry : SubObjectDependencyGraph)
				{
					if(Entry.Value.Num() == 0)
					{
						UObject* SubObject = Entry.Key;

						SubObjectDependencyGraph.Remove( SubObject );

						SubObjectsArray.Add(SubObject);

						for(auto& SubEntry : SubObjectDependencyGraph)
						{
							if(SubEntry.Value.Num() > 0)
							{
								SubEntry.Value.Remove(SubObject);
							}
						}

						break;
					}
				}
			}
		}

		// Serialize size of array
		int32 SubObjectsCount = SubObjectsArray.Num();
		MemAr << SubObjectsCount;

		// Serialize class and path of each sub-object
		for(UObject* SubObject : SubObjectsArray)
		{
			UClass* SubObjectClass = SubObject->GetClass();

			FString ClassName = SubObjectClass->GetName();
			MemAr << ClassName;

			int32 ObjectFlags = SubObject->GetFlags();
			MemAr << ObjectFlags;
		}

		// Serialize sub-objects' outer path and name
		// Done in reverse order since a sub-object can be the outer of another sub-object
		// it depends on. Not the opposite
		for(int32 Index = SubObjectsArray.Num() - 1; Index >= 0; --Index)
		{
			const UObject* SubObject = SubObjectsArray[Index];

			FSoftObjectPath SoftPath( SubObject->GetOuter() );

			FString SoftPathString = SoftPath.ToString();
			MemAr << SoftPathString;

			FString SubObjectName = SubObject->GetName();
			MemAr << SubObjectName;
		}

		// Serialize sub-objects' content
		for(UObject* SubObject : SubObjectsArray)
		{
			SubObject->Serialize(Ar);
		}

		// Serialize object
		Object->Serialize(Ar);

		if(UTexture* Texture = Cast<UTexture>(Object))
		{
			bool bRebuildResource = !!Texture->Resource;
			Ar << bRebuildResource;
		}
	}

	void ReadSnapshotData(UObject* Object, const TArray<uint8>& InSerializedData, TMap<FString, UClass*>& InClassesMap, TArray<UObject*>& ObjectsToDelete)
	{
		// Remove all objects created by default that InObject is dependent on
		// This method must obviously be called just after the InObject is created
		auto RemoveDefaultDependencies = [&ObjectsToDelete](UObject* InObject)
		{
			TArray< UObject* > ObjectsWithOuter;
			GetObjectsWithOuter( InObject, ObjectsWithOuter, /*bIncludeNestedObjects = */ true );

			for(UObject* ObjectWithOuter : ObjectsWithOuter)
			{
				FDataprepCoreUtils::MoveToTransientPackage( ObjectWithOuter );
				ObjectsToDelete.Add( ObjectWithOuter );
			}
		};

		RemoveDefaultDependencies( Object );

		FMemoryReader MemAr(InSerializedData);
		FSnapshotCustomArchive Ar(MemAr);

		// Deserialize count of sub-objects
		int32 SubObjectsCount = 0;
		MemAr << SubObjectsCount;

		// Create empty sub-objects based on class and patch
		TArray< UObject* > SubObjectsArray;
		SubObjectsArray.Reserve(SubObjectsCount);

		for(int32 Index = 0; Index < SubObjectsCount; ++Index)
		{
			FString ClassName;
			MemAr << ClassName;

			UClass* SubObjectClass = FindObject<UClass>(ANY_PACKAGE, *ClassName);
			check( SubObjectClass );

			int32 ObjectFlags;
			MemAr << ObjectFlags;

			UObject* SubObject = NewObject<UObject>( Object, SubObjectClass, NAME_None, EObjectFlags(ObjectFlags) );
			SubObjectsArray.Add( SubObject );

			RemoveDefaultDependencies( SubObject );
		}

		// Restore sub-objects' outer if original outer differs from Object
		// Restoration is done in the order the serialization was done: reverse order
		for(int32 Index = SubObjectsArray.Num() - 1; Index >= 0; --Index)
		{
			FString SoftPathString;
			MemAr << SoftPathString;

			FString SubObjectName;
			MemAr << SubObjectName;

			const FSoftObjectPath SoftPath( SoftPathString );

			UObject* NewOuter = SoftPath.ResolveObject();
			ensure( NewOuter );

			UObject* SubObject = SubObjectsArray[Index];
			if( NewOuter != SubObject->GetOuter() )
			{
				FDataprepCoreUtils::RenameObject( SubObject, *SubObjectName, NewOuter );
			}
		}

		// Deserialize sub-objects
		for(UObject* SubObject : SubObjectsArray)
		{
			SubObject->Serialize(Ar);
		}

		// Deserialize object
		Object->Serialize(Ar);

		if(UTexture* Texture = Cast<UTexture>(Object))
		{
			bool bRebuildResource = false;
			Ar << bRebuildResource;

			if(bRebuildResource)
			{
				Texture->UpdateResource();
			}
		}
	}
}

class FDataprepExportObjectInnerContext : public FExportObjectInnerContext
{
public:
	FDataprepExportObjectInnerContext( UWorld* World)
		//call the empty version of the base class
		: FExportObjectInnerContext(false)
	{
		// For each object . . .
		for ( TObjectIterator<UObject> It ; It ; ++It )
		{
			UObject* InnerObj = *It;
			UObject* OuterObj = InnerObj->GetOuter();

			// By default assume object does not need to be copied
			bool bObjectMustBeCopied = false;

			UObject* TestParent = OuterObj;
			while (TestParent)
			{
				AActor* TestParentAsActor = Cast<AActor>(TestParent);

				const bool bIsValidActor = TestParentAsActor &&
					TestParentAsActor->GetWorld() == World &&
					!TestParentAsActor->IsPendingKill() &&
					TestParentAsActor->IsEditable() &&
					!TestParentAsActor->IsTemplate() &&
					!FActorEditorUtils::IsABuilderBrush(TestParentAsActor) &&
					!TestParentAsActor->IsA(AWorldSettings::StaticClass());

				if ( bIsValidActor )
				{
					// Select actor so it will be processed during the copy
					if(SelectedActors.Find(TestParentAsActor) == nullptr)
					{
						SelectedActors.Add(TestParentAsActor);
						GSelectedActorAnnotation.Set(TestParentAsActor);
					}

					bObjectMustBeCopied = true;
					break;
				}

				TestParent = TestParent->GetOuter();
			}

			if (bObjectMustBeCopied)
			{
				InnerList* Inners = ObjectToInnerMap.Find( OuterObj );
				if ( Inners )
				{
					// Add object to existing inner list.
					Inners->Add( InnerObj );
				}
				else
				{
					// Create a new inner list for the outer object.
					InnerList& InnersForOuterObject = ObjectToInnerMap.Add( OuterObj, InnerList() );
					InnersForOuterObject.Add( InnerObj );
				}
			}
		}
	}

	~FDataprepExportObjectInnerContext()
	{
		// Deselect all actors we processed
		for(AActor* SelectedActor : SelectedActors)
		{
			GSelectedActorAnnotation.Clear(SelectedActor);
		}
	}

	/** Set of actors marked as selected so they get included in the copy */
	TSet<AActor*> SelectedActors;
};

void FDataprepEditor::TakeSnapshot()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::TakeSnapshot);

	uint64 StartTime = FPlatformTime::Cycles64();
	UE_LOG( LogDataprepEditor, Verbose, TEXT("Taking snapshot...") );

	FScopedSlowTask SlowTask( 100.0f, LOCTEXT("SaveSnapshot_Title", "Creating snapshot of world content ...") );
	SlowTask.MakeDialog(false);

	// Clean up temporary folder with content of previous snapshot(s)
	SlowTask.EnterProgressFrame( 10.0f, LOCTEXT("SaveSnapshot_Cleanup", "Snapshot : Cleaning previous content ...") );
	{
		DataprepSnapshotUtil::RemoveSnapshotFiles( TempDir );
		ContentSnapshot.DataEntries.Empty( Assets.Num() );
		SnapshotClassesMap.Reset();
	}

	// Sort assets to serialize and deserialize them according to their dependency
	// Texture first, then Material, then ...
	// Note: This classification must be updated as type of assets are added
	auto GetAssetClassEnum = [&](const UClass* AssetClass)
	{
		if (AssetClass->IsChildOf(UStaticMesh::StaticClass()))
		{
			return EDataprepAssetClass::EStaticMesh;
		}
		else if (AssetClass->IsChildOf(UMaterialFunction::StaticClass()))
		{
			return EDataprepAssetClass::EMaterialFunction;
		}
		else if (AssetClass->IsChildOf(UMaterialFunctionInstance::StaticClass()))
		{
			return EDataprepAssetClass::EMaterialFunctionInstance;
		}
		else if (AssetClass->IsChildOf(UMaterial::StaticClass()))
		{
			return EDataprepAssetClass::EMaterial;
		}
		else if (AssetClass->IsChildOf(UMaterialInstance::StaticClass()))
		{
			return EDataprepAssetClass::EMaterialInstance;
		}
		else if (AssetClass->IsChildOf(UTexture::StaticClass()))
		{
			return EDataprepAssetClass::ETexture;
		}

		return EDataprepAssetClass::EOther;
	};

	Assets.Sort([&](const TWeakObjectPtr<UObject>& A, const TWeakObjectPtr<UObject>& B)
	{
		EDataprepAssetClass AValue = A.IsValid() ? GetAssetClassEnum(A->GetClass()) : EDataprepAssetClass::EMaxClasses;
		EDataprepAssetClass BValue = B.IsValid() ? GetAssetClassEnum(B->GetClass()) : EDataprepAssetClass::EMaxClasses;
		return AValue < BValue;
	});

	// Cache the asset's path, class and flags
	for( const TWeakObjectPtr<UObject>& AssetPtr : Assets )
	{
		if(UObject* AssetObject = AssetPtr.Get())
		{
			FSoftObjectPath AssetPath( AssetObject );
			ContentSnapshot.DataEntries.Emplace(AssetPath.GetAssetPathString(), AssetObject->GetClass(), AssetObject->GetFlags());
		}
	}

	TAtomic<bool> bGlobalIsValid(true);
	{
		FText Message = LOCTEXT("SaveSnapshot_SaveAssets", "Snapshot : Caching assets ...");
		SlowTask.EnterProgressFrame( 40.0f, Message );

		FScopedSlowTask SlowSaveAssetTask( (float)Assets.Num(), Message );
		SlowSaveAssetTask.MakeDialog(false);

		TArray<TFuture<bool>> AsyncTasks;
		AsyncTasks.Reserve( Assets.Num() );

		for (TWeakObjectPtr<UObject> AssetObjectPtr : Assets)
		{
			AsyncTasks.Emplace(
				Async(
					EAsyncExecution::LargeThreadPool,
					[this, AssetObjectPtr, &bGlobalIsValid]()
					{
						if(UObject* AssetObject = AssetObjectPtr.Get())
						{
							if ( bGlobalIsValid.Load(EMemoryOrder::Relaxed) )
							{
								EObjectFlags ObjectFlags = AssetObject->GetFlags();
								AssetObject->ClearFlags(RF_Transient);
								AssetObject->SetFlags(RF_Public);

								FSoftObjectPath AssetPath( AssetObject );
								const FString AssetPathString = AssetPath.GetAssetPathString();
								UE_LOG( LogDataprepEditor, Verbose, TEXT("Saving asset %s"), *AssetPathString );

								bool bLocalIsValid = false;

								// Serialize asset
								{
									TArray<uint8> SerializedData;
									DataprepSnapshotUtil::WriteSnapshotData( AssetObject, SerializedData );

									FString AssetFilePath = DataprepSnapshotUtil::BuildAssetFileName( this->TempDir, AssetPathString );

									bLocalIsValid = FFileHelper::SaveArrayToFile( SerializedData, *AssetFilePath );
								}

								AssetObject->ClearFlags( RF_AllFlags );
								AssetObject->SetFlags( ObjectFlags );

								return bLocalIsValid;
							}
							else
							{
								return false;
							}
						}

						return true;
					}
				)
			);
		}

		for (int32 Index = 0; Index < AsyncTasks.Num(); ++Index)
		{
			if(UObject* AssetObject = Assets[Index].Get())
			{
				SlowSaveAssetTask.EnterProgressFrame();

				const FSoftObjectPath AssetPath( AssetObject );
				const FString AssetPathString = AssetPath.GetAssetPathString();

				// Wait the result of the async task
				if(!AsyncTasks[Index].Get())
				{
					UE_LOG( LogDataprepEditor, Log, TEXT("Failed to save %s"), *AssetPathString );

					bGlobalIsValid = false;
					break;
				}
				else
				{
					UE_LOG( LogDataprepEditor, Verbose, TEXT("Asset %s successfully saved"), *AssetPathString );
				}
			}
		}
	}

	ContentSnapshot.bIsValid = bGlobalIsValid;

	// Serialize world if applicable
	if(ContentSnapshot.bIsValid)
	{
		FText Message = LOCTEXT("SaveSnapshot_World", "Snapshot : caching level ...");
		SlowTask.EnterProgressFrame( 50.0f, Message );
		UE_LOG( LogDataprepEditor, Verbose, TEXT("Saving preview world") );

		FScopedSlowTask SlowSaveAssetTask( (float)PreviewWorld->GetCurrentLevel()->Actors.Num(), Message );
		SlowSaveAssetTask.MakeDialog(false);

		PreviewWorld->ClearFlags(RF_Transient);
		{
			// Code inspired from UUnrealEdEngine::edactCopySelected
			FStringOutputDevice Ar;
			uint32 ExportFlags = PPF_DeepCompareInstances | PPF_ExportsNotFullyQualified | PPF_IncludeTransient;
			const FDataprepExportObjectInnerContext Context( PreviewWorld.Get() );
			UExporter::ExportToOutputDevice( &Context, PreviewWorld.Get(), nullptr, Ar, TEXT("copy"), 0, ExportFlags);

			// Save text into file
			FString PackageFilePath = DataprepSnapshotUtil::BuildAssetFileName( TempDir, GetTransientContentFolder() / SessionID ) + TEXT(".asc");
			ContentSnapshot.bIsValid &= FFileHelper::SaveStringToFile( Ar, *PackageFilePath );
		}
		PreviewWorld->SetFlags(RF_Transient);

		if(ContentSnapshot.bIsValid)
		{
			UE_LOG( LogDataprepEditor, Verbose, TEXT("Level successfully saved") );
		}
		else
		{
			UE_LOG( LogDataprepEditor, Warning, TEXT("Failed to save level") );
		}
	}

	if(!ContentSnapshot.bIsValid)
	{
		DataprepSnapshotUtil::RemoveSnapshotFiles( TempDir );
		ContentSnapshot.DataEntries.Empty();
		return;
	}

	ContentSnapshot.DataEntries.Sort([&](const FSnapshotDataEntry& A, const FSnapshotDataEntry& B)
	{
		return GetAssetClassEnum( A.Get<1>() ) < GetAssetClassEnum( B.Get<1>() );
	});

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG( LogDataprepEditor, Verbose, TEXT("Snapshot taken in [%d min %.3f s]"), ElapsedMin, ElapsedSeconds );
}

void FDataprepEditor::RestoreFromSnapshot(bool bUpdateViewport)
{
	// Snapshot is not usable, rebuild the world from the producers
	if ( !ContentSnapshot.bIsValid )
	{
		UE_LOG( LogDataprepEditor, Log, TEXT("Snapshot is invalid. Running the producers...") );
		OnBuildWorld();
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::RestoreFromSnapshot);

	// Clean up all assets and world content
	{
		CleanPreviewWorld();
		Assets.Reset(ContentSnapshot.DataEntries.Num());
	}

	FScopedSlowTask SlowTask( 100.0f, LOCTEXT("RestoreFromSnapshot_Title", "Restoring world initial content ...") );
	SlowTask.MakeDialog(false);

	uint64 StartTime = FPlatformTime::Cycles64();
	UE_LOG( LogDataprepEditor, Verbose, TEXT("Restoring snapshot...") );

	TMap<FString, UPackage*> PackagesCreated;
	PackagesCreated.Reserve(ContentSnapshot.DataEntries.Num());

	UPackage* RootPackage = NewObject< UPackage >( nullptr, *GetTransientContentFolder(), RF_Transient );
	RootPackage->FullyLoad();

	uint32 PortFlags = PPF_DeepCompareInstances | PPF_ExportsNotFullyQualified | PPF_IncludeTransient | PPF_Copy;
	TArray<UObject*> ObjectsToDelete;

	SlowTask.EnterProgressFrame( 40.0f, LOCTEXT("RestoreFromSnapshot_Assets", "Restoring assets ...") );
	{
		TArray<UMaterialInterface*> MaterialInterfaces;

		FScopedSlowTask SubSlowTask( ContentSnapshot.DataEntries.Num(), LOCTEXT("RestoreFromSnapshot_Assets", "Restoring assets ...") );
		SubSlowTask.MakeDialog(false);

		for (FSnapshotDataEntry& DataEntry : ContentSnapshot.DataEntries)
		{
			const FSoftObjectPath ObjectPath( DataEntry.Get<0>() );
			const FString PackageToLoadPath = ObjectPath.GetLongPackageName();
			const FString AssetName = ObjectPath.GetAssetName();

			SubSlowTask.EnterProgressFrame( 1.0f, FText::Format( LOCTEXT("RestoreFromSnapshot_OneAsset", "Restoring asset {0}"), FText::FromString( ObjectPath.GetAssetName() ) ) );
			UE_LOG( LogDataprepEditor, Verbose, TEXT("Loading asset %s"), *ObjectPath.GetAssetPathString() );

			if(PackagesCreated.Find(PackageToLoadPath) == nullptr)
			{
				UPackage* PackageCreated = NewObject< UPackage >( nullptr, *PackageToLoadPath, RF_Transient );
				PackageCreated->FullyLoad();
				PackageCreated->MarkPackageDirty();

				PackagesCreated.Add( PackageToLoadPath, PackageCreated );
			}

			// Duplicate sub-objects to delete after all assets are read
			UPackage* Package = PackagesCreated[PackageToLoadPath];
			UObject* Asset = NewObject<UObject>( Package, DataEntry.Get<1>(), *AssetName, DataEntry.Get<2>() );

			{
				TArray<uint8> SerializedData;
				FString AssetFilePath = DataprepSnapshotUtil::BuildAssetFileName( TempDir, ObjectPath.GetAssetPathString() );
				if(FFileHelper::LoadFileToArray(SerializedData, *AssetFilePath))
				{
					DataprepSnapshotUtil::ReadSnapshotData( Asset, SerializedData, SnapshotClassesMap, ObjectsToDelete );
				}
				else
				{
					UE_LOG( LogDataprepEditor, Error, TEXT("Failed to restore asset %s"), *ObjectPath.GetAssetPathString() );
				}
			}

			if(UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Asset))
			{
				{
					FMaterialUpdateContext MaterialUpdateContext;

					MaterialUpdateContext.AddMaterialInterface( MaterialInterface );

					MaterialInterface->PreEditChange(nullptr);
					MaterialInterface->PostEditChange();
				}
			}

			Assets.Add( Asset );

			UE_LOG( LogDataprepEditor, Verbose, TEXT("Asset %s loaded"), *ObjectPath.GetAssetPathString() );
		}

		FDataprepCoreUtils::PurgeObjects( MoveTemp( ObjectsToDelete ) );
	}

	// Make sure all assets have RF_Public flag set so the actors in the level can find the assets they are referring to
	// Cache boolean representing if RF_Public flag was set or not on asset
	TArray<bool> AssetFlags;
	AssetFlags.AddDefaulted( Assets.Num() );

	for(int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if( UObject* Asset = Assets[Index].Get() )
		{
			AssetFlags[Index] = bool(Asset->GetFlags() & RF_Public);
			Asset->SetFlags( RF_Public );
		}
	}

	SlowTask.EnterProgressFrame( 60.0f, LOCTEXT("RestoreFromSnapshot_Level", "Restoring level ...") );
	{
		// Code inspired from UUnrealEdEngine::edactPasteSelected
		ULevel* WorldLevel = PreviewWorld->GetCurrentLevel();

		FString PackageFilePath = DataprepSnapshotUtil::BuildAssetFileName( TempDir, GetTransientContentFolder() / SessionID ) + TEXT(".asc");
		const bool bBSPAutoUpdate = GetDefault<ULevelEditorMiscSettings>()->bBSPAutoUpdate;
		GetMutableDefault<ULevelEditorMiscSettings>()->bBSPAutoUpdate = false;

		// Load the text file to a string
		FString FileBuffer;
		check( FFileHelper::LoadFileToString(FileBuffer, *PackageFilePath) );

		// Set the GWorld to the preview world since ULevelFactory::FactoryCreateText uses GWorld
		UWorld* PrevGWorld = GWorld;
		GWorld = PreviewWorld.Get();

		// Cache and disable recording of transaction
		TGuardValue<UTransactor*> NormalTransactor( GEditor->Trans, nullptr );

		// Cache and disable warnings from LogExec because ULevelFactory::FactoryCreateText is pretty verbose on harmless warnings
		ELogVerbosity::Type PrevLogExecVerbosity = LogExec.GetVerbosity();
		LogExec.SetVerbosity( ELogVerbosity::Error );

		// Cache and disable Editor's selection
		TGuardValue<bool> EdSelectionLock( GEdSelectionLock, true );

		const TCHAR* Paste = *FileBuffer;
		ULevelFactory* Factory = NewObject<ULevelFactory>();
		Factory->FactoryCreateText( ULevel::StaticClass(), WorldLevel, WorldLevel->GetFName(), RF_Transactional, NULL, TEXT("paste"), Paste, Paste + FileBuffer.Len(), GWarn );

		// Restore LogExec verbosity
		LogExec.SetVerbosity( PrevLogExecVerbosity );

		// Reinstate old BSP update setting, and force a rebuild - any levels whose geometry has changed while pasting will be rebuilt
		GetMutableDefault<ULevelEditorMiscSettings>()->bBSPAutoUpdate = bBSPAutoUpdate;

		// Restore GWorld
		GWorld = PrevGWorld;
	}
	UE_LOG( LogDataprepEditor, Verbose, TEXT("Level loaded") );

	// Restore RF_Public on each asset
	for(int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if( UObject* Asset = Assets[Index].Get() )
		{
			if( !AssetFlags[Index] )
			{
				Asset->ClearFlags( RF_Public );
			}
		}
	}

	{
		TSharedPtr< IDataprepProgressReporter > ProgressReporter( new FDataprepCoreUtils::FDataprepProgressUIReporter() );
		FDataprepCoreUtils::BuildAssets( Assets, ProgressReporter );
	}

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG( LogDataprepEditor, Verbose, TEXT("Preview world restored in [%d min %.3f s]"), ElapsedMin, ElapsedSeconds );

	// Update preview panels to reflect restored content
	UpdatePreviewPanels( bUpdateViewport );
}

#undef LOCTEXT_NAMESPACE
