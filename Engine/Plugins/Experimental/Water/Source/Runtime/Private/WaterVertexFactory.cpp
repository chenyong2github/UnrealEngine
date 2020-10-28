// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterVertexFactory.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "MeshMaterialShader.h"
#include "WaterInstanceDataBuffer.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FWaterVertexFactoryParameters, "WaterVF");

/**
 * Shader parameters for water vertex factory.
 */
template <bool bWithWaterSelectionSupport>
class TWaterVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(TWaterVertexFactoryShaderParameters<bWithWaterSelectionSupport>, NonVirtual);

public:
	using WaterVertexFactoryType = TWaterVertexFactory<bWithWaterSelectionSupport>;
	using WaterMeshUserDataType = TWaterMeshUserData<bWithWaterSelectionSupport>;
	using WaterInstanceDataBuffersType = TWaterInstanceDataBuffers<bWithWaterSelectionSupport>;

	void Bind(const FShaderParameterMap& ParameterMap)
	{
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const class FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const class FVertexFactory* InVertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		WaterVertexFactoryType* VertexFactory = (WaterVertexFactoryType*)InVertexFactory;

		const WaterMeshUserDataType* WaterMeshUserData = (const WaterMeshUserDataType*)BatchElement.UserData;

		const WaterInstanceDataBuffersType* InstanceDataBuffers = WaterMeshUserData->InstanceDataBuffers;

		const int32 InstanceOffsetValue = BatchElement.UserIndex;

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FWaterVertexFactoryParameters>(), VertexFactory->GetWaterVertexFactoryUniformBuffer(WaterMeshUserData->RenderGroupType));

		if (VertexStreams.Num() > 0)
		{
			for (int32 i = 0; i < WaterInstanceDataBuffersType::NumBuffers; ++i)
			{
				FVertexInputStream* InstanceInputStream = VertexStreams.FindByPredicate([i](const FVertexInputStream& InStream) { return InStream.StreamIndex == i+1; });
				check(InstanceInputStream);
				
				// Bind vertex buffer
				check(InstanceDataBuffers->GetBuffer(i));
				InstanceInputStream->VertexBuffer = InstanceDataBuffers->GetBuffer(i);
			}

			if (InstanceOffsetValue > 0)
			{
				VertexFactory->OffsetInstanceStreams(InstanceOffsetValue, InputStreamType, VertexStreams);
			}
		}
	}
};

// ----------------------------------------------------------------------------------

// Always implement the basic vertex factory so that it's there for both editor and non-editor builds :
IMPLEMENT_TEMPLATE_TYPE_LAYOUT(template<>, TWaterVertexFactoryShaderParameters</*bWithWaterSelectionSupport = */ false>);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(TWaterVertexFactory</*bWithWaterSelectionSupport = */ false>, SF_Vertex, TWaterVertexFactoryShaderParameters</*bWithWaterSelectionSupport = */ false>);
IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE_EX(template<>, TWaterVertexFactory</*bWithWaterSelectionSupport = */ false>, "/Plugin/Water/Private/WaterMeshVertexFactory.ush", true, false, true, true, false, false, true);

#if WITH_WATER_SELECTION_SUPPORT

// In editor builds, also implement the vertex factory that supports water selection:
IMPLEMENT_TEMPLATE_TYPE_LAYOUT(template<>, TWaterVertexFactoryShaderParameters</*bWithWaterSelectionSupport = */ true>);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(TWaterVertexFactory</*bWithWaterSelectionSupport = */ true>, SF_Vertex, TWaterVertexFactoryShaderParameters</*bWithWaterSelectionSupport = */ true>);
IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE_EX(template<>, TWaterVertexFactory</*bWithWaterSelectionSupport = */ true>, "/Plugin/Water/Private/WaterMeshVertexFactory.ush", true, false, true, true, false, false, true);

#endif // WITH_WATER_SELECTION_SUPPORT
