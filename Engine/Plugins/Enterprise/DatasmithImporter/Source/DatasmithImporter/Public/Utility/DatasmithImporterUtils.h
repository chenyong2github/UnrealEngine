// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DatasmithImportContext.h"
#include "DatasmithScene.h"
#include "UObject/UObjectHash.h"

class ADatasmithSceneActor;
class IDatasmithScene;
class UDatasmithScene;
class UWorld;
class UPackage;
class UFunction;

DECLARE_LOG_CATEGORY_EXTERN( LogDatasmithImport, Log, All );

class DATASMITHIMPORTER_API FDatasmithImporterUtils
{
public:
	/** Loads an IDatasmithScene from a UDatasmithScene */
	static TSharedPtr< IDatasmithScene > LoadDatasmithScene( UDatasmithScene* DatasmithSceneAsset );

	/** Saves an IDatasmithScene into a UDatasmithScene */
	static void SaveDatasmithScene( TSharedRef< IDatasmithScene > DatasmithScene, UDatasmithScene* DatasmithSceneAsset );

	/** Spawns a ADatasmithSceneActor and adds it to the ImportContext */
	static ADatasmithSceneActor* CreateImportSceneActor( FDatasmithImportContext& ImportContext, FTransform WorldTransform );

	/**
	 * Finds all the ADatasmithSceneActor in the world that refers to the given scene
	 * @param World			    Scope of the search
	 * @param DatasmithScene    SceneActor must reference this scene to be included
	 */
	static TArray< ADatasmithSceneActor* > FindSceneActors( UWorld* World, UDatasmithScene* DatasmithScene );

	/**
	 * Delete non imported datasmith elements (actors and components) from a Datasmith Scene Actor hierarchy
	 *
	 * @param SourceSceneActor           The scene on which the actors where imported
	 * @param DestinationSceneActor      The scene on which the actors will be deleted
	 * @param IgnoredDatasmithElements   The Datasmith actors that shouldn't be deleted if they aren't imported
	 */
	static void DeleteNonImportedDatasmithElementFromSceneActor(ADatasmithSceneActor& SourceSceneActor, ADatasmithSceneActor& DestinationSceneActor, const TSet< FName >& IgnoredDatasmithElements);

	/**
	 * Delete an actor
	 * Remove it from the it's level, mark it pending kill and move it the transient package to avoid any potential name collision
	 * @param Actor The actor to delete
	 */
	static void DeleteActor( AActor& Actor );

	/**
	 * Finds a UStaticMesh, UTexture or UMaterialInterface.
	 * Relative paths are resolved based on the AssetsContext.
	 * Absolute paths are sent through FDatasmithImporterUtils::FindObject.
	 */
	template< typename ObjectType >
	static ObjectType* FindAsset( const FDatasmithAssetsImportContext& AssetsContext, const TCHAR* ObjectPathName );

	/**
	 * Find an object with a given name in a package
	 * Use FSoftObjectPath to perform the search
	 * Load the package /ParentPackage/ObjectName if it exists and is not in memory yet
	 *
	 * @param	ParentPackage				Parent package to look in
	 * @param	ObjectName					Name of object to look for
	 */
	template< class ObjectType >
	inline static ObjectType* FindObject( const UPackage* ParentPackage, const FString& ObjectName );

	/**
	 * Add a layer to the world if there is no other layer with the same name
	 *
	 * @param World			The world to which the layers will be added
	 * @param LayerNames	The name of the layers to be added
	 */
	static void AddUniqueLayersToWorld( UWorld* World, const TSet< FName >& LayerNames );

	/**
	 * @param Package			Package to create the asset in
	 * @param AssetName			The name of the asset to create
	 * @param OutFailReason		Text containing explanation about failure
	 * Given a package, a name and a class, check if an existing asset with a different class
	 * would not prevent the creation of such asset. A special report is done for object redirector
	 */
	template< class ObjectType >
	static bool CanCreateAsset(const UPackage* Package, const FString& AssetName, FText& OutFailReason);

	/**
	 * @param AssetPathName		Full path name of the asset to create
	 * @param OutFailReason		Text containing explanation about failure
	 * Returns true if the asset can be safely created
	 * Given a path and a class, check if an existing asset with a different class
	 * would not prevent the creation of such asset. A special report is done for object redirector
	 */
	template< class ObjectType >
	static bool CanCreateAsset(const FString& AssetPathName, FText& OutFailReason)
	{
		return CanCreateAsset( AssetPathName, ObjectType::StaticClass(), OutFailReason );
	}

	/**
	 * @param AssetPathName		Full path name of the asset to create
	 * @param AssetClass		Class of the asset to create
	 * @param OutFailReason		Text containing explanation about failure
	 * Returns true if the asset can be safely created
	 * Given a path and a class, check if an existing asset with a different class
	 * would not prevent the creation of such asset. A special report is done for object redirector
	 */
	static bool CanCreateAsset(const FString& AssetPathName, const UClass* AssetClass, FText& OutFailReason);

	/**
	 * Finds the UDatasmithScene for which the Asset belongs to.
	 */
	static UDatasmithScene* FindDatasmithSceneForAsset( UObject* Asset );

	static FName GetDatasmithElementId( UObject* Object );
	static FString GetDatasmithElementIdString( UObject* Object );

	/**
	 * Converts AActor objects into DatasmithActorElement objects and add them to a DatasmithScene
	 * @param SceneElement		DatasmithScene to populate
	 * @param RootActors		Array of root actors to convert and add to the DatasmithScene
	 */
	static void FillSceneElement( TSharedPtr< IDatasmithScene >& SceneElement, const TArray<AActor*>& RootActors );

	/**
	 * Finds all materials that are referenced by other materials in the scene and returns a list ordered
	 * by dependencies, making sure that materials referencing other materials in the list will come after.
	 *
	 * @param SceneElement		DatasmithScene holding the materials.
	 */
	static TArray< TSharedPtr< IDatasmithBaseMaterialElement > > GetOrderedListOfMaterialsReferencedByMaterials( TSharedPtr< IDatasmithScene >& SceneElement );
};

template< typename ObjectType >
struct FDatasmithFindAssetTypeHelper
{
};

template<>
struct FDatasmithFindAssetTypeHelper< UStaticMesh >
{
	static const TMap< TSharedRef< IDatasmithMeshElement >, UStaticMesh* >& GetImportedAssetsMap( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.ParentContext.ImportedStaticMeshes;
	}

	static UPackage* GetFinalPackage( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.StaticMeshesFinalPackage.Get();
	}

	static const TMap< FName, TSoftObjectPtr< UStaticMesh > >* GetAssetsMap( const UDatasmithScene* SceneAsset )
	{
		return SceneAsset ? &SceneAsset->StaticMeshes : nullptr;
	}

	static const TSharedRef<IDatasmithMeshElement>* GetImportedElementByName( const FDatasmithAssetsImportContext& AssetsContext, const TCHAR* ObjectPathName )
	{
		return AssetsContext.ParentContext.ImportedStaticMeshesByName.Find(ObjectPathName);
	}
};

template<>
struct FDatasmithFindAssetTypeHelper< UTexture >
{
	static const TMap< TSharedRef< IDatasmithTextureElement >, UTexture* >& GetImportedAssetsMap( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.ParentContext.ImportedTextures;
	}

	static UPackage* GetFinalPackage( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.TexturesFinalPackage.Get();
	}

	static const TMap< FName, TSoftObjectPtr< UTexture > >* GetAssetsMap( const UDatasmithScene* SceneAsset )
	{
		return SceneAsset ? &SceneAsset->Textures : nullptr;
	}

	static const TSharedRef<IDatasmithTextureElement>* GetImportedElementByName( const FDatasmithAssetsImportContext& AssetsContext, const TCHAR* ObjectPathName )
	{
		return nullptr;
	}
};

template<>
struct FDatasmithFindAssetTypeHelper< UMaterialFunction >
{
	static const TMap< TSharedRef< IDatasmithBaseMaterialElement >, UMaterialFunction* >& GetImportedAssetsMap(const FDatasmithAssetsImportContext& AssetsContext)
	{
		return AssetsContext.ParentContext.ImportedMaterialFunctions;
	}

	static UPackage* GetFinalPackage(const FDatasmithAssetsImportContext& AssetsContext)
	{
		return nullptr;
	}

	static const TMap< FName, TSoftObjectPtr< UMaterialFunction > >* GetAssetsMap(const UDatasmithScene* SceneAsset)
	{
		return SceneAsset ? &SceneAsset->MaterialFunctions : nullptr;
	}
	
	static const TSharedRef<IDatasmithBaseMaterialElement>* GetImportedElementByName( const FDatasmithAssetsImportContext& AssetsContext, const TCHAR* ObjectPathName )
	{
		return AssetsContext.ParentContext.ImportedMaterialFunctionsByName.Find(ObjectPathName);
	}
};

template<>
struct FDatasmithFindAssetTypeHelper< UMaterialInterface >
{
	static const TMap< TSharedRef< IDatasmithBaseMaterialElement >, UMaterialInterface* >& GetImportedAssetsMap( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.ParentContext.ImportedMaterials;
	}

	static UPackage* GetFinalPackage( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.MaterialsFinalPackage.Get();
	}

	static const TMap< FName, TSoftObjectPtr< UMaterialInterface > >* GetAssetsMap( const UDatasmithScene* SceneAsset )
	{
		return SceneAsset ? &SceneAsset->Materials : nullptr;
	}

	static const TSharedRef<IDatasmithBaseMaterialElement>* GetImportedElementByName( const FDatasmithAssetsImportContext& AssetsContext, const TCHAR* ObjectPathName )
	{
		return nullptr;
	}
};

template<>
struct FDatasmithFindAssetTypeHelper< ULevelSequence >
{
	static UPackage* GetImportPackage( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.LevelSequencesImportPackage.Get();
	}

	static UPackage* GetFinalPackage( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.LevelSequencesFinalPackage.Get();
	}

	static const TMap< FName, TSoftObjectPtr< ULevelSequence > >* GetAssetsMap( const UDatasmithScene* SceneAsset )
	{
		return SceneAsset ? &SceneAsset->LevelSequences : nullptr;
	}
};

template<>
struct FDatasmithFindAssetTypeHelper< ULevelVariantSets >
{
	static UPackage* GetImportPackage( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.LevelVariantSetsImportPackage.Get();
	}

	static UPackage* GetFinalPackage( const FDatasmithAssetsImportContext& AssetsContext )
	{
		return AssetsContext.LevelVariantSetsFinalPackage.Get();
	}

	static const TMap< FName, TSoftObjectPtr< ULevelVariantSets > >* GetAssetsMap( const UDatasmithScene* SceneAsset )
	{
		return SceneAsset ? &SceneAsset->LevelVariantSets : nullptr;
	}
};

template< typename ObjectType >
inline ObjectType* FDatasmithImporterUtils::FindAsset( const FDatasmithAssetsImportContext& AssetsContext, const TCHAR* ObjectPathName )
{
	if ( FCString::Strlen( ObjectPathName ) <= 0 )
	{
		return nullptr;
	}

	if ( FPaths::IsRelative( ObjectPathName ) )
	{
		const auto& ImportedElement   = FDatasmithFindAssetTypeHelper< ObjectType >::GetImportedElementByName( AssetsContext, ObjectPathName );
		const auto& ImportedAssetsMap = FDatasmithFindAssetTypeHelper< ObjectType >::GetImportedAssetsMap( AssetsContext );

		if (ImportedElement)
		{
			auto Result = ImportedAssetsMap.Find(*ImportedElement);
			if (Result)
			{
				return *Result;
			}
		}
		else
		{
			for ( const auto& ImportedAssetPair : ImportedAssetsMap )
			{
				if ( FCString::Stricmp( ImportedAssetPair.Key->GetName(), ObjectPathName ) == 0 )
				{
					return ImportedAssetPair.Value;
				}
			}
		}

		{
			const auto* AssetsMap = FDatasmithFindAssetTypeHelper< ObjectType >::GetAssetsMap( AssetsContext.ParentContext.SceneAsset );

			// Check if the AssetsMap is already tracking our asset
			if ( AssetsMap && AssetsMap->Contains( ObjectPathName ) )
			{
				return (*AssetsMap)[ FName( ObjectPathName ) ].LoadSynchronous();
			}
			else
			{
				UPackage* FinalPackage = FDatasmithFindAssetTypeHelper< ObjectType >::GetFinalPackage( AssetsContext );
				return FindObject< ObjectType >( FinalPackage, ObjectPathName );
			}
		}
	}
	else
	{
		return FindObject< ObjectType >( nullptr, ObjectPathName );
	}
}

template< class ObjectType >
inline ObjectType* FDatasmithImporterUtils::FindObject( const UPackage* ParentPackage, const FString& ObjectName )
{
	if ( ObjectName.Len() <= 0 )
	{
		return nullptr;
	}

	FString PathName = ObjectName;
	if ( FPaths::IsRelative( PathName ) && ParentPackage )
	{
		PathName = FPaths::Combine( ParentPackage->GetPathName(), ObjectName );
	}

	FSoftObjectPath ObjectPath( PathName );

	// Find the package
	FString LongPackageName = ObjectPath.GetAssetName().IsEmpty() ? ObjectPath.ToString() : ObjectPath.GetLongPackageName();

	// Look for the package in memory
	UPackage* Package = FindPackage( nullptr, *LongPackageName );

	// Look for the package on disk
	if ( !Package && FPackageName::DoesPackageExist( LongPackageName ) )
	{
		Package = LoadPackage( nullptr, *LongPackageName, LOAD_None );
	}

	ObjectType* Object = nullptr;

	if ( Package )
	{
		Package->FullyLoad();

		Object = static_cast< ObjectType* >( FindObjectWithOuter( Package, ObjectType::StaticClass(), FName( *ObjectPath.GetAssetName() ) ) );

		// The object might have been moved away from the ParentPackage but still accessible through an object redirector, so try to load with the SoftObjectPath
		// Note that the object redirector itself is in the Package at the initial location of import
		// No Package means we are trying to find a new object, so don't need to try loading it
		if ( !Object )
		{
			Object = Cast<ObjectType>( ObjectPath.TryLoad() );
		}
	}

	return Object;
}

template< class ObjectType >
bool FDatasmithImporterUtils::CanCreateAsset(const UPackage* Package, const FString& AssetName, FText& OutFailReason)
{
	FString ObjectPath = FPaths::Combine( Package->GetPathName(), AssetName ).AppendChar(L'.').Append(AssetName);
	return CanCreateAsset( ObjectPath, ObjectType::StaticClass(), OutFailReason );
}
