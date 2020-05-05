// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShader.h: Material shader definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "Engine/EngineTypes.h"

/** A macro to implement material shaders. */
#define IMPLEMENT_MATERIAL_SHADER_TYPE(TemplatePrefix,ShaderClass,SourceFilename,FunctionName,Frequency) \
	IMPLEMENT_SHADER_TYPE( \
		TemplatePrefix, \
		ShaderClass, \
		SourceFilename, \
		FunctionName, \
		Frequency \
		);

class FMaterial;
class FShaderCommonCompileJob;
class FShaderCompileJob;
class FUniformExpressionSet;
class FVertexFactoryType;
struct FMaterialShaderParameters;

DECLARE_DELEGATE_RetVal_OneParam(FString, FShadingModelToStringDelegate, EMaterialShadingModel)

/** Converts an EMaterialShadingModel to a string description. */
extern ENGINE_API FString GetShadingModelString(EMaterialShadingModel ShadingModel);

/** Converts an FMaterialShadingModelField to a string description, base on the passed in delegate. */
extern ENGINE_API FString GetShadingModelFieldString(FMaterialShadingModelField ShadingModels, const FShadingModelToStringDelegate& Delegate, const FString& Delimiter = " ");

/** Converts an FMaterialShadingModelField to a string description, base on a default function. */
extern ENGINE_API FString GetShadingModelFieldString(FMaterialShadingModelField ShadingModels);

/** Converts an EBlendMode to a string description. */
extern ENGINE_API FString GetBlendModeString(EBlendMode BlendMode);

/** Called for every material shader to update the appropriate stats. */
extern void UpdateMaterialShaderCompilingStats(const FMaterial* Material);

/**
 * Dump material stats for a given platform.
 * 
 * @param	Platform	Platform to dump stats for.
 */
extern ENGINE_API void DumpMaterialStats( EShaderPlatform Platform );

/**
 * A shader meta type for material-linked shaders.
 */
class FMaterialShaderType : public FShaderType
{
public:
	struct CompiledShaderInitializerType : FGlobalShaderType::CompiledShaderInitializerType
	{
		const FUniformExpressionSet& UniformExpressionSet;
		const FString DebugDescription;

		CompiledShaderInitializerType(
			FShaderType* InType,
			int32 InPermutationId,
			const FShaderCompilerOutput& CompilerOutput,
			const FUniformExpressionSet& InUniformExpressionSet,
			const FSHAHash& InMaterialShaderMapHash,
			const FShaderPipelineType* InShaderPipeline,
			FVertexFactoryType* InVertexFactoryType,
			const FString& InDebugDescription
			)
		: FGlobalShaderType::CompiledShaderInitializerType(InType,InPermutationId,CompilerOutput,InMaterialShaderMapHash,InShaderPipeline,InVertexFactoryType)
		, UniformExpressionSet(InUniformExpressionSet)
		, DebugDescription(InDebugDescription)
		{}
	};

	FMaterialShaderType(
		FTypeLayoutDesc& InTypeLayout,
		const TCHAR* InName,
		const TCHAR* InSourceFilename,
		const TCHAR* InFunctionName,
		uint32 InFrequency,
		int32 InTotalPermutationCount,
		ConstructSerializedType InConstructSerializedRef,
		ConstructCompiledType InConstructCompiledRef,
		ModifyCompilationEnvironmentType InModifyCompilationEnvironmentRef,
		ShouldCompilePermutationType InShouldCompilePermutationRef,
		ValidateCompiledResultType InValidateCompiledResultRef,
		uint32 InTypeSize,
		const FShaderParametersMetadata* InRootParametersMetadata = nullptr
		):
		FShaderType(EShaderTypeForDynamicCast::Material, InTypeLayout, InName, InSourceFilename, InFunctionName, InFrequency, InTotalPermutationCount,
			InConstructSerializedRef,
			InConstructCompiledRef,
			InModifyCompilationEnvironmentRef,
			InShouldCompilePermutationRef,
			InValidateCompiledResultRef,
			InTypeSize,
			InRootParametersMetadata)
	{
		checkf(FPaths::GetExtension(InSourceFilename) == TEXT("usf"),
			TEXT("Incorrect virtual shader path extension for material shader '%s': Only .usf files should be compiled."),
			InSourceFilename);
	}

	/**
	 * Enqueues a compilation for a new shader of this type.
	 * @param Material - The material to link the shader with.
	 */
	class FShaderCompileJob* BeginCompileShader(
		uint32 ShaderMapId,
		int32 PermutationId,
		const FMaterial* Material,
		FShaderCompilerEnvironment* MaterialEnvironment,
		const FShaderPipelineType* ShaderPipeline,
		EShaderPlatform Platform,
		TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& NewJobs,
		const FString& DebugDescription,
		const FString& DebugExtension
		);

	static void BeginCompileShaderPipeline(
		uint32 ShaderMapId,
		EShaderPlatform Platform,
		const FMaterial* Material,
		FShaderCompilerEnvironment* MaterialEnvironment,
		const FShaderPipelineType* ShaderPipeline,
		const TArray<const FShaderType*>& ShaderStages,
		TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& NewJobs,
		const FString& DebugDescription,
		const FString& DebugExtension
	);

	/**
	 * Either creates a new instance of this type or returns an equivalent existing shader.
	 * @param Material - The material to link the shader with.
	 * @param CurrentJob - Compile job that was enqueued by BeginCompileShader.
	 */
	FShader* FinishCompileShader(
		const FUniformExpressionSet& UniformExpressionSet,
		const FSHAHash& MaterialShaderMapHash,
		const FShaderCompileJob& CurrentJob,
		const FShaderPipelineType* ShaderPipeline,
		const FString& InDebugDescription
		);

	/**
	 * Checks if the shader type should be cached for a particular platform and material.
	 * @param Platform - The platform to check.
	 * @param Material - The material to check.
	 * @return True if this shader type should be cached.
	 */
	bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters, int32 PermutationId) const;

	static bool ShouldCompilePipeline(const FShaderPipelineType* ShaderPipelineType, EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters);

protected:
	/**
	 * Sets up the environment used to compile an instance of this shader type.
	 * @param Platform - Platform to compile for.
	 * @param Environment - The shader compile environment that the function modifies.
	 */
	void SetupCompileEnvironment(EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters, int32 PermutationId, FShaderCompilerEnvironment& Environment);
};
