// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Containers/Array.h"
#include "Math/Color.h"
#include "Misc/Paths.h"

class FArchive;

using FCadId = uint32; // Identifier defined in the input CAD file
using FColorId = uint32; // Identifier defined in the input CAD file
using FMaterialId = uint32; // Identifier defined in the input CAD file
using FCADUUID = uint32;  // Universal unique identifier that be used for the unreal asset name (Actor, Material)


namespace CADLibrary
{

enum class ECADFormat
{
	ACIS,
	AUTOCAD,
	CATIA,
	CATIA_CGR,
	CATIA_3DXML,
	CATIAV4,
	CREO,
	DWG,
	DGN,
	TECHSOFT,
	IFC,
	IGES,
	INVENTOR,
	JT,
	NX,
	MICROSTATION,
	PARASOLID,
	SOLID_EDGE,
	SOLIDWORKS,
	STEP,
	OTHER
};

CADTOOLS_API ECADFormat FileFormat(const FString& Extension);

enum class ECADParsingResult : uint8
{
	Unknown,
	Running,
	UnTreated,
	ProcessOk,
	ProcessFailed,
	FileNotFound,
};

// TODO: Remove from hear and replace by DatasmithUtils::GetCleanFilenameAndExtension... But need to remove DatasmithCore dependancies 
CADTOOLS_API void GetCleanFilenameAndExtension(const FString& InFilePath, FString& OutFilename, FString& OutExtension);
CADTOOLS_API FString GetExtension(const FString& InFilePath);


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
	FCADUUID DefaultMaterialName = 0;
	FMaterialId Material = 0;
	FColorId Color = 0; // => FastHash == ColorId+Transparency
};

class CADTOOLS_API FFileDescriptor
{
public:
	FFileDescriptor() = default;
	
	explicit FFileDescriptor(const TCHAR* InFilePath, const TCHAR* InConfiguration = nullptr, const TCHAR* InRootFolder = nullptr)
		: SourceFilePath(InFilePath)
		, Configuration(InConfiguration)
	{
		Name = FPaths::GetCleanFilename(InFilePath);
		Format = FileFormat(GetExtension(InFilePath));
		RootFolder = InRootFolder ? InRootFolder : FPaths::GetPath(InFilePath);
	}

	/**
	 * Used define and then load the cache of the CAD File instead of the source file
	 */ 
	void SetCacheFile(const FString& InCacheFilePath)
	{
		CacheFilePath = InCacheFilePath;
	}

	bool operator==(const FFileDescriptor& Other) const
	{
		return (Name.Equals(Other.Name, ESearchCase::IgnoreCase) && (Configuration == Other.Configuration));
	}

	bool IsEmpty()
	{
		return Name.IsEmpty();
	}

	void Empty()
	{
		SourceFilePath.Empty(); 
		CacheFilePath.Empty();
		Name.Empty();
		Configuration.Empty();
		RootFolder.Empty();
		DescriptorHash = 0;
	}

	friend CADTOOLS_API FArchive& operator<<(FArchive& Ar, FFileDescriptor& File);

	friend CADTOOLS_API uint32 GetTypeHash(const FFileDescriptor& FileDescription);

	uint32 GetDescriptorHash() const
	{
		if (!DescriptorHash)
		{
			DescriptorHash = GetTypeHash(*this);
		}
		return DescriptorHash;
	}

	const FString& GetSourcePath() const 
	{
		return SourceFilePath;
	}
	
	bool HasConfiguration() const
	{
		return !Configuration.IsEmpty();
	}

	const FString& GetConfiguration() const
	{
		return Configuration;
	}

	void SetConfiguration(const FString& NewConfiguration)
	{
		Configuration = NewConfiguration;
	}

	ECADFormat GetFileFormat() const
	{
		return Format;
	}

	const FString& GetPathOfFileToLoad() const
	{
		if (CacheFilePath.IsEmpty())
		{
			return SourceFilePath;
		}
		else
		{
			return CacheFilePath;
		}
	}

	/** Set the file path if SourceFilePath was not the real path */
	void SetSourceFilePath(const FString& NewFilePath)
	{
		SourceFilePath = NewFilePath;
	}

	const FString& GetRootFolder() const 
	{
		return RootFolder;
	}

	const FString& GetFileName() const
	{
		return Name;
	}

private:

	FString SourceFilePath; // e.g. d:/folder/content.jt
	FString CacheFilePath; // if the file has already been loaded 
	FString Name; // content.jt
	ECADFormat Format; // ECADFormat::JT
	FString Configuration; // dedicated to JT or SW file to read the good configuration (SW) or only a sub-file (JT)
	FString RootFolder; // alternative folder where the file could be if its path is not valid.

	mutable uint32 DescriptorHash = 0;
};

/**
 * Helper struct to store tessellation data from CoreTech or CADKernel
 *
 * FBodyMesh and FTessellationData are design to manage mesh from CoreTech and CADKernel.
 * FTessellationData is the mesh of a face
 * FBodyMesh is the mesh of a body composed by an array of FTessellationData (one FTessellationData by body face)
 *
 * CoreTech mesh are defined surface by surface. The mesh is not connected
 * CADKernel mesh is connected.
 */
struct CADTOOLS_API FTessellationData
{
	friend CADTOOLS_API FArchive& operator<<(FArchive& Ar, FTessellationData& Tessellation);

	/** Empty with CADKernel as set in FBodyMesh, Set by CoreTech (this is only the vertices of the face) */
	TArray<FVector> PositionArray;

	/** Index of each vertex in FBody::VertexArray. Empty with CoreTech and filled by FillKioVertexPosition */
	TArray<int32> PositionIndices;

	/** Index of Vertices of each face in the local Vertices set (i.e. VerticesBodyIndex for CADKernel, VertexArray for Coretech) */
	TArray<int32> VertexIndices;

	/** Normal of each vertex */
	TArray<FVector> NormalArray;

	/** UV coordinates of each vertex */
	TArray<FVector2D> TexCoordArray;

	FCADUUID ColorName = 0;
	FCADUUID MaterialName = 0;

	int32 PatchId;
};

class CADTOOLS_API FBodyMesh
{
public:
	FBodyMesh(FCadId InBodyID = 0) : BodyID(InBodyID)
	{
		BBox.Init();
	}

	friend FArchive& operator<<(FArchive& Ar, FBodyMesh& BodyMesh);

public:
	TArray<FVector> VertexArray; // set by CADKernel, filled by FillKioVertexPosition that merges coincident vertices (CoreTechHelper)
	TArray<FTessellationData> Faces;
	FBox BBox;

	uint32 TriangleCount = 0;
	FCadId BodyID = 0;
	FCADUUID MeshActorName = 0;

	TArray<int32> VertexIds;  // StaticMesh FVertexID NO Serialize, filled by FillKioVertexPosition or FillVertexPosition
	TArray<int32> SymmetricVertexIds; // StaticMesh FVertexID for sym part NO Serialize, filled by FillKioVertexPosition or FillVertexPosition

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

