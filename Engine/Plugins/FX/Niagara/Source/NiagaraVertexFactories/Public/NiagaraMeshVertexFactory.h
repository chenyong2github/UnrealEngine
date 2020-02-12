// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
ParticleVertexFactory.h: Particle vertex factory definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "NiagaraVertexFactory.h"
#include "../../Niagara/Classes/NiagaraDataSet.h"
#include "SceneView.h"
#include "Components.h"
#include "SceneManagement.h"
#include "VertexFactory.h"



class FMaterial;
class FVertexBuffer;
struct FDynamicReadBuffer;
struct FShaderCompilerEnvironment;


/**
* Uniform buffer for mesh particle vertex factories.
*/
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNiagaraMeshUniformParameters, NIAGARAVERTEXFACTORIES_API)
	SHADER_PARAMETER(uint32, bLocalSpace)
	SHADER_PARAMETER(FVector4, SubImageSize)
	SHADER_PARAMETER(uint32, TexCoordWeightA)
	SHADER_PARAMETER(uint32, TexCoordWeightB)
	SHADER_PARAMETER(uint32, PrevTransformAvailable)
	SHADER_PARAMETER(float, DeltaSeconds)
	SHADER_PARAMETER(int, PositionDataOffset)
	SHADER_PARAMETER(int, VelocityDataOffset)
	SHADER_PARAMETER(int, ColorDataOffset)
	SHADER_PARAMETER(int, TransformDataOffset)
	SHADER_PARAMETER(int, ScaleDataOffset)
	SHADER_PARAMETER(int, SizeDataOffset)
	SHADER_PARAMETER(uint32, MaterialParamValidMask)
	SHADER_PARAMETER(int, SubImageDataOffset)
	SHADER_PARAMETER(int, MaterialParamDataOffset)
	SHADER_PARAMETER(int, MaterialParam1DataOffset)
	SHADER_PARAMETER(int, MaterialParam2DataOffset)
	SHADER_PARAMETER(int, MaterialParam3DataOffset)
	SHADER_PARAMETER(int, NormalizedAgeDataOffset)
	SHADER_PARAMETER(int, MaterialRandomDataOffset)
	SHADER_PARAMETER(int, CameraOffsetDataOffset)
	SHADER_PARAMETER(FVector4, DefaultPos)
	SHADER_PARAMETER(int, SubImageBlendMode)
	SHADER_PARAMETER(uint32, FacingMode)
	SHADER_PARAMETER(uint32, bLockedAxisEnable)
	SHADER_PARAMETER(FVector, LockedAxis)
	SHADER_PARAMETER(uint32, LockedAxisSpace)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FNiagaraMeshUniformParameters> FNiagaraMeshUniformBufferRef;

class FNiagaraMeshInstanceVertices;


/**
* Vertex factory for rendering instanced mesh particles with out dynamic parameter support.
*/
class NIAGARAVERTEXFACTORIES_API FNiagaraMeshVertexFactory : public FNiagaraVertexFactoryBase
{
	DECLARE_VERTEX_FACTORY_TYPE(FNiagaraMeshVertexFactory);
public:
	
	/** Default constructor. */
	FNiagaraMeshVertexFactory(ENiagaraVertexFactoryType InType, ERHIFeatureLevel::Type InFeatureLevel)
		: FNiagaraVertexFactoryBase(InType, InFeatureLevel)
		, LODIndex(-1)
		, InstanceVerticesCPU(nullptr)
		, FloatDataStride(0)
		, SortedIndicesOffset(0)
	{}

	FNiagaraMeshVertexFactory()
		: FNiagaraVertexFactoryBase(NVFT_MAX, ERHIFeatureLevel::Num)
		, LODIndex(-1)
		, InstanceVerticesCPU(nullptr)
		, FloatDataStride(0)
		, SortedIndicesOffset(0)
	{}

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory?
	*/
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);


	/**
	* Modify compile environment to enable instancing
	* @param OutEnvironment - shader compile environment to modify
	*/
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNiagaraVertexFactoryBase::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Set a define so we can tell in MaterialTemplate.usf when we are compiling a mesh particle vertex factory
		OutEnvironment.SetDefine(TEXT("NIAGARA_MESH_FACTORY"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("NIAGARA_MESH_INSTANCED"), TEXT("1"));
	}

	void SetParticleData(const FShaderResourceViewRHIRef& InParticleDataFloatSRV, uint32 InFloatDataStride)
	{
		ParticleDataFloatSRV = InParticleDataFloatSRV;
		FloatDataStride = InFloatDataStride;
	}

	void SetSortedIndices(const FShaderResourceViewRHIRef& InSortedIndicesSRV, uint32 InSortedIndicesOffset)
	{
		SortedIndicesSRV = InSortedIndicesSRV;
		SortedIndicesOffset = InSortedIndicesOffset;
	}

	FORCEINLINE FRHIShaderResourceView* GetParticleDataFloatSRV()
	{
		return ParticleDataFloatSRV;
	}

	FORCEINLINE int32 GetFloatDataStride()
	{
		return FloatDataStride;
	}

	FORCEINLINE FRHIShaderResourceView* GetSortedIndicesSRV()
	{
		return SortedIndicesSRV;
	}

	FORCEINLINE int32 GetSortedIndicesOffset()
	{
		return SortedIndicesOffset;
	}

	/**
	* An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	*/
	void SetData(const FStaticMeshDataType& InData);

	/**
	* Set the uniform buffer for this vertex factory.
	*/
	FORCEINLINE void SetUniformBuffer(const FNiagaraMeshUniformBufferRef& InMeshParticleUniformBuffer)
	{
		MeshParticleUniformBuffer = InMeshParticleUniformBuffer;
	}

	/**
	* Retrieve the uniform buffer for this vertex factory.
	*/
	FORCEINLINE FRHIUniformBuffer* GetUniformBuffer()
	{
		return MeshParticleUniformBuffer;
	}
	
	//uint8* LockPreviousTransformBuffer(uint32 ParticleCount);
	//void UnlockPreviousTransformBuffer();
	//FRHIShaderResourceView* GetPreviousTransformBufferSRV() const;

	/**
	* Copy the data from another vertex factory
	* @param Other - factory to copy from
	*/
	void Copy(const FNiagaraMeshVertexFactory& Other);

	// FRenderResource interface.
	virtual void InitRHI() override;

	static bool SupportsTessellationShaders() { return true; }

	int32 GetLODIndex() const { return LODIndex; }
	void SetLODIndex(int32 InLODIndex) { LODIndex = InLODIndex; }
	
protected:
	FStaticMeshDataType Data;
	int32 LODIndex;	

	/** Uniform buffer with mesh particle parameters. */
	FRHIUniformBuffer* MeshParticleUniformBuffer;
	
	/** Used to remember this in the case that we reuse the same vertex factory for multiple renders . */
	FNiagaraMeshInstanceVertices* InstanceVerticesCPU;

	FShaderResourceViewRHIRef ParticleDataFloatSRV;
	uint32 FloatDataStride;

	FShaderResourceViewRHIRef SortedIndicesSRV;
	uint32 SortedIndicesOffset;
};

inline FNiagaraMeshVertexFactory* ConstructNiagaraMeshVertexFactory()
{
	return new FNiagaraMeshVertexFactory();
}

inline FNiagaraMeshVertexFactory* ConstructNiagaraMeshVertexFactory(ENiagaraVertexFactoryType InType, ERHIFeatureLevel::Type InFeatureLevel)
{
	return new FNiagaraMeshVertexFactory(InType, InFeatureLevel);
}
