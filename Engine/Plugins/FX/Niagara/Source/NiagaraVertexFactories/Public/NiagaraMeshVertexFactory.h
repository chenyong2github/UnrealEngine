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
	SHADER_PARAMETER(FVector, PivotOffset)
	SHADER_PARAMETER(int, bPivotOffsetIsWorldSpace)
	SHADER_PARAMETER(FVector, MeshScale)
	SHADER_PARAMETER(FVector4, SubImageSize)
	SHADER_PARAMETER(uint32, TexCoordWeightA)
	SHADER_PARAMETER(uint32, TexCoordWeightB)
	SHADER_PARAMETER(float, DeltaSeconds)
	SHADER_PARAMETER(uint32, MaterialParamValidMask)
	SHADER_PARAMETER(int, SortedIndicesOffset)

	SHADER_PARAMETER(int, PositionDataOffset)
	SHADER_PARAMETER(int, PrevPositionDataOffset)
	SHADER_PARAMETER(int, VelocityDataOffset)
	SHADER_PARAMETER(int, PrevVelocityDataOffset)
	SHADER_PARAMETER(int, ColorDataOffset)
	SHADER_PARAMETER(int, RotationDataOffset)
	SHADER_PARAMETER(int, PrevRotationDataOffset)
	SHADER_PARAMETER(int, ScaleDataOffset)
	SHADER_PARAMETER(int, PrevScaleDataOffset)
	SHADER_PARAMETER(int, MaterialParamDataOffset)
	SHADER_PARAMETER(int, MaterialParam1DataOffset)
	SHADER_PARAMETER(int, MaterialParam2DataOffset)
	SHADER_PARAMETER(int, MaterialParam3DataOffset)
	SHADER_PARAMETER(int, NormalizedAgeDataOffset)
	SHADER_PARAMETER(int, SubImageDataOffset)
	SHADER_PARAMETER(int, MaterialRandomDataOffset)
	SHADER_PARAMETER(int, CameraOffsetDataOffset)
	SHADER_PARAMETER(int, PrevCameraOffsetDataOffset)

	SHADER_PARAMETER(FVector4, DefaultPos)
	SHADER_PARAMETER(FVector4, DefaultPrevPos)
	SHADER_PARAMETER(FVector, DefaultVelocity)
	SHADER_PARAMETER(FVector, DefaultPrevVelocity)
	SHADER_PARAMETER(FVector4, DefaultColor)
	SHADER_PARAMETER(FVector4, DefaultRotation)
	SHADER_PARAMETER(FVector4, DefaultPrevRotation)
	SHADER_PARAMETER(FVector, DefaultScale)
	SHADER_PARAMETER(FVector, DefaultPrevScale)
	SHADER_PARAMETER(FVector4, DefaultDynamicMaterialParameter0)
	SHADER_PARAMETER(FVector4, DefaultDynamicMaterialParameter1)
	SHADER_PARAMETER(FVector4, DefaultDynamicMaterialParameter2)
	SHADER_PARAMETER(FVector4, DefaultDynamicMaterialParameter3)
	SHADER_PARAMETER(float, DefaultNormAge)
	SHADER_PARAMETER(float, DefaultSubImage)
	SHADER_PARAMETER(float, DefaultMatRandom)
	SHADER_PARAMETER(float, DefaultCamOffset)
	SHADER_PARAMETER(float, DefaultPrevCamOffset)

	SHADER_PARAMETER(int, SubImageBlendMode)
	SHADER_PARAMETER(uint32, FacingMode)
	SHADER_PARAMETER(uint32, bLockedAxisEnable)
	SHADER_PARAMETER(FVector, LockedAxis)
	SHADER_PARAMETER(uint32, LockedAxisSpace)
	SHADER_PARAMETER(uint32, NiagaraFloatDataStride)
	SHADER_PARAMETER_SRV(Buffer<float>, NiagaraParticleDataFloat)
	SHADER_PARAMETER_SRV(Buffer<float>, NiagaraParticleDataHalf)
	SHADER_PARAMETER_SRV(Buffer<int>, SortedIndices)
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
		, MeshIndex(-1)
		, LODIndex(-1)
		, InstanceVerticesCPU(nullptr)
		, FloatDataStride(0)
	{}

	FNiagaraMeshVertexFactory()
		: FNiagaraVertexFactoryBase(NVFT_MAX, ERHIFeatureLevel::Num)
		, MeshIndex(-1)
		, LODIndex(-1)
		, InstanceVerticesCPU(nullptr)
		, FloatDataStride(0)
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
		OutEnvironment.SetDefine(TEXT("NiagaraVFLooseParameters"), TEXT("NiagaraMeshVF"));
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
	
	int32 GetMeshIndex() const { return MeshIndex; }
	void SetMeshIndex(int32 InMeshIndex) { MeshIndex = InMeshIndex; }

	int32 GetLODIndex() const { return LODIndex; }
	void SetLODIndex(int32 InLODIndex) { LODIndex = InLODIndex; }
	
protected:
	FStaticMeshDataType Data;
	int32 MeshIndex;	
	int32 LODIndex;	

	/** Uniform buffer with mesh particle parameters. */
	FRHIUniformBuffer* MeshParticleUniformBuffer;
	
	/** Used to remember this in the case that we reuse the same vertex factory for multiple renders . */
	FNiagaraMeshInstanceVertices* InstanceVerticesCPU;

	FShaderResourceViewRHIRef ParticleDataFloatSRV;
	uint32 FloatDataStride;

	FShaderResourceViewRHIRef ParticleDataHalfSRV;
	uint32 HalfDataStride;
};

/**
* Advanced mesh vertex factory. Used for enabling accurate motion vector output
*/
class NIAGARAVERTEXFACTORIES_API FNiagaraMeshVertexFactoryEx : public FNiagaraMeshVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FNiagaraMeshVertexFactoryEx);
public:
	FNiagaraMeshVertexFactoryEx(ENiagaraVertexFactoryType InType, ERHIFeatureLevel::Type InFeatureLevel)
		: FNiagaraMeshVertexFactory(InType, InFeatureLevel)
	{}

	FNiagaraMeshVertexFactoryEx() {}

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNiagaraMeshVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("ENABLE_PRECISE_MOTION_VECTORS"), TEXT("1"));
	}
};