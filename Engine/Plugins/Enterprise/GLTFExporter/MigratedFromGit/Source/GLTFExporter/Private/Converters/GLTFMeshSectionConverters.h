// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFConverter.h"
#include "Converters/GLTFMeshSection.h"
#include "Converters/GLTFHashableArray.h"

template <typename MeshLODType>
class TGLTFMeshSectionConverter final : public TGLTFConverter<const FGLTFMeshSection*, const MeshLODType*, FGLTFHashableArray<int32>>
{
	TArray<TUniquePtr<FGLTFMeshSection>> Outputs;

	const FGLTFMeshSection* Convert(const MeshLODType* MeshLOD, FGLTFHashableArray<int32> SectionIndices)
	{
		return Outputs.Add_GetRef(MakeUnique<FGLTFMeshSection>(MeshLOD, SectionIndices)).Get();
	}
};

typedef TGLTFMeshSectionConverter<FStaticMeshLODResources> FGLTFStaticMeshSectionConverter;
typedef TGLTFMeshSectionConverter<FSkeletalMeshLODRenderData> FGLTFSkeletalMeshSectionConverter;
