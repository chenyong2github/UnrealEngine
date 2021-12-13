// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"

class FSkeletalMeshObject;
class FRDGPooledBuffer;
class FRHIShaderResourceView;

/** Functions that expose some functionality of FSkeletalMeshObject required by MeshDeformer systems. */
class FSkeletalMeshDeformerHelpers
{
public:
	/** Get direct access to bone matrix buffer SRV. */
	ENGINE_API static FRHIShaderResourceView* GetBoneBufferForReading(
		FSkeletalMeshObject* MeshObject,
		int32 LODIndex,
		int32 SectionIndex,
		bool bPreviousFrame);

	/** Buffer override behavior for SetVertexFactoryBufferOverrides. */
	enum class EOverrideType
	{
		All,		// Clear overrides for each buffer input that is null.
		Partial,	// Leave existing overrides for each buffer input that is null.
	};

	/** Apply buffer overrides to the pass through vertex factory. */
	ENGINE_API static void SetVertexFactoryBufferOverrides(
		FSkeletalMeshObject* MeshObject,
		int32 LODIndex, 
		EOverrideType OverrideType,
		TRefCountPtr<FRDGPooledBuffer> const& PositionBuffer, 
		TRefCountPtr<FRDGPooledBuffer> const& TangentBuffer, 
		TRefCountPtr<FRDGPooledBuffer> const& ColorBuffer);

	/** Reset all buffer overrides that were applied through SetVertexFactoryBufferOverrides. */
	ENGINE_API static void ResetVertexFactoryBufferOverrides_GameThread(FSkeletalMeshObject* MeshObject, int32 LODIndex);
};
