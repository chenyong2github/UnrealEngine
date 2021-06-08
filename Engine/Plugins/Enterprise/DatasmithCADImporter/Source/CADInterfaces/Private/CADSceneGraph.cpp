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

FArchive& operator<<(FArchive& Ar, FArchiveSceneGraph& SceneGraph)
{
	Ar << SceneGraph.CADFileName;
	Ar << SceneGraph.ArchiveFileName;
	Ar << SceneGraph.FullPath;
	Ar << SceneGraph.ExternalRefSet;

	Ar << SceneGraph.ColorHIdToColor;
	Ar << SceneGraph.MaterialHIdToMaterial;

	Ar << SceneGraph.Instances;
	Ar << SceneGraph.ComponentSet;
	Ar << SceneGraph.UnloadedComponentSet;
	Ar << SceneGraph.BodySet;

	Ar << SceneGraph.CADIdToInstanceIndex;
	Ar << SceneGraph.CADIdToComponentIndex;
	Ar << SceneGraph.CADIdToUnloadedComponentIndex;
	Ar << SceneGraph.CADIdToBodyIndex;

	return Ar;
}

void FArchiveSceneGraph::SerializeMockUp(const TCHAR* Filename)
{
	TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileWriter(Filename));
	*Archive << *this;
	Archive->Close();
}

void FArchiveSceneGraph::DeserializeMockUpFile(const TCHAR* Filename)
{
	TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileReader(Filename));
	if (Archive.IsValid())
	{
		*Archive << *this;
		Archive->Close();
	}
}

}


