// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	NiagaraShared.cpp: Shared Niagara compute shader implementation.
=============================================================================*/

#include "NiagaraShared.h"
#include "NiagaraShaderModule.h"
#include "NiagaraShaderType.h"
#include "NiagaraShader.h"
#include "NiagaraScript.h"
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

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParamRef);
IMPLEMENT_TYPE_LAYOUT(FNiagaraShaderMapContent);
IMPLEMENT_TYPE_LAYOUT(FNiagaraShaderMapId);
IMPLEMENT_TYPE_LAYOUT(FNiagaraComputeShaderCompilationOutput);

#if WITH_EDITOR
	NIAGARASHADER_API FNiagaraCompilationQueue* FNiagaraCompilationQueue::Singleton = nullptr;
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


NIAGARASHADER_API bool FNiagaraShaderScript::ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType) const
{
	check(ShaderType->GetNiagaraShaderType() != nullptr);
	return true;
}

NIAGARASHADER_API uint32 FNiagaraShaderScript::GetUseSimStagesDefine() const
{
	if (AdditionalDefines.Contains(TEXT("Emitter.UseSimulationStages")))
	{
		return 1;
	}
	else if (AdditionalDefines.Contains(TEXT("Emitter.UseOldShaderStages")))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}


NIAGARASHADER_API void FNiagaraShaderScript::NotifyCompilationFinished()
{
	OnCompilationCompleteDelegate.Broadcast();
}

NIAGARASHADER_API void FNiagaraShaderScript::CancelCompilation()
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

NIAGARASHADER_API void FNiagaraShaderScript::RemoveOutstandingCompileId(const int32 OldOutstandingCompileShaderMapId)
{
	check(IsInGameThread());
	if (0 <= OutstandingCompileShaderMapIds.Remove(OldOutstandingCompileShaderMapId))
	{
		UE_LOG(LogShaders, Log, TEXT("RemoveOutstandingCompileId %p %d"), this, OldOutstandingCompileShaderMapId);
	}
}

NIAGARASHADER_API void FNiagaraShaderScript::Invalidate()
{
	CancelCompilation();
	ReleaseShaderMap();
	CompileErrors.Empty();
	HlslOutput.Empty();
}


NIAGARASHADER_API void FNiagaraShaderScript::LegacySerialize(FArchive& Ar)
{
}

bool FNiagaraShaderScript::IsSame(const FNiagaraShaderMapId& InId) const
{
	if (InId.ReferencedCompileHashes.Num() != ReferencedCompileHashes.Num() ||
		InId.AdditionalDefines.Num() != AdditionalDefines.Num())
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

	return
		InId.FeatureLevel == FeatureLevel &&/*
		InId.BaseScriptID == BaseScriptId &&*/
		InId.bUsesRapidIterationParams == bUsesRapidIterationParams &&
		InId.BaseCompileHash == BaseCompileHash &&
		InId.CompilerVersionID == CompilerVersionId;
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



NIAGARASHADER_API void FNiagaraShaderScript::GetShaderMapId(EShaderPlatform Platform, FNiagaraShaderMapId& OutId) const
{
	if (bLoadedCookedShaderMapId)
	{
		OutId = CookedShaderMapId;
	}
	else
	{
		TArray<FShaderType*> ShaderTypes;
		GetDependentShaderTypes(Platform, ShaderTypes);
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

		FNiagaraShaderScript* Script = this;
		ENQUEUE_RENDER_COMMAND(ReleaseShaderMap)(
			[Script](FRHICommandListImmediate& RHICmdList)
			{
				Script->SetRenderingThreadShaderMap(nullptr);
			});
	}
}

void FNiagaraShaderScript::SerializeShaderMap(FArchive& Ar)
{
	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

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
					FString AssetFile = FPackageName::LongPackageNameToFilename(BaseVMScript->GetOutermost()->GetName(), TEXT(".uasset"));
					GameThreadShaderMap->MarkAsAssociatedWithAsset(AssetFile);
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
				}
				else
				{
					//LoadedShaderMap->DiscardSerializedShaders();
				}
			}
		}
	}
}

void FNiagaraShaderScript::SetScript(UNiagaraScript* InScript, ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform, const FGuid& InCompilerVersionID,  const TArray<FString>& InAdditionalDefines,
		const FNiagaraCompileHash& InBaseCompileHash, const TArray<FNiagaraCompileHash>& InReferencedCompileHashes, 
		bool bInUsesRapidIterationParams, FString InFriendlyName)
{
	checkf(InBaseCompileHash.IsValid(), TEXT("Invalid base compile hash.  Script caching will fail."))
	BaseVMScript = InScript;
	CompilerVersionId = InCompilerVersionID;
	//BaseScriptId = InBaseScriptID;
	AdditionalDefines = InAdditionalDefines;
	bUsesRapidIterationParams = bInUsesRapidIterationParams;
	BaseCompileHash = InBaseCompileHash;
	ReferencedCompileHashes = InReferencedCompileHashes;
	FriendlyName = InFriendlyName;
	SetFeatureLevel(InFeatureLevel);
	ShaderPlatform = InShaderPlatform;
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

NIAGARASHADER_API  void FNiagaraShaderScript::SetRenderingThreadShaderMap(FNiagaraShaderMap* InShaderMap)
{
	check(IsInRenderingThread());
	RenderingThreadShaderMap = InShaderMap;
}

NIAGARASHADER_API  bool FNiagaraShaderScript::IsCompilationFinished() const
{
	check(IsInGameThread());
	bool bRet = GameThreadShaderMap && GameThreadShaderMap.IsValid() && GameThreadShaderMap->IsCompilationFinalized();
	if (OutstandingCompileShaderMapIds.Num() == 0)
	{
		return true;
	}
	return bRet;
}

/**
* Cache the script's shaders
*/
#if WITH_EDITOR

bool FNiagaraShaderScript::CacheShaders(bool bApplyCompletedShaderMapForRendering, bool bForceRecompile, bool bSynchronous)
{
	FNiagaraShaderMapId NoStaticParametersId;
	GetShaderMapId(ShaderPlatform, NoStaticParametersId);
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
			NIAGARASHADER_API extern FCriticalSection GIdToNiagaraShaderMapCS;
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

	FNiagaraShaderScript* Script = this;
	FNiagaraShaderMap* LoadedShaderMap = GameThreadShaderMap;
	ENQUEUE_RENDER_COMMAND(FSetShaderMapOnScriptResources)(
		[Script, LoadedShaderMap](FRHICommandListImmediate& RHICmdList)
		{
			Script->SetRenderingThreadShaderMap(LoadedShaderMap);
		});

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

NIAGARASHADER_API  FNiagaraShaderRef FNiagaraShaderScript::GetShader() const
{
	check(!GIsThreadedRendering || !IsInGameThread());
	if (!GIsEditor || RenderingThreadShaderMap /*&& RenderingThreadShaderMap->IsComplete(this, true)*/)
	{
		return RenderingThreadShaderMap->GetShader<FNiagaraShader>();
	}
	return FNiagaraShaderRef();
};

NIAGARASHADER_API  FNiagaraShaderRef FNiagaraShaderScript::GetShaderGameThread() const
{
	if (GameThreadShaderMap)
	{
		return GameThreadShaderMap->GetShader<FNiagaraShader>();
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