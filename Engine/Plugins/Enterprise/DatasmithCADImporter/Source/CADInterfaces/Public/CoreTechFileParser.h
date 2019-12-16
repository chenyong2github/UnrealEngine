// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADOptions.h"
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

CADINTERFACES_API void GetColor(uint32 ColorHash, FColor& OutMaterial);
CADINTERFACES_API bool GetMaterial(uint32 MaterialID, FCADMaterial& OutMaterial);

CADINTERFACES_API int32 BuildColorHash(uint32 ColorHId);
CADINTERFACES_API int32 BuildMaterialHash(uint32 MaterialId);

CADINTERFACES_API uint32 GetFaceTessellation(CT_OBJECT_ID FaceID, TArray<FTessellationData>& FaceTessellationSet, int32& OutRawDataSize, const float ScaleFactor);
CADINTERFACES_API uint32 GetBodiesTessellations(TArray<CT_OBJECT_ID>& BodySet, TArray<FTessellationData>& FaceTessellations, TMap<uint32, uint32>& MaterialIdToMaterialHashMap, int32& OutRawDataSize, const FImportParameters&);
CADINTERFACES_API uint32 GetBodiesFaceSetNum(TArray<CT_OBJECT_ID>& BodySet);
CADINTERFACES_API void GetCTObjectDisplayDataIds(CT_OBJECT_ID ObjectID, FObjectDisplayDataId& Material);
CADINTERFACES_API void GetBodiesMaterials(TArray<CT_OBJECT_ID>& BodySet, TMap<uint32, uint32>& MaterialIdToHash, bool bPreferPartData);

uint32 GetFileHash(const FString& FileName, const FFileStatData& FileStatData, const FString& Config);
uint32 GetGeomFileHash(const uint32 InSGHash, CADLibrary::FImportParameters& ImportParam);

/**
 * According to the preference (bPreferPartData), select the best material to use
 */
CADINTERFACES_API void SetFaceMainMaterial(FObjectDisplayDataId& InFaceMaterial, FObjectDisplayDataId& InBodyMaterial, TMap<uint32, uint32>& MaterialIdToMaterialHashMap, FTessellationData& OutFaceTessellations);

uint32 GetSize(CT_TESS_DATA_TYPE type);


class CADINTERFACES_API FCoreTechFileParser
{
public:
	using EProcessResult = ECoretechParsingResult;

	FCoreTechFileParser(const FString& InCTFullPath, const FString& InCachePath, const FImportParameters& ImportParams, bool bPreferBodyData, bool bScaleUVMap);

	double GetMetricUnit() const { return 0.01; }

	EProcessResult ProcessFile();

	const TSet<FString>& GetExternalRefSet()
	{
		return ExternalRefSet;
	}

	const FString& GetSceneGraphFile()
	{
		return SceneGraphFile;
	}
	const FString& GetMeshFile()
	{
		return MeshFile;
	}
	const FString& GetFileName()
	{
		return CADFile;
	}

private:
	EProcessResult ReadFileWithKernelIO();
	bool ReadNode(CT_OBJECT_ID NodeId);
	bool ReadInstance(CT_OBJECT_ID NodeId);
	bool ReadComponent(CT_OBJECT_ID NodeId);
	bool ReadUnloadedComponent(CT_OBJECT_ID NodeId);
	bool ReadBody(CT_OBJECT_ID NodeId);

	uint32 GetMaterialNum();
	void ReadColor();
	void ReadMaterial();

	void ReadNodeMetaDatas(CT_OBJECT_ID NodeId);

	CT_FLAGS SetCoreTechImportOption(const FString& MainFileExt);
	void GetAttributeValue(CT_ATTRIB_TYPE attrib_type, int ith_field, FString& value);

	void ExportFileSceneGraph();

protected:
	const FString& CachePath;

	FString FullPath;
	FString CADFile;

	FString FileConfiguration;
	FString NodeConfiguration;

	FString SceneGraphFile;
	FString RawDataGeom;
	FString MeshFile;

	TSet<FString> ExternalRefSet;

	TArray<FString> SceneGraphDescription;
	TMap<uint32, uint32> CTIdToRawLineMap;

	TMap<uint32, uint32> MaterialIdToMaterialHashMap;

	bool bNeedSaveCTFile;
	bool bPreferBodyData;
	bool bScaleUVMap;

	const FImportParameters& ImportParameters;
};

#endif // CAD_INTERFACE

} // ns CADLibrary
