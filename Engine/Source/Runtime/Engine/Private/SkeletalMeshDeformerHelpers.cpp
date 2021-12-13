// Copyright Epic Games, Inc. All Rights Reserved.
#include "SkeletalMeshDeformerHelpers.h"

#include "GPUSkinVertexFactory.h"
#include "SkeletalRenderGPUSkin.h"

FRHIShaderResourceView* FSkeletalMeshDeformerHelpers::GetBoneBufferForReading(
	FSkeletalMeshObject* MeshObject,
	int32 LODIndex,
	int32 SectionIndex,
	bool bPreviousFrame)
{
	if (MeshObject->IsCPUSkinned())
	{
		return nullptr;
	}

	FSkeletalMeshObjectGPUSkin* MeshObjectGPU = static_cast<FSkeletalMeshObjectGPUSkin*>(MeshObject);
	const FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD& LOD = MeshObjectGPU->LODs[LODIndex];
	FGPUBaseSkinVertexFactory* BaseVertexFactory = LOD.GPUSkinVertexFactories.VertexFactories[SectionIndex].Get();
	FShaderResourceViewRHIRef SRV = BaseVertexFactory->GetShaderData().GetBoneBufferForReading(bPreviousFrame).VertexBufferSRV;
	return SRV.IsValid() ? SRV : nullptr;
}

void FSkeletalMeshDeformerHelpers::SetVertexFactoryBufferOverrides(
	FSkeletalMeshObject* MeshObject,
	int32 LODIndex,
	EOverrideType OverrideType,
	TRefCountPtr<FRDGPooledBuffer> const& PositionBuffer,
	TRefCountPtr<FRDGPooledBuffer> const& TangentBuffer,
	TRefCountPtr<FRDGPooledBuffer> const& ColorBuffer)
{
	if (MeshObject->IsCPUSkinned())
	{
		return;
	}

	FGPUSkinPassthroughVertexFactory::EOverrideFlags OverrideFlags = FGPUSkinPassthroughVertexFactory::EOverrideFlags::All;
	if (OverrideType == EOverrideType::Partial)
	{
		OverrideFlags = FGPUSkinPassthroughVertexFactory::EOverrideFlags::None;
 		OverrideFlags |= (PositionBuffer.IsValid() ? FGPUSkinPassthroughVertexFactory::EOverrideFlags::Position : FGPUSkinPassthroughVertexFactory::EOverrideFlags::None);
 		OverrideFlags |= (TangentBuffer.IsValid() ? FGPUSkinPassthroughVertexFactory::EOverrideFlags::Tangent : FGPUSkinPassthroughVertexFactory::EOverrideFlags::None);
 		OverrideFlags |= (ColorBuffer.IsValid() ? FGPUSkinPassthroughVertexFactory::EOverrideFlags::Color : FGPUSkinPassthroughVertexFactory::EOverrideFlags::None);
	}

	FSkeletalMeshObjectGPUSkin* MeshObjectGPU = static_cast<FSkeletalMeshObjectGPUSkin*>(MeshObject);
	const FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectLOD& LOD = MeshObjectGPU->LODs[LODIndex];
	
	const int32 NumSections = MeshObject->GetRenderSections(LODIndex).Num();
	for (int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
	{
		FGPUBaseSkinVertexFactory* BaseVertexFactory = LOD.GPUSkinVertexFactories.VertexFactories[SectionIndex].Get();
		FGPUSkinPassthroughVertexFactory* TargetVertexFactory = LOD.GPUSkinVertexFactories.PassthroughVertexFactories[SectionIndex].Get();
		TargetVertexFactory->InvalidateStreams();
		TargetVertexFactory->UpdateVertexDeclaration(OverrideFlags, BaseVertexFactory, PositionBuffer, TangentBuffer, ColorBuffer);
	}
}

void FSkeletalMeshDeformerHelpers::ResetVertexFactoryBufferOverrides_GameThread(FSkeletalMeshObject* MeshObject, int32 LODIndex)
{
	if (MeshObject->IsCPUSkinned())
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(ResetSkinPassthroughVertexFactory)([MeshObject, LODIndex](FRHICommandList& CmdList)
	{
		SetVertexFactoryBufferOverrides(MeshObject, LODIndex, EOverrideType::All, nullptr, nullptr, nullptr);
	});
}
