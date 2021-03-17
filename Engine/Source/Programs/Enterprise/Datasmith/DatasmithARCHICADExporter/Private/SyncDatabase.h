// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

#include <map>

BEGIN_NAMESPACE_UE_AC

class FSyncContext;
class FSyncData;
class FMaterialsDatabase;
class FTexturesCache;
class FElementID;

// Class that maintain synchronization datas (SyncData, Material, Texture)
class FSyncDatabase
{
  public:
	// Constructor
	FSyncDatabase(const TCHAR* InSceneLabel, const TCHAR* InAssetsPath);

	// Destructor
	~FSyncDatabase();

	// SetSceneInfo
	void SetSceneInfo();

	// Synchronize
	void Synchronize(const FSyncContext& InSyncContext);

	// Get scene
	const TSharedRef< IDatasmithScene >& GetScene() const { return Scene; }

	// Return the asset file path
	const TCHAR* GetAssetsFolderPath() const;

	// Get access to material database
	FMaterialsDatabase& GetMaterialsDatabase() const { return *MaterialsDatabase; }

	// Get access to textures cache
	FTexturesCache& GetTexturesCache() const { return *TexturesCache; }

	// Before a scan we reset our sync data, so we can detect when an element has been modified or destroyed
	void ResetBeforeScan();

	// After a scan, but before syncing, we delete obsolete syncdata (and it's Datasmith Element)
	void CleanAfterScan();

	// Get existing sync data for the specified guid
	FSyncData*& GetSyncData(const GS::Guid& InGuid);

	FSyncData& GetSceneSyncData();

	FSyncData& GetLayerSyncData(short InLayer);

	// Set a new sync data for the specified guid
	//	void SetSyncData(const GS::Guid& InGuid, FSyncData* InSyncData);

	// Delete obsolete syncdata (and it's Datasmith Element)
	void DeleteSyncData(const GS::Guid& InGuid);

	// Return the name of the specified layer
	const FString& GetLayerName(short InLayerIndex);

  private:
	typedef std::map< GS::Guid, FSyncData* > FMapGuid2SyncData;
	typedef std::map< short, FString >		 FMapLayerIndex2Name;

	// Scan all elements, to determine if they need to be synchronized
	UInt32 ScanElements(const FSyncContext& InSyncContext);

	// Scan all lights
	void ScanLights(const FElementID& InElementID);

	// Scan all cameras
	void ScanCameras(const FSyncContext& InSyncContext);

	// The scene
	TSharedRef< IDatasmithScene > Scene;

	// Path where to save assets
	FString AssetsFolderPath;

	// Fast access to material
	FMaterialsDatabase* MaterialsDatabase;

	// Cache to have fast access to textures
	FTexturesCache* TexturesCache;

	// Map guid to sync data
	FMapGuid2SyncData ElementsSyncDataMap;

	// Map layer index to it's name
	FMapLayerIndex2Name LayerIndex2Name;
};

END_NAMESPACE_UE_AC
