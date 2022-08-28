// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFConverter.h"
#include "Converters/GLTFMeshData.h"
#include "Engine.h"

template <typename MeshType, typename MeshComponentType>
class TGLTFMeshDataConverter final : public TGLTFConverter<const FGLTFMeshData*, const MeshType*, const MeshComponentType*, int32>
{
	TArray<TUniquePtr<FGLTFMeshData>> Outputs;

	const FGLTFMeshData* Convert(const MeshType* Mesh, const MeshComponentType* MeshComponent, int32 LODIndex)
	{
		return Outputs.Add_GetRef(MakeUnique<FGLTFMeshData>(Mesh, MeshComponent, LODIndex)).Get();
	}
};

typedef TGLTFMeshDataConverter<UStaticMesh, UStaticMeshComponent> FGLTFStaticMeshDataConverter;
typedef TGLTFMeshDataConverter<USkeletalMesh, USkeletalMeshComponent> FGLTFSkeletalMeshDataConverter;
