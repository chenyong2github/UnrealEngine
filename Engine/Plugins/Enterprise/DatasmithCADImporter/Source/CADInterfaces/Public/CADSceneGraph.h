// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"

class FArchive;

namespace CADLibrary
{

class CADINTERFACES_API ICADArchiveObject
{
public:
	ICADArchiveObject(FCadId Id = 0)
		: ObjectId(Id)
	{
	}

	virtual ~ICADArchiveObject() = default;

public:
	uint32 ObjectId;
	TMap<FString, FString> MetaData;
};

class CADINTERFACES_API FArchiveInstance : public ICADArchiveObject
{
public:
	FArchiveInstance(FCadId Id = 0)
		: ICADArchiveObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveInstance& C);

public:
	FMatrix TransformMatrix = FMatrix::Identity;
	FCadId ReferenceNodeId = 0;
	bool bIsExternalRef = false;
	FFileDescription ExternalRef;
};

class CADINTERFACES_API FArchiveComponent : public ICADArchiveObject
{
public:
	FArchiveComponent(FCadId Id = 0)
		: ICADArchiveObject(Id)
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

class CADINTERFACES_API FArchiveBody : public ICADArchiveObject
{
public:
	FArchiveBody(FCadId Id = 0)
		: ICADArchiveObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveBody& C);

public:
	FCADUUID MeshActorName;

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

	TArray<FArchiveBody> BodySet;
	TArray<FArchiveComponent> ComponentSet;
	TArray<FArchiveUnloadedComponent> UnloadedComponentSet;
	TArray<FFileDescription> ExternalRefSet;
	TArray<FArchiveInstance> Instances;

	TMap<FCadId, int32> CADIdToBodyIndex;
	TMap<FCadId, int32> CADIdToComponentIndex;
	TMap<FCadId, int32> CADIdToUnloadedComponentIndex;
	TMap<FCadId, int32> CADIdToInstanceIndex;
};


}


