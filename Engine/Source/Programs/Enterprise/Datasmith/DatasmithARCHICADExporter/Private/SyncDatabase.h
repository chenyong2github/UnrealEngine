// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/AddonTools.h"
#include "Utils/LibPartInfo.h"

#include "Lock.hpp"

#include "Map.h"

BEGIN_NAMESPACE_UE_AC

class FSyncContext;
class FSyncData;
class FMaterialsDatabase;
class FTexturesCache;
class FElementID;
class FLibPartInfo;
class FInstance;

class FMeshDimensions
{
  public:
	FMeshDimensions()
		: Area(0.0f)
		, Width(0.0f)
		, Height(0.0f)
		, Depth(0.0f)
	{
	}

	FMeshDimensions(const IDatasmithMeshElement& InMesh)
		: Area(InMesh.GetArea())
		, Width(InMesh.GetWidth())
		, Height(InMesh.GetHeight())
		, Depth(InMesh.GetDepth())
	{
	}

	float Area;
	float Width;
	float Height;
	float Depth;
};

class FMeshCacheIndexor
{
  public:
	// Constructor
	FMeshCacheIndexor(const TCHAR* InIndexFilePath);

	// Destructor
	~FMeshCacheIndexor();

	// Return the mesh dimension if mesh is already created.
	const FMeshDimensions* FindMesh(const TCHAR* InMeshName) const;

	// Add this mesh to the indexor
	void AddMesh(const IDatasmithMeshElement& InMesh);

	// If changed, save the indexor to the specified file.
	void SaveToFile();

	// Read the indexor from the specified file
	void ReadFromFile();

  private:
	// We index with mesh name (aka. Hash of the mesh), and we save it's dimensions
	typedef TMap< FString, TUniquePtr< FMeshDimensions > > MapName2Dimensions;

	FString IndexFilePath;

	// Indexor
	MapName2Dimensions Name2Dimensions;

	// Flag to know if we must save
	bool bChanged = false;

	// Control access on this object
	mutable GS::Lock AccessControl;

	// Condition variable
	GS::Condition AccessCondition;
};

// Class that maintain synchronization datas (SyncData, Material, Texture)
class FSyncDatabase
{
  public:
	// Constructor
	FSyncDatabase(const TCHAR* InSceneName, const TCHAR* InSceneLabel, const TCHAR* InAssetsPath,
				  const GS::UniString& InAssetsCache);

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

	// Return the libpart from it's index
	FLibPartInfo* GetLibPartInfo(GS::Int32 InIndex);

	// Return the libpart from it's unique id
	FLibPartInfo* GetLibPartInfo(const char* InUnID);

	FInstance* GetInstance(GS::ULong InHash) const;

	void AddInstance(GS::ULong InHash, TUniquePtr< FInstance >&& InInstance);

	// Return the cache path
	static GS::UniString GetCachePath();

	FMeshCacheIndexor& GetMeshIndexor() { return MeshIndexor; }

  private:
	typedef TMap< FGuid, FSyncData* >							FMapGuid2SyncData;
	typedef TMap< short, FString >								FMapLayerIndex2Name;
	typedef TMap< GS::Int32, TUniquePtr< FLibPartInfo > >		MapIndex2LibPart;
	typedef TMap< FGSUnID, FLibPartInfo* >						MapUnId2LibPart;
	typedef GS::HashTable< GS::ULong, TUniquePtr< FInstance > > HashTableInstances;

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

	void ResetInstances();

	void ReportInstances() const;

	// Scan all elements, to determine if they need to be synchronized
	UInt32 ScanElements(const FSyncContext& InSyncContext);

	// Scan all lights
	void ScanLights(FElementID& InElementID);

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

	// Map lib part by index
	MapIndex2LibPart IndexToLibPart;

	// Map lib part by UnId
	MapUnId2LibPart UnIdToLibPart;

	HashTableInstances Instances;

	FMeshCacheIndexor MeshIndexor;
};

END_NAMESPACE_UE_AC
