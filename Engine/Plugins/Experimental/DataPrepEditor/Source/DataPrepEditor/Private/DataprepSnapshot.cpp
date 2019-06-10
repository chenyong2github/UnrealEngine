// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepEditor.h"

#include "DataprepEditorLogCategory.h"

#include "ActorEditorUtils.h"
#include "AutoReimport/AutoReimportManager.h"
#include "Async/ParallelFor.h"
#include "Editor/UnrealEdEngine.h"
#include "EngineUtils.h"
#include "Exporters/Exporter.h"
#include "Factories/LevelFactory.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformOutputDevices.h"
#include "Materials/MaterialInstance.h"
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
const bool bUseSnapshot = false;

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

	struct FDataprepSnapshotParser : public FArchiveUObject
	{
		virtual FArchive& operator<<(UObject*& Obj) override
		{
			if(Obj != nullptr)
			{
				if( Obj->IsA<UPackage>() || Obj->HasAnyFlags(RF_Public) )
				{
					return FArchiveUObject::operator<<(Obj);
				}
				else if(SerializedObjects.Find(Obj) == nullptr)
				{
					SerializedObjects.Add(Obj);
				}
			}

			return *this;
		}

		TSet<UObject*> SerializedObjects;
	};

	void WriteSnapshotData(UObject* Object, TArray<uint8>& OutSerializedData, TMap<FString, UClass*>& OutClassesMap)
	{
		FMemoryWriter MemAr(OutSerializedData);
		FObjectAndNameAsStringProxyArchive Ar(MemAr, false);
		Ar.SetIsTransacting(true);

		// Collect sub-objects depending on input object including nested objects
		TArray< UObject* > SubObjectsArray;
		GetObjectsWithOuter( Object, SubObjectsArray, /*bIncludeNestedObjects = */ true );

		{
			TMap< UObject*, TSet<UObject*> > SubObjectDependencyGraph;
			SubObjectDependencyGraph.Reserve( SubObjectsArray.Num() );

			for(UObject* SubObject : SubObjectsArray)
			{
				SubObjectDependencyGraph.Add( SubObject );
			}

			for(UObject* SubObject : SubObjectsArray)
			{
				FDataprepSnapshotParser Parser;
				SubObject->Serialize(Parser);

				for(UObject* SubObjectDependence : Parser.SerializedObjects)
				{
					if(SubObjectDependence != SubObject->GetOuter())
					{
						SubObjectDependencyGraph[SubObject].Add( SubObjectDependence );
					}
				}
			}

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

		int32 Count = SubObjectsArray.Num();
		MemAr << Count;

		for(UObject* SubObject : SubObjectsArray)
		{
			UClass* SubObjectClass = SubObject->GetClass();

			FString ClassName = SubObjectClass->GetName();
			MemAr << ClassName;

			OutClassesMap.Add(ClassName, SubObjectClass);

			FString ObjectPath = SubObject->GetPathName();
			MemAr << ObjectPath;
		}

		for(UObject* SubObject : SubObjectsArray)
		{
			SubObject->Serialize(Ar);
		}

		Object->Serialize(Ar);
	}

	void ReadSnapshotData(UObject* Object, const TArray<uint8>& InSerializedData, TMap<FString, UClass*>& InClassesMap)
	{
		FMemoryReader MemAr(InSerializedData);
		FObjectAndNameAsStringProxyArchive Ar(MemAr, false);
		Ar.SetIsTransacting(true);

		int32 Count = 0;
		MemAr << Count;

		TArray< UObject* > SubObjectsArray;
		SubObjectsArray.Reserve(Count);

		for(int32 Index = 0; Index < Count; ++Index)
		{
			FString ClassName;
			MemAr << ClassName;

			UClass** SubObjectClassPtr = InClassesMap.Find(ClassName);
			check( SubObjectClassPtr );

			FString StrObjectPath;
			MemAr << StrObjectPath;

			UObject* SubObject = NewObject<UObject>( Object, *SubObjectClassPtr, NAME_None, RF_Transient );
			SubObjectsArray.Add( SubObject );
		}

		for(UObject* SubObject : SubObjectsArray)
		{
			SubObject->Serialize(Ar);
		}

		Object->Serialize(Ar);

		SubObjectsArray.Empty();
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

	UE_LOG( LogDataprepEditor, Log, TEXT("Restoring snapshot...") );

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

			UE_LOG( LogDataprepEditor, Log, TEXT("Saving asset %s"), *AssetPath.GetAssetPathString() );

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

			UE_LOG( LogDataprepEditor, Log, TEXT("Asset %s successfully saved"), *AssetPath.GetAssetPathString() );
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
			FString PackageFilePath = DataprepSnapshotUtil::BuildAssetFileName( TempDir, GetTransientContentFolder() / SessionID );
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

	UE_LOG( LogDataprepEditor, Log, TEXT("Restoring snapshot...") );

	// Clean up all assets and world content
	{
		CleanPreviewWorld();
		Assets.Reset(ContentSnapshot.DataEntries.Num());
	}

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

		UE_LOG( LogDataprepEditor, Log, TEXT("Loading asset %s"), *ObjectPath.GetAssetPathString() );

		if(PackagesCreated.Find(PackageToLoadPath) == nullptr)
		{
			UPackage* PackageCreated = NewObject< UPackage >( nullptr, *PackageToLoadPath, RF_Transient );
			PackageCreated->FullyLoad();
			PackageCreated->MarkPackageDirty();

			PackagesCreated.Add( PackageToLoadPath, PackageCreated );
		}

		UPackage* Package = PackagesCreated[PackageToLoadPath];
		UObject* Asset = NewObject<UObject>( Package, DataEntry.Get<1>(), *AssetName, DataEntry.Get<2>() );

		{
			TArray<uint8> SerializedData;
			FString AssetFilePath = DataprepSnapshotUtil::BuildAssetFileName( TempDir, ObjectPath.GetAssetPathString() );
			FFileHelper::LoadFileToArray( SerializedData, *AssetFilePath );

			DataprepSnapshotUtil::ReadSnapshotData( Asset, SerializedData, SnapshotClassesMap );
		}

		Assets.Add( Asset );

		UE_LOG( LogDataprepEditor, Log, TEXT("Asset %s loaded"), *ObjectPath.GetAssetPathString() );
	}

	UE_LOG( LogDataprepEditor, Log, TEXT("Loading level") );
	{
		// Code inspired from UUnrealEdEngine::edactPasteSelected
		ULevel* WorldLevel = PreviewWorld->GetCurrentLevel();

		FString PackageFilePath = DataprepSnapshotUtil::BuildAssetFileName( TempDir, GetTransientContentFolder() / SessionID );
		const bool bBSPAutoUpdate = GetDefault<ULevelEditorMiscSettings>()->bBSPAutoUpdate;
		GetMutableDefault<ULevelEditorMiscSettings>()->bBSPAutoUpdate = false;

		// Load the text file to a string
		FString FileBuffer;
		check( FFileHelper::LoadFileToString(FileBuffer, *PackageFilePath) );

		// Set the GWorl to the preview world since ULevelFactory::FactoryCreateText uses GWorld
		UWorld* CachedWorld = GWorld;
		GWorld = PreviewWorld.Get();

		const TCHAR* Paste = *FileBuffer;
		ULevelFactory* Factory = NewObject<ULevelFactory>();
		Factory->FactoryCreateText( ULevel::StaticClass(), WorldLevel, WorldLevel->GetFName(), RF_Transactional, NULL, TEXT("paste"), Paste, Paste + FileBuffer.Len(), FGenericPlatformOutputDevices::GetFeedbackContext());

		// Reinstate old BSP update setting, and force a rebuild - any levels whose geometry has changed while pasting will be rebuilt
		GetMutableDefault<ULevelEditorMiscSettings>()->bBSPAutoUpdate = bBSPAutoUpdate;

		// Reset the GWorld to its previous value
		GWorld = CachedWorld;
	}
	UE_LOG( LogDataprepEditor, Log, TEXT("Level loaded") );
}

#undef LOCTEXT_NAMESPACE
