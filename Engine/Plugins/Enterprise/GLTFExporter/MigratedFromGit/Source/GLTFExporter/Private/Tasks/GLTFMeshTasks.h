// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Converters/GLTFMeshSectionConverters.h"
#include "Converters/GLTFMaterialArray.h"
#include "Converters/GLTFNameUtility.h"
#include "Engine.h"

class FGLTFStaticMeshTask : public FGLTFTask
{
public:

	FGLTFStaticMeshTask(FGLTFConvertBuilder& Builder, FGLTFStaticMeshSectionConverter& MeshSectionConverter, const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex, FGLTFMaterialArray OverrideMaterials, FGLTFJsonMeshIndex MeshIndex)
		: FGLTFTask(EGLTFTaskPriority::Mesh)
		, Builder(Builder)
		, MeshSectionConverter(MeshSectionConverter)
		, StaticMesh(StaticMesh)
		, StaticMeshComponent(StaticMeshComponent)
		, LODIndex(LODIndex)
		, OverrideMaterials(OverrideMaterials)
        , MeshIndex(MeshIndex)
	{
	}

	virtual FString GetName() override
	{
		return StaticMeshComponent != nullptr ? FGLTFNameUtility::GetName(StaticMeshComponent) : FGLTFNameUtility::GetName(StaticMesh, LODIndex);
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	FGLTFStaticMeshSectionConverter& MeshSectionConverter;
	const UStaticMesh* StaticMesh;
	const UStaticMeshComponent* StaticMeshComponent;
	int32 LODIndex;
	FGLTFMaterialArray OverrideMaterials;
	const FGLTFJsonMeshIndex MeshIndex;
};

class FGLTFSkeletalMeshTask : public FGLTFTask
{
public:

	FGLTFSkeletalMeshTask(FGLTFConvertBuilder& Builder, FGLTFSkeletalMeshSectionConverter& MeshSectionConverter, const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex, FGLTFMaterialArray OverrideMaterials, FGLTFJsonMeshIndex MeshIndex)
		: FGLTFTask(EGLTFTaskPriority::Mesh)
		, Builder(Builder)
		, MeshSectionConverter(MeshSectionConverter)
		, SkeletalMesh(SkeletalMesh)
		, SkeletalMeshComponent(SkeletalMeshComponent)
		, LODIndex(LODIndex)
		, OverrideMaterials(OverrideMaterials)
		, MeshIndex(MeshIndex)
	{
	}

	virtual FString GetName() override
	{
		return SkeletalMeshComponent != nullptr ? FGLTFNameUtility::GetName(SkeletalMeshComponent) : FGLTFNameUtility::GetName(SkeletalMesh, LODIndex);
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	FGLTFSkeletalMeshSectionConverter& MeshSectionConverter;
	const USkeletalMesh* SkeletalMesh;
	const USkeletalMeshComponent* SkeletalMeshComponent;
	int32 LODIndex;
	FGLTFMaterialArray OverrideMaterials;
	const FGLTFJsonMeshIndex MeshIndex;
};
