// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFSkeletalMeshConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Builders/GLTFConvertBuilder.h"

FGLTFJsonBufferViewIndex FGLTFIndexContainerConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FMultiSizeIndexContainer* IndexContainer)
{
	return FGLTFJsonBufferViewIndex(INDEX_NONE);
}

FGLTFJsonAccessorIndex FGLTFSkeletalMeshSectionConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const FSkelMeshRenderSection* MeshSection, const FMultiSizeIndexContainer* IndexContainer)
{
	return FGLTFJsonAccessorIndex(INDEX_NONE);
}

FGLTFJsonMeshIndex FGLTFSkeletalMeshConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, FGLTFMaterialArray OverrideMaterials)
{
	return FGLTFJsonMeshIndex(INDEX_NONE);
}
