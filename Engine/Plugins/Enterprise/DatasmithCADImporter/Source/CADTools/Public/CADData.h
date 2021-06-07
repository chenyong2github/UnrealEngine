// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Containers/Array.h"
#include "Math/Color.h"
#include "Misc/Paths.h"

class FArchive;

using CadId = uint32; // Identifier defined in the input CAD file
using ColorId = uint32; // Identifier defined in the input CAD file
using MaterialId = uint32; // Identifier defined in the input CAD file
using CADUUID = uint32;  // Universal unique identifier that be used for the unreal asset name (Actor, Material)


namespace CADLibrary
{

// TODO: Remove from hear and replace by DatasmithUtils::GetCleanFilenameAndExtension... But need to remove DatasmithCore dependancies 
CADTOOLS_API void GetCleanFilenameAndExtension(const FString& InFilePath, FString& OutFilename, FString& OutExtension);

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

struct CADTOOLS_API FFileDescription
{
	explicit FFileDescription(const TCHAR* InFilePath = nullptr, const TCHAR* InConfiguration = nullptr, const TCHAR* InRootFilePath = nullptr)
		: Path(InFilePath)
		, OriginalPath(InFilePath)
		, Configuration(InConfiguration)
		, MainCadFilePath(InRootFilePath)
	{
		if (MainCadFilePath.IsEmpty() && !Path.IsEmpty())
		{
			MainCadFilePath = FPaths::GetPath(Path);
		}

		GetCleanFilenameAndExtension(Path, Name, Extension);
		Name += TEXT(".") + Extension;
	}

	/**
	 * Used to replace CADFile path by the path of the file saved in KernelIO format (*.ct)
	 */ 
	void ReplaceByKernelIOBackup(const FString& InKernelIOBackupPath)
	{
		Path = InKernelIOBackupPath;
	}

	bool operator==(const FFileDescription& Other) const
	{
		return (Name.Equals(Other.Name, ESearchCase::IgnoreCase) && (Configuration == Other.Configuration));
	}

	friend CADTOOLS_API FArchive& operator<<(FArchive& Ar, FFileDescription& File);

	uint32 GetFileHash();

	FString Path;
	FString OriginalPath;
	FString Name;
	FString Extension;
	FString Configuration;
	FString MainCadFilePath;
};

/**
 * Helper struct to store tessellation data from CoreTech
 */
struct CADTOOLS_API FTessellationData
{
	friend CADTOOLS_API FArchive& operator<<(FArchive& Ar, FTessellationData& Tessellation);

	int32    PatchId = 0;

	TArray<FVector> VertexArray;
	TArray<FVector> NormalArray;
	TArray<int32> IndexArray;
	TArray<FVector2D> TexCoordArray;

	uint32    StartVertexIndex = 0;

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
		BBox.Init();
	}

	friend FArchive& operator<<(FArchive& Ar, FBodyMesh& BodyMesh);

public:
	TArray<FTessellationData> Faces;
	FBox BBox;

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

using ::GetTypeHash;
FORCEINLINE CADTOOLS_API uint32 GetTypeHash(const FFileDescription& FileDescription)
{
	return HashCombine(GetTypeHash(FileDescription.Name), GetTypeHash(FileDescription.Configuration));
};

}

