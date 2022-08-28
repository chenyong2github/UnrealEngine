// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Converters/GLTFMeshSection.h"
#include "Converters/GLTFMaterialArray.h"
#include "Engine.h"

template <typename... InputTypes>
class TGLTFMeshConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonMeshIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

template <typename MeshLODType, typename MaterialIndexType>
class TGLTFMeshSectionConverter final : public TGLTFConverter<const FGLTFMeshSection*, const MeshLODType*, const MaterialIndexType>
{
	TArray<TUniquePtr<FGLTFMeshSection>> Outputs;

	const FGLTFMeshSection* Convert(const MeshLODType* MeshLOD, const MaterialIndexType MaterialIndex)
	{
		return Outputs.Add_GetRef(MakeUnique<FGLTFMeshSection>(MeshLOD, MaterialIndex)).Get();
	}
};

class FGLTFStaticMeshConverter final : public TGLTFMeshConverter<const UStaticMesh*, int32, const FColorVertexBuffer*, FGLTFMaterialArray>
{
	using TGLTFMeshConverter::TGLTFMeshConverter;

	virtual void Sanitize(const UStaticMesh*& StaticMesh, int32& LODIndex, const FColorVertexBuffer*& OverrideVertexColors, FGLTFMaterialArray& OverrideMaterials) override;

	virtual FGLTFJsonMeshIndex Convert(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, FGLTFMaterialArray OverrideMaterials) override;

	TGLTFMeshSectionConverter<FStaticMeshLODResources, int32> MeshSectionConverter;
};

class FGLTFSkeletalMeshConverter final : public TGLTFMeshConverter<const USkeletalMesh*, int32, const FColorVertexBuffer*, const FSkinWeightVertexBuffer*, FGLTFMaterialArray>
{
	using TGLTFMeshConverter::TGLTFMeshConverter;

	virtual void Sanitize(const USkeletalMesh*& SkeletalMesh, int32& LODIndex, const FColorVertexBuffer*& OverrideVertexColors, const FSkinWeightVertexBuffer*& OverrideSkinWeights, FGLTFMaterialArray& OverrideMaterials) override;

	virtual FGLTFJsonMeshIndex Convert(const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, FGLTFMaterialArray OverrideMaterials) override;

	TGLTFMeshSectionConverter<FSkeletalMeshLODRenderData, uint16> MeshSectionConverter;
};
