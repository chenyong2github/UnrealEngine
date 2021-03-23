// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	NiagaraShared.cpp: Shared Niagara compute shader implementation.
=============================================================================*/

#include "NiagaraShared.h"
#include "NiagaraShaderModule.h"
#include "NiagaraShaderType.h"
#include "NiagaraShader.h"
#include "NiagaraScriptBase.h"
#include "NiagaraScript.h"		//-TODO: This should be fixed so we are not reading structures from modules we do not depend on
#include "Stats/StatsMisc.h"
#include "UObject/CoreObjectVersion.h"
#include "Misc/App.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "ShaderCompiler.h"
#include "NiagaraShaderCompilationManager.h"
#include "RendererInterface.h"
#include "Modules/ModuleManager.h"
#include "NiagaraCustomVersion.h"
#if WITH_EDITOR
#include "Interfaces/ITargetPlatform.h"
#endif

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParamRef);
IMPLEMENT_TYPE_LAYOUT(FNiagaraShaderMapContent);
IMPLEMENT_TYPE_LAYOUT(FNiagaraShaderMapId);
IMPLEMENT_TYPE_LAYOUT(FNiagaraComputeShaderCompilationOutput);

#if WITH_EDITOR
	FNiagaraCompilationQueue* FNiagaraCompilationQueue::Singleton = nullptr;
#endif

FNiagaraShaderScript::~FNiagaraShaderScript()
{
#if WITH_EDITOR
	check(IsInGameThread());
	CancelCompilation();
#endif
}

/** Populates OutEnvironment with defines needed to compile shaders for this script. */
void FNiagaraShaderScript::SetupShaderCompilationEnvironment(
	EShaderPlatform Platform,
	FShaderCompilerEnvironment& OutEnvironment
	) const
{
	OutEnvironment.SetDefine(TEXT("GPU_SIMULATION_SHADER"), TEXT("1"));
}


bool FNiagaraShaderScript::ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType) const
{
	check(ShaderType->GetNiagaraShaderType() != nullptr);
	return true;
}

void FNiagaraShaderScript::ModifyCompilationEnvironment(struct FShaderCompilerEnvironment& OutEnvironment) const
{
	if ( BaseVMScript )
	{
		BaseVMScript->ModifyCompilationEnvironment(OutEnvironment);
	}
}

bool FNiagaraShaderScript::GetUsesSimulationStages() const
{
	return AdditionalDefines.Contains(TEXT("Emitter.UseSimulationStages"));
}

bool FNiagaraShaderScript::GetUsesOldShaderStages() const
{
	return AdditionalDefines.Contains(TEXT("Emitter.UseOldShaderStages"));
}

bool FNiagaraShaderScript::GetUsesCompressedAttributes() const
{
	return AdditionalDefines.Contains(TEXT("CompressAttributes"));
}

void FNiagaraShaderScript::NotifyCompilationFinished()
{
	UpdateCachedData_PostCompile();

	OnCompilationCompleteDelegate.Broadcast();
}

void FNiagaraShaderScript::CancelCompilation()
{
#if WITH_EDITOR
	check(IsInGameThread());
	bool bWasPending = FNiagaraShaderMap::RemovePendingScript(this);
	FNiagaraCompilationQueue::Get()->RemovePending(this);

	// don't spam the log if no cancelling actually happened : 
	if (bWasPending)
	{
		UE_LOG(LogShaders, Log, TEXT("CancelCompilation %p."), this);
	}
	OutstandingCompileShaderMapIds.Empty();
#endif
}

void FNiagaraShaderScript::RemoveOutstandingCompileId(const int32 OldOutstandingCompileShaderMapId)
{
	check(IsInGameThread());
	if (0 <= OutstandingCompileShaderMapIds.Remove(OldOutstandingCompileShaderMapId))
	{
		UE_LOG(LogShaders, Log, TEXT("RemoveOutstandingCompileId %p %d"), this, OldOutstandingCompileShaderMapId);
	}
}

void FNiagaraShaderScript::Invalidate()
{
	CancelCompilation();
	ReleaseShaderMap();
#if WITH_EDITOR
	CompileErrors.Empty();
	HlslOutput.Empty();
#endif
}

void FNiagaraShaderScript::LegacySerialize(FArchive& Ar)
{
}

bool FNiagaraShaderScript::IsSame(const FNiagaraShaderMapId& InId) const
{
	if (InId.ReferencedCompileHashes.Num() != ReferencedCompileHashes.Num() ||
		InId.AdditionalDefines.Num() != AdditionalDefines.Num() ||
		InId.AdditionalVariables.Num() != AdditionalVariables.Num())
	{
		return false;
	}

	for (int32 i = 0; i < ReferencedCompileHashes.Num(); ++i)
	{
		if (ReferencedCompileHashes[i] != InId.ReferencedCompileHashes[i])
		{
			return false;
		}
	}
	for (int32 i = 0; i < AdditionalDefines.Num(); ++i)
	{
		if (AdditionalDefines[i] != *InId.AdditionalDefines[i])
		{
			return false;
		}
	}
	for (int32 i = 0; i < AdditionalVariables.Num(); ++i)
	{
		if (AdditionalVariables[i] != *InId.AdditionalVariables[i])
		{
			return false;
		}
	}

	return
		InId.FeatureLevel == FeatureLevel &&/*
		InId.BaseScriptID == BaseScriptId &&*/
		InId.bUsesRapidIterationParams == bUsesRapidIterationParams &&
		InId.BaseCompileHash == BaseCompileHash &&
		InId.CompilerVersionID == CompilerVersionId;
}

int32 FNiagaraShaderScript::PermutationIdToShaderStageIndex(int32 PermutationId) const
{
	return ShaderStageToPermutation[PermutationId].Key;
}

bool FNiagaraShaderScript::IsShaderMapComplete() const
{
	if (GameThreadShaderMap == nullptr)
	{
		return false;
	}

	if (FNiagaraShaderMap::GetShaderMapBeingCompiled(this) != nullptr)
	{
		return false;
	}

	if (!GameThreadShaderMap->IsValid())
	{
		return false;
	}

	for (int i=0; i < GetNumPermutations(); ++i)
	{
		if (GameThreadShaderMap->GetShader<FNiagaraShader>(i).IsNull())
		{
			return false;
		}
	}
	return true;
}

int32 FNiagaraShaderScript::ShaderStageIndexToPermutationId_RenderThread(int32 ShaderStageIndex) const
{
	check(IsInRenderingThread());
	if (CachedData_RenderThread.NumPermutations > 1)
	{
		for (int32 i = 0; i < CachedData_RenderThread.ShaderStageToPermutation.Num(); ++i)
		{
			const TPair<int32, int32> MinMaxStage = CachedData_RenderThread.ShaderStageToPermutation[i];
			if ((ShaderStageIndex >= MinMaxStage.Key) && (ShaderStageIndex < MinMaxStage.Value))
			{
				return i;
			}
		}
		UE_LOG(LogShaders, Fatal, TEXT("FNiagaraShaderScript::ShaderStageIndexToPermutationId_RenderThread: Failed to map from simulation stage(%d) to permutation id."), ShaderStageIndex);
	}

	return 0;
}

void FNiagaraShaderScript::GetDependentShaderTypes(EShaderPlatform Platform, TArray<FShaderType*>& OutShaderTypes) const
{
	for (TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList()); ShaderTypeIt; ShaderTypeIt.Next())
	{
		FNiagaraShaderType* ShaderType = ShaderTypeIt->GetNiagaraShaderType();

		if ( ShaderType && ShaderType->ShouldCache(Platform, this) && ShouldCache(Platform, ShaderType) )
		{
			OutShaderTypes.Add(ShaderType);
		}
	}
}



void FNiagaraShaderScript::GetShaderMapId(EShaderPlatform Platform, const ITargetPlatform* TargetPlatform, FNiagaraShaderMapId& OutId) const
{
	if (bLoadedCookedShaderMapId)
	{
		OutId = CookedShaderMapId;
	}
	else
	{
		INiagaraShaderModule* Module = INiagaraShaderModule::Get();

		OutId.FeatureLevel = GetFeatureLevel();/*
		OutId.BaseScriptID = BaseScriptId;*/
		OutId.bUsesRapidIterationParams = bUsesRapidIterationParams;		
		BaseCompileHash.ToSHAHash(OutId.BaseCompileHash);
		OutId.CompilerVersionID = FNiagaraCustomVersion::LatestScriptCompileVersion;

		OutId.ReferencedCompileHashes.Reserve(ReferencedCompileHashes.Num());
		for (const FNiagaraCompileHash& Hash : ReferencedCompileHashes)
		{
			Hash.ToSHAHash(OutId.ReferencedCompileHashes.AddDefaulted_GetRef());
		}

		OutId.AdditionalDefines.Empty(AdditionalDefines.Num());
		for(const FString& Define : AdditionalDefines)
		{
			OutId.AdditionalDefines.Emplace(Define);
		}

		OutId.AdditionalVariables.Empty(AdditionalVariables.Num());
		for(const FString& Variable : AdditionalVariables)
		{
			OutId.AdditionalVariables.Emplace(Variable);
		}

		TArray<FShaderType*> DependentShaderTypes;
		GetDependentShaderTypes(Platform, DependentShaderTypes);
		for (FShaderType* ShaderType : DependentShaderTypes)
		{
			OutId.ShaderTypeDependencies.Emplace(ShaderType, Platform);
		}

		if (TargetPlatform)
		{
#if WITH_EDITOR
			OutId.LayoutParams.InitializeForPlatform(TargetPlatform);
#else
			UE_LOG(LogShaders, Error, TEXT("FNiagaraShaderScript::GetShaderMapId: TargetPlatform is not null, but a cooked executable cannot target platforms other than its own."));
#endif
		}
		else
		{
			OutId.LayoutParams.InitializeForCurrent();
		}
	}
}



void FNiagaraShaderScript::AddReferencedObjects(FReferenceCollector& Collector)
{
}

void  FNiagaraShaderScript::DiscardShaderMap()
{
	if (GameThreadShaderMap)
	{
		//GameThreadShaderMap->DiscardSerializedShaders();
	}
}

void FNiagaraShaderScript::ReleaseShaderMap()
{
	if (GameThreadShaderMap)
	{
		GameThreadShaderMap = nullptr;

		if (!bQueuedForRelease)
		{
			FNiagaraShaderScript* Script = this;
			ENQUEUE_RENDER_COMMAND(ReleaseShaderMap)(
				[Script](FRHICommandListImmediate& RHICmdList)
				{
					Script->SetRenderingThreadShaderMap(nullptr);
				});
		}

		UpdateCachedData_All();
	}
}

void FNiagaraShaderScript::SerializeShaderMap(FArchive& Ar)
{
	bool bCooked = Ar.IsCooking();
	Ar << bCooked;
	Ar << NumPermutations;
	Ar << ShaderStageToPermutation;

	if (Ar.IsLoading())
	{
		bLoadedFromCookedMaterial = bCooked;
	}

	if (FPlatformProperties::RequiresCookedData() && !bCooked && Ar.IsLoading())
	{
		UE_LOG(LogShaders, Fatal, TEXT("This platform requires cooked packages, and shaders were not cooked into this Niagara script %s."), *GetFriendlyName());
	}

	if (bCooked)
	{
		if (Ar.IsCooking())
		{
#if WITH_EDITOR
			FinishCompilation();

			bool bValid = GameThreadShaderMap != nullptr && GameThreadShaderMap->CompiledSuccessfully();
			Ar << bValid;

			if (bValid)
			{
				// associate right here
				if (BaseVMScript)
				{
					GameThreadShaderMap->AssociateWithAsset(BaseVMScript->GetOutermost()->GetFName());
				}
				GameThreadShaderMap->Serialize(Ar);
			}
			//else if (GameThreadShaderMap != nullptr && !GameThreadShaderMap->CompiledSuccessfully())
			//{
			//	FString Name;
			//	UE_LOG(LogShaders, Error, TEXT("Failed to compile Niagara shader %s."), *GetFriendlyName());
			//}
#endif
		}
		else
		{
			bool bValid = false;
			Ar << bValid;

			if (bValid)
			{
				TRefCountPtr<FNiagaraShaderMap> LoadedShaderMap = new FNiagaraShaderMap();
				bool bLoaded = LoadedShaderMap->Serialize(Ar, true, true);

				// Toss the loaded shader data if this is a server only instance
				//@todo - don't cook it in the first place
				if (FApp::CanEverRender() && bLoaded)
				{
					GameThreadShaderMap = RenderingThreadShaderMap = LoadedShaderMap;

					UpdateCachedData_PostCompile(true);
				}
				else
				{
					//LoadedShaderMap->DiscardSerializedShaders();
				}
			}
		}
	}
}

void FNiagaraShaderScript::SetScript(UNiagaraScriptBase* InScript, ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform, const FGuid& InCompilerVersionID,  const TArray<FString>& InAdditionalDefines, const TArray<FString>& InAdditionalVariables,
		const FNiagaraCompileHash& InBaseCompileHash, const TArray<FNiagaraCompileHash>& InReferencedCompileHashes, 
		bool bInUsesRapidIterationParams, FString InFriendlyName)
{
	checkf(InBaseCompileHash.IsValid(), TEXT("Invalid base compile hash.  Script caching will fail."))
	BaseVMScript = InScript;
	CompilerVersionId = InCompilerVersionID;
	//BaseScriptId = InBaseScriptID;
	AdditionalDefines = InAdditionalDefines;
	AdditionalVariables = InAdditionalVariables;
	bUsesRapidIterationParams = bInUsesRapidIterationParams;
	BaseCompileHash = InBaseCompileHash;
	ReferencedCompileHashes = InReferencedCompileHashes;
	FriendlyName = InFriendlyName;
	SetFeatureLevel(InFeatureLevel);
	ShaderPlatform = InShaderPlatform;

	UpdateCachedData_All();
}

#if WITH_EDITOR
bool FNiagaraShaderScript::MatchesScript(ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform, const FNiagaraVMExecutableDataId& ScriptId) const
{
	return CompilerVersionId == ScriptId.CompilerVersionID
		&& AdditionalDefines == ScriptId.AdditionalDefines
		&& bUsesRapidIterationParams == ScriptId.bUsesRapidIterationParams
		&& BaseCompileHash == ScriptId.BaseScriptCompileHash
		&& ReferencedCompileHashes == ScriptId.ReferencedCompileHashes
		&& FeatureLevel == InFeatureLevel
		&& ShaderPlatform == InShaderPlatform;
}
#endif

void FNiagaraShaderScript::SetRenderingThreadShaderMap(FNiagaraShaderMap* InShaderMap)
{
	check(IsInRenderingThread());
	RenderingThreadShaderMap = InShaderMap;
}

bool FNiagaraShaderScript::IsCompilationFinished() const
{
	check(IsInGameThread());
	bool bRet = GameThreadShaderMap && GameThreadShaderMap.IsValid() && GameThreadShaderMap->IsCompilationFinalized();
	if (OutstandingCompileShaderMapIds.Num() == 0)
	{
		return true;
	}
	return bRet;
}

void FNiagaraShaderScript::SetRenderThreadCachedData(const FNiagaraShaderMapCachedData& CachedData)
{
	CachedData_RenderThread = CachedData;
}

bool FNiagaraShaderScript::QueueForRelease(FThreadSafeBool& Fence)
{
	check(!bQueuedForRelease);

	if (BaseVMScript)
	{
		bQueuedForRelease = true;
		Fence = false;
		FThreadSafeBool* Released = &Fence;

		ENQUEUE_RENDER_COMMAND(BeginDestroyCommand)(
			[Released](FRHICommandListImmediate& RHICmdList)
			{
				*Released = true;
			});
	}

	return bQueuedForRelease;
}

void FNiagaraShaderScript::UpdateCachedData_All()
{
	UpdateCachedData_PreCompile();
	UpdateCachedData_PostCompile();
}

void FNiagaraShaderScript::UpdateCachedData_PreCompile()
{
	if (BaseVMScript)
	{
		NumPermutations = 1;
		ShaderStageToPermutation.Empty();

		TConstArrayView<FSimulationStageMetaData> SimulationStages = BaseVMScript->GetSimulationStageMetaData();

		// We add the number of simulation stages as Stage 0 is always the particle stage currently
		NumPermutations += SimulationStages.Num();

		ShaderStageToPermutation.Emplace(0, 1);
		for (const FSimulationStageMetaData& StageMeta : SimulationStages)
		{
			ShaderStageToPermutation.Emplace(StageMeta.MinStage, StageMeta.MaxStage);
		}
	}
	else
	{
		NumPermutations = 0;
		ShaderStageToPermutation.Empty();
	}
}

void FNiagaraShaderScript::UpdateCachedData_PostCompile(bool bCalledFromSerialize)
{
	check(IsInGameThread() || bCalledFromSerialize);

	FNiagaraShaderMapCachedData CachedData;
	CachedData.NumPermutations = GetNumPermutations();
	CachedData.bIsComplete = 1;
	CachedData.bGlobalConstantBufferUsed = 0;
	CachedData.bSystemConstantBufferUsed = 0;
	CachedData.bOwnerConstantBufferUsed = 0;
	CachedData.bEmitterConstantBufferUsed = 0;
	CachedData.bExternalConstantBufferUsed = 0;
	CachedData.bViewUniformBufferUsed = 0;

	if (GameThreadShaderMap != nullptr && GameThreadShaderMap->IsValid())
	{
		for (int32 iPermutation = 0; iPermutation < CachedData.NumPermutations; ++iPermutation)
		{
			TNiagaraShaderRef<FShader> Shader = GameThreadShaderMap->GetShader(&FNiagaraShader::StaticType, iPermutation);
			if (!Shader.IsValid())
			{
				CachedData.bIsComplete = 0;
				break;
			}
			FNiagaraShader* NiagaraShader = static_cast<FNiagaraShader*>(Shader.GetShader());

			for (int i = 0; i < 2; ++i)
			{
				const uint32 BitToSet = 1 << i;
				CachedData.bGlobalConstantBufferUsed |= NiagaraShader->GlobalConstantBufferParam[i].IsBound() ? BitToSet : 0;
				CachedData.bSystemConstantBufferUsed |= NiagaraShader->SystemConstantBufferParam[i].IsBound() ? BitToSet : 0;
				CachedData.bOwnerConstantBufferUsed |= NiagaraShader->OwnerConstantBufferParam[i].IsBound() ? BitToSet : 0;
				CachedData.bEmitterConstantBufferUsed |= NiagaraShader->EmitterConstantBufferParam[i].IsBound() ? BitToSet : 0;
				CachedData.bExternalConstantBufferUsed |= NiagaraShader->ExternalConstantBufferParam[i].IsBound() ? BitToSet : 0;
			}
			CachedData.bViewUniformBufferUsed |= NiagaraShader->ViewUniformBufferParam.IsBound() ? 1 : 0;
		}
	}
	else
	{
		CachedData.bIsComplete = 0;
	}

	CachedData.ShaderStageToPermutation = ShaderStageToPermutation;

	if (bCalledFromSerialize)
	{
		CachedData_RenderThread = MoveTemp(CachedData);
	}
	else if (!bQueuedForRelease)
	{
		ENQUEUE_RENDER_COMMAND(UpdateCachedData)(
				[Script_RT = this, CachedData_RT = CachedData](FRHICommandListImmediate& RHICmdList)
				{
					Script_RT->SetRenderThreadCachedData(CachedData_RT);
				});
	}
}

/**
* Cache the script's shaders
*/
#if WITH_EDITOR

bool FNiagaraShaderScript::CacheShaders(bool bApplyCompletedShaderMapForRendering, bool bForceRecompile, bool bSynchronous, const ITargetPlatform* TargetPlatform)
{
	FNiagaraShaderMapId NoStaticParametersId;
	GetShaderMapId(ShaderPlatform, TargetPlatform, NoStaticParametersId);
	return CacheShaders(NoStaticParametersId, bApplyCompletedShaderMapForRendering, bForceRecompile, bSynchronous);
}

/**
* Caches the shaders for this script
*/
bool FNiagaraShaderScript::CacheShaders(const FNiagaraShaderMapId& ShaderMapId, bool bApplyCompletedShaderMapForRendering, bool bForceRecompile, bool bSynchronous)
{
	bool bSucceeded = false;

	check(IsInGameThread());

	{
		GameThreadShaderMap = nullptr;
		{
			extern FCriticalSection GIdToNiagaraShaderMapCS;
			FScopeLock ScopeLock(&GIdToNiagaraShaderMapCS);
			// Find the script's cached shader map.
			GameThreadShaderMap = FNiagaraShaderMap::FindId(ShaderMapId, ShaderPlatform);
		}

		// Attempt to load from the derived data cache if we are uncooked
		if (!bForceRecompile && !GameThreadShaderMap && !FPlatformProperties::RequiresCookedData())
		{
			FNiagaraShaderMap::LoadFromDerivedDataCache(this, ShaderMapId, ShaderPlatform, GameThreadShaderMap);
			if (GameThreadShaderMap && GameThreadShaderMap->IsValid())
			{
				UE_LOG(LogTemp, Verbose, TEXT("Loaded shader %s for Niagara script %s from DDC"), *GameThreadShaderMap->GetFriendlyName(), *GetFriendlyName());
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("Loading shader for Niagara script %s from DDC failed. Shader needs recompile."), *GetFriendlyName());
			}
		}
	}

	bool bAssumeShaderMapIsComplete = false;
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	bAssumeShaderMapIsComplete = FPlatformProperties::RequiresCookedData();
#endif

	UpdateCachedData_PreCompile();

	if (GameThreadShaderMap && GameThreadShaderMap->TryToAddToExistingCompilationTask(this))
	{
		//FNiagaraShaderMap::ShaderMapsBeingCompiled.Find(GameThreadShaderMap);
#if DEBUG_INFINITESHADERCOMPILE
		UE_LOG(LogTemp, Display, TEXT("Found existing compiling shader for Niagara script %s, linking to other GameThreadShaderMap 0x%08X%08X"), *GetFriendlyName(), (int)((int64)(GameThreadShaderMap.GetReference()) >> 32), (int)((int64)(GameThreadShaderMap.GetReference())));
#endif
		OutstandingCompileShaderMapIds.AddUnique(GameThreadShaderMap->GetCompilingId());
		UE_LOG(LogShaders, Log, TEXT("CacheShaders AddUniqueExisting %p %d"), this, GameThreadShaderMap->GetCompilingId());

		// Reset the shader map so we fall back to CPU sim until the compile finishes.
		GameThreadShaderMap = nullptr;
		bSucceeded = true;
	}
	else if (bForceRecompile || !GameThreadShaderMap || !(bAssumeShaderMapIsComplete || GameThreadShaderMap->IsComplete(this, false)))
	{
		if (FPlatformProperties::RequiresCookedData())
		{
			UE_LOG(LogShaders, Log, TEXT("Can't compile %s with cooked content!"), *GetFriendlyName());
			// Reset the shader map so we fall back to CPU sim
			GameThreadShaderMap = nullptr;
		}
		else
		{
			UE_LOG(LogShaders, Log, TEXT("%s cached shader map for script %s, compiling."), GameThreadShaderMap? TEXT("Incomplete") : TEXT("Missing"), *GetFriendlyName());

			// If there's no cached shader map for this script compile a new one.
			// This is just kicking off the compile, GameThreadShaderMap will not be complete yet
			bSucceeded = BeginCompileShaderMap(ShaderMapId, GameThreadShaderMap, bApplyCompletedShaderMapForRendering, bSynchronous);

			if (!bSucceeded)
			{
				GameThreadShaderMap = nullptr;
			}
		}
	}
	else
	{
		bSucceeded = true;
	}

	UpdateCachedData_PostCompile();

	if (!bQueuedForRelease)
	{
		FNiagaraShaderScript* Script = this;
		FNiagaraShaderMap* LoadedShaderMap = GameThreadShaderMap;
		ENQUEUE_RENDER_COMMAND(FSetShaderMapOnScriptResources)(
			[Script, LoadedShaderMap](FRHICommandListImmediate& RHICmdList)
			{
				Script->SetRenderingThreadShaderMap(LoadedShaderMap);
			});
	}

	return bSucceeded;
}

void FNiagaraShaderScript::FinishCompilation()
{
	TArray<int32> ShaderMapIdsToFinish;
	GetShaderMapIDsWithUnfinishedCompilation(ShaderMapIdsToFinish);

	if (ShaderMapIdsToFinish.Num() > 0)
	{
		for (int32 i = 0; i < ShaderMapIdsToFinish.Num(); i++)
		{
			UE_LOG(LogShaders, Log, TEXT("FinishCompilation()[%d] %s id %d!"), i, *GetFriendlyName(), ShaderMapIdsToFinish[i]);
		}
		// Block until the shader maps that we will save have finished being compiled
		// NIAGARATODO: implement when async compile works
		GNiagaraShaderCompilationManager.FinishCompilation(*GetFriendlyName(), ShaderMapIdsToFinish);

		// Shouldn't have anything left to do...
		TArray<int32> ShaderMapIdsToFinish2;
		GetShaderMapIDsWithUnfinishedCompilation(ShaderMapIdsToFinish2);
		if (ShaderMapIdsToFinish2.Num() != 0)
		{
			UE_LOG(LogShaders, Warning, TEXT("Skipped multiple Niagara shader maps for compilation! May be indicative of no support for a given platform. Count: %d"), ShaderMapIdsToFinish2.Num());
		}
	}
}

#endif

void FNiagaraShaderScript::SetDataInterfaceParamInfo(const TArray< FNiagaraDataInterfaceGPUParamInfo >& InDIParamInfo)
{
	DIParamInfo = InDIParamInfo;
}

FNiagaraShaderRef FNiagaraShaderScript::GetShader(int32 PermutationId) const
{
	check(!GIsThreadedRendering || !IsInGameThread());
	if (!GIsEditor || RenderingThreadShaderMap /*&& RenderingThreadShaderMap->IsComplete(this, true)*/)
	{
		return RenderingThreadShaderMap->GetShader<FNiagaraShader>(PermutationId);
	}
	return FNiagaraShaderRef();
};

FNiagaraShaderRef FNiagaraShaderScript::GetShaderGameThread(int32 PermutationId) const
{
	if (GameThreadShaderMap && GameThreadShaderMap->IsValid())
	{
		return GameThreadShaderMap->GetShader<FNiagaraShader>(PermutationId);
	}

	return FNiagaraShaderRef();
};


void FNiagaraShaderScript::GetShaderMapIDsWithUnfinishedCompilation(TArray<int32>& ShaderMapIds)
{
	// Build an array of the shader map Id's are not finished compiling.
	if (GameThreadShaderMap && GameThreadShaderMap.IsValid() && !GameThreadShaderMap->IsCompilationFinalized())
	{
		ShaderMapIds.Add(GameThreadShaderMap->GetCompilingId());
	}
	else if (OutstandingCompileShaderMapIds.Num() != 0)
	{
		ShaderMapIds.Append(OutstandingCompileShaderMapIds);
	}
}

#if WITH_EDITOR

/**
* Compiles this script for Platform, storing the result in OutShaderMap
*
* @param ShaderMapId - the set of static parameters to compile
* @param Platform - the platform to compile for
* @param OutShaderMap - the shader map to compile
* @return - true if compile succeeded or was not necessary (shader map for ShaderMapId was found and was complete)
*/
bool FNiagaraShaderScript::BeginCompileShaderMap(
	const FNiagaraShaderMapId& ShaderMapId,
	TRefCountPtr<FNiagaraShaderMap>& OutShaderMap,
	bool bApplyCompletedShaderMapForRendering,
	bool bSynchronous)
{
	check(IsInGameThread());
#if WITH_EDITORONLY_DATA
	bool bSuccess = false;

	STAT(double NiagaraCompileTime = 0);


	SCOPE_SECONDS_COUNTER(NiagaraCompileTime);

	// Queue hlsl generation and shader compilation - Unlike materials, we queue this here, and compilation happens from the editor module
	TRefCountPtr<FNiagaraShaderMap> NewShaderMap = new FNiagaraShaderMap();
	OutstandingCompileShaderMapIds.AddUnique(NewShaderMap->GetCompilingId());		
	UE_LOG(LogShaders, Log, TEXT("BeginCompileShaderMap AddUnique %p %d"), this, NewShaderMap->GetCompilingId());

	FNiagaraCompilationQueue::Get()->Queue(this, NewShaderMap, ShaderMapId, ShaderPlatform, bApplyCompletedShaderMapForRendering);
	if (bSynchronous)
	{
		INiagaraShaderModule NiagaraShaderModule = FModuleManager::GetModuleChecked<INiagaraShaderModule>(TEXT("NiagaraShader"));
		NiagaraShaderModule.ProcessShaderCompilationQueue();
		OutShaderMap = NewShaderMap;
	}
	else
	{
		// For async compile, set to nullptr so that we fall back to CPU side simulation until shader compile is finished
		OutShaderMap = nullptr;
	}

	INC_FLOAT_STAT_BY(STAT_ShaderCompiling_NiagaraShaders, (float)NiagaraCompileTime);

	return true;
#else
	UE_LOG(LogShaders, Fatal, TEXT("Compiling of shaders in a build without editordata is not supported."));
	return false;
#endif
}

#endif