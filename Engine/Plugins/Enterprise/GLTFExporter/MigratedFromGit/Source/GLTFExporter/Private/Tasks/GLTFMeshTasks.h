// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Converters/GLTFMeshSectionConverters.h"
#include "Converters/GLTFMeshDataConverters.h"
#include "Converters/GLTFMaterialArray.h"
#include "Converters/GLTFNameUtility.h"
#include "Engine.h"

class FGLTFStaticMeshTask : public FGLTFTask
{
public:

	FGLTFStaticMeshTask(FGLTFConvertBuilder& Builder, FGLTFStaticMeshSectionConverter& MeshSectionConverter, FGLTFStaticMeshDataConverter& MeshDataConverter, const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, FGLTFMaterialArray OverrideMaterials, int32 LODIndex, FGLTFJsonMeshIndex MeshIndex)
		: FGLTFTask(EGLTFTaskPriority::Mesh)
		, Builder(Builder)
		, MeshSectionConverter(MeshSectionConverter)
		, MeshDataConverter(MeshDataConverter)
		, StaticMesh(StaticMesh)
		, StaticMeshComponent(StaticMeshComponent)
		, OverrideMaterials(OverrideMaterials)
		, LODIndex(LODIndex)
        , MeshIndex(MeshIndex)
	{
	}

	virtual FString GetName() override
	{
		return StaticMeshComponent != nullptr ? FGLTFNameUtility::GetName(StaticMeshComponent) : StaticMesh->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	FGLTFStaticMeshSectionConverter& MeshSectionConverter;
	FGLTFStaticMeshDataConverter& MeshDataConverter;
	const UStaticMesh* StaticMesh;
	const UStaticMeshComponent* StaticMeshComponent;
	const FGLTFMaterialArray OverrideMaterials;
	const int32 LODIndex;
	const FGLTFJsonMeshIndex MeshIndex;
};

class FGLTFSkeletalMeshTask : public FGLTFTask
{
public:

	FGLTFSkeletalMeshTask(FGLTFConvertBuilder& Builder, FGLTFSkeletalMeshSectionConverter& MeshSectionConverter, FGLTFSkeletalMeshDataConverter& MeshDataConverter, const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, FGLTFMaterialArray OverrideMaterials, int32 LODIndex, FGLTFJsonMeshIndex MeshIndex)
		: FGLTFTask(EGLTFTaskPriority::Mesh)
		, Builder(Builder)
		, MeshSectionConverter(MeshSectionConverter)
		, MeshDataConverter(MeshDataConverter)
		, SkeletalMesh(SkeletalMesh)
		, SkeletalMeshComponent(SkeletalMeshComponent)
		, OverrideMaterials(OverrideMaterials)
		, LODIndex(LODIndex)
		, MeshIndex(MeshIndex)
	{
	}

	virtual FString GetName() override
	{
		return SkeletalMeshComponent != nullptr ? FGLTFNameUtility::GetName(SkeletalMeshComponent) : SkeletalMesh->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	FGLTFSkeletalMeshSectionConverter& MeshSectionConverter;
	FGLTFSkeletalMeshDataConverter& MeshDataConverter;
	const USkeletalMesh* SkeletalMesh;
	const USkeletalMeshComponent* SkeletalMeshComponent;
	const FGLTFMaterialArray OverrideMaterials;
	const int32 LODIndex;
	const FGLTFJsonMeshIndex MeshIndex;
};
