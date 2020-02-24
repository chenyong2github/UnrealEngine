// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ParticleVertexFactory.cpp: Particle vertex factory implementation.
=============================================================================*/

#include "NiagaraMeshVertexFactory.h"
#include "ParticleHelper.h"
#include "ParticleResources.h"
#include "ShaderParameterUtils.h"
#include "MeshMaterialShader.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNiagaraMeshUniformParameters, "NiagaraMeshVF");

class FNiagaraMeshVertexFactoryShaderParametersVS : public FVertexFactoryShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FNiagaraMeshVertexFactoryShaderParametersVS, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		//PrevTransformBuffer.Bind(ParameterMap, TEXT("PrevTransformBuffer"));
		NiagaraParticleDataPosition.Bind(ParameterMap, TEXT("NiagaraParticleDataPosition"));
		NiagaraParticleDataVelocity.Bind(ParameterMap, TEXT("NiagaraParticleDataVelocity"));
		NiagaraParticleDataColor.Bind(ParameterMap, TEXT("NiagaraParticleDataColor"));
		NiagaraParticleDataScale.Bind(ParameterMap, TEXT("NiagaraParticleDataScale"));
		NiagaraParticleDataTransform.Bind(ParameterMap, TEXT("NiagaraParticleDataTransform"));
		NiagaraParticleDataNormalizedAge.Bind(ParameterMap, TEXT("NiagaraParticleDataNormalizedAge"));
		NiagaraParticleDataMaterialRandom.Bind(ParameterMap, TEXT("NiagaraParticleDataMaterialRandom"));
		NiagaraParticleDataMaterialParam0.Bind(ParameterMap, TEXT("NiagaraParticleDataMaterialParam0"));
		NiagaraParticleDataMaterialParam1.Bind(ParameterMap, TEXT("NiagaraParticleDataMaterialParam1"));
		NiagaraParticleDataMaterialParam2.Bind(ParameterMap, TEXT("NiagaraParticleDataMaterialParam2"));
		NiagaraParticleDataMaterialParam3.Bind(ParameterMap, TEXT("NiagaraParticleDataMaterialParam3"));
		NiagaraParticleDataSubImage.Bind(ParameterMap, TEXT("NiagaraParticleDataSubImage"));
		NiagaraParticleDataCameraOffset.Bind(ParameterMap, TEXT("NiagaraParticleDataCameraOffset"));

		FloatDataStride.Bind(ParameterMap, TEXT("NiagaraFloatDataStride"));

		// 		NiagaraParticleDataInt.Bind(ParameterMap, TEXT("NiagaraParticleDataInt"));
		// 		FloatDataOffset.Bind(ParameterMap, TEXT("NiagaraInt32DataOffset"));
		// 		FloatDataStride.Bind(ParameterMap, TEXT("NiagaraInt3DataStride"));

		SortedIndices.Bind(ParameterMap, TEXT("SortedIndices"));
		SortedIndicesOffset.Bind(ParameterMap, TEXT("SortedIndicesOffset"));
	}

	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		FNiagaraMeshVertexFactory* NiagaraMeshVF = (FNiagaraMeshVertexFactory*)VertexFactory;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FNiagaraMeshUniformParameters>(), NiagaraMeshVF->GetUniformBuffer());

		ShaderBindings.Add(NiagaraParticleDataPosition, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataVelocity, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataColor, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataScale, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataTransform, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataNormalizedAge, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataMaterialRandom, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataMaterialParam0, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataMaterialParam1, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataMaterialParam2, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataMaterialParam3, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataSubImage, NiagaraMeshVF->GetParticleDataFloatSRV());
		ShaderBindings.Add(NiagaraParticleDataCameraOffset, NiagaraMeshVF->GetParticleDataFloatSRV());

		ShaderBindings.Add(FloatDataStride, NiagaraMeshVF->GetFloatDataStride());

		FRHIShaderResourceView* SortedSRV = NiagaraMeshVF->GetSortedIndicesSRV();
		ShaderBindings.Add(SortedIndices, SortedSRV != nullptr ? SortedSRV : GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV.GetReference());
		ShaderBindings.Add(SortedIndicesOffset, NiagaraMeshVF->GetSortedIndicesOffset());
	}

private:


		//LAYOUT_FIELD(FShaderResourceParameter, PrevTransformBuffer)

	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataPosition);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataVelocity);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataColor);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataScale);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataTransform);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataNormalizedAge);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataMaterialRandom);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataMaterialParam0);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataMaterialParam1);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataMaterialParam2);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataMaterialParam3);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataSubImage);
	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataCameraOffset);
	LAYOUT_FIELD(FShaderParameter, FloatDataStride);

		// 	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataInt)
		// 	LAYOUT_FIELD(FShaderParameter, Int32DataOffset)
		// 	LAYOUT_FIELD(FShaderParameter, Int32DataStride)
		LAYOUT_FIELD(FShaderResourceParameter, SortedIndices)
		LAYOUT_FIELD(FShaderParameter, SortedIndicesOffset)

};

class FNiagaraMeshVertexFactoryShaderParametersPS : public FVertexFactoryShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FNiagaraMeshVertexFactoryShaderParametersPS, NonVirtual);
public:
	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		FNiagaraMeshVertexFactory* NiagaraMeshVF = (FNiagaraMeshVertexFactory*)VertexFactory;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FNiagaraMeshUniformParameters>(), NiagaraMeshVF->GetUniformBuffer());
	}

	
	
};

void FNiagaraMeshVertexFactory::InitRHI()
{
	FVertexDeclarationElementList Elements;

	{
		if (Data.PositionComponent.VertexBuffer != NULL)
		{
			Elements.Add(AccessStreamComponent(Data.PositionComponent, 0));
		}

		// only tangent,normal are used by the stream. the binormal is derived in the shader
		uint8 TangentBasisAttributes[2] = { 1, 2 };
		for (int32 AxisIndex = 0; AxisIndex < 2; AxisIndex++)
		{
			if (Data.TangentBasisComponents[AxisIndex].VertexBuffer != NULL)
			{
				Elements.Add(AccessStreamComponent(Data.TangentBasisComponents[AxisIndex], TangentBasisAttributes[AxisIndex]));
			}
		}

		if (Data.ColorComponentsSRV == nullptr)
		{
			Data.ColorComponentsSRV = GNullColorVertexBuffer.VertexBufferSRV;
			Data.ColorIndexMask = 0;
		}

		// Vertex color
		if (Data.ColorComponent.VertexBuffer != NULL)
		{
			Elements.Add(AccessStreamComponent(Data.ColorComponent, 3));
		}
		else
		{
			//If the mesh has no color component, set the null color buffer on a new stream with a stride of 0.
			//This wastes 4 bytes of bandwidth per vertex, but prevents having to compile out twice the number of vertex factories.
			FVertexStreamComponent NullColorComponent(&GNullColorVertexBuffer, 0, 0, VET_Color, EVertexStreamUsage::ManualFetch);
			Elements.Add(AccessStreamComponent(NullColorComponent, 3));
		}

		if (Data.TextureCoordinates.Num())
		{
			const int32 BaseTexCoordAttribute = 4;
			for (int32 CoordinateIndex = 0; CoordinateIndex < Data.TextureCoordinates.Num(); CoordinateIndex++)
			{
				Elements.Add(AccessStreamComponent(
					Data.TextureCoordinates[CoordinateIndex],
					BaseTexCoordAttribute + CoordinateIndex
					));
			}

			for (int32 CoordinateIndex = Data.TextureCoordinates.Num(); CoordinateIndex < MAX_TEXCOORDS; CoordinateIndex++)
			{
				Elements.Add(AccessStreamComponent(
					Data.TextureCoordinates[Data.TextureCoordinates.Num() - 1],
					BaseTexCoordAttribute + CoordinateIndex
					));
			}
		}

		//if (Streams.Num() > 0)
		{
			InitDeclaration(Elements);
			check(IsValidRef(GetDeclaration()));
		}
	}
}

/*
uint8* FNiagaraMeshVertexFactory::LockPreviousTransformBuffer(uint32 ParticleCount)
{
	const static uint32 ElementSize = sizeof(FVector4);
	const static uint32 ParticleSize = ElementSize * 3;
	const uint32 AllocationRequest = ParticleCount * ParticleSize;

	check(!PrevTransformBuffer.MappedBuffer);

	if (AllocationRequest > PrevTransformBuffer.NumBytes)
	{
		PrevTransformBuffer.Release();
		PrevTransformBuffer.Initialize(ElementSize, ParticleCount * 3, PF_A32B32G32R32F, BUF_Dynamic);
	}

	PrevTransformBuffer.Lock();

	return PrevTransformBuffer.MappedBuffer;
}

void FNiagaraMeshVertexFactory::UnlockPreviousTransformBuffer()
{
	check(PrevTransformBuffer.MappedBuffer);

	PrevTransformBuffer.Unlock();
}

FRHIShaderResourceView* FNiagaraMeshVertexFactory::GetPreviousTransformBufferSRV() const
{
	return PrevTransformBuffer.SRV;
}
*/

bool FNiagaraMeshVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (FNiagaraUtilities::SupportsNiagaraRendering(Parameters.Platform)) && (Parameters.MaterialParameters.bIsUsedWithNiagaraMeshParticles || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

void FNiagaraMeshVertexFactory::SetData(const FStaticMeshDataType& InData)
{
	check(IsInRenderingThread());
	Data = InData;
	UpdateRHI();
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FNiagaraMeshVertexFactory, SF_Vertex, FNiagaraMeshVertexFactoryShaderParametersVS);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FNiagaraMeshVertexFactory, SF_Compute, FNiagaraMeshVertexFactoryShaderParametersVS);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FNiagaraMeshVertexFactory, SF_RayHitGroup, FNiagaraMeshVertexFactoryShaderParametersVS);
#endif
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FNiagaraMeshVertexFactory, SF_Pixel, FNiagaraMeshVertexFactoryShaderParametersPS);

IMPLEMENT_VERTEX_FACTORY_TYPE(FNiagaraMeshVertexFactory, "/Plugin/FX/Niagara/Private/NiagaraMeshVertexFactory.ush", true, false, true, false, false);

