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
	ICADArchiveObject(CadId Id = 0)
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
	FArchiveInstance(CadId Id = 0)
		: ICADArchiveObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveInstance& C);

public:
	FMatrix TransformMatrix = FMatrix::Identity;
	CadId ReferenceNodeId = 0;
	bool bIsExternalRef = false;
	FFileDescription ExternalRef;
};

class CADINTERFACES_API FArchiveComponent : public ICADArchiveObject
{
public:
	FArchiveComponent(CadId Id = 0)
		: ICADArchiveObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveComponent& C);

public:
	TArray<CadId> Children;
};

class CADINTERFACES_API FArchiveUnloadedComponent : public FArchiveComponent
{
public:
	FArchiveUnloadedComponent(CadId Id = 0)
		: FArchiveComponent(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveUnloadedComponent& C);
};

class CADINTERFACES_API FArchiveBody : public ICADArchiveObject
{
public:
	FArchiveBody(CadId Id = 0)
		: ICADArchiveObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveBody& C);

public:
	CADUUID MeshActorName;

	TSet<MaterialId> MaterialFaceSet;
	TSet<ColorId> ColorFaceSet;

};

class CADINTERFACES_API FArchiveColor
{
public:
	FArchiveColor(ColorId Id = 0)
		: ObjectId(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveColor& C);

public:
	ColorId ObjectId;
	FColor Color;
	CADUUID UEMaterialName;
};

class CADINTERFACES_API FArchiveMaterial
{
public:
	FArchiveMaterial(MaterialId Id = 0)
		: ObjectId(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveMaterial& C);

public:
	MaterialId ObjectId;
	CADUUID UEMaterialName;
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

	TMap<ColorId, FArchiveColor> ColorHIdToColor;
	TMap<MaterialId, FArchiveMaterial> MaterialHIdToMaterial;

	TArray<FArchiveBody> BodySet;
	TArray<FArchiveComponent> ComponentSet;
	TArray<FArchiveUnloadedComponent> UnloadedComponentSet;
	TArray<FFileDescription> ExternalRefSet;
	TArray<FArchiveInstance> Instances;

	TMap<CadId, int32> CADIdToBodyIndex;
	TMap<CadId, int32> CADIdToComponentIndex;
	TMap<CadId, int32> CADIdToUnloadedComponentIndex;
	TMap<CadId, int32> CADIdToInstanceIndex;
};


}


