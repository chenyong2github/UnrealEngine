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
#include "NiagaraScriptBase.h"
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

	static FIntVector GetDefaultThreadGroupSize(ENiagaraGpuDispatchType DispatchType)
	{
		//-TODO: Grab this from FDataDrivenShaderPlatformInfo
		switch (DispatchType)
		{
			case ENiagaraGpuDispatchType::OneD:		return FIntVector(64, 1, 1);
			case ENiagaraGpuDispatchType::TwoD:		return FIntVector(8, 8, 1);
			case ENiagaraGpuDispatchType::ThreeD:	return FIntVector(4, 4, 2);
			default:								return FIntVector(64, 1, 1);
		}
	}

	static void ModifyCompilationEnvironment(const FNiagaraShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		const bool bUseWaveIntrinsics = false; // TODO: Some content breaks with this - FDataDrivenShaderPlatformInfo::GetInfo(Platform).bSupportsIntrinsicWaveOnce;
		OutEnvironment.SetDefine(TEXT("USE_WAVE_INTRINSICS"), bUseWaveIntrinsics ? 1 : 0);
	}

	static bool ShouldCompilePermutation(const FNiagaraShaderPermutationParameters& Parameters)
	{
		//@todo - lit materials only 
		return FNiagaraUtilities::SupportsComputeShaders(Parameters.Platform);
	}

	FNiagaraShader(const FNiagaraShaderType::CompiledShaderInitializerType& Initializer);

	// Bind parameters
	void BindParams(const TArray<FNiagaraDataInterfaceGPUParamInfo>& InDIParamInfo, const FShaderParameterMap &ParameterMap);

	const TMemoryImageArray<FNiagaraDataInterfaceParamRef>& GetDIParameters()
	{
		return DataInterfaceParameters;
	}

	LAYOUT_FIELD(FShaderResourceParameter, FloatInputBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, IntInputBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, HalfInputBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, StaticInputFloatParam);
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
	LAYOUT_FIELD(FShaderParameter, NumSpawnedInstancesParam);

	LAYOUT_FIELD(FShaderParameter, SimulationStageIterationInfoParam);
	LAYOUT_FIELD(FShaderParameter, SimulationStageNormalizedIterationIndexParam);
	LAYOUT_FIELD(FShaderParameter, ParticleIterationStateInfoParam);
	LAYOUT_FIELD(FShaderParameter, DispatchThreadIdBoundsParam);
	LAYOUT_FIELD(FShaderParameter, DispatchThreadIdToLinearParam);

	LAYOUT_FIELD(FShaderParameter, ComponentBufferSizeReadParam);
	LAYOUT_FIELD(FShaderParameter, ComponentBufferSizeWriteParam);

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
