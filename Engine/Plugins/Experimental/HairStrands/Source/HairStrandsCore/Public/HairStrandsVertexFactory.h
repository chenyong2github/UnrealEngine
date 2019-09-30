// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Components.h"
#include "VertexFactory.h"
#include "RenderGraphResources.h"
#include "HairStrandsDatas.h"
#include "HairStrandsRendering.h"

class FMaterial;
class FSceneView;
struct FMeshBatchElement;

/**
 * A vertex factory which simply transforms explicit vertex attributes from local to world space.
 */
class HAIRSTRANDSCORE_API FHairStrandsVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FHairStrandsVertexFactory);
public:

	FHairStrandsVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, const char* InDebugName)
		: FVertexFactory(InFeatureLevel)
		, DebugName(InDebugName)
	{
		bSupportsManualVertexFetch = true;
	}
	
	struct FDataType
	{
		FHairStrandsInterpolationOutput* InterpolationOutput = nullptr;
		
		float MinStrandRadius = 0;
		float MaxStrandRadius = 0;
		float MaxStrandLength = 0;
		float HairDensity = 1;
		FVector HairWorldOffset = FVector::ZeroVector;
	};

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory? 
	 */
	static bool ShouldCompilePermutation(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType);
	static void ModifyCompilationEnvironment(const FVertexFactoryType* Type, EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment);
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const FDataType& InData);

	/**
	* Copy the data from another vertex factory
	* @param Other - factory to copy from
	*/
	void Copy(const FHairStrandsVertexFactory& Other);

	// FRenderResource interface.
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	static bool SupportsTessellationShaders() { return true; }

	static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency);

	inline FRHIShaderResourceView* GetPositionSRV() const			{ check(Data.InterpolationOutput); return Data.InterpolationOutput->VFInput.HairPositionBuffer; };
	inline FRHIShaderResourceView* GetPreviousPositionSRV() const	{ check(Data.InterpolationOutput); return Data.InterpolationOutput->VFInput.HairPreviousPositionBuffer; }
	inline FRHIShaderResourceView* GetAttributeSRV() const			{ check(Data.InterpolationOutput); return Data.InterpolationOutput->VFInput.HairAttributeBuffer; }
	inline FRHIShaderResourceView* GetTangentSRV() const			{ check(Data.InterpolationOutput); return Data.InterpolationOutput->VFInput.HairTangentBuffer; }

	float GetMaxStrandRadius() const;
	inline float GetMinStrandRadius() const							{ return Data.MinStrandRadius; }
	inline float GetMaxStrandLength() const							{ return Data.MaxStrandLength; }
	inline float GetHairDensity() const								{ return Data.HairDensity;  }
	inline const FVector& GetWorldOffset() const					{ return Data.HairWorldOffset; }

	const FDataType& GetData() const { return Data; }
protected:

	FDataType Data;

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