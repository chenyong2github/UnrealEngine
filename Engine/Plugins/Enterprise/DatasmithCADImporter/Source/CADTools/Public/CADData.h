// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Containers/Array.h"
#include "Math/Color.h"

namespace CADLibrary
{

enum ECTTessellationLine
{
	LineType = 0,
	LineRawSize,
	LineBodyId,
	LineBodyUuId,
	LineBodyFaceNum,
	LineMaterialId,
	LineMaterialHash,
	LineVertexCount,
	LineVertexType,
	LineNormalCount,
	LineNormalType,
	LineIndexCount,
	LineIndexType,
	LineTexCoordType,
	LineTexCoordCount,
	LastLine
};

struct CADTOOLS_API FCADMaterial
{
	int32 MaterialId;  //#ueent_CAD
	FString MaterialName;
	FColor Diffuse;
	FColor Ambient;
	FColor Specular;
	float Shininess;
	float Transparency;
	float Reflexion;
	FString TextureName;
};

struct CADTOOLS_API FObjectDisplayDataId
{
	uint32 MaterialId = 0;
	uint32 ColorHId = 0; // => FastHash == ColorId+Transparency
};

/**
 * Helper struct to store tessellation data from CoreTech
 */
struct CADTOOLS_API FTessellationData
{
	TArray<uint8> VertexArray;
	TArray<uint8> NormalArray;
	TArray<uint8> IndexArray;
	TArray<uint8> TexCoordArray;
	uint32        VertexCount = 0;
	uint32        NormalCount = 0;
	uint32        IndexCount = 0;
	uint32        TexCoordCount = 0;

	uint32    StartVertexIndex = 0;

	uint8 SizeOfVertexType;
	uint8 SizeOfTexCoordType;
	uint8 SizeOfNormalType;
	uint8 SizeOfIndexType;
	bool  bHasRGBColor;

	uint32 BodyId = 0;
	uint32 BodyUuId = 0;
	uint32 BodyFaceNum = 0;
	uint32 MaterialId = 0;
	uint32 MaterialHash = 0;

	TArray<int32> VertexIdSet;  // StaticMesh FVertexID
	TArray<int32> SymVertexIdSet; // StaticMesh FVertexID for sym part
};

class CADTOOLS_API FBodyMesh
{
public:
	FBodyMesh(uint32 BodyID, int32 FaceNum);

	TArray<FTessellationData>& GetTessellationSet()
	{
		return FaceTessellationSet;
	}

	uint32 GetTriangleCount() const
	{
		return TriangleCount;
	}

	uint32 GetBodyId() const
	{
		return BodyID;
	}

	uint32 GetBodyUuid() const 
	{
		return BodyUuid;
	}

	void SetBodyId(uint32 Id)
	{
		BodyID = Id;
	}

	void SetBodyUuid(uint32 Uuid)
	{
		BodyUuid = Uuid;
	}

protected:

	/**
	 * FCTTessellation is an elementary structure of Mesh
	 */
	TArray<FTessellationData> FaceTessellationSet;

	/**
	 * Structure use to map Material ID to Material Hash. Material hash is an unique value by material
	 */
	TMap<uint32, uint32> MaterialIdToMaterialHash;
	uint32 TriangleCount;
	uint32 BodyID;
	uint32 BodyUuid;
};

class CADTOOLS_API FCADMeshFile
{
public:
	FCADMeshFile(FString& InFileName, TMap< uint32, FBodyMesh* >& InBodyUuidToCTBodyMap);

	FString& GetFileName()
	{
		return FileName;
	}

protected:
	FString FileName;
	TArray<FBodyMesh> CTBodySet;
	TMap<uint32, FBodyMesh* >& BodyUuidToBodyMap;
};

CADTOOLS_API uint32 BuildFastColorHash(uint32 ColorId, uint8 Alpha);
CADTOOLS_API void UnhashFastColorHash(uint32 ColorHash, uint32& OutColorId, uint8& OutAlpha);

CADTOOLS_API int32 BuildColorHash(const FColor& Color);
CADTOOLS_API int32 BuildMaterialHash(const FCADMaterial& Material);

CADTOOLS_API void WriteTessellationInRawData(FTessellationData& Tessellation, TArray<uint8>& GlobalRawData);
CADTOOLS_API bool ReadTessellationSetFromFile(TArray<uint8>& RawData, uint32& OutNbBody, TArray<FBodyMesh>& FaceTessellations);

inline void CopyValue(const uint8* Source, int Offset, uint8 Size, bool bIs3D, FVector& Dest, const FMatrix* Matrix = nullptr)
{
	if (Source == nullptr)
	{
		return;
	}

	switch (Size)
	{
		case sizeof(uint8) :
		{
			Dest[0] = ((uint8*)Source)[Offset + 0] / 255.;
			Dest[1] = ((uint8*)Source)[Offset + 1] / 255.;
			Dest[2] = bIs3D ? ((uint8*)Source)[Offset + 2] / 255. : 0.;
			break;
		}
		case sizeof(float) :
		{
			Dest[0] = ((float*)Source)[Offset + 0];
			Dest[1] = ((float*)Source)[Offset + 1];
			Dest[2] = bIs3D ? ((float*)Source)[Offset + 2] : 0.;
			break;
		}
		case sizeof(double) :
		{
			Dest[0] = ((double*)Source)[Offset + 0];
			Dest[1] = ((double*)Source)[Offset + 1];
			Dest[2] = bIs3D ? ((double*)Source)[Offset + 2] : 0.;
			break;
		}
		default:
		{
			Dest[0] = 0.;
			Dest[1] = 0.;
			Dest[2] = 0.;
			break;
		}
	}

	if (bIs3D && Matrix != nullptr)
	{
		Dest = Matrix->TransformPosition(Dest);
	}
}

inline void CopyValue(const void* Source, int Offset, uint8 Size, int32 Dest[3])
{
	if (Source == nullptr)
	{
		return;
	}

	switch (Size)
	{
		case sizeof(uint8) :
		{
			Dest[0] = ((uint8*)Source)[Offset + 0];
			Dest[1] = ((uint8*)Source)[Offset + 1];
			Dest[2] = ((uint8*)Source)[Offset + 2];
			break;
		}
		case sizeof(uint16) :
		{
			Dest[0] = ((uint16*)Source)[Offset + 0];
			Dest[1] = ((uint16*)Source)[Offset + 1];
			Dest[2] = ((uint16*)Source)[Offset + 2];
			break;
		}
		case sizeof(uint32) :
		{
			Dest[0] = ((uint32*)Source)[Offset + 0];
			Dest[1] = ((uint32*)Source)[Offset + 1];
			Dest[2] = ((uint32*)Source)[Offset + 2];
			break;
		}
		default:
		{
			Dest[0] = 0.;
			Dest[1] = 0.;
			Dest[2] = 0.;
			break;
		}
	}
}

}