// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFMeshSectionConverters.h"
#include "Converters/GLTFMaterialArray.h"

typedef TGLTFConverter<FGLTFJsonMesh*, const UStaticMesh*, const UStaticMeshComponent*, FGLTFMaterialArray, int32> IGLTFStaticMeshConverter;
typedef TGLTFConverter<FGLTFJsonMesh*, const USkeletalMesh*, const USkeletalMeshComponent*, FGLTFMaterialArray, int32> IGLTFSkeletalMeshConverter;

class GLTFEXPORTER_API FGLTFStaticMeshConverter final : public FGLTFBuilderContext, public IGLTFStaticMeshConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual void Sanitize(const UStaticMesh*& StaticMesh, const UStaticMeshComponent*& StaticMeshComponent, FGLTFMaterialArray& Materials, int32& LODIndex) override;

	virtual FGLTFJsonMesh* Convert(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, FGLTFMaterialArray Materials, int32 LODIndex) override;

	FGLTFStaticMeshSectionConverter MeshSectionConverter;
};

class GLTFEXPORTER_API FGLTFSkeletalMeshConverter final : public FGLTFBuilderContext, public IGLTFSkeletalMeshConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual void Sanitize(const USkeletalMesh*& SkeletalMesh, const USkeletalMeshComponent*& SkeletalMeshComponent, FGLTFMaterialArray& Materials, int32& LODIndex) override;

	virtual FGLTFJsonMesh* Convert(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, FGLTFMaterialArray Materials, int32 LODIndex) override;

	FGLTFSkeletalMeshSectionConverter MeshSectionConverter;
};
