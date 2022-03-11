// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADSceneGraph.h"

#include "Misc/FileHelper.h"
#include "Serialization/Archive.h"
#include "HAL/FileManager.h"

namespace CADLibrary
{

FArchive& operator<<(FArchive& Ar, FCADArchiveObject& Object)
{
	Ar << Object.ObjectId;
	Ar << Object.MetaData;
	Ar << Object.TransformMatrix;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveInstance& Instance) 
{
	Ar << (FCADArchiveObject&) Instance;
	Ar << Instance.ReferenceNodeId;
	Ar << Instance.bIsExternalReference;
	Ar << Instance.ExternalReference;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveComponent& Component)
{
	Ar << (FCADArchiveObject&) Component;
	Ar << Component.Children;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveUnloadedComponent& Unloaded) 
{
	Ar << (FArchiveComponent&) Unloaded;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FArchiveBody& Body) 
{
	Ar << (FCADArchiveObject&) Body;
	Ar << Body.MaterialFaceSet;
	Ar << Body.ColorFaceSet;
	Ar << Body.ParentId;
	Ar << Body.MeshActorName;
	Ar << Body.BodyUnit;

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
	Ar << SceneGraph.ExternalReferences;

	Ar << SceneGraph.ColorHIdToColor;
	Ar << SceneGraph.MaterialHIdToMaterial;

	Ar << SceneGraph.Instances;
	Ar << SceneGraph.Components;
	Ar << SceneGraph.UnloadedComponents;
	Ar << SceneGraph.Bodies;

	Ar << SceneGraph.CADIdToInstanceIndex;
	Ar << SceneGraph.CADIdToComponentIndex;
	Ar << SceneGraph.CADIdToUnloadedComponentIndex;
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


