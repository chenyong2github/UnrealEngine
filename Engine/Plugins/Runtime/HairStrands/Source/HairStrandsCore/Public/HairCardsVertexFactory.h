// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Components.h"
#include "VertexFactory.h"
#include "RenderGraphResources.h"
#include "HairCardsDatas.h"
#include "HairStrandsRendering.h"
#include "HairStrandsInterface.h"
#include "PrimitiveSceneProxy.h"

class FMaterial;
class FSceneView;
struct FMeshBatchElement;

/**
 * A vertex factory which simply transforms explicit vertex attributes from local to world space.
 */
class HAIRSTRANDSCORE_API FHairCardsVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FHairCardsVertexFactory);
public:
	struct FDataType
	{
		FHairGroupInstance* Instance = nullptr;
		uint32 GroupIndex = 0;
		uint32 LODIndex = 0;
		uint32 BufferIndex = 0;// If the RHI does not support manual fetch, we create two vertex factory to have two declarations: 1) for position0:current & position1:previous 2) for position0:previous & position1:current
		EHairGeometryType GeometryType = EHairGeometryType::NoneGeometry;
	};

	FHairCardsVertexFactory(FHairGroupInstance* Instance, uint32 GroupIndex, uint32 LODIndex, uint32 BufferIndex, EHairGeometryType GeometryType, EShaderPlatform InShaderPlatform, ERHIFeatureLevel::Type InFeatureLevel, const char* InDebugName);
	
	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory? 
	 */
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const FDataType& InData);

	/**
	* Copy the data from another vertex factory
	* @param Other - factory to copy from
	*/
	void Copy(const FHairCardsVertexFactory& Other);

	// FRenderResource interface.
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;
	static bool SupportsTessellationShaders() { return false; }
	const FDataType& GetData() const { return Data; }
	FDataType Data;
protected:


	struct FDebugName
	{
		FDebugName(const char* InDebugName)
#if !UE_BUILD_SHIPPING
			: DebugName(InDebugName)
#endif
		{}
	private:
#if !UE_BUILD_SHIPPING
		const char* DebugName;
#endif
	} DebugName;
};