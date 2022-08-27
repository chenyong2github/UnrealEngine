// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFConverter.h"
#include "Converters/GLTFMeshSection.h"
#include "Engine.h"

template <typename MeshLODType, typename MaterialIndexType>
class TGLTFMeshSectionConverter final : public TGLTFConverter<const FGLTFMeshSection*, const MeshLODType*, const MaterialIndexType>
{
	TArray<TUniquePtr<FGLTFMeshSection>> Outputs;

	const FGLTFMeshSection* Convert(const MeshLODType* MeshLOD, const MaterialIndexType MaterialIndex)
	{
		return Outputs.Add_GetRef(MakeUnique<FGLTFMeshSection>(MeshLOD, MaterialIndex)).Get();
	}
};

typedef TGLTFMeshSectionConverter<FStaticMeshLODResources, int32> FGLTFStaticMeshSectionConverter;
typedef TGLTFMeshSectionConverter<FSkeletalMeshLODRenderData, uint16> FGLTFSkeletalMeshSectionConverter;
