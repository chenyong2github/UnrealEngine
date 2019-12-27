// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Containers/Array.h"
#include "Math/Color.h"

class FArchive;

using CadId = uint32; // Identifier defined in the input CAD file
using ColorId = uint32; // Identifier defined in the input CAD file
using MaterialId = uint32; // Identifier defined in the input CAD file
using CADUUID = uint32;  // Universal unique identifier that be used for the unreal asset name (Actor, Material)


namespace CADLibrary
{

class CADTOOLS_API FCADMaterial
{
public:
	friend CADTOOLS_API FArchive& operator<<(FArchive& Ar, FCADMaterial& Material);

public:
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
	CADUUID DefaultMaterialName = 0;
	MaterialId Material = 0;
	ColorId Color = 0; // => FastHash == ColorId+Transparency
};

/**
 * Helper struct to store tessellation data from CoreTech
 */
struct CADTOOLS_API FTessellationData
{
	friend CADTOOLS_API FArchive& operator<<(FArchive& Ar, FTessellationData& Tessellation);

	TArray<uint8> VertexArray;
	TArray<uint8> NormalArray;
	TArray<uint8> IndexArray;
	TArray<uint8> TexCoordArray;
	uint32        VertexCount = 0;
	uint32        NormalCount = 0;
	uint32        IndexCount = 0;
	uint32        TexCoordCount = 0;

	uint32    StartVertexIndex = 0;

	uint8 SizeOfVertexType = 0;
	uint8 SizeOfTexCoordType = 0;
	uint8 SizeOfNormalType = 0;
	uint8 SizeOfIndexType = 0;

	CADUUID ColorName = 0;
	CADUUID MaterialName = 0;

	TArray<int32> VertexIdSet;  // StaticMesh FVertexID NO Serialize
	TArray<int32> SymVertexIdSet; // StaticMesh FVertexID for sym part NO Serialize
};

class CADTOOLS_API FBodyMesh
{
public:
	FBodyMesh(CadId InBodyID = 0) : BodyID(InBodyID)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FBodyMesh& BodyMesh);

public:
	TArray<FTessellationData> Faces;

	uint32 TriangleCount = 0;
	CadId BodyID = 0;
	CADUUID MeshActorName = 0;

	TSet<uint32> MaterialSet;
	TSet<uint32> ColorSet;
};

CADTOOLS_API uint32 BuildColorId(uint32 ColorId, uint8 Alpha);
CADTOOLS_API void GetCTColorIdAlpha(uint32 ColorHash, uint32& OutColorId, uint8& OutAlpha);

CADTOOLS_API int32 BuildColorName(const FColor& Color);
CADTOOLS_API int32 BuildMaterialName(const FCADMaterial& Material);

CADTOOLS_API void SerializeBodyMeshSet(const TCHAR* Filename, TArray<FBodyMesh>& InBodySet);
CADTOOLS_API void DeserializeBodyMeshFile(const TCHAR* Filename, TArray<FBodyMesh>& OutBodySet);

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