// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshDataConverters.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFConvertBuilder.h"

template class TGLTFMeshDataConverter<UStaticMesh, UStaticMeshComponent>;
template class TGLTFMeshDataConverter<USkeletalMesh, USkeletalMeshComponent>;

template <typename MeshType, typename MeshComponentType>
void TGLTFMeshDataConverter<MeshType, MeshComponentType>::Sanitize(const MeshType*& Mesh, const MeshComponentType*& MeshComponent, int32& LODIndex)
{
	if (LODIndex < 0)
	{
		LODIndex = FGLTFMeshUtility::GetLOD(Mesh, MeshComponent, Builder.ExportOptions->DefaultLevelOfDetail);
	}
	else
	{
		LODIndex = FMath::Min(LODIndex, FGLTFMeshUtility::GetMaximumLOD(Mesh));
	}
}

template <typename MeshType, typename MeshComponentType>
const FGLTFMeshData* TGLTFMeshDataConverter<MeshType, MeshComponentType>::Convert(const MeshType* Mesh, const MeshComponentType* MeshComponent, int32 LODIndex)
{
	TUniquePtr<FGLTFMeshData> Output = MakeUnique<FGLTFMeshData>(Mesh, MeshComponent, LODIndex);

	if (MeshComponent != nullptr)
	{
		Output->Parent = this->GetOrAdd(Mesh, nullptr, LODIndex);
	}

	return Outputs.Add_GetRef(MoveTemp(Output)).Get();
}
