// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Misc/Paths.h"

#define COLORSETLINE    3
#define MATERIALSETLINE 4
#define EXTERNALREFLINE 7
#define MAPCTIDLINE     8

#ifdef CAD_INTERFACE
#include "kernel_io/attribute_io/attribute_enum.h"
#include "kernel_io/ct_types.h"
#include "kernel_io/kernel_io_type.h"
#include "kernel_io/material_io/material_io.h"
#endif // CAD_INTERFACE


struct FFileStatData;

namespace CADLibrary
{

enum class ECoretechParsingResult
{
	Unknown,
	Running,
	UnTreated,
	ProcessOk,
	ProcessFailed,
	FileNotFound,
};

#ifdef CAD_INTERFACE

CADINTERFACES_API bool GetColor(uint32 ColorHash, FColor& OutColor);
CADINTERFACES_API bool GetMaterial(uint32 MaterialID, FCADMaterial& OutMaterial);

CADINTERFACES_API uint32 GetBodiesFaceSetNum(TArray<CT_OBJECT_ID>& BodySet);
CADINTERFACES_API void GetCTObjectDisplayDataIds(CT_OBJECT_ID ObjectID, FObjectDisplayDataId& Material);

uint32 GetGeomFileHash(const uint32 InSGHash, CADLibrary::FImportParameters& ImportParam);

uint32 GetSize(CT_TESS_DATA_TYPE type);

class CADINTERFACES_API FCoreTechFileParser
{
public:
	using EProcessResult = ECoretechParsingResult;

	/**
	 * @param InCTFullPath Full path of the CAD file to parse
	 * @param InCachePath Full path of the cache in which the data will be saved
	 * @param ImportParams Parameters that setting import data like mesh SAG...
	 * @param KernelIOPath Full Path of KernelIO libraries (oda_translator.exe, ...). Mandatory to import DWG, or DGN files
	 */
	FCoreTechFileParser(const FString& InCTFullPath, const FString& InCachePath, const FImportParameters& ImportParams, const TCHAR* KernelIOPath = TEXT(""));

	double GetMetricUnit() const { return 0.01; }

	EProcessResult ProcessFile();
	void GetBodyTessellation(CT_OBJECT_ID BodyId, FBodyMesh& OutBodyMesh, const FImportParameters& ImportParams, uint32 ParentMaterialHash);


	const TSet<FString>& GetExternalRefSet()
	{
		return MockUpDescription.ExternalRefSet;
	}

	const FString& GetSceneGraphFile()
	{
		return MockUpDescription.SceneGraphArchive;
	}
	const FString& GetMeshFileName()
	{
		return MeshArchiveFile;
	}
	const FString& GetCADFileName()
	{
		return CADFile;
	}

private:
	EProcessResult ReadFileWithKernelIO();
	bool ReadNode(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);
	bool ReadInstance(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);
	bool ReadComponent(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);
	bool ReadUnloadedComponent(CT_OBJECT_ID NodeId);
	bool ReadBody(CT_OBJECT_ID NodeId, uint32 ParentMaterialHash);

	void LoadSceneGraphArchive(const FString& SceneGraphFilePath);

	uint32 GetMaterialNum();
	void ReadMaterials();

	uint32 GetObjectMaterial(ICADArchiveObject& Object);
	FArchiveColor& FindOrAddColor(uint32 ColorHId);
	FArchiveMaterial& FindOrAddMaterial(CT_MATERIAL_ID MaterialId);
	void SetFaceMainMaterial(FObjectDisplayDataId& InFaceMaterial, FObjectDisplayDataId& InBodyMaterial, FBodyMesh& BodyMesh, int32 FaceIndex);

	void ReadNodeMetaData(CT_OBJECT_ID NodeId, TMap<FString, FString>& OutMetaData);
	void GetStringMetaDataValue(CT_OBJECT_ID NodeId, const TCHAR* InMetaDataName, FString& OutMetaDataValue);

	CT_FLAGS SetCoreTechImportOption(const FString& MainFileExt);
	void GetAttributeValue(CT_ATTRIB_TYPE attrib_type, int ith_field, FString& value);

	void ExportSceneGraphFile();
	void ExportMeshArchiveFile();

protected:
	const FString& CachePath;

	FString FullPath;
	FString CADFile;

	FString FileConfiguration;
	FString NodeConfiguration;

	FArchiveMockUp MockUpDescription;

	FString MeshArchiveFilePath;
	FString MeshArchiveFile;
	TArray<FBodyMesh> BodyMeshes;

	bool bNeedSaveCTFile;

	const FImportParameters& ImportParameters;
};

#endif // CAD_INTERFACE

} // ns CADLibrary
