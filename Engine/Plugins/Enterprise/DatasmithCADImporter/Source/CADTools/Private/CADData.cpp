// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADData.h"

#include "CADOptions.h"

#include "Misc/FileHelper.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

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

FArchive& operator<<(FArchive& Ar, FTessellationData& TessellationData)
{
	Ar << TessellationData.VertexArray;
	Ar << TessellationData.NormalArray;
	Ar << TessellationData.IndexArray;
	Ar << TessellationData.TexCoordArray;
	Ar << TessellationData.VertexCount;
	Ar << TessellationData.NormalCount;
	Ar << TessellationData.IndexCount;
	Ar << TessellationData.TexCoordCount;

	Ar << TessellationData.StartVertexIndex;

	Ar << TessellationData.SizeOfVertexType;
	Ar << TessellationData.SizeOfTexCoordType;
	Ar << TessellationData.SizeOfNormalType;
	Ar << TessellationData.SizeOfIndexType;

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

	Ar << BodyMesh.MaterialSet;
	Ar << BodyMesh.ColorSet;

	return Ar;
}

void SerializeBodyMeshSet(const TCHAR* Filename, TArray<FBodyMesh>& InBodySet)
{
	TArray<uint8> OutBuffer;
	FMemoryWriter ArWriter(OutBuffer);
	uint32 type = 234561;
	ArWriter << type;

	ArWriter << InBodySet;

	FFileHelper::SaveArrayToFile(OutBuffer, Filename);
}

void DeserializeBodyMeshFile(const TCHAR* Filename, TArray<FBodyMesh>& OutBodySet)
{
	TArray<uint8> InBuffer;
	FFileHelper::LoadFileToArray(InBuffer, Filename);

	FMemoryReader ArReader(InBuffer);
	uint32 type = 0;
	ArReader << type;
	if (type != 234561)
	{
		return;
	}

	ArReader << OutBodySet;
}

}
