// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADData.h"

#include "CADOptions.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace CADLibrary
{
uint32 BuildColorId(uint32 ColorId, uint8 Alpha)
{
	if (Alpha == 0)
	{
		Alpha = 1;
	}
	return ColorId | Alpha << 24;
}

void GetCTColorIdAlpha(ColorId ColorId, uint32& CTColorId, uint8& Alpha)
{
	CTColorId = ColorId & 0x00ffffff;
	Alpha = (uint8)((ColorId & 0xff000000) >> 24);
}

int32 BuildColorName(const FColor& Color)
{
	FString Name = FString::Printf(TEXT("%02x%02x%02x%02x"), Color.R, Color.G, Color.B, Color.A);
	return FGenericPlatformMath::Abs((int32)GetTypeHash(Name));
}

int32 BuildMaterialName(const FCADMaterial& Material)
{
	FString Name;
	if (!Material.MaterialName.IsEmpty())
	{
		Name += Material.MaterialName;  // we add material name because it could be used by the end user so two material with same parameters but different name are different.
	}
	Name += FString::Printf(TEXT("%02x%02x%02x "), Material.Diffuse.R, Material.Diffuse.G, Material.Diffuse.B);
	Name += FString::Printf(TEXT("%02x%02x%02x "), Material.Ambient.R, Material.Ambient.G, Material.Ambient.B);
	Name += FString::Printf(TEXT("%02x%02x%02x "), Material.Specular.R, Material.Specular.G, Material.Specular.B);
	Name += FString::Printf(TEXT("%02x%02x%02x"), (int)(Material.Shininess * 255.0), (int)(Material.Transparency * 255.0), (int)(Material.Reflexion * 255.0));

	if (!Material.TextureName.IsEmpty())
	{
		Name += Material.TextureName;
	}
	return FMath::Abs((int32)GetTypeHash(Name));
}

FArchive& operator<<(FArchive& Ar, FCADMaterial& Material)
{
	Ar << Material.MaterialName;
	Ar << Material.Diffuse;
	Ar << Material.Ambient;
	Ar << Material.Specular;
	Ar << Material.Shininess;
	Ar << Material.Transparency;
	Ar << Material.Reflexion;
	Ar << Material.TextureName;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FFileDescription& File)
{
	Ar << File.Path;
	Ar << File.OriginalPath;
	Ar << File.Name;
	Ar << File.Extension;
	Ar << File.Configuration;
	Ar << File.MainCadFilePath;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FTessellationData& TessellationData)
{
	Ar << TessellationData.PatchId;

	Ar << TessellationData.VertexArray;
	Ar << TessellationData.NormalArray;
	Ar << TessellationData.IndexArray;
	Ar << TessellationData.TexCoordArray;

	Ar << TessellationData.StartVertexIndex;

	Ar << TessellationData.ColorName;
	Ar << TessellationData.MaterialName;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FBodyMesh& BodyMesh)
{
	Ar << BodyMesh.TriangleCount;
	Ar << BodyMesh.BodyID;
	Ar << BodyMesh.MeshActorName;
	Ar << BodyMesh.Faces;

	Ar << BodyMesh.BBox;

	Ar << BodyMesh.MaterialSet;
	Ar << BodyMesh.ColorSet;

	return Ar;
}

void SerializeBodyMeshSet(const TCHAR* Filename, TArray<FBodyMesh>& InBodySet)
{
	TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileWriter(Filename));

	uint32 type = 234561;
	*Archive << type;

	*Archive << InBodySet;

	Archive->Close();
}

void DeserializeBodyMeshFile(const TCHAR* Filename, TArray<FBodyMesh>& OutBodySet)
{
	TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileReader(Filename));

	uint32 type = 0;
	*Archive << type;
	if (type != 234561)
	{
		Archive->Close();
		return;
	}

	*Archive << OutBodySet;
	Archive->Close();
}

// Duplicated with FDatasmithUtils::GetCleanFilenameAndExtension, to delete as soon as possible
void GetCleanFilenameAndExtension(const FString& InFilePath, FString& OutFilename, FString& OutExtension)
{
	if (InFilePath.IsEmpty())
	{
		OutFilename.Empty();
		OutExtension.Empty();
		return;
	}

	FString BaseFile = FPaths::GetCleanFilename(InFilePath);
	BaseFile.Split(TEXT("."), &OutFilename, &OutExtension, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

	if (!OutExtension.IsEmpty() && FCString::IsNumeric(*OutExtension))
	{
		BaseFile = OutFilename;
		BaseFile.Split(TEXT("."), &OutFilename, &OutExtension, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		OutExtension = OutExtension + TEXT(".*");
		return;
	}

	if (OutExtension.IsEmpty())
	{
		OutFilename = BaseFile;
	}
}

uint32 FFileDescription::GetFileHash()
{
	FFileStatData FileStatData = IFileManager::Get().GetStatData(*OriginalPath);

	FDateTime ModificationTime = FileStatData.ModificationTime;

	uint32 FileHash = GetTypeHash(*Name);
	FileHash = HashCombine(FileHash, GetTypeHash(*Configuration));
	FileHash = HashCombine(FileHash, GetTypeHash(FileStatData.FileSize));
	FileHash = HashCombine(FileHash, GetTypeHash(ModificationTime));

	return FileHash;
}

}
