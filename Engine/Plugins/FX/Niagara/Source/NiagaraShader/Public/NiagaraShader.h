// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShader.h: Shader base classes
=============================================================================*/

#pragma once

#include "ShaderParameters.h"
#include "SceneView.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "Math/Vector.h"
#include "NiagaraCommon.h"
#include "NiagaraScriptBase.h"
#include "NiagaraShared.h"
#include "NiagaraShaderType.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_1
#include "SceneRenderTargetParameters.h"
#endif

class UClass;

template<typename TBufferStruct> class TUniformBufferRef;

/** Base class of all shaders that need material parameters. */
class NIAGARASHADER_API FNiagaraShader : public FShader
{
public:
	DECLARE_SHADER_TYPE(FNiagaraShader, Niagara);
	//SHADER_USE_PARAMETER_STRUCT(FNiagaraShader, FShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32,									ComponentBufferSizeRead)
		SHADER_PARAMETER(uint32,									ComponentBufferSizeWrite)
		SHADER_PARAMETER(uint32,									SimStart)

		SHADER_PARAMETER_SRV(Buffer<float>,							InputFloat)
		SHADER_PARAMETER_SRV(Buffer<half>,							InputHalf)
		SHADER_PARAMETER_SRV(Buffer<int>,							InputInt)
		SHADER_PARAMETER_SRV(Buffer<float>,							StaticInputFloat)
	
		SHADER_PARAMETER_UAV(RWBuffer<float>,						RWOutputFloat)
		SHADER_PARAMETER_UAV(RWBuffer<half>,						RWOutputHalf)
		SHADER_PARAMETER_UAV(RWBuffer<int>,							RWOutputInt)

		SHADER_PARAMETER_UAV(RWBuffer<uint>,						RWInstanceCounts)
		SHADER_PARAMETER(uint32,									ReadInstanceCountOffset)
		SHADER_PARAMETER(uint32,									WriteInstanceCountOffset)

		SHADER_PARAMETER_SRV(Buffer<int>,							FreeIDList)
		SHADER_PARAMETER_UAV(RWBuffer<int>,							RWIDToIndexTable)

		SHADER_PARAMETER(FIntVector4,								SimulationStageIterationInfo)
		SHADER_PARAMETER(float,										SimulationStageNormalizedIterationIndex)

		SHADER_PARAMETER(FIntVector3,								ParticleIterationStateInfo)

		SHADER_PARAMETER(uint32,									EmitterTickCounter)
		SHADER_PARAMETER_ARRAY(FIntVector4,							EmitterSpawnInfoOffsets,	[(NIAGARA_MAX_GPU_SPAWN_INFOS + 3) / 4])
		SHADER_PARAMETER_ARRAY(FVector4f,							EmitterSpawnInfoParams,		[NIAGARA_MAX_GPU_SPAWN_INFOS])
		SHADER_PARAMETER(uint32,									NumSpawnedInstances)

		SHADER_PARAMETER(FUintVector3,								DispatchThreadIdBounds)
		SHADER_PARAMETER(FUintVector3,								DispatchThreadIdToLinear)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationParameters = FNiagaraShaderPermutationParameters;

	static FName UniformBufferLayoutName;

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
		return FNiagaraUtilities::SupportsComputeShaders(Parameters.Platform);
	}

	FNiagaraShader() {}
	FNiagaraShader(const FNiagaraShaderType::CompiledShaderInitializerType& Initializer);

	const TMemoryImageArray<FNiagaraDataInterfaceParamRef>& GetDIParameters()
	{
		return DataInterfaceParameters;
	}

	LAYOUT_FIELD(bool, bNeedsViewUniformBuffer);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, GlobalConstantBufferParam, 2);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, SystemConstantBufferParam, 2);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, OwnerConstantBufferParam, 2);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, EmitterConstantBufferParam, 2);
	LAYOUT_ARRAY(FShaderUniformBufferParameter, ExternalConstantBufferParam, 2);

private:
	// Data about parameters used for each Data Interface.
	LAYOUT_FIELD(TMemoryImageArray<FNiagaraDataInterfaceParamRef>, DataInterfaceParameters);

	/*
	FDebugUniformExpressionSet	DebugUniformExpressionSet;
	FRHIUniformBufferLayout		DebugUniformExpressionUBLayout;
	*/
	LAYOUT_FIELD(FMemoryImageString, DebugDescription);
};

extern NIAGARASHADER_API int32 GNiagaraSkipVectorVMBackendOptimizations;
