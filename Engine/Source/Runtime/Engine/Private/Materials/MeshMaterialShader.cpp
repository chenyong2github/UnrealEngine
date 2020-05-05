// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshMaterialShader.cpp: Mesh material shader implementation.
=============================================================================*/

#include "MeshMaterialShader.h"
#include "MaterialShaderMapLayout.h"
#include "ShaderCompiler.h"
#include "ProfilingDebugging/CookStats.h"

#if ENABLE_COOK_STATS
namespace MaterialMeshCookStats
{
	static int32 ShadersCompiled = 0;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		AddStat(TEXT("MeshMaterial.Misc"), FCookStatsManager::CreateKeyValueArray(
			TEXT("ShadersCompiled"), ShadersCompiled
			));
	});
}
#endif

/**
 * Enqueues a compilation for a new shader of this type.
 * @param Platform - The platform to compile for.
 * @param Material - The material to link the shader with.
 * @param VertexFactoryType - The vertex factory to compile with.
 */
FShaderCompileJob* FMeshMaterialShaderType::BeginCompileShader(
	uint32 ShaderMapId,
	int32 PermutationId,
	EShaderPlatform Platform,
	const FMaterial* Material,
	FShaderCompilerEnvironment* MaterialEnvironment,
	FVertexFactoryType* VertexFactoryType,
	const FShaderPipelineType* ShaderPipeline,
	TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& NewJobs,
	FString DebugDescription,
	FString DebugExtension)
{
	FShaderCompileJob* NewJob = new FShaderCompileJob(ShaderMapId, VertexFactoryType, this, PermutationId);

	NewJob->Input.SharedEnvironment = MaterialEnvironment;
	FShaderCompilerEnvironment& ShaderEnvironment = NewJob->Input.Environment;
	ShaderEnvironment.TargetPlatform = MaterialEnvironment->TargetPlatform;

	const FMaterialShaderParameters MaterialParameters(Material);

	// apply the vertex factory changes to the compile environment
	check(VertexFactoryType);
	VertexFactoryType->ModifyCompilationEnvironment(FVertexFactoryShaderPermutationParameters(Platform, MaterialParameters, VertexFactoryType), ShaderEnvironment);

	Material->SetupExtaCompilationSettings(Platform, NewJob->Input.ExtraSettings);

	//update material shader stats
	UpdateMaterialShaderCompilingStats(Material);

	UE_LOG(LogShaders, Verbose, TEXT("			%s"), GetName());
	COOK_STAT(MaterialMeshCookStats::ShadersCompiled++);

	// Allow the shader type to modify the compile environment.
	SetupCompileEnvironment(Platform, MaterialParameters, VertexFactoryType, PermutationId, ShaderEnvironment);

	bool bAllowDevelopmentShaderCompile = Material->GetAllowDevelopmentShaderCompile();

	// Compile the shader environment passed in with the shader type's source code.
	TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe> ShaderJob(NewJob);
	::GlobalBeginCompileShader(
		Material->GetFriendlyName(),
		VertexFactoryType,
		this,
		ShaderPipeline,
		GetShaderFilename(),
		GetFunctionName(),
		FShaderTarget(GetFrequency(),Platform),
		ShaderJob,
		NewJobs,
		bAllowDevelopmentShaderCompile,
		DebugDescription,
		DebugExtension
		);
	return NewJob;
}

void FMeshMaterialShaderType::BeginCompileShaderPipeline(
	uint32 ShaderMapId,
	int32 PermutationId,
	EShaderPlatform Platform,
	const FMaterial* Material,
	FShaderCompilerEnvironment* MaterialEnvironment,
	FVertexFactoryType* VertexFactoryType,
	const FShaderPipelineType* ShaderPipeline,
	const TArray<FMeshMaterialShaderType*>& ShaderStages,
	TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& NewJobs,
	FString DebugDescription,
	FString DebugExtension)
{
	check(ShaderStages.Num() > 0);
	check(ShaderPipeline);
	UE_LOG(LogShaders, Verbose, TEXT("	Pipeline: %s"), ShaderPipeline->GetName());

	// Add all the jobs as individual first, then add the dependencies into a pipeline job
	auto* NewPipelineJob = new FShaderPipelineCompileJob(ShaderMapId, ShaderPipeline, ShaderStages.Num());
	for (int32 Index = 0; Index < ShaderStages.Num(); ++Index)
	{
		auto* ShaderStage = ShaderStages[Index];
		ShaderStage->BeginCompileShader(ShaderMapId, PermutationId, Platform, Material, MaterialEnvironment, VertexFactoryType, ShaderPipeline, NewPipelineJob->StageJobs, DebugDescription, DebugExtension);
	}

	NewJobs.Add(TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>(NewPipelineJob));
}


static inline FString GetJobName(const FShaderCompileJob* SingleJob, const FShaderPipelineType* ShaderPipelineType, const FString& InDebugDescription)
{
	FString String = SingleJob->Input.GenerateShaderName();
	if (ShaderPipelineType)
	{
		String += FString::Printf(TEXT(" Pipeline '%s'"), ShaderPipelineType->GetName());
	}
	if (SingleJob->VFType)
	{
		String += FString::Printf(TEXT(" VF '%s'"), SingleJob->VFType->GetName());
	}
	String += FString::Printf(TEXT(" Type '%s'"), SingleJob->ShaderType->GetName());
	String += FString::Printf(TEXT(" '%s' Entry '%s' Permutation %i %s"), *SingleJob->Input.VirtualSourceFilePath, *SingleJob->Input.EntryPointName, SingleJob->PermutationId, *InDebugDescription);
	return String;
}

/**
 * Either creates a new instance of this type or returns an equivalent existing shader.
 * @param Material - The material to link the shader with.
 * @param CurrentJob - Compile job that was enqueued by BeginCompileShader.
 */
FShader* FMeshMaterialShaderType::FinishCompileShader( 
	const FUniformExpressionSet& UniformExpressionSet, 
	const FSHAHash& MaterialShaderMapHash,
	const FShaderCompileJob& CurrentJob,
	const FShaderPipelineType* ShaderPipelineType,
	const FString& InDebugDescription)
{
	checkf(CurrentJob.bSucceeded, TEXT("Failed MeshMaterialType compilation job: %s"), *GetJobName(&CurrentJob, ShaderPipelineType, InDebugDescription));
	checkf(CurrentJob.VFType, TEXT("No VF on MeshMaterialType compilation job: %s"), *GetJobName(&CurrentJob, ShaderPipelineType, InDebugDescription));

	if (ShaderPipelineType && !ShaderPipelineType->ShouldOptimizeUnusedOutputs(CurrentJob.Input.Target.GetPlatform()))
	{
		// If sharing shaders in this pipeline, remove it from the type/id so it uses the one in the shared shadermap list
		ShaderPipelineType = nullptr;
	}

	FShader* Shader = ConstructCompiled(CompiledShaderInitializerType(this, CurrentJob.PermutationId, CurrentJob.Output, UniformExpressionSet, MaterialShaderMapHash, InDebugDescription, ShaderPipelineType, CurrentJob.VFType));
	ValidateAfterBind(this, (FMeshMaterialShader*)Shader);
	CurrentJob.Output.ParameterMap.VerifyBindingsAreComplete(GetName(), CurrentJob.Output.Target, CurrentJob.VFType);

	return Shader;
}

bool FMeshMaterialShaderType::ShouldCompilePermutation(EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters, const FVertexFactoryType* VertexFactoryType, int32 PermutationId) const
{
	return FShaderType::ShouldCompilePermutation(FMeshMaterialShaderPermutationParameters(Platform, MaterialParameters, VertexFactoryType, PermutationId));
}

bool FMeshMaterialShaderType::ShouldCompileVertexFactoryPermutation(const FVertexFactoryType* VertexFactoryType, EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters)
{
	return VertexFactoryType->ShouldCache(FVertexFactoryShaderPermutationParameters(Platform, MaterialParameters, VertexFactoryType));
}

bool FMeshMaterialShaderType::ShouldCompilePipeline(const FShaderPipelineType* ShaderPipelineType, EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters, const FVertexFactoryType* VertexFactoryType)
{
	const FMeshMaterialShaderPermutationParameters Parameters(Platform, MaterialParameters, VertexFactoryType, kUniqueShaderPermutationId);
	for (const FShaderType* ShaderType : ShaderPipelineType->GetStages())
	{
		checkSlow(ShaderType->GetMeshMaterialShaderType());
		if (!ShaderType->ShouldCompilePermutation(Parameters))
		{
			return false;
		}
	}
	return true;
}

void FMeshMaterialShaderType::SetupCompileEnvironment(EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters, const FVertexFactoryType* VertexFactoryType, int32 PermutationId, FShaderCompilerEnvironment& Environment)
{
	// Allow the shader type to modify its compile environment.
	FShaderType::ModifyCompilationEnvironment(FMeshMaterialShaderPermutationParameters(Platform, MaterialParameters, VertexFactoryType, PermutationId), Environment);
}

/**
 * Enqueues compilation for all shaders for a material and vertex factory type.
 * @param Material - The material to compile shaders for.
 * @param VertexFactoryType - The vertex factory type to compile shaders for.
 * @param Platform - The platform to compile for.
 */
uint32 FMeshMaterialShaderMap::BeginCompile(
	uint32 ShaderMapId,
	const FMaterialShaderMapId& InShaderMapId, 
	const FMaterial* Material,
	const FMeshMaterialShaderMapLayout& MeshLayout,
	FShaderCompilerEnvironment* MaterialEnvironment,
	EShaderPlatform InPlatform,
	TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& NewJobs,
	FString DebugDescription,
	FString DebugExtension
	)
{
	uint32 NumShadersPerVF = 0;
	TSet<FString> ShaderTypeNames;

	// Iterate over all mesh material shader types.
	TMap<TShaderTypePermutation<const FShaderType>, FShaderCompileJob*> SharedShaderJobs;
	for (const FShaderLayoutEntry& Shader : MeshLayout.Shaders)
	{
		FMeshMaterialShaderType* ShaderType = static_cast<FMeshMaterialShaderType*>(Shader.ShaderType);
		if (!Material->ShouldCache(InPlatform, ShaderType, MeshLayout.VertexFactoryType))
		{
			continue;
		}

		// Verify that the shader map Id contains inputs for any shaders that will be put into this shader map
#if WITH_EDITOR
		check(InShaderMapId.ContainsVertexFactoryType(MeshLayout.VertexFactoryType));
		check(InShaderMapId.ContainsShaderType(ShaderType, kUniqueShaderPermutationId));
#endif

		NumShadersPerVF++;
		// only compile the shader if we don't already have it
		if (!HasShader(ShaderType, Shader.PermutationId))
		{
			// Compile this mesh material shader for this material and vertex factory type.
			FShaderCompileJob* Job = ShaderType->BeginCompileShader(
				ShaderMapId,
				Shader.PermutationId,
				InPlatform,
				Material,
				MaterialEnvironment,
				MeshLayout.VertexFactoryType,
				nullptr,
				NewJobs,
				DebugDescription,
				DebugExtension
			);
			TShaderTypePermutation<const FShaderType> ShaderTypePermutation(ShaderType, Shader.PermutationId);
			check(!SharedShaderJobs.Find(ShaderTypePermutation));
			SharedShaderJobs.Add(ShaderTypePermutation, Job);
		}
	}

	// Now the pipeline jobs; if it's a shareable pipeline, do not add duplicate jobs
	for (FShaderPipelineType* Pipeline : MeshLayout.ShaderPipelines)
	{
		if (!Material->ShouldCachePipeline(InPlatform, Pipeline, MeshLayout.VertexFactoryType))
		{
			continue;
		}

		auto& StageTypes = Pipeline->GetStages();

		// Verify that the shader map Id contains inputs for any shaders that will be put into this shader map
#if WITH_EDITOR
		check(InShaderMapId.ContainsShaderPipelineType(Pipeline));
#endif
		if (Pipeline->ShouldOptimizeUnusedOutputs(InPlatform))
		{
			NumShadersPerVF += StageTypes.Num();
			TArray<FMeshMaterialShaderType*> ShaderStagesToCompile;
			for (auto* Shader : StageTypes)
			{
				const FMeshMaterialShaderType* ShaderType = Shader->GetMeshMaterialShaderType();

				// Verify that the shader map Id contains inputs for any shaders that will be put into this shader map
#if WITH_EDITOR
				check(InShaderMapId.ContainsVertexFactoryType(MeshLayout.VertexFactoryType));
				check(InShaderMapId.ContainsShaderType(ShaderType, kUniqueShaderPermutationId));
#endif
				ShaderStagesToCompile.Add((FMeshMaterialShaderType*)ShaderType);
			}

			// Make a pipeline job with all the stages
			FMeshMaterialShaderType::BeginCompileShaderPipeline(ShaderMapId, kUniqueShaderPermutationId, InPlatform, Material, MaterialEnvironment, MeshLayout.VertexFactoryType, Pipeline, ShaderStagesToCompile, NewJobs, DebugDescription, DebugExtension);
		}
		else
		{
			// If sharing shaders amongst pipelines, add this pipeline as a dependency of an existing job
			for (const FShaderType* ShaderType : StageTypes)
			{
				TShaderTypePermutation<const FShaderType> ShaderTypePermutation(ShaderType, kUniqueShaderPermutationId);
				FShaderCompileJob** Job = SharedShaderJobs.Find(ShaderTypePermutation);
				checkf(Job, TEXT("Couldn't find existing shared job for mesh shader %s on pipeline %s!"), ShaderType->GetName(), Pipeline->GetName());
				auto* SingleJob = (*Job)->GetSingleShaderJob();
				auto& PipelinesToShare = SingleJob->SharingPipelines.FindOrAdd(MeshLayout.VertexFactoryType);
				check(!PipelinesToShare.Contains(Pipeline));
				PipelinesToShare.Add(Pipeline);
			}
		}
	}

	if (NumShadersPerVF > 0)
	{
		UE_LOG(LogShaders, Verbose, TEXT("			%s - %u shaders"), MeshLayout.VertexFactoryType->GetName(), NumShadersPerVF);
	}

	return NumShadersPerVF;
}

#if WITH_EDITOR
void FMeshMaterialShaderMap::LoadMissingShadersFromMemory(
	const FSHAHash& MaterialShaderMapHash, 
	const FMaterial* Material, 
	EShaderPlatform InPlatform)
{
#if 0
	for (TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList());ShaderTypeIt;ShaderTypeIt.Next())
	{
		FMeshMaterialShaderType* ShaderType = ShaderTypeIt->GetMeshMaterialShaderType();
		const int32 PermutationCount = ShaderType ? ShaderType->GetPermutationCount() : 0;
		for (int32 PermutationId = 0; PermutationId < PermutationCount; ++PermutationId)
		{
			if (ShouldCacheMeshShader(ShaderType, InPlatform, Material, VertexFactoryType, PermutationId) && !HasShader((FShaderType*)ShaderType, PermutationId))
			{
				const FShaderKey ShaderKey(MaterialShaderMapHash, nullptr, VertexFactoryType, PermutationId, InPlatform);
				FShader* FoundShader = ((FShaderType*)ShaderType)->FindShaderByKey(ShaderKey);

				if (FoundShader)
				{
					AddShader((FShaderType*)ShaderType, PermutationId, FoundShader);
				}
			}
		}
	}

	// Try to find necessary FShaderPipelineTypes in memory
	const bool bHasTessellation = Material->GetTessellationMode() != MTM_NoTessellation;
	for (TLinkedList<FShaderPipelineType*>::TIterator ShaderPipelineIt(FShaderPipelineType::GetTypeList());ShaderPipelineIt;ShaderPipelineIt.Next())
	{
		const FShaderPipelineType* PipelineType = *ShaderPipelineIt;
		if (PipelineType && PipelineType->IsMeshMaterialTypePipeline() && !HasShaderPipeline(PipelineType) && PipelineType->HasTessellation() == bHasTessellation)
		{
			auto& Stages = PipelineType->GetStages();
			int32 NumShaders = 0;
			for (const FShaderType* Shader : Stages)
			{
				FMeshMaterialShaderType* ShaderType = (FMeshMaterialShaderType*)Shader->GetMeshMaterialShaderType();
				if (ShaderType && ShouldCacheMeshShader(ShaderType, InPlatform, Material, VertexFactoryType, kUniqueShaderPermutationId))
				{
					++NumShaders;
				}
				else
				{
					break;
				}
			}

			if (NumShaders == Stages.Num())
			{
				TArray<FShader*> ShadersForPipeline;
				for (auto* Shader : Stages)
				{
					FMeshMaterialShaderType* ShaderType = (FMeshMaterialShaderType*)Shader->GetMeshMaterialShaderType();
					if (!HasShader(ShaderType, kUniqueShaderPermutationId))
					{
						const FShaderKey ShaderKey(MaterialShaderMapHash, PipelineType->ShouldOptimizeUnusedOutputs(InPlatform) ? PipelineType : nullptr, VertexFactoryType, kUniqueShaderPermutationId, InPlatform);
						FShader* FoundShader = ShaderType->FindShaderByKey(ShaderKey);
						if (FoundShader)
						{
							AddShader(ShaderType, kUniqueShaderPermutationId, FoundShader);
							ShadersForPipeline.Add(FoundShader);
						}
					}
				}

				if (ShadersForPipeline.Num() == NumShaders && !HasShaderPipeline(PipelineType))
				{
					auto* Pipeline = new FShaderPipeline(PointerTable, PipelineType, ShadersForPipeline);
					AddShaderPipeline(PipelineType, Pipeline);
				}
			}
		}
	}
#endif
}
#endif // WITH_EDITOR

/**
 * Removes all entries in the cache with exceptions based on a shader type
 * @param ShaderType - The shader type to flush
 */
void FMeshMaterialShaderMap::FlushShadersByShaderType(const FShaderType* ShaderType)
{
	if (ShaderType->GetMeshMaterialShaderType())
	{
		const int32 PermutationCount = ShaderType->GetPermutationCount();
		for (int32 PermutationId = 0; PermutationId < PermutationCount; ++PermutationId)
		{
			RemoveShaderTypePermutaion(ShaderType, PermutationId);
		}
	}
}

void FMeshMaterialShaderMap::FlushShadersByShaderPipelineType(const FShaderPipelineType* ShaderPipelineType)
{
	if (ShaderPipelineType->IsMeshMaterialTypePipeline())
	{
		RemoveShaderPipelineType(ShaderPipelineType);
	}
}
