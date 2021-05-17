// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraShader.h"
#include "NiagaraShared.h"
#include "NiagaraShaderMap.h"
#include "NiagaraScriptBase.h"
#include "Stats/StatsMisc.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "ShaderCompiler.h"
#include "NiagaraShaderDerivedDataVersion.h"
#include "NiagaraShaderCompilationManager.h"
#include "UObject/CoreRedirects.h"
#if WITH_EDITOR
	#include "Interfaces/ITargetPlatformManagerModule.h"
	#include "TickableEditorObject.h"
	#include "DerivedDataCacheInterface.h"
	#include "Interfaces/ITargetPlatformManagerModule.h"
	#include "Interfaces/ITargetPlatform.h"
#endif
#include "ProfilingDebugging/CookStats.h"

#include "NiagaraDataInterfaceBase.h"
#include "UObject/UObjectGlobals.h"
#include "NiagaraShaderModule.h"
#include "NiagaraCustomVersion.h"
#include "UObject/UObjectThreadContext.h"

IMPLEMENT_SHADER_TYPE(, FNiagaraShader, TEXT("/Plugin/FX/Niagara/Private/NiagaraEmitterInstanceShader.usf"),TEXT("SimulateMain"), SF_Compute)

int32 GCreateNiagaraShadersOnLoad = 0;
static FAutoConsoleVariableRef CVarCreateNiagaraShadersOnLoad(
	TEXT("niagara.CreateShadersOnLoad"),
	GCreateNiagaraShadersOnLoad,
	TEXT("Whether to create Niagara's simulation shaders on load, which can reduce hitching, but use more memory.  Otherwise they will be created as needed.")
);

int32 GNiagaraSkipVectorVMBackendOptimizations = 1;
static FAutoConsoleVariableRef CVarNiagaraSkipVectorVMBackendOptimizations(
	TEXT("fx.SkipVectorVMBackendOptimizations"),
	GNiagaraSkipVectorVMBackendOptimizations,
	TEXT("If 1, skip HLSLCC's backend optimization passes during VectorVM compilation. \n"),
	ECVF_Default
);

#if ENABLE_COOK_STATS
namespace NiagaraShaderCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static int32 ShadersCompiled = 0;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("NiagaraShader.Usage"), TEXT(""));
		AddStat(TEXT("NiagaraShader.Misc"), FCookStatsManager::CreateKeyValueArray(
			TEXT("ShadersCompiled"), ShadersCompiled
			));
	});
}
#endif


//
// Globals
//
NIAGARASHADER_API FCriticalSection GIdToNiagaraShaderMapCS;
TMap<FNiagaraShaderMapId, FNiagaraShaderMap*> FNiagaraShaderMap::GIdToNiagaraShaderMap[SP_NumPlatforms];
TArray<FNiagaraShaderMap*> FNiagaraShaderMap::AllNiagaraShaderMaps;


/** 
 * Tracks FNiagaraShaderScripts and their shader maps that are being compiled.
 * Uses a TRefCountPtr as this will be the only reference to a shader map while it is being compiled.
 */
TMap<TRefCountPtr<FNiagaraShaderMap>, TArray<FNiagaraShaderScript*> > FNiagaraShaderMap::NiagaraShaderMapsBeingCompiled;



static inline bool ShouldCacheNiagaraShader(const FNiagaraShaderType* ShaderType, EShaderPlatform Platform, const FNiagaraShaderScript* Script)
{
	return ShaderType->ShouldCache(Platform, Script) && Script->ShouldCache(Platform, ShaderType);
}



/** Called for every script shader to update the appropriate stats. */
void UpdateNiagaraShaderCompilingStats(const FNiagaraShaderScript* Script)
{
	INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumTotalNiagaraShaders,1);
}

int32 FNiagaraShaderMapPointerTable::AddIndexedPointer(const FTypeLayoutDesc& TypeDesc, void* Ptr)
{
	int32 Index = INDEX_NONE;
	if (DITypes.TryAddIndexedPtr(TypeDesc, Ptr, Index)) return Index;
	return Super::AddIndexedPointer(TypeDesc, Ptr);
}

void* FNiagaraShaderMapPointerTable::GetIndexedPointer(const FTypeLayoutDesc& TypeDesc, uint32 i) const
{
	void* Ptr = nullptr;
	if (DITypes.TryGetIndexedPtr(TypeDesc, i, Ptr)) return Ptr;
	return Super::GetIndexedPointer(TypeDesc, i);
}

void FNiagaraShaderMapPointerTable::SaveToArchive(FArchive& Ar, void* FrozenContent, bool bInlineShaderResources) const
{
	Super::SaveToArchive(Ar, FrozenContent, bInlineShaderResources);

	int32 NumDITypes = DITypes.Num();

	Ar << NumDITypes;

	for (int32 TypeIndex = 0; TypeIndex < NumDITypes; ++TypeIndex)
	{
		const UNiagaraDataInterfaceBase* DIType = DITypes.GetIndexedPointer(TypeIndex);
		FString DIClassName = DIType->GetClass()->GetFullName();
		Ar << DIClassName;
	}
}

void FNiagaraShaderMapPointerTable::LoadFromArchive(FArchive& Ar, void* FrozenContent, bool bInlineShaderResources, bool bLoadedByCookedMaterial)
{
	Super::LoadFromArchive(Ar, FrozenContent, bInlineShaderResources, bLoadedByCookedMaterial);
	INiagaraShaderModule * Module = INiagaraShaderModule::Get();

	int32 NumDITypes = 0;

	Ar << NumDITypes;

	DITypes.Empty(NumDITypes);
	for (int32 TypeIndex = 0; TypeIndex < NumDITypes; ++TypeIndex)
	{
		FString DIClassName;
		Ar << DIClassName;
		UNiagaraDataInterfaceBase* DIType = Module->RequestDefaultDataInterface(*DIClassName);
		DITypes.LoadIndexedPointer(DIType);
	}
}


/** Hashes the script-specific part of this shader map Id. */
void FNiagaraShaderMapId::GetScriptHash(FSHAHash& OutHash) const
{
	FSHA1 HashState;

	HashState.Update((const uint8*)&CompilerVersionID, sizeof(CompilerVersionID));
	//HashState.Update((const uint8*)&BaseScriptID, sizeof(BaseScriptID));
	HashState.Update(BaseCompileHash.Hash, FNiagaraCompileHash::HashSize);
	HashState.Update((const uint8*)&FeatureLevel, sizeof(FeatureLevel));

	for (int32 Index = 0; Index < AdditionalDefines.Num(); Index++)
	{
		HashState.UpdateWithString(*AdditionalDefines[Index], AdditionalDefines[Index].Len());
	}

	for (int32 Index = 0; Index < AdditionalVariables.Num(); Index++)
	{
		HashState.UpdateWithString(*AdditionalVariables[Index], AdditionalVariables[Index].Len());
	}

	for (int32 Index = 0; Index < ReferencedCompileHashes.Num(); Index++)
	{
		HashState.Update(ReferencedCompileHashes[Index].Hash, FNiagaraCompileHash::HashSize);
	}

	for (const FShaderTypeDependency& Dependency : ShaderTypeDependencies)
	{
		HashState.Update(Dependency.SourceHash.Hash, sizeof(Dependency.SourceHash));
	}

	//ParameterSet.UpdateHash(HashState);		// will need for static switches
	
	HashState.Final();
	HashState.GetHash(&OutHash.Hash[0]);
}

/** 
* Tests this set against another for equality, disregarding override settings.
* 
* @param ReferenceSet	The set to compare against
* @return				true if the sets are equal
*/
bool FNiagaraShaderMapId::operator==(const FNiagaraShaderMapId& ReferenceSet) const
{
	if (/*BaseScriptID != ReferenceSet.BaseScriptID 
		|| */BaseCompileHash != ReferenceSet.BaseCompileHash
		|| FeatureLevel != ReferenceSet.FeatureLevel
		|| CompilerVersionID != ReferenceSet.CompilerVersionID 
		|| bUsesRapidIterationParams != ReferenceSet.bUsesRapidIterationParams
		|| LayoutParams != ReferenceSet.LayoutParams)
	{
		return false;
	}

	if (AdditionalDefines.Num() != ReferenceSet.AdditionalDefines.Num())
	{
		return false;
	}

	if (AdditionalVariables.Num() != ReferenceSet.AdditionalVariables.Num())
	{
		return false;
	}

	if (ReferencedCompileHashes.Num() != ReferenceSet.ReferencedCompileHashes.Num())
	{
		return false;
	}

	for (int32 Idx = 0; Idx < ReferenceSet.AdditionalDefines.Num(); Idx++)
	{
		const FString& ReferenceStr = ReferenceSet.AdditionalDefines[Idx];

		if (AdditionalDefines[Idx] != ReferenceStr)
		{
			return false;
		}
	}

	for (int32 Idx = 0; Idx < ReferenceSet.AdditionalVariables.Num(); Idx++)
	{
		const FString& ReferenceStr = ReferenceSet.AdditionalVariables[Idx];

		if (AdditionalVariables[Idx] != ReferenceStr)
		{
			return false;
		}
	}

	for (int32 i = 0; i < ReferencedCompileHashes.Num(); i++)
	{
		if (ReferencedCompileHashes[i] != ReferenceSet.ReferencedCompileHashes[i])
		{
			return false;
		}
	}

	if (ShaderTypeDependencies != ReferenceSet.ShaderTypeDependencies)
	{
		return false;
	}

	/*
	if (ParameterSet != ReferenceSet.ParameterSet
		|| ReferencedFunctions.Num() != ReferenceSet.ReferencedFunctions.Num()
	{
		return false;
	}

	for (int32 RefFunctionIndex = 0; RefFunctionIndex < ReferenceSet.ReferencedFunctions.Num(); RefFunctionIndex++)
	{
		const FGuid& ReferenceGuid = ReferenceSet.ReferencedFunctions[RefFunctionIndex];

		if (ReferencedFunctions[RefFunctionIndex] != ReferenceGuid)
		{
			return false;
		}
	}
	*/
	return true;
}

void FNiagaraShaderMapId::AppendKeyString(FString& KeyString) const
{
	//KeyString += BaseScriptID.ToString();
	//KeyString += TEXT("_");

	KeyString += BaseCompileHash.ToString();
	KeyString += TEXT("_");

	FString FeatureLevelString;
	GetFeatureLevelName(FeatureLevel, FeatureLevelString);

	{
		const FSHAHash LayoutHash = Freeze::HashLayout(StaticGetTypeLayoutDesc<FNiagaraShaderMapContent>(), LayoutParams);
		KeyString += TEXT("_");
		KeyString += LayoutHash.ToString();
		KeyString += TEXT("_");
	}

	{
		const FSHAHash LayoutHash = Freeze::HashLayout(StaticGetTypeLayoutDesc<FNiagaraShader>(), LayoutParams);
		KeyString += TEXT("_");
		KeyString += LayoutHash.ToString();
		KeyString += TEXT("_");
	}

	/*for (const FHashedName& DITypeName : DITypeNames)
	{
		const FTypeLayoutDesc* DIType = FTypeLayoutDesc::Find(DITypeName.GetHash());
		if (DIType)
		{
			const FSHAHash LayoutHash = Freeze::HashLayout(*DIType, LayoutParams);
			KeyString += TEXT("_");
			KeyString += LayoutHash.ToString();
			KeyString += TEXT("_");
		}
	}*/

	KeyString += FeatureLevelString + TEXT("_");
	KeyString += CompilerVersionID.ToString();
	KeyString += TEXT("_");

	if (bUsesRapidIterationParams)
	{
		KeyString += TEXT("USESRI_");
	}
	else
	{
		KeyString += TEXT("NORI_");
	}
	
	// Add additional defines
	for (int32 DefinesIndex = 0; DefinesIndex < AdditionalDefines.Num(); DefinesIndex++)
	{
		KeyString += AdditionalDefines[DefinesIndex];
		if (DefinesIndex < AdditionalDefines.Num() - 1)
		{
			KeyString += TEXT("_");
		}
	}

	// Add additional variables
	for (int32 VariablesIndex = 0; VariablesIndex < AdditionalVariables.Num(); VariablesIndex++)
	{
		KeyString += AdditionalVariables[VariablesIndex];
		if (VariablesIndex < AdditionalVariables.Num() - 1)
		{
			KeyString += TEXT("_");
		}
	}

	// Add any referenced top level compile hashes to the key so that we will recompile when they are changed
	for (int32 HashIndex = 0; HashIndex < ReferencedCompileHashes.Num(); HashIndex++)
	{
		KeyString += ReferencedCompileHashes[HashIndex].ToString();
		if (HashIndex < ReferencedCompileHashes.Num() - 1)
		{
			KeyString += TEXT("_");
		}
	}

	for (const FShaderTypeDependency& Dependency : ShaderTypeDependencies)
	{
		KeyString += TEXT("_");
		KeyString += Dependency.SourceHash.ToString();
	}

	/*
	ParameterSet.AppendKeyString(KeyString);

	KeyString += TEXT("_");
	KeyString += FString::FromInt(Usage);
	KeyString += TEXT("_");

	// Add any referenced functions to the key so that we will recompile when they are changed
	for (int32 FunctionIndex = 0; FunctionIndex < ReferencedFunctions.Num(); FunctionIndex++)
	{
		KeyString += ReferencedFunctions[FunctionIndex].ToString();
	}
	*/
}


TMap<const FShaderCompileJob*, TArray<FNiagaraDataInterfaceGPUParamInfo> > FNiagaraShaderType::ExtraParamInfo;

#if WITH_EDITOR

/**
 * Enqueues a compilation for a new shader of this type.
 * @param script - The script to link the shader with.
 */
void FNiagaraShaderType::BeginCompileShader(
	uint32 ShaderMapId,
	int32 PermutationId,
	const FNiagaraShaderScript* Script,
	FSharedShaderCompilerEnvironment* CompilationEnvironment,
	EShaderPlatform Platform,
	TArray<FShaderCommonCompileJobPtr>& NewJobs,
	FShaderTarget Target,
	TArray<FNiagaraDataInterfaceGPUParamInfo>& InDIParamInfo
	)
{
	FShaderCompileJob* NewJob = GShaderCompilingManager->PrepareShaderCompileJob(ShaderMapId, FShaderCompileJobKey(this, nullptr, PermutationId), EShaderCompileJobPriority::Normal);
	if (!NewJob)
	{
		return;
	}

	//NewJob->DIParamInfo = InDIParamInfo;	// from hlsl translation and need to be passed to the FNiagaraShader on completion

	NewJob->Input.SharedEnvironment = CompilationEnvironment;
	NewJob->Input.Target = Target;
	NewJob->Input.ShaderFormat = LegacyShaderPlatformToShaderFormat(Platform);
	NewJob->Input.VirtualSourceFilePath = TEXT("/Plugin/FX/Niagara/Private/NiagaraEmitterInstanceShader.usf");
	NewJob->Input.EntryPointName = TEXT("SimulateMainComputeCS");
	NewJob->Input.Environment.SetDefine(TEXT("GPU_SIMULATION"), 1);
	NewJob->Input.Environment.SetDefine(TEXT("NIAGARA_MAX_GPU_SPAWN_INFOS"), NIAGARA_MAX_GPU_SPAWN_INFOS);
	Script->GetScriptHLSLSource(NewJob->Input.Environment.IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/NiagaraEmitterInstance.ush")));
	NewJob->Input.Environment.SetDefine(TEXT("SHADER_STAGE_PERMUTATION"), PermutationId);

	const bool bUsesSimulationStages = Script->GetUsesSimulationStages();
	if (bUsesSimulationStages)
	{
		const int32 StageIndex = Script->PermutationIdToShaderStageIndex(PermutationId);
		NewJob->Input.Environment.SetDefine(TEXT("NIAGARA_SHADER_PERMUTATIONS"), 1);
		NewJob->Input.Environment.SetDefine(TEXT("DefaultSimulationStageIndex"), 0);
		NewJob->Input.Environment.SetDefine(TEXT("SimulationStageIndex"), StageIndex);
		NewJob->Input.Environment.SetDefine(TEXT("USE_SIMULATION_STAGES"), 1);

		if (PermutationId > 0)
		{
			TConstArrayView<FSimulationStageMetaData> SimStageMetaDataArray = const_cast<FNiagaraShaderScript*>(Script)->GetBaseVMScript()->GetSimulationStageMetaData();
			const FSimulationStageMetaData& SimStageMetaData = SimStageMetaDataArray[PermutationId - 1];
			if (SimStageMetaData.bWritesParticles && SimStageMetaData.bPartialParticleUpdate)
			{
				NewJob->Input.Environment.SetDefine(TEXT("NIAGARA_PARTICLE_PARTIAL_ENABLED"), 1);
			}
		}
	}
	else
	{
		const bool bUsesOldShaderStages = Script->GetUsesOldShaderStages();

		NewJob->Input.Environment.SetDefine(TEXT("NIAGARA_SHADER_PERMUTATIONS"), 0);
		NewJob->Input.Environment.SetDefine(TEXT("USE_SIMULATION_STAGES"), bUsesOldShaderStages ? 1 : 0);
	}

	NewJob->Input.Environment.SetDefine(TEXT("NIAGARA_COMPRESSED_ATTRIBUTES_ENABLED"), Script->GetUsesCompressedAttributes() ? 1 : 0);

	Script->ModifyCompilationEnvironment(NewJob->Input.Environment);

	AddReferencedUniformBufferIncludes(NewJob->Input.Environment, NewJob->Input.SourceFilePrefix, (EShaderPlatform)Target.Platform);
	
	FShaderCompilerEnvironment& ShaderEnvironment = NewJob->Input.Environment;

	UE_LOG(LogShaders, Verbose, TEXT("			%s"), GetName());
	COOK_STAT(NiagaraShaderCookStats::ShadersCompiled++);

	//update script shader stats
	UpdateNiagaraShaderCompilingStats(Script);

	// Allow the shader type to modify the compile environment.
	SetupCompileEnvironment(Platform, Script, ShaderEnvironment);

	::GlobalBeginCompileShader(
		Script->GetFriendlyName(),
		nullptr,
		this,
		nullptr,//ShaderPipeline,
		PermutationId,
		TEXT("/Plugin/FX/Niagara/Private/NiagaraEmitterInstanceShader.usf"),
		TEXT("SimulateMainComputeCS"),
		FShaderTarget(GetFrequency(), Platform),
		NewJob->Input
	);

	ExtraParamInfo.Add(NewJob, InDIParamInfo);
	NewJobs.Add(FShaderCommonCompileJobPtr(NewJob));
}
#endif

void FNiagaraShaderType::CacheUniformBufferIncludes(TMap<const TCHAR*, FCachedUniformBufferDeclaration>& Cache, EShaderPlatform Platform) const
{
	for (TMap<const TCHAR*, FCachedUniformBufferDeclaration>::TIterator It(Cache); It; ++It)
	{
		FCachedUniformBufferDeclaration& BufferDeclaration = It.Value();

		for (TLinkedList<FShaderParametersMetadata*>::TIterator StructIt(FShaderParametersMetadata::GetStructList()); StructIt; StructIt.Next())
		{
			if (It.Key() == StructIt->GetShaderVariableName())
			{
				BufferDeclaration.Declaration = MakeShared<FString, ESPMode::ThreadSafe>();
				CreateUniformBufferShaderDeclaration(StructIt->GetShaderVariableName(), **StructIt, Platform, *BufferDeclaration.Declaration.Get());
				break;
			}
		}
	}
}



void FNiagaraShaderType::AddReferencedUniformBufferIncludes(FShaderCompilerEnvironment& OutEnvironment, FString& OutSourceFilePrefix, EShaderPlatform Platform) const
{
	// Cache uniform buffer struct declarations referenced by this shader type's files
	if (!bCachedUniformBufferStructDeclarations)
	{
		CacheUniformBufferIncludes(ReferencedUniformBufferStructsCache, Platform);
		bCachedUniformBufferStructDeclarations = true;
	}

	FString UniformBufferIncludes;

	for (TMap<const TCHAR*, FCachedUniformBufferDeclaration>::TConstIterator It(ReferencedUniformBufferStructsCache); It; ++It)
	{
		check(It.Value().Declaration.IsValid());
		UniformBufferIncludes += FString::Printf(TEXT("#include \"/Engine/Generated/UniformBuffers/%s.ush\"") LINE_TERMINATOR, It.Key());
		FString* Declaration = It.Value().Declaration.Get();
		check(Declaration);
		OutEnvironment.IncludeVirtualPathToContentsMap.Add(
			*FString::Printf(TEXT("/Engine/Generated/UniformBuffers/%s.ush"), It.Key()), *Declaration
		);

		for (TLinkedList<FShaderParametersMetadata*>::TIterator StructIt(FShaderParametersMetadata::GetStructList()); StructIt; StructIt.Next())
		{
			if (It.Key() == StructIt->GetShaderVariableName())
			{
				StructIt->AddResourceTableEntries(OutEnvironment.ResourceTableMap, OutEnvironment.ResourceTableLayoutHashes, OutEnvironment.ResourceTableLayoutSlots);
			}
		}
	}

	FString& GeneratedUniformBuffersInclude = OutEnvironment.IncludeVirtualPathToContentsMap.FindOrAdd("/Engine/Generated/GeneratedUniformBuffers.ush");
	GeneratedUniformBuffersInclude.Append(UniformBufferIncludes);

	ERHIFeatureLevel::Type MaxFeatureLevel = GetMaxSupportedFeatureLevel(Platform);
	if (MaxFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		OutEnvironment.SetDefine(TEXT("PLATFORM_SUPPORTS_SRV_UB"), TEXT("1"));
	}
}


/**
 * Either creates a new instance of this type or returns an equivalent existing shader.
 * @param CurrentJob - Compile job that was enqueued by BeginCompileShader.
 */
FShader* FNiagaraShaderType::FinishCompileShader(
	//const FUniformExpressionSet& UniformExpressionSet,
	const FSHAHash& ShaderMapHash,
	const FShaderCompileJob& CurrentJob,
	const FString& InDebugDescription
	) const
{
	check(CurrentJob.bSucceeded);

	TArray<FNiagaraDataInterfaceGPUParamInfo> DIParamInfo;
	if (!ExtraParamInfo.RemoveAndCopyValue(&CurrentJob, DIParamInfo))
	{
		check(false);
	}

	FShader* Shader = ConstructCompiled(FNiagaraShaderType::CompiledShaderInitializerType(this, CurrentJob.Key.PermutationId, CurrentJob.Output, ShaderMapHash, InDebugDescription, DIParamInfo));
	//CurrentJob.Output.ParameterMap.VerifyBindingsAreComplete(GetName(), CurrentJob.Output.Target, nullptr); // b/c we don't bind data interfaces yet...

	return Shader;
}

/**
* Finds the shader map for a script.
* @param ShaderMapId - The script id and static parameter set identifying the shader map
* @param Platform - The platform to lookup for
* @return NULL if no cached shader map was found.
*/
FNiagaraShaderMap* FNiagaraShaderMap::FindId(const FNiagaraShaderMapId& ShaderMapId, EShaderPlatform InPlatform)
{
	FNiagaraShaderMap* Result = GIdToNiagaraShaderMap[InPlatform].FindRef(ShaderMapId);
	check(Result == nullptr || !Result->bDeletedThroughDeferredCleanup);
	return Result;
}

/** Flushes the given shader types from any loaded FNiagaraShaderMap's. */
void FNiagaraShaderMap::FlushShaderTypes(TArray<const FShaderType*>& ShaderTypesToFlush)
{
	for (int32 ShaderMapIndex = 0; ShaderMapIndex < AllNiagaraShaderMaps.Num(); ShaderMapIndex++)
	{
		FNiagaraShaderMap* CurrentShaderMap = AllNiagaraShaderMaps[ShaderMapIndex];

		for (int32 ShaderTypeIndex = 0; ShaderTypeIndex < ShaderTypesToFlush.Num(); ShaderTypeIndex++)
		{
			CurrentShaderMap->FlushShadersByShaderType(ShaderTypesToFlush[ShaderTypeIndex]);
		}
	}
}

#if 0
void FNiagaraShaderMap::FixupShaderTypes(EShaderPlatform Platform, const TMap<FShaderType*, FString>& ShaderTypeNames)
{
	FScopeLock ScopeLock(&GIdToNiagaraShaderMapCS);

	TArray<FNiagaraShaderMapId> Keys;
	FNiagaraShaderMap::GIdToNiagaraShaderMap[Platform].GenerateKeyArray(Keys);

	TArray<FNiagaraShaderMap*> Values;
	FNiagaraShaderMap::GIdToNiagaraShaderMap[Platform].GenerateValueArray(Values);

	//@todo - what about the shader maps in AllNiagaraShaderMaps that are not in GIdToNiagaraShaderMap?
	FNiagaraShaderMap::GIdToNiagaraShaderMap[Platform].Empty();

	for (int32 PairIndex = 0; PairIndex < Keys.Num(); PairIndex++)
	{
		FNiagaraShaderMapId& Key = Keys[PairIndex];
		FNiagaraShaderMap::GIdToNiagaraShaderMap[Platform].Add(Key, Values[PairIndex]);
	}
}
#endif

void NiagaraShaderMapAppendKeyString(EShaderPlatform Platform, FString& KeyString)
{
	// does nothing at the moment, but needs to append to keystring if runtime options impacting selection of sim shader permutations are added
	// for example static switches will need to go here
}

#if WITH_EDITOR

/** Creates a string key for the derived data cache given a shader map id. */
static FString GetNiagaraShaderMapKeyString(const FNiagaraShaderMapId& ShaderMapId, EShaderPlatform Platform)
{
	FName Format = LegacyShaderPlatformToShaderFormat(Platform);
	FString ShaderMapKeyString = Format.ToString() + TEXT("_") + FString(FString::FromInt(GetTargetPlatformManagerRef().ShaderFormatVersion(Format))) + TEXT("_");
	NiagaraShaderMapAppendKeyString(Platform, ShaderMapKeyString);
	ShaderMapAppendKeyString(Platform, ShaderMapKeyString);
	ShaderMapId.AppendKeyString(ShaderMapKeyString);
	return FDerivedDataCacheInterface::BuildCacheKey(TEXT("NIAGARASM"), NIAGARASHADERMAP_DERIVEDDATA_VER, *ShaderMapKeyString);
}



void FNiagaraShaderMap::LoadFromDerivedDataCache(const FNiagaraShaderScript* Script, const FNiagaraShaderMapId& ShaderMapId, EShaderPlatform Platform, TRefCountPtr<FNiagaraShaderMap>& InOutShaderMap)
{
	if (InOutShaderMap != NULL)
	{
		check(InOutShaderMap->GetShaderPlatform() == Platform);
		// If the shader map was non-NULL then it was found in memory but is incomplete, attempt to load the missing entries from memory
		InOutShaderMap->LoadMissingShadersFromMemory(Script);
	}
	else
	{
		// Shader map was not found in memory, try to load it from the DDC
		STAT(double NiagaraShaderDDCTime = 0);
		{
			SCOPE_SECONDS_COUNTER(NiagaraShaderDDCTime);
			COOK_STAT(auto Timer = NiagaraShaderCookStats::UsageStats.TimeSyncWork());

			TArray<uint8> CachedData;
			const FString DataKey = GetNiagaraShaderMapKeyString(ShaderMapId, Platform);

			if (GetDerivedDataCacheRef().GetSynchronous(*DataKey, CachedData, Script->GetFriendlyName()))
			{
				COOK_STAT(Timer.AddHit(CachedData.Num()));
				InOutShaderMap = new FNiagaraShaderMap();
				FMemoryReader Ar(CachedData, true);

				// Deserialize from the cached data
				if (InOutShaderMap->Serialize(Ar))
				{
					checkSlow(InOutShaderMap->GetShaderMapId() == ShaderMapId);

					// Register in the global map
					InOutShaderMap->Register(Platform);
				}
				else
				{
					// if the serialization failed it's likely because the resource is out of date now (i.e. shader parameters changed)
					COOK_STAT(Timer.TrackCyclesOnly());
					InOutShaderMap = nullptr;
				}
			}
			else
			{
				// We should be build the data later, and we can track that the resource was built there when we push it to the DDC.
				COOK_STAT(Timer.TrackCyclesOnly());
				InOutShaderMap = nullptr;
			}
		}
		INC_FLOAT_STAT_BY(STAT_ShaderCompiling_DDCLoading,(float)NiagaraShaderDDCTime);
	}
}

void FNiagaraShaderMap::SaveToDerivedDataCache()
{
	COOK_STAT(auto Timer = NiagaraShaderCookStats::UsageStats.TimeSyncWork());
	TArray<uint8> SaveData;
	FMemoryWriter Ar(SaveData, true);
	Serialize(Ar);

	GetDerivedDataCacheRef().Put(*GetNiagaraShaderMapKeyString(GetContent()->ShaderMapId, GetShaderPlatform()), SaveData, FStringView(*GetFriendlyName()));
	COOK_STAT(Timer.AddMiss(SaveData.Num()));
}

TArray<uint8>* FNiagaraShaderMap::BackupShadersToMemory()
{
	TArray<uint8>* SavedShaderData = new TArray<uint8>();
	FMemoryWriter Ar(*SavedShaderData);

	check(false);
	//SerializeInline(Ar, true, true, false);
	//RegisterSerializedShaders(false);
	//Empty();

	return SavedShaderData;
}

void FNiagaraShaderMap::RestoreShadersFromMemory(const TArray<uint8>& ShaderData)
{
	FMemoryReader Ar(ShaderData);
	check(false);
	//SerializeInline(Ar, true, true, false);
	//RegisterSerializedShaders(false);
}

/**
* Compiles the shaders for a script and caches them in this shader map.
* @param script - The script to compile shaders for.
* @param InShaderMapId - the script id and set of static parameters to compile for
* @param Platform - The platform to compile to
*/
void FNiagaraShaderMap::Compile(
	FNiagaraShaderScript* Script,
	const FNiagaraShaderMapId& InShaderMapId,
	TRefCountPtr<FSharedShaderCompilerEnvironment> CompilationEnvironment,
	const FNiagaraComputeShaderCompilationOutput& InNiagaraCompilationOutput,
	EShaderPlatform InPlatform,
	bool bSynchronousCompile,
	bool bApplyCompletedShaderMapForRendering)
{
	if (FPlatformProperties::RequiresCookedData())
	{
		UE_LOG(LogShaders, Fatal, TEXT("Trying to compile Niagara shader %s at run-time, which is not supported on consoles!"), *Script->GetFriendlyName() );
	}
	else
	{
		// Make sure we are operating on a referenced shader map or the below Find will cause this shader map to be deleted,
		// Since it creates a temporary ref counted pointer.
		check(NumRefs > 0);
  
		//All access to NiagaraShaderMapsBeingCompiled must be done on the game thread!
		check(IsInGameThread());
		// Add this shader map and to NiagaraShaderMapsBeingCompiled
		TArray<FNiagaraShaderScript*>* CorrespondingScripts = NiagaraShaderMapsBeingCompiled.Find(this);
  
		if (CorrespondingScripts)
		{
			check(!bSynchronousCompile);
			CorrespondingScripts->AddUnique(Script);
		}
		else
		{
			Script->RemoveOutstandingCompileId(CompilingId);
			// Assign a unique identifier so that shaders from this shader map can be associated with it after a deferred compile
			CompilingId = FShaderCommonCompileJob::GetNextJobId();
			Script->AddCompileId(CompilingId);
  
			TArray<FNiagaraShaderScript*> NewCorrespondingScripts;
			NewCorrespondingScripts.Add(Script);
			NiagaraShaderMapsBeingCompiled.Add(this, NewCorrespondingScripts);
#if DEBUG_INFINITESHADERCOMPILE
			UE_LOG(LogTemp, Display, TEXT("Added Niagara ShaderMap 0x%08X%08X with Script 0x%08X%08X to NiagaraShaderMapsBeingCompiled"), (int)((int64)(this) >> 32), (int)((int64)(this)), (int)((int64)(Script) >> 32), (int)((int64)(Script)));
#endif  
			// Setup the compilation environment.
			Script->SetupShaderCompilationEnvironment(InPlatform, *CompilationEnvironment);
  
			// Store the script name for debugging purposes.
			FNiagaraShaderMapContent* NewContent = new FNiagaraShaderMapContent(InPlatform);
			NewContent->FriendlyName = Script->GetFriendlyName();
			NewContent->NiagaraCompilationOutput = InNiagaraCompilationOutput;
			NewContent->ShaderMapId = InShaderMapId;
			AssignContent(NewContent);

			uint32 NumShaders = 0;
			TArray<FShaderCommonCompileJobPtr> NewJobs;
	
			// Iterate over all shader types.
			for(TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList());ShaderTypeIt;ShaderTypeIt.Next())
			{
				FNiagaraShaderType* ShaderType = ShaderTypeIt->GetNiagaraShaderType();
				if (ShaderType && ShouldCacheNiagaraShader(ShaderType, InPlatform, Script))
				{
					// Compile this niagara shader .
					TArray<FString> ShaderErrors;
  
					for (int32 PermutationId = 0; PermutationId < Script->GetNumPermutations(); ++PermutationId)
					{
						// Only compile the shader if we don't already have it
						if (!NewContent->HasShader(ShaderType, PermutationId))
						{
							ShaderType->BeginCompileShader(
								CompilingId,
								PermutationId, 
								Script,
								CompilationEnvironment,
								InPlatform,
								NewJobs,
								FShaderTarget(ShaderType->GetFrequency(), InPlatform),
								Script->GetDataInterfaceParamInfo()
							);
						}
						NumShaders++;
					}
				}
				else if (ShaderType)
				{
					UE_LOG(LogShaders, Display, TEXT("Skipping compilation of %s as it isn't supported on this target type."), *Script->SourceName);
					Script->RemoveOutstandingCompileId(CompilingId);
					// Can't call NotifyCompilationFinished() when post-loading. 
					// This normally happens when compiled in-sync for which the callback is not required.
					if (!FUObjectThreadContext::Get().IsRoutingPostLoad)
					{
						Script->NotifyCompilationFinished();
					}
				}
			}
  
			if (!CorrespondingScripts)
			{
				UE_LOG(LogShaders, Log, TEXT("		%u Shaders"), NumShaders);
			}

			// Register this shader map in the global script->shadermap map
			Register(InPlatform);
  
			// Mark the shader map as not having been finalized with ProcessCompilationResults
			bCompilationFinalized = false;
  
			// Mark as not having been compiled
			bCompiledSuccessfully = false;
  
			GNiagaraShaderCompilationManager.AddJobs(NewJobs);
  
			// Compile the shaders for this shader map now if not deferring and deferred compiles are not enabled globally
			if (bSynchronousCompile)
			{
				TArray<int32> CurrentShaderMapId;
				CurrentShaderMapId.Add(CompilingId);
				GNiagaraShaderCompilationManager.FinishCompilation(*NewContent->FriendlyName, CurrentShaderMapId);
			}
		}
	}
}

FShader* FNiagaraShaderMap::ProcessCompilationResultsForSingleJob(const TRefCountPtr<class FShaderCommonCompileJob>& SingleJob, const FSHAHash& ShaderMapHash)
{
	auto CurrentJob = SingleJob->GetSingleShaderJob();
	check(CurrentJob->Id == CompilingId);

	GetResourceCode()->AddShaderCompilerOutput(CurrentJob->Output);

	FShader* Shader = nullptr;

	const FNiagaraShaderType* NiagaraShaderType = CurrentJob->Key.ShaderType->GetNiagaraShaderType();
	check(NiagaraShaderType);
	Shader = NiagaraShaderType->FinishCompileShader(ShaderMapHash, *CurrentJob, GetContent()->FriendlyName);
	bCompiledSuccessfully = CurrentJob->bSucceeded;

	FNiagaraShader *NiagaraShader = static_cast<FNiagaraShader*>(Shader);

	// UE-67395 - we had a case where we polluted the DDC with a shader containing no bytecode.
	check(Shader && Shader->GetCodeSize() > 0);
	check(!GetContent()->HasShader(NiagaraShaderType, CurrentJob->Key.PermutationId));
	return GetMutableContent()->FindOrAddShader(NiagaraShaderType->GetHashedName(), CurrentJob->Key.PermutationId, Shader);
}

bool FNiagaraShaderMap::ProcessCompilationResults(const TArray<FShaderCommonCompileJobPtr>& InCompilationResults, int32& InOutJobIndex, float& TimeBudget)
{
	check(InOutJobIndex < InCompilationResults.Num());

	double StartTime = FPlatformTime::Seconds();

	FSHAHash ShaderMapHash;
	GetContent()->ShaderMapId.GetScriptHash(ShaderMapHash);

	do
	{
		{
			ProcessCompilationResultsForSingleJob(InCompilationResults[InOutJobIndex], ShaderMapHash);
		}

		InOutJobIndex++;
		
		double NewStartTime = FPlatformTime::Seconds();
		TimeBudget -= NewStartTime - StartTime;
		StartTime = NewStartTime;
	}
	while ((TimeBudget > 0.0f) && (InOutJobIndex < InCompilationResults.Num()));

	if (InOutJobIndex == InCompilationResults.Num())
	{
		FinalizeContent();

		SaveToDerivedDataCache();
		// The shader map can now be used on the rendering thread
		bCompilationFinalized = true;
		return true;
	}

	return false;
}

bool FNiagaraShaderMap::TryToAddToExistingCompilationTask(FNiagaraShaderScript* Script)
{
	check(NumRefs > 0);
	//All access to NiagaraShaderMapsBeingCompiled must be done on the game thread!
	check(IsInGameThread());
	TArray<FNiagaraShaderScript*>* CorrespondingScripts = FNiagaraShaderMap::NiagaraShaderMapsBeingCompiled.Find(this);

	if (CorrespondingScripts)
	{
		CorrespondingScripts->AddUnique(Script);

		UE_LOG(LogShaders, Log, TEXT("TryToAddToExistingCompilationTask %p %d"), Script, GetCompilingId());

#if DEBUG_INFINITESHADERCOMPILE
		UE_LOG(LogTemp, Display, TEXT("Added shader map 0x%08X%08X from Niagara script 0x%08X%08X"), (int)((int64)(this) >> 32), (int)((int64)(this)), (int)((int64)(Script) >> 32), (int)((int64)(Script)));
#endif
		return true;
	}

	return false;
}

bool FNiagaraShaderMap::IsNiagaraShaderComplete(const FNiagaraShaderScript* Script, const FNiagaraShaderType* ShaderType, bool bSilent)
{
	// If we should cache this script, it's incomplete if the shader is missing
	if (ShouldCacheNiagaraShader(ShaderType, GetShaderPlatform(), Script))
	{
		for (int32 PermutationId = 0; PermutationId < Script->GetNumPermutations(); ++PermutationId)
		{
			if (!GetContent()->HasShader((FShaderType*)ShaderType, PermutationId))
			{
				if (!bSilent)
				{
					UE_LOG(LogShaders, Warning, TEXT("Incomplete shader %s, missing FNiagaraShader %s."), *Script->GetFriendlyName(), ShaderType->GetName());
				}
				return false;
			}
		}
	}

	return true;
}

bool FNiagaraShaderMap::IsComplete(const FNiagaraShaderScript* Script, bool bSilent)
{
	check(!GIsThreadedRendering || !IsInActualRenderingThread());
	// Make sure we are operating on a referenced shader map or the below Find will cause this shader map to be deleted,
	// Since it creates a temporary ref counted pointer.
	check(NumRefs > 0);
	//All access to NiagaraShaderMapsBeingCompiled must be done on the game thread!
	check(IsInGameThread());
	const TArray<FNiagaraShaderScript*>* CorrespondingScripts = FNiagaraShaderMap::NiagaraShaderMapsBeingCompiled.Find(this);

	if (CorrespondingScripts)
	{
		check(!bCompilationFinalized);
		return false;
	}

	// Iterate over all shader types.
	for(TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList());ShaderTypeIt;ShaderTypeIt.Next())
	{
		// Find this shader type in the script's shader map.
		const FNiagaraShaderType* ShaderType = ShaderTypeIt->GetNiagaraShaderType();
		if (ShaderType && !IsNiagaraShaderComplete(Script, ShaderType, bSilent))
		{
			return false;
		}
	}

	return true;
}

void FNiagaraShaderMap::LoadMissingShadersFromMemory(const FNiagaraShaderScript* Script)
{
#if 0
	// Make sure we are operating on a referenced shader map or the below Find will cause this shader map to be deleted,
	// Since it creates a temporary ref counted pointer.
	check(NumRefs > 0);

	//All access to NiagaraShaderMapsBeingCompiled must be done on the game thread!
	check(IsInGameThread());
	const TArray<FNiagaraShaderScript*>* CorrespondingScripts = FNiagaraShaderMap::NiagaraShaderMapsBeingCompiled.Find(this);

	if (CorrespondingScripts)
	{
		check(!bCompilationFinalized);
		return;
	}

	FSHAHash ShaderMapHash;
	ShaderMapId.GetScriptHash(ShaderMapHash);

	// Try to find necessary FNiagaraShaderType's in memory
	for (TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList());ShaderTypeIt;ShaderTypeIt.Next())
	{
		FNiagaraShaderType* ShaderType = ShaderTypeIt->GetNiagaraShaderType();
		if (ShaderType && ShouldCacheNiagaraShader(ShaderType, Platform, Script) && !HasShader(ShaderType, /* PermutationId = */ 0))
		{
			FShaderKey ShaderKey(ShaderMapHash, nullptr, nullptr, /** PermutationId = */ 0, Platform);
			FShader* FoundShader = ShaderType->FindShaderByKey(ShaderKey);
			if (FoundShader)
			{
				AddShader(ShaderType, /* PermutationId = */ 0, FoundShader);
			}
		}
	}
#endif
}
#endif

void FNiagaraShaderMap::GetShaderList(TMap<FShaderId, TShaderRef<FShader>>& OutShaders) const
{
	GetContent()->GetShaderList(*this, FSHAHash(), OutShaders);
}

/**
 * Registers a Niagara shader map in the global map so it can be used by scripts.
 */
void FNiagaraShaderMap::Register(EShaderPlatform InShaderPlatform)
{
	extern int32 GCreateNiagaraShadersOnLoad;
	if (GCreateNiagaraShadersOnLoad && GetShaderPlatform() == InShaderPlatform)
	{
		// TODO
	}

	if (!bRegistered)
	{
		INC_DWORD_STAT(STAT_Shaders_NumShaderMaps);
	}

	{
		FScopeLock ScopeLock(&GIdToNiagaraShaderMapCS);
		GIdToNiagaraShaderMap[GetShaderPlatform()].Add(GetContent()->ShaderMapId, this);
		bRegistered = true;
	}
}

void FNiagaraShaderMap::AddRef()
{
	FScopeLock ScopeLock(&GIdToNiagaraShaderMapCS);
	check(!bDeletedThroughDeferredCleanup);
	++NumRefs;
}

void FNiagaraShaderMap::Release()
{
	{
		FScopeLock ScopeLock(&GIdToNiagaraShaderMapCS);

		check(NumRefs > 0);
		if (--NumRefs == 0)
		{
			if (bRegistered)
			{
				DEC_DWORD_STAT(STAT_Shaders_NumShaderMaps);

				GIdToNiagaraShaderMap[GetShaderPlatform()].Remove(GetContent()->ShaderMapId);
				bRegistered = false;
			}

			check(!bDeletedThroughDeferredCleanup);
			bDeletedThroughDeferredCleanup = true;
		}
	}
	if (bDeletedThroughDeferredCleanup)
	{
		BeginCleanup(this);
	}
}

FNiagaraShaderMap::FNiagaraShaderMap() :
	CompilingId(1),
	NumRefs(0),
	bDeletedThroughDeferredCleanup(false),
	bRegistered(false),
	bCompilationFinalized(true),
	bCompiledSuccessfully(true),
	bIsPersistent(true) 
{
	checkSlow(IsInGameThread() || IsAsyncLoading());
	AllNiagaraShaderMaps.Add(this);
}

FNiagaraShaderMap::~FNiagaraShaderMap()
{ 
	checkSlow(IsInGameThread() || IsAsyncLoading());
	check(bDeletedThroughDeferredCleanup);
	check(!bRegistered);
	AllNiagaraShaderMaps.RemoveSwap(this);
}

/**
 * Removes all entries in the cache with exceptions based on a shader type
 * @param ShaderType - The shader type to flush
 */
void FNiagaraShaderMap::FlushShadersByShaderType(const FShaderType* ShaderType)
{
	if (ShaderType->GetNiagaraShaderType())
	{
		for (int32 PermutationId = 0; PermutationId < ShaderType->GetPermutationCount(); ++PermutationId)
		{
			GetMutableContent()->RemoveShaderTypePermutaion(ShaderType->GetNiagaraShaderType(), PermutationId);
		}
	}
}

bool FNiagaraShaderMap::Serialize(FArchive& Ar, bool bInlineShaderResources, bool bLoadedByCookedMaterial)
{
	// Note: This is saved to the DDC, not into packages (except when cooked)
	// Backwards compatibility therefore will not work based on the version of Ar
	// Instead, just bump NIAGARASHADERMAP_DERIVEDDATA_VER
	return Super::Serialize(Ar, bInlineShaderResources, bLoadedByCookedMaterial);
}

bool FNiagaraShaderMap::RemovePendingScript(FNiagaraShaderScript* Script)
{
	bool bRemoved = false;
	//All access to NiagaraShaderMapsBeingCompiled must be done on the game thread!
	check(IsInGameThread());
	for (TMap<TRefCountPtr<FNiagaraShaderMap>, TArray<FNiagaraShaderScript*> >::TIterator It(NiagaraShaderMapsBeingCompiled); It; ++It)
	{
		TArray<FNiagaraShaderScript*>& Scripts = It.Value();
		int32 Result = Scripts.Remove(Script);
		if (Result)
		{
			Script->RemoveOutstandingCompileId(It.Key()->CompilingId);
			// Can't call NotifyCompilationFinished() when post-loading. 
			// This normally happens when compiled in-sync for which the callback is not required.
			if (!FUObjectThreadContext::Get().IsRoutingPostLoad)
			{
				Script->NotifyCompilationFinished();
			}
			bRemoved = true;
		}
#if DEBUG_INFINITESHADERCOMPILE
		if ( Result )
		{
			UE_LOG(LogTemp, Display, TEXT("Removed shader map 0x%08X%08X from script 0x%08X%08X"), (int)((int64)(It.Key().GetReference()) >> 32), (int)((int64)(It.Key().GetReference())), (int)((int64)(Script) >> 32), (int)((int64)(Script)));
		}
#endif
	}

	return bRemoved;
}


void FNiagaraShaderMap::RemovePendingMap(FNiagaraShaderMap* Map)
{
	//All access to NiagaraShaderMapsBeingCompiled must be done on the game thread!
	check(IsInGameThread());
	TArray<FNiagaraShaderScript*>* Scripts = NiagaraShaderMapsBeingCompiled.Find(Map);
	if (Scripts)
	{
		for (FNiagaraShaderScript* Script : *Scripts)
		{
			Script->RemoveOutstandingCompileId(Map->CompilingId);
			// Can't call NotifyCompilationFinished() when post-loading. 
			// This normally happens when compiled in-sync for which the callback is not required.
			if (!FUObjectThreadContext::Get().IsRoutingPostLoad)
			{
				Script->NotifyCompilationFinished();
			}
		}
	}

	NiagaraShaderMapsBeingCompiled.Remove(Map);
}

const FNiagaraShaderMap* FNiagaraShaderMap::GetShaderMapBeingCompiled(const FNiagaraShaderScript* Script)
{
	// Inefficient search, but only when compiling a lot of shaders
	//All access to NiagaraShaderMapsBeingCompiled must be done on the game thread!
	check(IsInGameThread());
	for (TMap<TRefCountPtr<FNiagaraShaderMap>, TArray<FNiagaraShaderScript*> >::TIterator It(NiagaraShaderMapsBeingCompiled); It; ++It)
	{
		TArray<FNiagaraShaderScript*>& Scripts = It.Value();
		
		for (int32 ScriptIndex = 0; ScriptIndex < Scripts.Num(); ScriptIndex++)
		{
			if (Scripts[ScriptIndex] == Script)
			{
				return It.Key();
			}
		}
	}

	return NULL;
}

//////////////////////////////////////////////////////////////////////////


FNiagaraShader::FNiagaraShader(const FNiagaraShaderType::CompiledShaderInitializerType& Initializer)
	: FShader(Initializer)
	, DebugDescription(Initializer.DebugDescription)
{
	check(!DebugDescription.IsEmpty());
	BindParams(Initializer.DIParamInfo, Initializer.ParameterMap);
}

void FNiagaraShader::BindParams(const TArray<FNiagaraDataInterfaceGPUParamInfo>& InDIParamInfo, const FShaderParameterMap &ParameterMap)
{
	FloatInputBufferParam.Bind(ParameterMap, TEXT("InputFloat"));
	IntInputBufferParam.Bind(ParameterMap, TEXT("InputInt"));
	HalfInputBufferParam.Bind(ParameterMap, TEXT("InputHalf"));
	FloatOutputBufferParam.Bind(ParameterMap, TEXT("OutputFloat"));
	IntOutputBufferParam.Bind(ParameterMap, TEXT("OutputInt"));
	HalfOutputBufferParam.Bind(ParameterMap, TEXT("OutputHalf"));

	InstanceCountsParam.Bind(ParameterMap, TEXT("InstanceCounts"));
	ReadInstanceCountOffsetParam.Bind(ParameterMap, TEXT("ReadInstanceCountOffset"));
	WriteInstanceCountOffsetParam.Bind(ParameterMap, TEXT("WriteInstanceCountOffset"));

	FreeIDBufferParam.Bind(ParameterMap, TEXT("FreeIDList"));
	IDToIndexBufferParam.Bind(ParameterMap, TEXT("IDToIndexTable"));
	
	SimStartParam.Bind(ParameterMap, TEXT("SimStart"));
	EmitterTickCounterParam.Bind(ParameterMap, TEXT("EmitterTickCounter"));
	EmitterSpawnInfoOffsetsParam.Bind(ParameterMap, TEXT("EmitterSpawnInfoOffsets"));
	EmitterSpawnInfoParamsParam.Bind(ParameterMap, TEXT("EmitterSpawnInfoParams"));

	NumEventsPerParticleParam.Bind(ParameterMap, TEXT("NumEventsPerParticle"));
	NumParticlesPerEventParam.Bind(ParameterMap, TEXT("NumParticlesPerEvent"));
	CopyInstancesBeforeStartParam.Bind(ParameterMap, TEXT("CopyInstancesBeforeStart"));

	NumSpawnedInstancesParam.Bind(ParameterMap, TEXT("SpawnedInstances"));
	UpdateStartInstanceParam.Bind(ParameterMap, TEXT("UpdateStartInstance"));
	DefaultSimulationStageIndexParam.Bind(ParameterMap, TEXT("DefaultSimulationStageIndex"));
	SimulationStageIndexParam.Bind(ParameterMap, TEXT("SimulationStageIndex"));

	SimulationStageIterationInfoParam.Bind(ParameterMap, TEXT("SimulationStageIterationInfo"));
	SimulationStageNormalizedIterationIndexParam.Bind(ParameterMap, TEXT("SimulationStageNormalizedIterationIndex"));
	DispatchThreadIdToLinearParam.Bind(ParameterMap, TEXT("DispatchThreadIdToLinear"));

	ComponentBufferSizeReadParam.Bind(ParameterMap, TEXT("ComponentBufferSizeRead"));
	ComponentBufferSizeWriteParam.Bind(ParameterMap, TEXT("ComponentBufferSizeWrite"));
	ViewUniformBufferParam.Bind(ParameterMap, TEXT("View"));

	GlobalConstantBufferParam[0].Bind(ParameterMap, TEXT("FNiagaraGlobalParameters"));
	SystemConstantBufferParam[0].Bind(ParameterMap, TEXT("FNiagaraSystemParameters"));
	OwnerConstantBufferParam[0].Bind(ParameterMap, TEXT("FNiagaraOwnerParameters"));
	EmitterConstantBufferParam[0].Bind(ParameterMap, TEXT("FNiagaraEmitterParameters"));
	ExternalConstantBufferParam[0].Bind(ParameterMap, TEXT("FNiagaraExternalParameters"));

	GlobalConstantBufferParam[1].Bind(ParameterMap, TEXT("PREV_FNiagaraGlobalParameters"));
	SystemConstantBufferParam[1].Bind(ParameterMap, TEXT("PREV_FNiagaraSystemParameters"));
	OwnerConstantBufferParam[1].Bind(ParameterMap, TEXT("PREV_FNiagaraOwnerParameters"));
	EmitterConstantBufferParam[1].Bind(ParameterMap, TEXT("PREV_FNiagaraEmitterParameters"));
	ExternalConstantBufferParam[1].Bind(ParameterMap, TEXT("PREV_FNiagaraExternalParameters"));

	// params for event buffers
	// this is horrendous; need to do this in a uniform buffer instead.
	//
	for (uint32 i = 0; i < MAX_CONCURRENT_EVENT_DATASETS; i++)
	{
		FString VarName = TEXT("WriteDataSetFloat") + FString::FromInt(i + 1);
		EventFloatUAVParams[i].Bind(ParameterMap, *VarName);
		VarName = TEXT("WriteDataSetInt") + FString::FromInt(i + 1);
		EventIntUAVParams[i].Bind(ParameterMap, *VarName);

		VarName = TEXT("ReadDataSetFloat") + FString::FromInt(i + 1);
		EventFloatSRVParams[i].Bind(ParameterMap, *VarName);
		VarName = TEXT("ReadDataSetInt") + FString::FromInt(i + 1);
		EventIntSRVParams[i].Bind(ParameterMap, *VarName);

		VarName = TEXT("DSComponentBufferSizeReadFloat") + FString::FromInt(i + 1);
		EventReadFloatStrideParams[i].Bind(ParameterMap, *VarName);
		VarName = TEXT("DSComponentBufferSizeWriteFloat") + FString::FromInt(i + 1);
		EventWriteFloatStrideParams[i].Bind(ParameterMap, *VarName);
		VarName = TEXT("DSComponentBufferSizeReadInt") + FString::FromInt(i + 1);
		EventReadIntStrideParams[i].Bind(ParameterMap, *VarName);
		VarName = TEXT("DSComponentBufferSizeWriteInt") + FString::FromInt(i + 1);
		EventWriteIntStrideParams[i].Bind(ParameterMap, *VarName);
	}

	DataInterfaceParameters.Empty(InDIParamInfo.Num());
	for(const FNiagaraDataInterfaceGPUParamInfo& DIParamInfo : InDIParamInfo)
	{
		FNiagaraDataInterfaceParamRef& ParamRef = DataInterfaceParameters.AddDefaulted_GetRef();
		ParamRef.Bind(DIParamInfo, ParameterMap);
	}
}

//////////////////////////////////////////////////////////////////////////
FNiagaraDataInterfaceParamRef::FNiagaraDataInterfaceParamRef()
{

}

FNiagaraDataInterfaceParamRef::~FNiagaraDataInterfaceParamRef()
{
	Parameters.SafeDelete();
}

void FNiagaraDataInterfaceParamRef::Bind(const FNiagaraDataInterfaceGPUParamInfo& InParameterInfo, const class FShaderParameterMap& ParameterMap)
{
	INiagaraShaderModule* Module = INiagaraShaderModule::Get();
	UNiagaraDataInterfaceBase* Base = Module->RequestDefaultDataInterface(*InParameterInfo.DIClassName);
	ensureMsgf(Base, TEXT("Failed to load class for FNiagaraDataInterfaceParamRef. %s"), *InParameterInfo.DIClassName);
	if (Base)
	{
		DIType = TIndexedPtr<UNiagaraDataInterfaceBase>(Base); // TODO - clean up TIndexedPtr::operator=()
		Parameters = Base->CreateComputeParameters();
		if (Parameters)
		{
			checkf(Base->GetComputeParametersTypeDesc(), TEXT("Missing TypeDesc for compute parameters for class %s"), *InParameterInfo.DIClassName);
			Parameters->DIType = DIType;
			Base->BindParameters(Parameters, InParameterInfo, ParameterMap);
		}
	}
}

bool FNiagaraDataInterfaceGeneratedFunction::Serialize(FArchive& Ar)
{
	Ar << DefinitionName;
	Ar << InstanceName;
	Ar << Specifiers;
	return true;
}

bool operator<<(FArchive& Ar, FNiagaraDataInterfaceGeneratedFunction& DIFunction)
{
	return DIFunction.Serialize(Ar);
}

void FNiagaraDataInterfaceParamRef::WriteFrozenParameters(FMemoryImageWriter& Writer, const TMemoryImagePtr<FNiagaraDataInterfaceParametersCS>& InParameters) const
{
	const UNiagaraDataInterfaceBase* Base = DIType.Get(Writer.TryGetPrevPointerTable());
	InParameters.WriteMemoryImageWithDerivedType(Writer, Base->GetComputeParametersTypeDesc());
}

bool FNiagaraDataInterfaceGPUParamInfo::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
	const int32 NiagaraVer = Ar.CustomVer(FNiagaraCustomVersion::GUID);

	Ar << DataInterfaceHLSLSymbol;
	Ar << DIClassName;

	// If FNiagaraDataInterfaceGPUParamInfo was only included in the DI parameters of FNiagaraShader, we wouldn't need to worry about
	// custom versions, because bumping FNiagaraCustomVersion::LatestScriptCompileVersion is enough to cause all shaders to be
	// rebuilt and things to be serialized correctly. However, there's a property of type FNiagaraVMExecutableData inside UNiagaraScript,
	// which in turn contains an array of FNiagaraDataInterfaceGPUParamInfo, so we must check the version in order to be able to load
	// UNiagaraScript objects saved before GeneratedFunctions was introduced.
	const bool SkipGeneratedFunctions = Ar.IsLoading() && (NiagaraVer < FNiagaraCustomVersion::AddGeneratedFunctionsToGPUParamInfo);
	if (!SkipGeneratedFunctions)
	{
		Ar << GeneratedFunctions;
	}

	return true;
}
