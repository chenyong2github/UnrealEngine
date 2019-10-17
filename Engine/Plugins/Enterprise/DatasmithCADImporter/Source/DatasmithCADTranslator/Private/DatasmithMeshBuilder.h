// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#ifdef USE_CORETECH_MT_PARSER

#include "CoreMinimal.h"

#include "CADLibraryOptions.h"
#include "CoretechHelper.h"


class FCTRawGeomFile;
class IDatasmithMeshElement;

struct FMeshParameters;


class FCoretechBody
{
public:
	FCoretechBody(uint32 BodyID);
	void Add(CADLibrary::FCTTessellation* InFaceTessellation);
	TArray<CADLibrary::FCTTessellation*>& GetTessellationSet()
	{
		return FaceTessellationSet;
	}

	uint32 GetTriangleCount()
	{
		return TriangleCount;
	}

protected:

	/** 
	 * Array of FCTTessellation
	 * FCTTessellation is an elementary structure of Mesh
	 */
	TArray<CADLibrary::FCTTessellation*> FaceTessellationSet;

	/**
	 * Structure use to map Material ID to Material Hash. Material hash is an unique value by material 
	 */
	CADLibrary::FCTMaterialPartition MaterialToPartition;
	uint32 TriangleCount;
	uint32 BodyID;
};

class FCTRawGeomFile
{
public:
	FCTRawGeomFile(FString& InFileName, TMap< uint32, FCoretechBody* >& InBodyUuidToCTBodyMap);

	FCoretechBody* GetCTNode(int32 nodeId)
	{
		uint32* index = CTIdToBodyMap.Find(nodeId);
		if (index == nullptr)
		{
			return nullptr;
		}
		return &CTBodySet[*index];
	}

	FString& GetFileName()
	{
		return FileName;
	}

protected:
	FString FileName;
	TArray<uint8> RawData;
	TArray<FCoretechBody> CTBodySet;
	TMap<uint32, uint32> CTIdToBodyMap;
	TMap< uint32, FCoretechBody* >& BodyUuidToCTBodyMap;

	/**
	 * A body has a set of face. A face mesh is defined by is FCTTessellation
	 */
	TArray<CADLibrary::FCTTessellation> FaceTessellationSet;
};






class FDatasmithMeshBuilder
{
public:
	FDatasmithMeshBuilder(const FString& InCachePath, TMap<FString, FString>& InCADFileToUE4GeomMap, TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& InMeshElementToCTBodyUuidMap);
	TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, CADLibrary::FMeshParameters& OutMeshParameters);
	void LoadRawDataGeom();

	void SetScaleFactor(float InScaleFactor)
	{
		ScaleFactor = InScaleFactor;
	}

protected:
	//const FDatasmithSceneSource& Source;
	FString CachePath;

	TArray<FCTRawGeomFile> RawDataArray;

	/** Map linking Cad file to RawGeom file (*.gm) */
	TMap<FString, FString>& CADFileToUE4GeomMap;

	/** Datasmith mesh elements to BodyUuid */
	TMap< TSharedPtr< IDatasmithMeshElement >, uint32 >& MeshElementToCTBodyUuidMap;

	/** BodyUuid to CTBody */
	TMap< uint32, FCoretechBody* > BodyUuidToCoretechBodyMap;
	float ScaleFactor;
};


#endif // USE_CORETECH_MT_PARSER
