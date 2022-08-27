// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Converters/GLTFMeshSectionConverters.h"
#include "Converters/GLTFMaterialArray.h"
#include "Engine.h"

class FGLTFStaticMeshTask : public FGLTFTask
{
public:

	FGLTFStaticMeshTask(FGLTFConvertBuilder& Builder, FGLTFStaticMeshSectionConverter& MeshSectionConverter, const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, FGLTFMaterialArray OverrideMaterials, FGLTFJsonMeshIndex MeshIndex)
		: FGLTFTask(EGLTFTaskPriority::Mesh)
		, Builder(Builder)
		, MeshSectionConverter(MeshSectionConverter)
		, StaticMesh(StaticMesh)
		, LODIndex(LODIndex)
		, OverrideVertexColors(OverrideVertexColors)
		, OverrideMaterials(OverrideMaterials)
        , MeshIndex(MeshIndex)
	{
	}

	virtual FString GetName() override
	{
		return StaticMesh->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	FGLTFStaticMeshSectionConverter& MeshSectionConverter;
	const UStaticMesh* StaticMesh;
	int32 LODIndex;
	const FColorVertexBuffer* OverrideVertexColors;
	FGLTFMaterialArray OverrideMaterials;
	const FGLTFJsonMeshIndex MeshIndex;
};

class FGLTFSkeletalMeshTask : public FGLTFTask
{
public:

	FGLTFSkeletalMeshTask(FGLTFConvertBuilder& Builder, FGLTFSkeletalMeshSectionConverter& MeshSectionConverter, const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, FGLTFMaterialArray OverrideMaterials, FGLTFJsonMeshIndex MeshIndex)
		: FGLTFTask(EGLTFTaskPriority::Mesh)
		, Builder(Builder)
		, MeshSectionConverter(MeshSectionConverter)
		, SkeletalMesh(SkeletalMesh)
		, LODIndex(LODIndex)
		, OverrideVertexColors(OverrideVertexColors)
		, OverrideSkinWeights(OverrideSkinWeights)
		, OverrideMaterials(OverrideMaterials)
		, MeshIndex(MeshIndex)
	{
	}

	virtual FString GetName() override
	{
		return SkeletalMesh->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	FGLTFSkeletalMeshSectionConverter& MeshSectionConverter;
	const USkeletalMesh* SkeletalMesh;
	int32 LODIndex;
	const FColorVertexBuffer* OverrideVertexColors;
	const FSkinWeightVertexBuffer* OverrideSkinWeights;
	FGLTFMaterialArray OverrideMaterials;
	const FGLTFJsonMeshIndex MeshIndex;
};
