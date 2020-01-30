// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADSceneGraph.h"

#include "Misc/FileHelper.h"
#include "Serialization/Archive.h"
#include "HAL/FileManager.h"

namespace CADLibrary
{

FArchive& operator<<(FArchive& Ar, FArchiveInstance& Instance) 
{
	Ar << Instance.ObjectId;
	Ar << Instance.MetaData;
	Ar << Instance.TransformMatrix;
	Ar << Instance.ReferenceNodeId;
	Ar << Instance.bIsExternalRef;
	Ar << Instance.ExternalRef;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveComponent& Component)
{
	Ar << Component.ObjectId;
	Ar << Component.MetaData;
	Ar << Component.Children;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveUnloadedComponent& Unloaded) 
{
	Ar << Unloaded.ObjectId;
	Ar << Unloaded.MetaData;
	Ar << Unloaded.FileName;
	Ar << Unloaded.FileType;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveBody& Body) 
{
	Ar << Body.ObjectId;
	Ar << Body.MetaData;
	Ar << Body.MaterialFaceSet;
	Ar << Body.ColorFaceSet;
	Ar << Body.MeshActorName;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveColor& Color) 
{
	Ar << Color.ObjectId;
	Ar << Color.Color;
	Ar << Color.UEMaterialName;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveMaterial& Material)
{
	Ar << Material.ObjectId;
	Ar << Material.Material;
	Ar << Material.UEMaterialName;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveMockUp& MockUp)
{
	Ar << MockUp.CADFile;
	Ar << MockUp.SceneGraphArchive;
	Ar << MockUp.FullPath;
	Ar << MockUp.ExternalRefSet;

	Ar << MockUp.ColorHIdToColor;
	Ar << MockUp.MaterialHIdToMaterial;

	Ar << MockUp.Instances;
	Ar << MockUp.ComponentSet;
	Ar << MockUp.UnloadedComponentSet;
	Ar << MockUp.BodySet;

	Ar << MockUp.CADIdToInstanceIndex;
	Ar << MockUp.CADIdToComponentIndex;
	Ar << MockUp.CADIdToUnloadedComponentIndex;
	Ar << MockUp.CADIdToBodyIndex;

	return Ar;
}

void FArchiveMockUp::SerializeMockUp(const TCHAR* Filename)
{
	TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileWriter(Filename));
	*Archive << *this;
	Archive->Close();
}

void FArchiveMockUp::DeserializeMockUpFile(const TCHAR* Filename)
{
	TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileReader(Filename));
	*Archive << *this;
	Archive->Close();
}

}


