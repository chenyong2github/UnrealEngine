// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"

#include "Lock.hpp"

#include "Map.h"

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
	FSyncDatabase(const TCHAR* InSceneName, const TCHAR* InSceneLabel, const TCHAR* InAssetsPath);

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
	// If null, you must create it.
	FSyncData*& GetSyncData(const GS::Guid& InGuid);

	// Return scene sync data (Create it if not present)
	FSyncData& GetSceneSyncData();

	// Return layer sync data (Create it if not present)
	FSyncData& GetLayerSyncData(short InLayer);

	// Delete obsolete syncdata (and it's Datasmith Element)
	void DeleteSyncData(const GS::Guid& InGuid);

	// Return the name of the specified layer
	const FString& GetLayerName(short InLayerIndex);

	// Set the mesh in the handle and take care of mesh life cycle.
	bool SetMesh(TSharedPtr< IDatasmithMeshElement >* Handle, const TSharedPtr< IDatasmithMeshElement >& InMesh);

  private:
	typedef TMap< FGuid, FSyncData* > FMapGuid2SyncData;
	typedef TMap< short, FString >	  FMapLayerIndex2Name;

	// To take care of mesh life cycle.
	class FMeshInfo
	{
	  public:
		TSharedPtr< IDatasmithMeshElement > Mesh; // The mesh
		uint32								Count = 0; // Number of actors using this mesh

		FMeshInfo() {}
	};
	// Map mesh by their hash name.
	typedef TMap< FString, FMeshInfo > FMapHashToMeshInfo;

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

	// Map mesh name (hash) to mesh info
	FMapHashToMeshInfo HashToMeshInfo;
	GS::Lock		   HashToMeshInfoAccesControl;
};

END_NAMESPACE_UE_AC
