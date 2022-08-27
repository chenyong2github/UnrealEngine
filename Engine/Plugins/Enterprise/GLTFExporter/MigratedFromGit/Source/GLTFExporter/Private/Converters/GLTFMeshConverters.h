// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFMeshSectionConverters.h"
#include "Converters/GLTFMaterialArray.h"
#include "Engine.h"

template <typename... InputTypes>
class TGLTFMeshConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonMeshIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

class FGLTFStaticMeshConverter final : public TGLTFMeshConverter<const UStaticMesh*, const UStaticMeshComponent*, FGLTFMaterialArray>
{
	using TGLTFMeshConverter::TGLTFMeshConverter;

	virtual void Sanitize(const UStaticMesh*& StaticMesh, const UStaticMeshComponent*& StaticMeshComponent, FGLTFMaterialArray& OverrideMaterials) override;

	virtual FGLTFJsonMeshIndex Convert(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, FGLTFMaterialArray OverrideMaterials) override;

	FGLTFStaticMeshSectionConverter MeshSectionConverter;
};

class FGLTFSkeletalMeshConverter final : public TGLTFMeshConverter<const USkeletalMesh*, const USkeletalMeshComponent*, FGLTFMaterialArray>
{
	using TGLTFMeshConverter::TGLTFMeshConverter;

	virtual void Sanitize(const USkeletalMesh*& SkeletalMesh, const USkeletalMeshComponent*& SkeletalMeshComponent, FGLTFMaterialArray& OverrideMaterials) override;

	virtual FGLTFJsonMeshIndex Convert(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, FGLTFMaterialArray OverrideMaterials) override;

	FGLTFSkeletalMeshSectionConverter MeshSectionConverter;
};
