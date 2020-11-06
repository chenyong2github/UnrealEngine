// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShader.h: Shader base classes
=============================================================================*/

#pragma once

//#include "HAL/IConsoleManager.h"
//#include "RHI.h"
#include "ShaderParameters.h"
#include "SceneView.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraShaderType.h"
#include "SceneRenderTargetParameters.h"

class UClass;

template<typename TBufferStruct> class TUniformBufferRef;

/** Base class of all shaders that need material parameters. */
class NIAGARASHADER_API FNiagaraShader : public FShader
{
public:
	DECLARE_SHADER_TYPE(FNiagaraShader, Niagara);

	using FPermutationParameters = FNiagaraShaderPermutationParameters;

	static FName UniformBufferLayoutName;

	FNiagaraShader()
	{
	}

	static uint32 GetGroupSize(EShaderPlatform Platform)
	{
		//-TODO: Should come from DDPI
		return 64;
	}

	static void ModifyCompilationEnvironment(const FNiagaraShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize(Parameters.Platform));
	}

	static bool ShouldCompilePermutation(const FNiagaraShaderPermutationParameters& Parameters)
	{
		//@todo - lit materials only 
		return FNiagaraUtilities::SupportsComputeShaders(Parameters.Platform);
	}

	FNiagaraShader(const FNiagaraShaderType::CompiledShaderInitializerType& Initializer);

//	FRHIUniformBuffer* GetParameterCollectionBuffer(const FGuid& Id, const FSceneInterface* SceneInterface) const;
	/*
	template<typename ShaderRHIParamRef>
	FORCEINLINE_DEBUGGABLE void SetViewParameters(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FSceneView& View, const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer)
	{
		const auto& ViewUniformBufferParameter = GetUniformBufferParameter<FViewUniformShaderParameters>();
		const auto& BuiltinSamplersUBParameter = GetUniformBufferParameter<FBuiltinSamplersParameters>();
		CheckShaderIsValid();
		SetUniformBufferParameter(RHICmdList, ShaderRHI, ViewUniformBufferParameter, ViewUniformBuffer);

		if (View.bShouldBindInstancedViewUB && View.Family->Views.Num() > 0)
		{
			// When drawing the left eye in a stereo scene, copy the right eye view values into the instanced view uniform buffer.
			const EStereoscopicPass StereoPassIndex = (View.StereoPass != eSSP_FULL) ? eSSP_RIGHT_EYE : eSSP_FULL;

			const FSceneView& InstancedView = View.Family->GetStereoEyeView(StereoPassIndex);
			const auto& InstancedViewUniformBufferParameter = GetUniformBufferParameter<FInstancedViewUniformShaderParameters>();
			SetUniformBufferParameter(RHICmdList, ShaderRHI, InstancedViewUniformBufferParameter, InstancedView.ViewUniformBuffer);
		}
	}
	*/

	// Bind parameters
	void BindParams(const TArray<FNiagaraDataInterfaceGPUParamInfo>& InDIParamInfo, const FShaderParameterMap &ParameterMap);

	const TMemoryImageArray<FNiagaraDataInterfaceParamRef>& GetDIParameters()
	{
		return DataInterfaceParameters;
	}

	LAYOUT_FIELD(FShaderResourceParameter, FloatInputBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, IntInputBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, HalfInputBufferParam);
	LAYOUT_FIELD(FRWShaderParameter, FloatOutputBufferParam);
	LAYOUT_FIELD(FRWShaderParameter, IntOutputBufferParam);
	LAYOUT_FIELD(FRWShaderParameter, HalfOutputBufferParam);
	LAYOUT_FIELD(FRWShaderParameter, InstanceCountsParam);
	LAYOUT_FIELD(FShaderParameter, ReadInstanceCountOffsetParam);
	LAYOUT_FIELD(FShaderParameter, WriteInstanceCountOffsetParam);
	LAYOUT_FIELD(FShaderResourceParameter, FreeIDBufferParam);
	LAYOUT_FIELD(FRWShaderParameter, IDToIndexBufferParam);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, GlobalConstantBufferParam, 2);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, SystemConstantBufferParam, 2);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, OwnerConstantBufferParam, 2);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, EmitterConstantBufferParam, 2);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, ExternalConstantBufferParam, 2);
	LAYOUT_FIELD(FShaderUniformBufferParameter, ViewUniformBufferParam);
	LAYOUT_FIELD(FShaderParameter, SimStartParam);
	LAYOUT_FIELD(FShaderParameter, EmitterTickCounterParam);
	LAYOUT_FIELD(FShaderParameter, EmitterSpawnInfoOffsetsParam);
	LAYOUT_FIELD(FShaderParameter, EmitterSpawnInfoParamsParam);
	LAYOUT_FIELD(FShaderParameter, NumEventsPerParticleParam);
	LAYOUT_FIELD(FShaderParameter, NumParticlesPerEventParam);
	LAYOUT_FIELD(FShaderParameter, CopyInstancesBeforeStartParam);
	LAYOUT_FIELD(FShaderParameter, NumSpawnedInstancesParam);
	LAYOUT_FIELD(FShaderParameter, UpdateStartInstanceParam);
	LAYOUT_FIELD(FShaderParameter, DefaultSimulationStageIndexParam);
	LAYOUT_FIELD(FShaderParameter, SimulationStageIndexParam);

	LAYOUT_FIELD(FShaderParameter, SimulationStageIterationInfoParam);
	LAYOUT_FIELD(FShaderParameter, SimulationStageNormalizedIterationIndexParam);
	LAYOUT_FIELD(FShaderParameter, DispatchThreadIdToLinearParam);

	LAYOUT_FIELD(FShaderParameter, ComponentBufferSizeReadParam);
	LAYOUT_FIELD(FShaderParameter, ComponentBufferSizeWriteParam);
	LAYOUT_ARRAY(FRWShaderParameter, EventIntUAVParams, MAX_CONCURRENT_EVENT_DATASETS);
	LAYOUT_ARRAY(FRWShaderParameter, EventFloatUAVParams, MAX_CONCURRENT_EVENT_DATASETS);
	LAYOUT_ARRAY(FShaderResourceParameter, EventIntSRVParams, MAX_CONCURRENT_EVENT_DATASETS);
	LAYOUT_ARRAY(FShaderResourceParameter, EventFloatSRVParams, MAX_CONCURRENT_EVENT_DATASETS);
	LAYOUT_ARRAY(FShaderParameter, EventWriteFloatStrideParams, MAX_CONCURRENT_EVENT_DATASETS);
	LAYOUT_ARRAY(FShaderParameter, EventWriteIntStrideParams, MAX_CONCURRENT_EVENT_DATASETS);
	LAYOUT_ARRAY(FShaderParameter, EventReadFloatStrideParams, MAX_CONCURRENT_EVENT_DATASETS);
	LAYOUT_ARRAY(FShaderParameter, EventReadIntStrideParams, MAX_CONCURRENT_EVENT_DATASETS);

private:
	LAYOUT_FIELD(FShaderUniformBufferParameter, NiagaraUniformBuffer);

	// Data about parameters used for each Data Interface.
	LAYOUT_FIELD(TMemoryImageArray<FNiagaraDataInterfaceParamRef>, DataInterfaceParameters);

	/*
	FDebugUniformExpressionSet	DebugUniformExpressionSet;
	FRHIUniformBufferLayout		DebugUniformExpressionUBLayout;
	*/
	LAYOUT_FIELD(FMemoryImageString, DebugDescription);

	/* OPTODO: ? */
	/*
	// If true, cached uniform expressions are allowed.
	static int32 bAllowCachedUniformExpressions;
	// Console variable ref to toggle cached uniform expressions.
	static FAutoConsoleVariableRef CVarAllowCachedUniformExpressions;
	*/

	/*
#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING || !WITH_EDITOR)
	void VerifyExpressionAndShaderMaps(const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial& Material, const FUniformExpressionCache* UniformExpressionCache);
#endif
	*/
};

class FNiagaraEmitterInstanceShader : public FNiagaraShader
{

};

extern NIAGARASHADER_API int32 GNiagaraSkipVectorVMBackendOptimizations;
