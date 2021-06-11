// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ParticleVertexFactory.h: Particle vertex factory definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "NiagaraVertexFactory.h"
#include "SceneView.h"
#include "NiagaraDataSet.h"
#
class FMaterial;

//	FNiagaraRibbonVertexDynamicParameter
struct FNiagaraRibbonVertexDynamicParameter
{
	/** The dynamic parameter of the particle			*/
	float DynamicValue[4];
};

/**
* Uniform buffer for particle beam/trail vertex factories.
*/
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNiagaraRibbonUniformParameters, NIAGARAVERTEXFACTORIES_API)
	SHADER_PARAMETER(FVector4, CameraRight)
	SHADER_PARAMETER(FVector4, CameraUp)
	SHADER_PARAMETER(FVector4, ScreenAlignment)
	SHADER_PARAMETER(int, PositionDataOffset)
	SHADER_PARAMETER(int, VelocityDataOffset)
	SHADER_PARAMETER(int, WidthDataOffset)
	SHADER_PARAMETER(int, TwistDataOffset)
	SHADER_PARAMETER(int, ColorDataOffset)
	SHADER_PARAMETER(int, FacingDataOffset)
	SHADER_PARAMETER(int, NormalizedAgeDataOffset)
	SHADER_PARAMETER(int, MaterialRandomDataOffset)
	SHADER_PARAMETER(uint32, MaterialParamValidMask)
	SHADER_PARAMETER(int, MaterialParamDataOffset)
	SHADER_PARAMETER(int, MaterialParam1DataOffset)
	SHADER_PARAMETER(int, MaterialParam2DataOffset)
	SHADER_PARAMETER(int, MaterialParam3DataOffset)
	SHADER_PARAMETER(int, DistanceFromStartOffset)
	SHADER_PARAMETER(int, U0OverrideDataOffset)
	SHADER_PARAMETER(int, V0RangeOverrideDataOffset)
	SHADER_PARAMETER(int, U1OverrideDataOffset)
	SHADER_PARAMETER(int, V1RangeOverrideDataOffset)
	SHADER_PARAMETER(int, TotalNumInstances)
	SHADER_PARAMETER(int, InterpCount)
	SHADER_PARAMETER(float, OneOverInterpCount)
	SHADER_PARAMETER(int, ParticleIdShift)
	SHADER_PARAMETER(int, ParticleIdMask)
	SHADER_PARAMETER(int, InterpIdShift)
	SHADER_PARAMETER(int, InterpIdMask)
	SHADER_PARAMETER(int, SliceVertexIdMask)
	SHADER_PARAMETER(int, ShouldFlipNormalToView)
	SHADER_PARAMETER(int, U0DistributionMode)
	SHADER_PARAMETER(int, U1DistributionMode)
	SHADER_PARAMETER(FVector4, PackedVData)
	SHADER_PARAMETER(uint32, bLocalSpace)
	SHADER_PARAMETER_EX(float, DeltaSeconds, EShaderPrecisionModifier::Half)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
typedef TUniformBufferRef<FNiagaraRibbonUniformParameters> FNiagaraRibbonUniformBufferRef;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNiagaraRibbonVFLooseParameters, NIAGARAVERTEXFACTORIES_API)
	SHADER_PARAMETER(uint32, NiagaraFloatDataStride)
	SHADER_PARAMETER(uint32, SortedIndicesOffset)
	SHADER_PARAMETER(uint32, FacingMode)
	SHADER_PARAMETER(uint32, Shape)
	SHADER_PARAMETER_SRV(Buffer<int>, SortedIndices)
	SHADER_PARAMETER_SRV(Buffer<float4>, TangentsAndDistances)
	SHADER_PARAMETER_SRV(Buffer<uint>, MultiRibbonIndices)
	SHADER_PARAMETER_SRV(Buffer<float>, PackedPerRibbonDataByIndex)
	SHADER_PARAMETER_SRV(Buffer<float>, NiagaraParticleDataFloat)
	SHADER_PARAMETER_SRV(Buffer<float>, NiagaraParticleDataHalf)
	SHADER_PARAMETER_SRV(Buffer<float>, SliceVertexData)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
typedef TUniformBufferRef<FNiagaraRibbonVFLooseParameters> FNiagaraRibbonVFLooseParametersRef;

/**
* Beam/Trail particle vertex factory.
*/
class NIAGARAVERTEXFACTORIES_API FNiagaraRibbonVertexFactory : public FNiagaraVertexFactoryBase
{
	DECLARE_VERTEX_FACTORY_TYPE(FNiagaraRibbonVertexFactory);

public:

	/** Default constructor. */
	FNiagaraRibbonVertexFactory(ENiagaraVertexFactoryType InType, ERHIFeatureLevel::Type InFeatureLevel)
		: FNiagaraVertexFactoryBase(InType, InFeatureLevel)
		, IndexBuffer(nullptr)
		, FirstIndex(0)
		, OutTriangleCount(0)
		, DataSet(0)
	{}

	FNiagaraRibbonVertexFactory()
		: FNiagaraVertexFactoryBase(NVFT_MAX, ERHIFeatureLevel::Num)
		, IndexBuffer(nullptr)
		, FirstIndex(0)
		, OutTriangleCount(0)
		, DataSet(0)
	{}

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory?
	*/
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	/**
	* Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	*/
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	// FRenderResource interface.
	virtual void InitRHI() override;

	/**
	* Set the uniform buffer for this vertex factory.
	*/
	FORCEINLINE void SetRibbonUniformBuffer(FNiagaraRibbonUniformBufferRef InSpriteUniformBuffer)
	{
		NiagaraRibbonUniformBuffer = InSpriteUniformBuffer;
	}

	/**
	* Retrieve the uniform buffer for this vertex factory.
	*/
	FORCEINLINE FNiagaraRibbonUniformBufferRef GetRibbonUniformBuffer()
	{
		return NiagaraRibbonUniformBuffer;
	}

	/**
	* Set the source vertex buffer.
	*/
	void SetVertexBuffer(const FVertexBuffer* InBuffer, uint32 StreamOffset, uint32 Stride);

	/**
	* Set the source vertex buffer that contains particle dynamic parameter data.
	*/
	void SetDynamicParameterBuffer(const FVertexBuffer* InDynamicParameterBuffer, int32 ParameterIndex, uint32 StreamOffset, uint32 Stride);

	void SetSortedIndices(const FVertexBufferRHIRef& InSortedIndicesBuffer, const FShaderResourceViewRHIRef& InSortedIndicesSRV, uint32 InSortedIndicesOffset)
	{
		SortedIndicesBuffer = InSortedIndicesBuffer;
		SortedIndicesSRV = InSortedIndicesSRV;
		SortedIndicesOffset = InSortedIndicesOffset;
	}

	void SetTangentAndDistances(const FVertexBufferRHIRef& InTangentAndDistancesBuffer, const FShaderResourceViewRHIRef& InTangentAndDistancesSRV)
	{
		TangentAndDistancesBuffer = InTangentAndDistancesBuffer;
		TangentAndDistancesSRV = InTangentAndDistancesSRV;
	}


	void SetMultiRibbonIndicesSRV(const FVertexBufferRHIRef& InMultiRibbonIndicesBuffer, const FShaderResourceViewRHIRef& InMultiRibbonIndicesSRV)
	{
		MultiRibbonIndicesBuffer = InMultiRibbonIndicesBuffer;
		MultiRibbonIndicesSRV = InMultiRibbonIndicesSRV;
	}

	void SetPackedPerRibbonDataByIndexSRV(const FVertexBufferRHIRef& InPackedPerRibbonDataByIndexBuffer, const FShaderResourceViewRHIRef& InPackedPerRibbonDataByIndexSRV)
	{
		PackedPerRibbonDataByIndexBuffer = InPackedPerRibbonDataByIndexBuffer;
		PackedPerRibbonDataByIndexSRV = InPackedPerRibbonDataByIndexSRV;
	}

	void SetSliceVertexDataSRV(const FVertexBufferRHIRef& InSliceVertexDataBuffer, const FShaderResourceViewRHIRef& InSliceVertexDataSRV)
	{
		SliceVertexDataBuffer = InSliceVertexDataBuffer;
		SliceVertexDataSRV = InSliceVertexDataSRV;
	}

	void SetFacingMode(uint32 InFacingMode)
	{
		FacingMode = InFacingMode;
	}

	FORCEINLINE FRHIShaderResourceView* GetSortedIndicesSRV()
	{
		return SortedIndicesSRV;
	}

	FORCEINLINE int32 GetSortedIndicesOffset()
	{
		return SortedIndicesOffset;
	}

	FORCEINLINE FRHIShaderResourceView* GetTangentAndDistancesSRV()
	{
		return TangentAndDistancesSRV;
	}

	FORCEINLINE FRHIShaderResourceView* GetMultiRibbonIndicesSRV()
	{
		return MultiRibbonIndicesSRV;
	}

	FORCEINLINE FRHIShaderResourceView* GetPackedPerRibbonDataByIndexSRV()
	{
		return PackedPerRibbonDataByIndexSRV;
	}

	FORCEINLINE FRHIShaderResourceView* GetSliceVertexDataSRV()
	{
		return SliceVertexDataSRV;
	}

	FORCEINLINE int32 GetFacingMode()
	{
		return FacingMode;
	}

	FIndexBuffer*& GetIndexBuffer()
	{
		return IndexBuffer;
	}

	uint32& GetFirstIndex()
	{
		return FirstIndex;
	}

	int32& GetOutTriangleCount()
	{
		return OutTriangleCount;
	}

	FUniformBufferRHIRef LooseParameterUniformBuffer;

private:

	/** Uniform buffer with beam/trail parameters. */
	FNiagaraRibbonUniformBufferRef NiagaraRibbonUniformBuffer;

	/** Used to hold the index buffer allocation information when we call GDME more than once per frame. */
	FIndexBuffer* IndexBuffer;
	uint32 FirstIndex;
	int32 OutTriangleCount;

	const FNiagaraDataSet *DataSet;

	FVertexBufferRHIRef SortedIndicesBuffer;
	FVertexBufferRHIRef TangentAndDistancesBuffer;
	FVertexBufferRHIRef MultiRibbonIndicesBuffer;
	FVertexBufferRHIRef PackedPerRibbonDataByIndexBuffer;
	FVertexBufferRHIRef SliceVertexDataBuffer;

	FShaderResourceViewRHIRef SortedIndicesSRV;
	FShaderResourceViewRHIRef TangentAndDistancesSRV;
	FShaderResourceViewRHIRef MultiRibbonIndicesSRV;
	FShaderResourceViewRHIRef PackedPerRibbonDataByIndexSRV;
	FShaderResourceViewRHIRef SliceVertexDataSRV;

	uint32 SortedIndicesOffset;
	int32 FacingMode;
};