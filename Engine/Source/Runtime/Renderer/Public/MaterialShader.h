// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShader.h: Shader base classes
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "SceneView.h"
#include "Shader.h"
#include "MaterialShared.h"
#include "GlobalShader.h"
#include "MaterialShaderType.h"
#include "SceneRenderTargetParameters.h"
#include "ShaderParameterUtils.h"

template<typename TBufferStruct> class TUniformBufferRef;

/**
 * Debug information related to uniform expression sets.
 */
class FDebugUniformExpressionSet
{
public:
	/** The number of each type of expression contained in the set. */
	int32 NumVectorExpressions;
	int32 NumScalarExpressions;
	int32 Num2DTextureExpressions;
	int32 NumCubeTextureExpressions;
	int32 Num2DArrayTextureExpressions;
	int32 NumVolumeTextureExpressions;
	int32 NumVirtualTextureExpressions;

	FDebugUniformExpressionSet()
		: NumVectorExpressions(0)
		, NumScalarExpressions(0)
		, Num2DTextureExpressions(0)
		, NumCubeTextureExpressions(0)
		, Num2DArrayTextureExpressions(0)
		, NumVolumeTextureExpressions(0)
		, NumVirtualTextureExpressions(0)
	{
	}

	explicit FDebugUniformExpressionSet(const FUniformExpressionSet& InUniformExpressionSet)
	{
		InitFromExpressionSet(InUniformExpressionSet);
	}

	/** Initialize from a uniform expression set. */
	void InitFromExpressionSet(const FUniformExpressionSet& InUniformExpressionSet)
	{
		NumVectorExpressions = InUniformExpressionSet.UniformVectorExpressions.Num();
		NumScalarExpressions = InUniformExpressionSet.UniformScalarExpressions.Num();
		Num2DTextureExpressions = InUniformExpressionSet.Uniform2DTextureExpressions.Num();
		Num2DArrayTextureExpressions = InUniformExpressionSet.Uniform2DArrayTextureExpressions.Num();
		NumCubeTextureExpressions = InUniformExpressionSet.UniformCubeTextureExpressions.Num();
		NumVolumeTextureExpressions = InUniformExpressionSet.UniformVolumeTextureExpressions.Num();
		NumVirtualTextureExpressions = InUniformExpressionSet.UniformVirtualTextureExpressions.Num();
	}

	/** Returns true if the number of uniform expressions matches those with which the debug set was initialized. */
	bool Matches(const FUniformExpressionSet& InUniformExpressionSet) const
	{
		return NumVectorExpressions == InUniformExpressionSet.UniformVectorExpressions.Num()
			&& NumScalarExpressions == InUniformExpressionSet.UniformScalarExpressions.Num()
			&& Num2DTextureExpressions == InUniformExpressionSet.Uniform2DTextureExpressions.Num()
			&& NumCubeTextureExpressions == InUniformExpressionSet.UniformCubeTextureExpressions.Num()
			&& Num2DArrayTextureExpressions == InUniformExpressionSet.Uniform2DArrayTextureExpressions.Num()
			&& NumVolumeTextureExpressions == InUniformExpressionSet.UniformVolumeTextureExpressions.Num()
			&& NumVirtualTextureExpressions == InUniformExpressionSet.UniformVirtualTextureExpressions.Num();
	}
};

/** Serialization for debug uniform expression sets. */
inline FArchive& operator<<(FArchive& Ar, FDebugUniformExpressionSet& DebugExpressionSet)
{
	Ar << DebugExpressionSet.NumVectorExpressions;
	Ar << DebugExpressionSet.NumScalarExpressions;
	Ar << DebugExpressionSet.Num2DTextureExpressions;
	Ar << DebugExpressionSet.NumCubeTextureExpressions;
	Ar << DebugExpressionSet.Num2DArrayTextureExpressions;
	Ar << DebugExpressionSet.NumVolumeTextureExpressions;
	Ar << DebugExpressionSet.NumVirtualTextureExpressions;
	return Ar;
}


/** Base class of all shaders that need material parameters. */
class RENDERER_API FMaterialShader : public FShader
{
	DECLARE_SHADER_TYPE(FMaterialShader, Material);
public:
	static FName UniformBufferLayoutName;

	FMaterialShader()
#if ALLOW_SHADERMAP_DEBUG_DATA
		: DebugUniformExpressionUBLayout(FRHIUniformBufferLayout::Zero)
#endif
	{
	}

	FMaterialShader(const FMaterialShaderType::CompiledShaderInitializerType& Initializer);

	typedef void (*ModifyCompilationEnvironmentType)(EShaderPlatform, const FMaterial*, FShaderCompilerEnvironment&);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	static bool ValidateCompiledResult(EShaderPlatform Platform, const TArray<FMaterial*>& Materials, const FShaderParameterMap& ParameterMap, TArray<FString>& OutError)
	{
		return true;
	}

	FRHIUniformBuffer* GetParameterCollectionBuffer(const FGuid& Id, const FSceneInterface* SceneInterface) const;

	template<typename ShaderRHIParamRef>
	FORCEINLINE_DEBUGGABLE void SetViewParameters(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FSceneView& View, const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer)
	{
		const auto& ViewUniformBufferParameter = GetUniformBufferParameter<FViewUniformShaderParameters>();
		SetUniformBufferParameter(RHICmdList, ShaderRHI, ViewUniformBufferParameter, ViewUniformBuffer);

		if (View.bShouldBindInstancedViewUB && View.Family->Views.Num() > 0)
		{
			// When drawing the left eye in a stereo scene, copy the right eye view values into the instanced view uniform buffer.
			const EStereoscopicPass StereoPassIndex = IStereoRendering::IsStereoEyeView(View) ? eSSP_RIGHT_EYE : eSSP_FULL;

			const FSceneView& InstancedView = View.Family->GetStereoEyeView(StereoPassIndex);
			const auto& InstancedViewUniformBufferParameter = GetUniformBufferParameter<FInstancedViewUniformShaderParameters>();
			SetUniformBufferParameter(RHICmdList, ShaderRHI, InstancedViewUniformBufferParameter, InstancedView.ViewUniformBuffer);
		}
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{ }

	/** Sets pixel parameters that are material specific but not FMeshBatch specific. */
	template< typename TRHIShader >
	void SetParametersInner(
		FRHICommandList& RHICmdList,
		TRHIShader* ShaderRHI,
		const FMaterialRenderProxy* MaterialRenderProxy, 
		const FMaterial& Material,
		const FSceneView& View);

	/** Sets pixel parameters that are material specific but not FMeshBatch specific. */
	template< typename TRHIShader >
	void SetParameters(
		FRHICommandList& RHICmdList,
		TRHIShader* ShaderRHI,
		const FMaterialRenderProxy* MaterialRenderProxy, 
		const FMaterial& Material,
		const FSceneView& View, 
		const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
		ESceneTextureSetupMode SceneTextureSetupMode);

	/** Like SetParameters above, but takes a FViewInfo rather than FSceneView, which allows additional per-view parameters to be set */
	template< typename TRHIShader >
	void SetParameters(
		FRHICommandList& RHICmdList,
		TRHIShader* ShaderRHI,
		const FMaterialRenderProxy* MaterialRenderProxy,
		const FMaterial& Material,
		const FViewInfo& View,
		const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer,
		ESceneTextureSetupMode SceneTextureSetupMode);

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		FMeshDrawSingleShaderBindings& ShaderBindings) const;

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override;
	virtual uint32 GetAllocatedSize() const override;

protected:

	FSceneTextureShaderParameters SceneTextureParameters;

private:

	FShaderUniformBufferParameter MaterialUniformBuffer;
	TArray<FShaderUniformBufferParameter> ParameterCollectionUniformBuffers;

#if ALLOW_SHADERMAP_DEBUG_DATA
	FDebugUniformExpressionSet	DebugUniformExpressionSet;
	FRHIUniformBufferLayout		DebugUniformExpressionUBLayout;
	FString						DebugDescription;
#endif

	// Only needed to avoid unbound parameter error
	// This texture is bound as an UAV (RWTexture) and so it must be bound together with any RT. So it actually bound but not as part of the material
	FShaderResourceParameter VTFeedbackBuffer;

	/** If true, cached uniform expressions are allowed. */
	static int32 bAllowCachedUniformExpressions;
	/** Console variable ref to toggle cached uniform expressions. */
	static FAutoConsoleVariableRef CVarAllowCachedUniformExpressions;

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING || !WITH_EDITOR)
	void VerifyExpressionAndShaderMaps(const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial& Material, const FUniformExpressionCache* UniformExpressionCache) const;
#endif
};
