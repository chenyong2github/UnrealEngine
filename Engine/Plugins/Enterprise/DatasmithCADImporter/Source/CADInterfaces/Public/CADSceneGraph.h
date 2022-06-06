// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"

class FArchive;

namespace CADLibrary
{

class CADINTERFACES_API FArchiveCADObject
{
public:
	FArchiveCADObject(FCadId InId = 0)
		: Id(InId)
	{
	}

	virtual ~FArchiveCADObject() = default;

	friend FArchive& operator<<(FArchive& Ar, FArchiveCADObject& C);

public:
	uint32 Id;
	TMap<FString, FString> MetaData;
	FMatrix TransformMatrix = FMatrix::Identity;
};

class CADINTERFACES_API FArchiveInstance : public FArchiveCADObject
{
public:
	FArchiveInstance(FCadId Id = 0)
		: FArchiveCADObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveInstance& C);

public:
	FCadId ReferenceNodeId = 0;
	bool bIsExternalReference = false;
	FFileDescriptor ExternalReference;
};

class CADINTERFACES_API FArchiveReference : public FArchiveCADObject
{
public:
	FArchiveReference(FCadId Id = 0)
		: FArchiveCADObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveReference& C);

public:
	TArray<FCadId> Children;
};

class CADINTERFACES_API FArchiveUnloadedReference : public FArchiveReference
{
public:
	FArchiveUnloadedReference(FCadId Id = 0)
		: FArchiveReference(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveUnloadedReference& C);
};

class CADINTERFACES_API FArchiveBody : public FArchiveCADObject
{
public:
	FArchiveBody(FCadId Id = 0)
		: FArchiveCADObject(Id)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveBody& C);

public:
	FCadId ParentId = 0;
	FCadUuid MeshActorUId = 0;
	double BodyUnit = 1.;

	TSet<FMaterialUId> MaterialFaceSet;
	TSet<FMaterialUId> ColorFaceSet;

};

class CADINTERFACES_API FArchiveColor
{
public:
	FArchiveColor(FMaterialUId InId = 0)
		: Id(InId)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveColor& C);

public:
	FMaterialUId Id;
	FColor Color;
	FMaterialUId UEMaterialUId;
};

class CADINTERFACES_API FArchiveMaterial
{
public:
	FArchiveMaterial(FMaterialUId InId = 0)
		: Id(InId)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FArchiveMaterial& C);

public:
	FMaterialUId Id;
	FCADMaterial Material;
	FMaterialUId UEMaterialUId;
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

	TMap<FMaterialUId, FArchiveColor> ColorHIdToColor;
	TMap<FMaterialUId, FArchiveMaterial> MaterialHIdToMaterial;

	TArray<FArchiveBody> Bodies;
	TArray<FArchiveReference> References;
	TArray<FArchiveUnloadedReference> UnloadedReferences;
	TArray<FFileDescriptor> ExternalReferenceFiles;
	TArray<FArchiveInstance> Instances;

	TMap<FCadId, int32> CADIdToBodyIndex;
	TMap<FCadId, int32> CADIdToReferenceIndex;
	TMap<FCadId, int32> CADIdToUnloadedReferenceIndex;
	TMap<FCadId, int32> CADIdToInstanceIndex;

	void Reserve(int32 InstanceNum, int32 ReferenceNum, int32 BodyNum)
	{
		Instances.Reserve(InstanceNum);
		References.Reserve(ReferenceNum);
		UnloadedReferences.Reserve(ReferenceNum);
		ExternalReferenceFiles.Reserve(ReferenceNum);
		Bodies.Reserve(BodyNum);

		CADIdToInstanceIndex.Reserve(InstanceNum);
		CADIdToReferenceIndex.Reserve(ReferenceNum);
		CADIdToUnloadedReferenceIndex.Reserve(ReferenceNum);
		CADIdToBodyIndex.Reserve(BodyNum);
	}
};


}


