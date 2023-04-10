// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "UObject/SoftObjectPath.h"

#include "InterchangeSceneImportAsset.generated.h"

class UInterchangeAssetImportData;
class UInterchangeFactoryBaseNode;

/*
 * Class to hold all the data required to properly re-import a level
 */
UCLASS()
class INTERCHANGEENGINE_API UInterchangeSceneImportAsset : public UObject, public IInterface_AssetUserData
{
	GENERATED_BODY()

	virtual ~UInterchangeSceneImportAsset();

public:
#if WITH_EDITORONLY_DATA
	/** Importing data and options used for this Datasmith scene */
	UPROPERTY(EditAnywhere, Instanced, Category=ImportSettings)
	TObjectPtr<UInterchangeAssetImportData> AssetImportData;

	/** Array of user data stored with the asset */
	UPROPERTY()
	TArray< TObjectPtr<UAssetUserData> > AssetUserData;
#endif // #if WITH_EDITORONLY_DATA

	//~ Begin UObject Interface
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual void PostLoad() override;
	//~ End UObject Interface

	//~ Begin IInterface_AssetUserData Interface
	virtual void AddAssetUserData( UAssetUserData* InUserData ) override;
	virtual void RemoveUserDataOfClass( TSubclassOf<UAssetUserData> InUserDataClass ) override;
	virtual UAssetUserData* GetAssetUserDataOfClass( TSubclassOf<UAssetUserData> InUserDataClass ) override;
	virtual const TArray<UAssetUserData*>* GetAssetUserDataArray() const override;
	//~ End IInterface_AssetUserData Interface

#if WITH_EDITOR
	void RegisterWorldRenameCallbacks();
#endif

	/** Updates the SceneObjects cache based on the node container stored in AssetImportData */
	void UpdateSceneObjects();

	/**
	 * Returns the UObject which asset path is '//PackageName.AssetName[:SubPathString]'.
	 * Returns nullptr if the asset which path is '//PackageName.AssetName[:SubPathString]' was not part of
	 * the level import cached in this UInterchangeSceneImportAsset.
	 * @oaram PackageName: Package path of the actual object to reimport
	 * @oaram AssetName: Asset name of the actual object to reimport
	 * @oaram SubPathString: Optional subobject name
	 */
	UObject* GetSceneObject(const FString& PackageName, const FString& AssetName, const FString& SubPathString = FString()) const;

	/**
	 * Returns the factory node associated with the asset which path is '//PackageName.AssetName[:SubPathString]'.
	 * Returns nullptr if the asset which path is '//PackageName.AssetName[:SubPathString]' was not part of
	 * the level import cached in this UInterchangeSceneImportAsset.
	 * @oaram PackageName: Package path of the actual object to reimport
	 * @oaram AssetName: Asset name of the actual object to reimport
	 * @oaram SubPathString: Optional subobject name
	 */
	const UInterchangeFactoryBaseNode* GetFactoryNode(const FString& PackageName, const FString& AssetName, const FString& SubPathString = FString()) const;

private:

#if WITH_EDITOR
	/** Called before a world is renamed */
	void OnPreWorldRename(UWorld* World, const TCHAR* InName, UObject* NewOuter, ERenameFlags Flags, bool& bShouldFailRename);
	
	/** Invoked when a world is successfully renamed. Used to track when a temporary 'Untitled' unsaved map is saved with a new name. */
	void OnPostWorldRename(UWorld* World);

	/** Members used to cache the path and names related to the world to be renamed*/
	bool bWorldRenameCallbacksRegistered = false;
	FString PreviousWorldPath;
	FString PreviousWorldName;
	FString PreviousLevelName;
#endif

#if WITH_EDITORONLY_DATA
	/**
	 * Cache to easily retrieve a factory node from an asset's/actor's path
	 * FSoftObjectPath stores the path of an imported object
	 * FString stores the unique id of the factory node associated with the imported object
	 */
	TMap< FSoftObjectPath, FString > SceneObjects;
#endif // #if WITH_EDITORONLY_DATA
};
