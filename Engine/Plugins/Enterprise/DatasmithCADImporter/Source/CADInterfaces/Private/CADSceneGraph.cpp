// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADSceneGraph.h"

#include "Misc/FileHelper.h"
#include "Serialization/Archive.h"
#include "HAL/FileManager.h"

namespace CADLibrary
{

FArchive& operator<<(FArchive& Ar, FArchiveCADObject& Object)
{
	Ar << Object.Id;
	Ar << Object.MetaData;
	Ar << Object.TransformMatrix;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveInstance& Instance) 
{
	Ar << (FArchiveCADObject&) Instance;
	Ar << Instance.ReferenceNodeId;
	Ar << Instance.bIsExternalReference;
	Ar << Instance.ExternalReference;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveReference& Component)
{
	Ar << (FArchiveCADObject&) Component;
	Ar << Component.Children;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveUnloadedReference& Unloaded) 
{
	Ar << (FArchiveReference&) Unloaded;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveBody& Body) 
{
	Ar << (FArchiveCADObject&) Body;
	Ar << Body.MaterialFaceSet;
	Ar << Body.ColorFaceSet;
	Ar << Body.ParentId;
	Ar << Body.MeshActorUId;
	Ar << Body.BodyUnit;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveColor& Color) 
{
	Ar << Color.Id;
	Ar << Color.Color;
	Ar << Color.UEMaterialUId;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveMaterial& Material)
{
	Ar << Material.Id;
	Ar << Material.Material;
	Ar << Material.UEMaterialUId;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveSceneGraph& SceneGraph)
{
	Ar << SceneGraph.CADFileName;
	Ar << SceneGraph.ArchiveFileName;
	Ar << SceneGraph.FullPath;
	Ar << SceneGraph.ExternalReferenceFiles;

	Ar << SceneGraph.ColorHIdToColor;
	Ar << SceneGraph.MaterialHIdToMaterial;

	Ar << SceneGraph.Instances;
	Ar << SceneGraph.References;
	Ar << SceneGraph.UnloadedReferences;
	Ar << SceneGraph.Bodies;

	Ar << SceneGraph.CADIdToInstanceIndex;
	Ar << SceneGraph.CADIdToReferenceIndex;
	Ar << SceneGraph.CADIdToUnloadedReferenceIndex;
	Ar << SceneGraph.CADIdToBodyIndex;

	return Ar;
}

void FArchiveSceneGraph::SerializeMockUp(const TCHAR* Filename)
{
	TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileWriter(Filename));
	if (Archive)
	{
		*Archive << *this;
		Archive->Close();
	}
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


