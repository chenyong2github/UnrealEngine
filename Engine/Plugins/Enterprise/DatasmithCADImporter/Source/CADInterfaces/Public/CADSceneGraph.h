// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"

class FArchive;

namespace CADLibrary
{

class CADINTERFACES_API FCADArchiveObject
{
public:
	FCADArchiveObject(FCadId Id = 0)
		: ObjectId(Id)
	{
	}

	virtual ~FCADArchiveObject() = default;

	friend FArchive& operator<<(FArchive& Ar, FCADArchiveObject& C);

public:
	uint32 ObjectId;
	TMap<FString, FString> MetaData;
	FMatrix TransformMatrix = FMatrix::Identity;
};

class CADINTERFACES_API FArchiveInstance : public FCADArchiveObject
{
public:
	FArchiveInstance(FCadId Id = 0)
		: FCADArchiveObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveInstance& C);

public:
	FCadId ReferenceNodeId = 0;
	bool bIsExternalReference = false;
	FFileDescriptor ExternalReference;
};

class CADINTERFACES_API FArchiveComponent : public FCADArchiveObject
{
public:
	FArchiveComponent(FCadId Id = 0)
		: FCADArchiveObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveComponent& C);

public:
	TArray<FCadId> Children;
};

class CADINTERFACES_API FArchiveUnloadedComponent : public FArchiveComponent
{
public:
	FArchiveUnloadedComponent(FCadId Id = 0)
		: FArchiveComponent(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveUnloadedComponent& C);
};

class CADINTERFACES_API FArchiveBody : public FCADArchiveObject
{
public:
	FArchiveBody(FCadId Id = 0)
		: FCADArchiveObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveBody& C);

public:
	FCadId ParentId = 0;
	FCADUUID MeshActorName = 0;
	double BodyUnit = 1.;

	TSet<FMaterialId> MaterialFaceSet;
	TSet<FColorId> ColorFaceSet;

};

class CADINTERFACES_API FArchiveColor
{
public:
	FArchiveColor(FColorId Id = 0)
		: ObjectId(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveColor& C);

public:
	FColorId ObjectId;
	FColor Color;
	FCADUUID UEMaterialName;
};

class CADINTERFACES_API FArchiveMaterial
{
public:
	FArchiveMaterial(FMaterialId Id = 0)
		: ObjectId(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveMaterial& C);

public:
	FMaterialId ObjectId;
	FCADUUID UEMaterialName;
	FCADMaterial Material;
};

class CADINTERFACES_API FArchiveSceneGraph
{
public:
	friend FArchive& operator<<(FArchive& Ar, FArchiveSceneGraph& C);

	void SerializeMockUp(const TCHAR* Filename);
	void DeserializeMockUpFile(const TCHAR* Filename);

public:
	FString CADFileName;
	FString ArchiveFileName;
	FString FullPath;

	TMap<FColorId, FArchiveColor> ColorHIdToColor;
	TMap<FMaterialId, FArchiveMaterial> MaterialHIdToMaterial;

	TArray<FArchiveBody> Bodies;
	TArray<FArchiveComponent> Components;
	TArray<FArchiveUnloadedComponent> UnloadedComponents;
	TArray<FFileDescriptor> ExternalReferences;
	TArray<FArchiveInstance> Instances;

	TMap<FCadId, int32> CADIdToBodyIndex;
	TMap<FCadId, int32> CADIdToComponentIndex;
	TMap<FCadId, int32> CADIdToUnloadedComponentIndex;
	TMap<FCadId, int32> CADIdToInstanceIndex;

	void Reserve(int32 InstanceNum, int32 ComponentNum, int32 BodyNum)
	{
		Instances.Reserve(InstanceNum);
		Components.Reserve(ComponentNum);
		UnloadedComponents.Reserve(ComponentNum);
		ExternalReferences.Reserve(ComponentNum);
		Bodies.Reserve(BodyNum);

		CADIdToInstanceIndex.Reserve(InstanceNum);
		CADIdToComponentIndex.Reserve(ComponentNum);
		CADIdToUnloadedComponentIndex.Reserve(ComponentNum);
		CADIdToBodyIndex.Reserve(BodyNum);
	}
};


}


