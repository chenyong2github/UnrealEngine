// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepEditor.h"

#include "DataprepCoreUtils.h"
#include "DataprepEditorLogCategory.h"

#include "ActorEditorUtils.h"
#include "AutoReimport/AutoReimportManager.h"
#include "Async/ParallelFor.h"
#include "Editor/UnrealEdEngine.h"
#include "EngineUtils.h"
#include "Engine/Texture.h"
#include "Exporters/Exporter.h"
#include "Factories/LevelFactory.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformOutputDevices.h"
#include "GenericPlatform/GenericPlatformTime.h"
#include "Materials/MaterialInstance.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
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
	EMaterial,
	EMaterialInstance,
	EStaticMesh,
	EOther,
	EMaxClasses
};

// #ueent_todo: Boolean driving activating actual snapshot based logic
const bool bUseSnapshot = true;
const bool bUseCompression = true;

namespace DataprepSnapshotUtil
{
	const TCHAR* SnapshotExtension = TEXT(".dpc");

	void RemoveSnapshotFiles(const FString& RootDir)
	{
		TArray<FString> FileNames;
		IFileManager::Get().FindFiles( FileNames, *RootDir, SnapshotExtension );
		for ( FString& FileName : FileNames )
		{
			IFileManager::Get().Delete( *FPaths::Combine( RootDir, FileName ), false );
		}

	}

	// #ueent_todo: Find a solution using the path of the RootPackage instead of the UPackage object itself
	FString BuildAssetFileName(const FString& RootPath, const FString& AssetPath )
	{
		static FString FileNamePrefix( TEXT("stream_") );

		FString PackageFileName = FileNamePrefix + FString::Printf( TEXT("%08x"), ::GetTypeHash( AssetPath ) );
		return FPaths::ConvertRelativePathToFull( FPaths::Combine( RootPath, PackageFileName ) + SnapshotExtension );
	}

	void WriteSnapshotData(UObject* Object, TArray<uint8>& OutSerializedData, TMap<FString, UClass*>& OutClassesMap)
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

		TArray<uint8> MemoryBuffer;
		FMemoryWriter MemAr(MemoryBuffer);
		FObjectAndNameAsStringProxyArchive Ar(MemAr, false);
		Ar.SetIsTransacting(true);

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
			// #ueent_todo: Improve performance of building. Current is pretty brute force
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

			OutClassesMap.Add(ClassName, SubObjectClass);
		}

		// Serialize sub-objects' outer path
		// Done in reverse order since an object can be the outer of the object
		// it depends on. Not the opposite
		for(int32 Index = SubObjectsArray.Num() - 1; Index >= 0; --Index)
		{
			FSoftObjectPath SoftPath( SubObjectsArray[Index]->GetOuter() );

			FString SoftPathString = SoftPath.ToString();
			MemAr << SoftPathString;
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

		if(bUseCompression)
		{
			const int32 BufferHeaderSize = (int32)sizeof(int32);

			// allocate all of the input space for the output, with extra space for max overhead of zlib (when compressed > uncompressed)
			int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, MemoryBuffer.Num());
			OutSerializedData.SetNum( CompressedSize + BufferHeaderSize );

			// Store the size of the uncompressed buffer
			((int32*)OutSerializedData.GetData())[0] = MemoryBuffer.Num();

			// compress the data		
			bool bSucceeded = FCompression::CompressMemory(NAME_Zlib, OutSerializedData.GetData() + BufferHeaderSize, CompressedSize, MemoryBuffer.GetData(), MemoryBuffer.Num());

			// if it failed send the data uncompressed, which we mark by setting compressed size to 0
			checkf(bSucceeded, TEXT("zlib failed to compress, which is very unexpected"));

			OutSerializedData.SetNum( CompressedSize + BufferHeaderSize );
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
				ObjectWithOuter->Rename( nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional );
				ObjectsToDelete.Add( ObjectWithOuter );
			}
		};

		TArray<uint8> MemoryBuffer;
		if(bUseCompression)
		{
			const int32 BufferHeaderSize = (int32)sizeof(int32);

			// Allocate the space required for the uncompressed data
			int32 UncompressedSize = ((int32*)InSerializedData.GetData())[0];
			MemoryBuffer.SetNum(UncompressedSize);

			// uncompress the data		
			bool bSucceeded = FCompression::UncompressMemory(NAME_Zlib, MemoryBuffer.GetData(), MemoryBuffer.Num(), InSerializedData.GetData() + BufferHeaderSize, InSerializedData.Num() - BufferHeaderSize);

			// if it failed send the data uncompressed, which we mark by setting compressed size to 0
			checkf(bSucceeded, TEXT("zlib failed to uncompress, which is very unexpected"));
		}

		RemoveDefaultDependencies( Object );

		FMemoryReader MemAr(bUseCompression ? MemoryBuffer : InSerializedData);
		FObjectAndNameAsStringProxyArchive Ar(MemAr, false);
		Ar.SetIsTransacting(true);

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

			UClass** SubObjectClassPtr = InClassesMap.Find(ClassName);
			check( SubObjectClassPtr );

			UObject* SubObject = NewObject<UObject>( Object, *SubObjectClassPtr, NAME_None, RF_Transient );
			SubObjectsArray.Add( SubObject );

			RemoveDefaultDependencies( SubObject );
		}

		// Restore sub-objects' outer if original outer differs from Object
		// Restoration is done in the order the serialization was done: reverse order
		for(int32 Index = SubObjectsArray.Num() - 1; Index >= 0; --Index)
		{
			FString SoftPathString;
			MemAr << SoftPathString;

			const FSoftObjectPath SoftPath( SoftPathString );

			UObject* NewOuter = SoftPath.ResolveObject();
			ensure( NewOuter );

			UObject* SubObject = SubObjectsArray[Index];
			if( NewOuter != SubObject->GetOuter() )
			{
				SubObject->Rename( nullptr, NewOuter, REN_DontCreateRedirectors | REN_NonTransactional );
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
	if(!bUseSnapshot)
	{
		return;
	}

	uint64 StartTime = FPlatformTime::Cycles64();
	UE_LOG( LogDataprepEditor, Log, TEXT("Taking snapshot...") );

	// Clean up temporary folder with content of previous snapshot(s)
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

	// Cache assets' flags
	typedef TPair< UObject*, EObjectFlags > FAssetFlagsCache;
	TArray<FAssetFlagsCache> FlagsCacheArray;
	FlagsCacheArray.Reserve( Assets.Num() );

	UObject* FaultyObject = nullptr;
	ContentSnapshot.bIsValid = true;

	for (const TWeakObjectPtr<UObject>& Asset : Assets)
	{
		if (Asset.IsValid())
		{
			UObject* AssetObject = Asset.Get();

			FlagsCacheArray.Emplace(AssetObject, AssetObject->GetFlags());
			AssetObject->ClearFlags(RF_Transient);
			AssetObject->SetFlags(RF_Public);

			FSoftObjectPath AssetPath( AssetObject );
			ContentSnapshot.DataEntries.Emplace( AssetPath.GetAssetPathString(), AssetObject->GetClass(), AssetObject->GetFlags() );

			UE_LOG( LogDataprepEditor, Verbose, TEXT("Saving asset %s"), *AssetPath.GetAssetPathString() );

			// Serialize asset
			{
				TArray<uint8> SerializedData;
				DataprepSnapshotUtil::WriteSnapshotData( AssetObject, SerializedData, SnapshotClassesMap);

				FString AssetFilePath = DataprepSnapshotUtil::BuildAssetFileName( TempDir, AssetPath.GetAssetPathString() );
				ContentSnapshot.bIsValid &= FFileHelper::SaveArrayToFile( SerializedData, *AssetFilePath );
			}

			if(!ContentSnapshot.bIsValid)
			{
				UE_LOG( LogDataprepEditor, Log, TEXT("Failed to save %s"), *AssetPath.GetAssetPathString() );
				FaultyObject = AssetObject;
				break;
			}

			UE_LOG( LogDataprepEditor, Verbose, TEXT("Asset %s successfully saved"), *AssetPath.GetAssetPathString() );
		}
	}

	// Serialize world if applicable
	if(ContentSnapshot.bIsValid)
	{
		UE_LOG( LogDataprepEditor, Log, TEXT("Saving preview world") );

		PreviewWorld->ClearFlags(RF_Transient);
		{
			// Code inspired from UUnrealEdEngine::edactCopySelected
			FStringOutputDevice Ar;
			uint32 ExportFlags = PPF_DeepCompareInstances | PPF_ExportsNotFullyQualified | PPF_IncludeTransient;
			const FDataprepExportObjectInnerContext Context( PreviewWorld.Get() );
			UExporter::ExportToOutputDevice( &Context, PreviewWorld.Get(), NULL, Ar, TEXT("copy"), 0, ExportFlags);

			// Save text into file
			FString PackageFilePath = DataprepSnapshotUtil::BuildAssetFileName( TempDir, GetTransientContentFolder() / SessionID ) + TEXT(".asc");
			ContentSnapshot.bIsValid &= FFileHelper::SaveStringToFile( Ar, *PackageFilePath );
		}
		PreviewWorld->SetFlags(RF_Transient);

		if(ContentSnapshot.bIsValid)
		{
			UE_LOG( LogDataprepEditor, Log, TEXT("Level successfully saved") );
		}
		else
		{
			UE_LOG( LogDataprepEditor, Log, TEXT("Failed to save level") );
		}
	}

	// Restore flags on assets
	for (FAssetFlagsCache& CacheEntry : FlagsCacheArray)
	{
		UObject* Object = CacheEntry.Get<0>();
		Object->ClearFlags( RF_AllFlags );
		Object->SetFlags( CacheEntry.Get<1>() );
	}

	if(!ContentSnapshot.bIsValid)
	{
		DataprepSnapshotUtil::RemoveSnapshotFiles( TempDir );
		ContentSnapshot.DataEntries.Empty();
		return;
	}

	// #ueent_todo: Is that necessary since Assets has already been sorted?
	ContentSnapshot.DataEntries.Sort([&](const FSnapshotDataEntry& A, const FSnapshotDataEntry& B)
	{
		return GetAssetClassEnum( A.Get<1>() ) < GetAssetClassEnum( B.Get<1>() );
	});

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG( LogDataprepEditor, Log, TEXT("Snapshot taken in [%d min %.3f s]"), ElapsedMin, ElapsedSeconds );
}

void FDataprepEditor::RestoreFromSnapshot()
{
	if(!bUseSnapshot)
	{
		OnBuildWorld();
		return;
	}

	// Snapshot is not usable, rebuild the world from the producers
	if ( !ContentSnapshot.bIsValid )
	{
		// #ueent_todo: Inform user that snapshot is no good and world is going to be rebuilt from scratch
		OnBuildWorld();
		return;
	}

	uint64 StartTime = FPlatformTime::Cycles64();
	UE_LOG( LogDataprepEditor, Log, TEXT("Cleaning up preview world ...") );

	// Clean up all assets and world content
	{
		CleanPreviewWorld();
		Assets.Reset(ContentSnapshot.DataEntries.Num());
	}

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG( LogDataprepEditor, Log, TEXT("Preview world cleaned in [%d min %.3f s]"), ElapsedMin, ElapsedSeconds );

	StartTime = FPlatformTime::Cycles64();
	UE_LOG( LogDataprepEditor, Log, TEXT("Restoring snapshot...") );

	TMap<FString, UPackage*> PackagesCreated;
	PackagesCreated.Reserve(ContentSnapshot.DataEntries.Num());

	UPackage* RootPackage = NewObject< UPackage >( nullptr, *GetTransientContentFolder(), RF_Transient );
	RootPackage->FullyLoad();

	uint32 PortFlags = PPF_DeepCompareInstances | PPF_ExportsNotFullyQualified | PPF_IncludeTransient | PPF_Copy;

	for (FSnapshotDataEntry& DataEntry : ContentSnapshot.DataEntries)
	{
		const FSoftObjectPath ObjectPath( DataEntry.Get<0>() );
		const FString PackageToLoadPath = ObjectPath.GetLongPackageName();
		const FString AssetName = ObjectPath.GetAssetName();

		UE_LOG( LogDataprepEditor, Verbose, TEXT("Loading asset %s"), *ObjectPath.GetAssetPathString() );

		if(PackagesCreated.Find(PackageToLoadPath) == nullptr)
		{
			UPackage* PackageCreated = NewObject< UPackage >( nullptr, *PackageToLoadPath, RF_Transient );
			PackageCreated->FullyLoad();
			PackageCreated->MarkPackageDirty();

			PackagesCreated.Add( PackageToLoadPath, PackageCreated );
		}

		// Duplicate sub-objects to delete after all assets are read
		TArray<UObject*> ObjectsToDelete;
		UPackage* Package = PackagesCreated[PackageToLoadPath];
		UObject* Asset = NewObject<UObject>( Package, DataEntry.Get<1>(), *AssetName, DataEntry.Get<2>() );

		{
			TArray<uint8> SerializedData;
			FString AssetFilePath = DataprepSnapshotUtil::BuildAssetFileName( TempDir, ObjectPath.GetAssetPathString() );
			FFileHelper::LoadFileToArray( SerializedData, *AssetFilePath );

			DataprepSnapshotUtil::ReadSnapshotData( Asset, SerializedData, SnapshotClassesMap, ObjectsToDelete );
		}

		FDataprepCoreUtils::PurgeObjects( MoveTemp( ObjectsToDelete ) );

		Assets.Add( Asset );

		UE_LOG( LogDataprepEditor, Verbose, TEXT("Asset %s loaded"), *ObjectPath.GetAssetPathString() );
	}

	UE_LOG( LogDataprepEditor, Log, TEXT("Loading level") );
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
		UWorld* CachedWorld = GWorld;
		GWorld = PreviewWorld.Get();

		// Disable warnings from LogExec because ULevelFactory::FactoryCreateText is pretty verbose on harmless warnings
		ELogVerbosity::Type PrevLogExecVerbosity = LogExec.GetVerbosity();
		LogExec.SetVerbosity( ELogVerbosity::Error );

		const TCHAR* Paste = *FileBuffer;
		ULevelFactory* Factory = NewObject<ULevelFactory>();
		Factory->FactoryCreateText( ULevel::StaticClass(), WorldLevel, WorldLevel->GetFName(), RF_Transactional, NULL, TEXT("paste"), Paste, Paste + FileBuffer.Len(), FGenericPlatformOutputDevices::GetFeedbackContext());

		// Restore LogExec verbosity
		LogExec.SetVerbosity( PrevLogExecVerbosity );

		// Reinstate old BSP update setting, and force a rebuild - any levels whose geometry has changed while pasting will be rebuilt
		GetMutableDefault<ULevelEditorMiscSettings>()->bBSPAutoUpdate = bBSPAutoUpdate;

		// Reset the GWorld to its previous value
		GWorld = CachedWorld;
	}
	UE_LOG( LogDataprepEditor, Log, TEXT("Level loaded") );

	// Log time spent to import incoming file in minutes and seconds
	ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG( LogDataprepEditor, Log, TEXT("Preview world restored in [%d min %.3f s]"), ElapsedMin, ElapsedSeconds );

	// Update preview panels to reflect restored content
	UpdatePreviewPanels();
}

#undef LOCTEXT_NAMESPACE
