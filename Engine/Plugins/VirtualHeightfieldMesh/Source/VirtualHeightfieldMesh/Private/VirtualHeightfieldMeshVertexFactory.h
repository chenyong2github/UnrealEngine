// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "VertexFactory.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "SceneManagement.h"

/**
 * Uniform buffer to hold parameters specific to this vertex factory. Only set up once
 */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVirtualHeightfieldMeshVertexFactoryParameters, )
	SHADER_PARAMETER(float, LODScale)
	SHADER_PARAMETER(int32, NumQuadsPerTileSide)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FVirtualHeightfieldMeshVertexFactoryParameters> FVirtualHeightfieldMeshVertexFactoryBufferRef;

struct FVirtualHeightfieldMeshUserData : public FOneFrameResource
{
	FRHIShaderResourceView* InstanceBufferSRV;
	FRHITexture* HeightPhysicalTexture;
	FVector4 PageTableSize;
	float MaxLod;
	FMatrix VirtualHeightfieldToLocal;
	FMatrix VirtualHeightfieldToWorld;
	FVector LodViewOrigin;
	FVector4 LodDistances;
};

/**
 * 
 */
class FVirtualHeightfieldMeshIndexBuffer : public FIndexBuffer
{
public:

	FVirtualHeightfieldMeshIndexBuffer(int32 InNumQuadsPerSide) 
		: NumQuadsPerSide(InNumQuadsPerSide) 
	{}

	virtual void InitRHI() override;

	int32 GetIndexCount() const { return NumIndices; }

private:
	int32 NumIndices = 0;
	const int32 NumQuadsPerSide = 0;
};

/**
 *
 */
class FVirtualHeightfieldMeshVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FVirtualHeightfieldMeshVertexFactory);

public:
	FVirtualHeightfieldMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, int32 InNumQuadsPerSide);

	~FVirtualHeightfieldMeshVertexFactory();

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);

	inline const FUniformBufferRHIRef GetVirtualHeightfieldMeshVertexFactoryUniformBuffer() const
	{
		return UniformBuffer;
	}

	FVirtualHeightfieldMeshIndexBuffer* IndexBuffer = nullptr;

private:
	FVirtualHeightfieldMeshVertexFactoryBufferRef UniformBuffer;

	const int32 NumQuadsPerSide = 0;
};
