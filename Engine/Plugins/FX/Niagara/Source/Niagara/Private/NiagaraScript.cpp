// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScript.h"
#include "Modules/ModuleManager.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "UObject/Package.h"
#include "UObject/Linker.h"
#include "NiagaraModule.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraShaderCompilationManager.h"
#include "Serialization/MemoryReader.h"

#include "Stats/Stats.h"
#include "UObject/Linker.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"
#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraDataInterfaceStaticMesh.h"
#if WITH_EDITOR
	#include "NiagaraScriptDerivedData.h"
	#include "DerivedDataCacheInterface.h"
	#include "Interfaces/ITargetPlatform.h"
#endif

#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"

#include "NiagaraFunctionLibrary.h"
#include "VectorVM.h"

#include "Async/Async.h"

DECLARE_STATS_GROUP(TEXT("Niagara Detailed"), STATGROUP_NiagaraDetailed, STATCAT_Advanced);

FNiagaraScriptDebuggerInfo::FNiagaraScriptDebuggerInfo() : bWaitForGPU(false), FrameLastWriteId(-1), bWritten(false)
{
}


FNiagaraScriptDebuggerInfo::FNiagaraScriptDebuggerInfo(FName InName, ENiagaraScriptUsage InUsage, const FGuid& InUsageId) : HandleName(InName), Usage(InUsage), UsageId(InUsageId), FrameLastWriteId(-1), bWritten(false)
{
	if (InUsage == ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		bWaitForGPU = true;
	}
	else
	{
		bWaitForGPU = false;
	}
}


UNiagaraScriptSourceBase::UNiagaraScriptSourceBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
}


FNiagaraVMExecutableData::FNiagaraVMExecutableData() 
	: NumTempRegisters(0)
	, NumUserPtrs(0)
#if WITH_EDITORONLY_DATA
	, LastOpCount(0)
#endif
	, LastCompileStatus(ENiagaraScriptCompileStatus::NCS_Unknown)
#if WITH_EDITORONLY_DATA
	, bReadsAttributeData(false)
	, CompileTime(0.0f)
#endif
{
}

bool FNiagaraVMExecutableData::IsValid() const
{
	return LastCompileStatus != ENiagaraScriptCompileStatus::NCS_Unknown;
}

void FNiagaraVMExecutableData::Reset() 
{
	*this = FNiagaraVMExecutableData();
}

void FNiagaraVMExecutableData::SerializeData(FArchive& Ar, bool bDDCData)
{
	UScriptStruct* FNiagaraVMExecutableDataType = FNiagaraVMExecutableData::StaticStruct();
	FNiagaraVMExecutableDataType->SerializeTaggedProperties(Ar, (uint8*)this, FNiagaraVMExecutableDataType, nullptr);
}

bool FNiagaraVMExecutableDataId::IsValid() const
{
	return BaseScriptID.IsValid();
}

void FNiagaraVMExecutableDataId::Invalidate()
{
	*this = FNiagaraVMExecutableDataId();
}

bool FNiagaraVMExecutableDataId::HasInterpolatedParameters() const
{
	return bInterpolatedSpawn;
}

bool FNiagaraVMExecutableDataId::RequiresPersistentIDs() const
{
	return bRequiresPersistentIDs;
}

/**
* Tests this set against another for equality, disregarding override settings.
*
* @param ReferenceSet	The set to compare against
* @return				true if the sets are equal
*/
bool FNiagaraVMExecutableDataId::operator==(const FNiagaraVMExecutableDataId& ReferenceSet) const
{
	if (CompilerVersionID != ReferenceSet.CompilerVersionID ||
		ScriptUsageType != ReferenceSet.ScriptUsageType || 
		ScriptUsageTypeID != ReferenceSet.ScriptUsageTypeID ||
		BaseScriptID != ReferenceSet.BaseScriptID ||
#if WITH_EDITORONLY_DATA
		BaseScriptCompileHash != ReferenceSet.BaseScriptCompileHash ||
#endif
		DetailLevelMask != ReferenceSet.DetailLevelMask ||
		bUsesRapidIterationParams != ReferenceSet.bUsesRapidIterationParams ||
		bInterpolatedSpawn != ReferenceSet.bInterpolatedSpawn ||
		bRequiresPersistentIDs != ReferenceSet.bRequiresPersistentIDs )
	{
		return false;
	}

#if WITH_EDITORONLY_DATA
	if (ReferencedCompileHashes.Num() != ReferenceSet.ReferencedCompileHashes.Num())
	{
		return false;
	}

	for (int32 ReferencedHashIndex = 0; ReferencedHashIndex < ReferencedCompileHashes.Num(); ReferencedHashIndex++)
	{
		if (ReferencedCompileHashes[ReferencedHashIndex] != ReferenceSet.ReferencedCompileHashes[ReferencedHashIndex])
		{
			return false;
		}
	}

	if (AdditionalDefines.Num() != ReferenceSet.AdditionalDefines.Num())
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
#endif


	return true;
}

#if WITH_EDITORONLY_DATA
void FNiagaraVMExecutableDataId::AppendKeyString(FString& KeyString) const
{
	KeyString += FString::Printf(TEXT("%d_"), (int32)ScriptUsageType);
	KeyString += ScriptUsageTypeID.ToString();
	KeyString += TEXT("_");
	KeyString += CompilerVersionID.ToString();
	KeyString += TEXT("_");
	KeyString += BaseScriptID.ToString();
	KeyString += TEXT("_");
	KeyString += BaseScriptCompileHash.ToString();
	KeyString += TEXT("_");

	if (DetailLevelMask != 0xFFFFFFFF)
	{
		KeyString += FString::Printf(TEXT("DL_%d"), DetailLevelMask);
	}
	else
	{
		KeyString += TEXT("ALLDL_");
	}

	if (bUsesRapidIterationParams)
	{
		KeyString += TEXT("USESRI_");
	}
	else
	{
		KeyString += TEXT("NORI_");
	}

	for (int32 Idx = 0; Idx < AdditionalDefines.Num(); Idx++)
	{
		KeyString += AdditionalDefines[Idx];

		if (Idx < AdditionalDefines.Num() - 1)
		{
			KeyString += TEXT("_");
		}
	}
	
	// Add any referenced script compile hashes to the key so that we will recompile when they are changed
	for (int32 HashIndex = 0; HashIndex < ReferencedCompileHashes.Num(); HashIndex++)
	{
		KeyString += ReferencedCompileHashes[HashIndex].ToString();

		if (HashIndex < ReferencedCompileHashes.Num() - 1)
		{
			KeyString += TEXT("_");
		}
	}
}
#endif

UNiagaraScript::UNiagaraScript(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Usage(ENiagaraScriptUsage::Function)
#if WITH_EDITORONLY_DATA
	, UsageIndex_DEPRECATED(0)
	, ModuleUsageBitmask( (1 << (int32)ENiagaraScriptUsage::ParticleSpawnScript) | (1 << (int32)ENiagaraScriptUsage::ParticleSpawnScriptInterpolated) | (1 << (int32)ENiagaraScriptUsage::ParticleUpdateScript) | (1 << (int32)ENiagaraScriptUsage::ParticleEventScript) )
	, NumericOutputTypeSelectionMode(ENiagaraNumericOutputTypeSelectionMode::Largest)
#endif
{
#if WITH_EDITORONLY_DATA
	ScriptResource.OnCompilationComplete().AddUniqueDynamic(this, &UNiagaraScript::OnCompilationComplete);

	RapidIterationParameters.DebugName = *GetFullName();
#endif	
}

UNiagaraScript::~UNiagaraScript()
{
}

#if WITH_EDITORONLY_DATA
class UNiagaraSystem* UNiagaraScript::FindRootSystem()
{
	UObject* Obj = GetOuter();
	if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Obj))
	{
		Obj = Emitter->GetOuter();
	}

	if (UNiagaraSystem* Sys = Cast<UNiagaraSystem>(Obj))
	{
		return Sys;
	}

	return nullptr;
}

void UNiagaraScript::ComputeVMCompilationId(FNiagaraVMExecutableDataId& Id) const
{
	Id = FNiagaraVMExecutableDataId();

	Id.bUsesRapidIterationParams = true;
	Id.bInterpolatedSpawn = false;
	Id.bRequiresPersistentIDs = false;
	Id.DetailLevelMask = 0xFFFFFFFF; // Unused for now.
	
	// Ideally we wouldn't want to do this but rather than push the data down
	// from the emitter.
	UObject* Obj = GetOuter();
	if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Obj))
	{
		UNiagaraSystem* EmitterOwner = Cast<UNiagaraSystem>(Emitter->GetOuter());
		if (EmitterOwner && EmitterOwner->bBakeOutRapidIteration)
		{
			Id.bUsesRapidIterationParams = false;
		}

		if ((Emitter->bInterpolatedSpawning && Usage == ENiagaraScriptUsage::ParticleGPUComputeScript) || 
			(Emitter->bInterpolatedSpawning && Usage == ENiagaraScriptUsage::ParticleSpawnScript) ||
			Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)
		{
			Id.bInterpolatedSpawn = true;
			Id.AdditionalDefines.Add(TEXT("InterpolatedSpawn"));
		}
		if (Emitter->RequiresPersistantIDs())
		{
			Id.bRequiresPersistentIDs = true;
			Id.AdditionalDefines.Add(TEXT("RequiresPersistentIDs"));
		}
		if (Emitter->bLocalSpace)
		{
			Id.AdditionalDefines.Add(TEXT("Emitter.Localspace"));
		}
		if (Emitter->bDeterminism)
		{
			Id.AdditionalDefines.Add(TEXT("Emitter.Determinism"));
		}
		if (Emitter->bOverrideGlobalSpawnCountScale)
		{
			Id.AdditionalDefines.Add(TEXT("Emitter.OverrideGlobalSpawnCountScale"));
		}

		if (!Emitter->bBakeOutRapidIteration)
		{
			Id.bUsesRapidIterationParams = true;
		}
	}

	if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Obj))
	{
		if (System->bBakeOutRapidIteration)
		{
			Id.bUsesRapidIterationParams = false;
		}

		for (const FNiagaraEmitterHandle& EmitterHandle: System->GetEmitterHandles())
		{
			UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(EmitterHandle.GetInstance());
			if (Emitter && EmitterHandle.GetIsEnabled())
			{
				if (Emitter->bLocalSpace)
				{
					Id.AdditionalDefines.Add(Emitter->GetUniqueEmitterName() + TEXT(".Localspace"));
				}
				if (Emitter->bDeterminism)
				{
					Id.AdditionalDefines.Add(Emitter->GetUniqueEmitterName() + TEXT(".Determinism"));
				}
			}
		}
	}

	// If we aren't using rapid iteration parameters, we need to bake them into the hashstate for the compile id. This 
	// makes their values part of the lookup. 
	if (false == Id.bUsesRapidIterationParams)
	{
		FSHA1 HashState;
		TArray<FNiagaraVariable> Vars;
		RapidIterationParameters.GetParameters(Vars);
		//UE_LOG(LogNiagara, Display, TEXT("AreScriptAndSourceSynchronized %s ======================== "), *GetFullName());
		for (int32 i = 0; i < Vars.Num(); i++)
		{
			if (Vars[i].IsDataInterface() || Vars[i].IsUObject())
			{
				// Skip these types as they don't bake out, just normal parameters get baked.
			}
			else
			{
				// Hash the name, type, and value of each parameter..
				FString VarName = Vars[i].GetName().ToString();
				FString VarTypeName = Vars[i].GetType().GetName();
				HashState.UpdateWithString(*VarName, VarName.Len());
				HashState.UpdateWithString(*VarTypeName, VarTypeName.Len());
				const uint8* VarData = RapidIterationParameters.GetParameterData(Vars[i]);
				if (VarData)
				{
					//UE_LOG(LogNiagara, Display, TEXT("Param %s %s %s"), *VarTypeName, *VarName, *ByteStr);
					HashState.Update(VarData, Vars[i].GetType().GetSize());
				}
			}			
		}
		HashState.Final();

		TArray<uint8> DataHash;
		DataHash.AddUninitialized(20);
		HashState.GetHash(DataHash.GetData());

		FNiagaraCompileHash Hash(DataHash);
		Id.ReferencedCompileHashes.Add(Hash);
		Id.ReferencedObjects.Add(nullptr);
	}

	static const auto UseShaderStagesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.UseShaderStages"));
	if (UseShaderStagesCVar->GetInt())
	{
		Id.AdditionalDefines.Add(TEXT("fx.UseShaderStages"));
	}

	Source->ComputeVMCompilationId(Id, Usage, UsageId);
	
	LastGeneratedVMId = Id;
}
#endif

bool UNiagaraScript::ContainsUsage(ENiagaraScriptUsage InUsage) const
{
	if (IsEquivalentUsage(InUsage))
	{
		return true;
	}

	if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript && IsParticleScript(InUsage))
	{
		return true;
	}

	if (InUsage == ENiagaraScriptUsage::ParticleUpdateScript && Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)
	{
		return true;
	}

	if (InUsage == ENiagaraScriptUsage::EmitterSpawnScript && Usage == ENiagaraScriptUsage::SystemSpawnScript)
	{
		return true;
	}

	if (InUsage == ENiagaraScriptUsage::EmitterUpdateScript && Usage == ENiagaraScriptUsage::SystemUpdateScript)
	{
		return true;
	}

	return false;
}

FNiagaraScriptExecutionParameterStore* UNiagaraScript::GetExecutionReadyParameterStore(ENiagaraSimTarget SimTarget)
{
#if WITH_EDITORONLY_DATA
	if (SimTarget == ENiagaraSimTarget::CPUSim && IsReadyToRun(ENiagaraSimTarget::CPUSim))
	{
		if (ScriptExecutionParamStoreCPU.IsInitialized() == false)
		{
			ScriptExecutionParamStoreCPU.InitFromOwningScript(this, SimTarget, false);
		}
		return &ScriptExecutionParamStoreCPU;
	}
	else if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		if (ScriptExecutionParamStoreGPU.IsInitialized() == false)
		{
			ScriptExecutionParamStoreGPU.InitFromOwningScript(this, SimTarget, false);
		}
		return &ScriptExecutionParamStoreGPU;
	}
	else
	{
		return nullptr;
	}
#else
	check(GetSimTarget().GetValue() == SimTarget);
	return &ScriptExecutionParamStore;
#endif
}

TOptional<ENiagaraSimTarget> UNiagaraScript::GetSimTarget() const
{
	switch (Usage)
	{
	case ENiagaraScriptUsage::ParticleSpawnScript:
	case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
	case ENiagaraScriptUsage::ParticleUpdateScript:
	case ENiagaraScriptUsage::ParticleEventScript:
	case ENiagaraScriptUsage::ParticleGPUComputeScript:
		if (UNiagaraEmitter* OwningEmitter = GetTypedOuter<UNiagaraEmitter>())
		{
			if (OwningEmitter->SimTarget != ENiagaraSimTarget::CPUSim || CachedScriptVM.IsValid())
			{
				return OwningEmitter->SimTarget;
			}
		}
		break;
	case ENiagaraScriptUsage::EmitterSpawnScript:
	case ENiagaraScriptUsage::EmitterUpdateScript:
	case ENiagaraScriptUsage::SystemSpawnScript:
	case ENiagaraScriptUsage::SystemUpdateScript:
		if (CachedScriptVM.IsValid())
		{
			return ENiagaraSimTarget::CPUSim;
		}
		break;
	default:
		break;
	};
	return TOptional<ENiagaraSimTarget>();
}

void UNiagaraScript::AsyncOptimizeByteCode()
{
	if ( !CachedScriptVM.IsValid() || (CachedScriptVM.OptimizedByteCode.Num() > 0) || (CachedScriptVM.ByteCode.Num() == 0) )
	{
		return;
	}

	static const IConsoleVariable* CVarOptimizeVMCode = IConsoleManager::Get().FindConsoleVariable(TEXT("vm.OptimizeVMByteCode"));
	if (!CVarOptimizeVMCode || CVarOptimizeVMCode->GetInt() == 0 )
	{
		return;
	}

	// This has to be done game code side as we can not access anything in CachedScriptVM
	TArray<uint8, TInlineAllocator<32>> ExternalFunctionRegisterCounts;
	ExternalFunctionRegisterCounts.Reserve(CachedScriptVM.CalledVMExternalFunctions.Num());
	for (const FVMExternalFunctionBindingInfo FunctionBindingInfo : CachedScriptVM.CalledVMExternalFunctions)
	{
		const uint8 RegisterCount = FunctionBindingInfo.GetNumInputs() + FunctionBindingInfo.GetNumOutputs();
		ExternalFunctionRegisterCounts.Add(RegisterCount);
	}

	// If we wish to release the original ByteCode we must optimize synchronously currently
	//-TODO: Find a safe point where we can release the original ByteCode
	static const IConsoleVariable* CVarFreeUnoptimizedByteCode = IConsoleManager::Get().FindConsoleVariable(TEXT("vm.FreeUnoptimizedByteCode"));
	if ( FPlatformProperties::RequiresCookedData() && CVarFreeUnoptimizedByteCode && (CVarFreeUnoptimizedByteCode->GetInt() != 0) )
	{
		VectorVM::OptimizeByteCode(CachedScriptVM.ByteCode.GetData(), CachedScriptVM.OptimizedByteCode, MakeArrayView(ExternalFunctionRegisterCounts));
		if (CachedScriptVM.OptimizedByteCode.Num() > 0)
		{
			CachedScriptVM.ByteCode.Empty();
		}
	}
	else
	{
		// Async optimize the ByteCode
		AsyncTask(
			ENamedThreads::AnyThread,
			[WeakScript=TWeakObjectPtr<UNiagaraScript>(this), InExternalFunctionRegisterCounts=MoveTemp(ExternalFunctionRegisterCounts), InByteCode=CachedScriptVM.ByteCode, InCachedScriptVMId=CachedScriptVMId]() mutable
			{
				// Generate optimized byte code on any thread
				TArray<uint8> OptimizedByteCode;
				VectorVM::OptimizeByteCode(InByteCode.GetData(), OptimizedByteCode, MakeArrayView(InExternalFunctionRegisterCounts));

				// Kick off task to set optimized byte code on game thread
				AsyncTask(
					ENamedThreads::GameThread,
					[WeakScript, InOptimizedByteCode = MoveTemp(OptimizedByteCode), InCachedScriptVMId]() mutable
					{
						UNiagaraScript* NiagaraScript = WeakScript.Get();
						if ( (NiagaraScript != nullptr) && (NiagaraScript->CachedScriptVMId == InCachedScriptVMId) )
						{
							NiagaraScript->CachedScriptVM.OptimizedByteCode = MoveTemp(InOptimizedByteCode);
						}
					}
				);
			}
		);
	}
}

void UNiagaraScript::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

#if WITH_EDITORONLY_DATA
	ScriptExecutionParamStore.Empty();
	ScriptExecutionBoundParameters.Empty();

	if (TargetPlatform && TargetPlatform->RequiresCookedData())
	{
		TOptional<ENiagaraSimTarget> SimTarget = GetSimTarget();
		if (SimTarget)
		{
			// Partial execution of InitFromOwningScript()
			ScriptExecutionParamStore.AddScriptParams(this, SimTarget.GetValue(), false);
			FNiagaraParameterStoreBinding::GetBindingData(&ScriptExecutionParamStore, &RapidIterationParameters, ScriptExecutionBoundParameters);
		}
	}
#endif
}

void UNiagaraScript::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);		// only changes version if not loading
	const int32 NiagaraVer = Ar.CustomVer(FNiagaraCustomVersion::GUID);

	FNiagaraParameterStore TemporaryStore;
	int32 NumRemoved = 0;
	if (Ar.IsCooking())
	{
		bool bUsesRapidIterationParams = true;

#if WITH_EDITORONLY_DATA
		if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(GetOuter()))
		{
			UNiagaraSystem* EmitterOwner = Cast<UNiagaraSystem>(Emitter->GetOuter());
			if (EmitterOwner && EmitterOwner->bBakeOutRapidIteration)
			{
				bUsesRapidIterationParams = false;
			}

			if (!Emitter->bBakeOutRapidIteration)
			{
				bUsesRapidIterationParams = true;
			}
		}
		else if (UNiagaraSystem* System = Cast<UNiagaraSystem>(GetOuter()))
		{
			if (System && System->bBakeOutRapidIteration)
			{
				bUsesRapidIterationParams = false;
			}
		}
#endif

		if (!bUsesRapidIterationParams)
		{
			// Copy off the parameter store for now..
			TemporaryStore = RapidIterationParameters;

			// Get the active parameters
			TArray<FNiagaraVariable> Vars;
			RapidIterationParameters.GetParameters(Vars);

			// Remove all parameters that aren't data interfaces or uobjects
			for (const FNiagaraVariable& Var : Vars)
			{
				if (Var.IsDataInterface() || Var.IsUObject())
					continue;
				RapidIterationParameters.RemoveParameter(Var);
				NumRemoved++;
			}

			UE_LOG(LogNiagara, Verbose, TEXT("Pruned %d/%d parameters from script %s"), NumRemoved, TemporaryStore.GetNumParameters(), *GetFullName());
		}
	}

	Super::Serialize(Ar);

	// Restore after serialize
	if (Ar.IsCooking() && NumRemoved > 0)
	{
		RapidIterationParameters = TemporaryStore;
	}

	bool IsValidShaderScript = false;
	if (NiagaraVer < FNiagaraCustomVersion::DontCompileGPUWhenNotNeeded)
	{
		IsValidShaderScript = Usage != ENiagaraScriptUsage::Module && Usage != ENiagaraScriptUsage::Function && Usage != ENiagaraScriptUsage::DynamicInput
			&& (NiagaraVer < FNiagaraCustomVersion::NiagaraShaderMapCooking2 || (Usage != ENiagaraScriptUsage::SystemSpawnScript && Usage != ENiagaraScriptUsage::SystemUpdateScript))
			&& (NiagaraVer < FNiagaraCustomVersion::NiagaraCombinedGPUSpawnUpdate || (Usage != ENiagaraScriptUsage::ParticleUpdateScript && Usage != ENiagaraScriptUsage::EmitterSpawnScript && Usage != ENiagaraScriptUsage::EmitterUpdateScript));
	}
	else if (NiagaraVer < FNiagaraCustomVersion::MovedToDerivedDataCache)
	{
		IsValidShaderScript = LegacyCanBeRunOnGpu();
	}
	else
	{
		IsValidShaderScript = CanBeRunOnGpu();
	}

	if (IsValidShaderScript)
	{
		if (NiagaraVer < FNiagaraCustomVersion::UseHashesToIdentifyCompileStateOfTopLevelScripts)
		{
			// In some rare cases a GPU script could have been saved in an error state in a version where skeletal mesh or static mesh data interfaces didn't work properly on GPU.
			// This would fail in the current regime.
			for (const FNiagaraScriptDataInterfaceCompileInfo& InterfaceInfo : CachedScriptVM.DataInterfaceInfo)
			{
				if (InterfaceInfo.Type.GetClass() == UNiagaraDataInterfaceSkeletalMesh::StaticClass() ||
					InterfaceInfo.Type.GetClass() == UNiagaraDataInterfaceStaticMesh::StaticClass())
				{
					IsValidShaderScript = false;
				}
			}
		}
	}

	if ( (!Ar.IsLoading() && IsValidShaderScript)		// saving shader maps only for particle sim and spawn scripts
		|| (Ar.IsLoading() && NiagaraVer >= FNiagaraCustomVersion::NiagaraShaderMaps && (NiagaraVer < FNiagaraCustomVersion::NiagaraShaderMapCooking || IsValidShaderScript))  // load only if we know shader map is presen
		)
	{
#if WITH_EDITOR
		SerializeNiagaraShaderMaps(&CachedScriptResourcesForCooking, Ar, LoadedScriptResources);
#else
		SerializeNiagaraShaderMaps(nullptr, Ar, LoadedScriptResources);
#endif
	}
}

/** Is usage A dependent on Usage B?*/
bool UNiagaraScript::IsUsageDependentOn(ENiagaraScriptUsage InUsageA, ENiagaraScriptUsage InUsageB)
{
	if (InUsageA == InUsageB)
	{
		return false;
	}

	// Usages of the same phase are interdependent because we copy the attributes from one to the other and if those got 
	// out of sync, there could be problems.

	if ((InUsageA == ENiagaraScriptUsage::ParticleSpawnScript || InUsageA == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated || InUsageA == ENiagaraScriptUsage::ParticleUpdateScript || InUsageA == ENiagaraScriptUsage::ParticleEventScript)
		&& (InUsageB == ENiagaraScriptUsage::ParticleSpawnScript || InUsageB == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated || InUsageB == ENiagaraScriptUsage::ParticleUpdateScript || InUsageB == ENiagaraScriptUsage::ParticleEventScript))
	{
		return true;
	}

	// The GPU compute script is always dependent on the other particle scripts.
	if ((InUsageA == ENiagaraScriptUsage::ParticleGPUComputeScript)
		&& (InUsageB == ENiagaraScriptUsage::ParticleSpawnScript || InUsageB == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated || InUsageB == ENiagaraScriptUsage::ParticleUpdateScript || InUsageB == ENiagaraScriptUsage::ParticleEventScript))
	{
		return true;
	}

	if ((InUsageA == ENiagaraScriptUsage::EmitterSpawnScript || InUsageA == ENiagaraScriptUsage::EmitterUpdateScript)
		&& (InUsageB == ENiagaraScriptUsage::EmitterSpawnScript || InUsageB == ENiagaraScriptUsage::EmitterUpdateScript))
	{
		return true;
	}

	if ((InUsageA == ENiagaraScriptUsage::SystemSpawnScript || InUsageA == ENiagaraScriptUsage::SystemUpdateScript)
		&& (InUsageB == ENiagaraScriptUsage::SystemSpawnScript || InUsageB == ENiagaraScriptUsage::SystemUpdateScript))
	{
		return true;
	}

	return false;
}

bool UNiagaraScript::ConvertUsageToGroup(ENiagaraScriptUsage InUsage, ENiagaraScriptGroup& OutGroup)
{
	if (IsParticleScript(InUsage) || IsStandaloneScript(InUsage))
	{
		OutGroup = ENiagaraScriptGroup::Particle;
		return true;
	}
	else if (IsEmitterSpawnScript(InUsage) || IsEmitterUpdateScript(InUsage))
	{
		OutGroup = ENiagaraScriptGroup::Emitter;
		return true;
	}
	else if (IsSystemSpawnScript(InUsage) || IsSystemUpdateScript(InUsage))
	{
		OutGroup = ENiagaraScriptGroup::System;
		return true;
	}

	return false;
}

void UNiagaraScript::PostLoad()
{
	Super::PostLoad();
	
	RapidIterationParameters.PostLoad();

	if (FPlatformProperties::RequiresCookedData())
	{
		ScriptExecutionParamStore.PostLoad();
		RapidIterationParameters.Bind(&ScriptExecutionParamStore, &ScriptExecutionBoundParameters);
		ScriptExecutionParamStore.SetAsInitialized();
		ScriptExecutionBoundParameters.Empty();
	}

	bool bNeedsRecompile = false;
	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);

#if WITH_EDITORONLY_DATA
	if (Source != nullptr)
	{
		Source->ConditionalPostLoad();
		if (NiagaraVer < FNiagaraCustomVersion::UseHashesToIdentifyCompileStateOfTopLevelScripts && CachedScriptVMId.CompilerVersionID.IsValid())
		{
			FGuid BaseId = Source->GetCompileBaseId(Usage, UsageId);
			if (BaseId.IsValid() == false)
			{
				UE_LOG(LogNiagara, Warning,
					TEXT("Invalidating compile ids for script %s because it doesn't have a valid base id.  The owning asset will continue to compile on load until it is resaved."),
					*GetPathName());
				bool bForceRebuild = true;
				Source->InvalidateCachedCompileIds();
				Source->ComputeVMCompilationId(CachedScriptVMId, Usage, UsageId, bForceRebuild);
			}
			else
			{
				FNiagaraCompileHash CompileHash = Source->GetCompileHash(Usage, UsageId);
				if (CompileHash.IsValid())
				{
					CachedScriptVMId.BaseScriptCompileHash = CompileHash;
				}
				else
				{
					// If the compile hash isn't valid, recompute the entire cached VM Id.
					bool bForceRebuild = true;
					Source->ComputeVMCompilationId(CachedScriptVMId, Usage, UsageId, bForceRebuild);
				}
			}
		}
		if (NiagaraVer < FNiagaraCustomVersion::AddLibraryAssetProperty)
		{
			bExposeToLibrary = true;
		}
	}

	// Invalidate the CachedScriptVM if it's out of date to fix some cook errors, a further investigation is required in how to handle this correctly
	if (CachedScriptVMId.CompilerVersionID != FNiagaraCustomVersion::LatestScriptCompileVersion)
	{
		CachedScriptVM.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_Unknown;
	}
#endif
	
	// Resources can be processed / registered now that we're back on the main thread
	ProcessSerializedShaderMaps(this, LoadedScriptResources, ScriptResource, ScriptResourcesByFeatureLevel);

	// for now, force recompile until we can be sure everything is working
	//bNeedsRecompile = true;
#if WITH_EDITORONLY_DATA
	if (CachedScriptVMId.BaseScriptID.IsValid() && CachedScriptVMId.BaseScriptCompileHash.IsValid())
	{
		CacheResourceShadersForRendering(false, bNeedsRecompile);
	}
#endif
#if STATS
	GenerateStatScopeIDs();
#endif

	// Optimize the VM script for runtime usage
	AsyncOptimizeByteCode();

	//FNiagaraUtilities::DumpHLSLText(RapidIterationParameters.ToString(), *GetPathName());
}

bool UNiagaraScript::IsReadyToRun(ENiagaraSimTarget SimTarget) const
{
	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		if (CachedScriptVM.IsValid())
		{
			return true;
		}
	}
	else if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		return CanBeRunOnGpu();
	}

	return false;
}

bool UNiagaraScript::ShouldCacheShadersForCooking() const
{
	if (CanBeRunOnGpu())
	{
		UNiagaraEmitter* OwningEmitter = GetTypedOuter<UNiagaraEmitter>();
		if (OwningEmitter != nullptr && OwningEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			return true;
		}
	}
	return false;
}

#if STATS
void UNiagaraScript::GenerateStatScopeIDs()
{
	StatScopesIDs.Empty();
	if (IsReadyToRun(ENiagaraSimTarget::CPUSim))
	{
		for (FNiagaraStatScope& StatScope : CachedScriptVM.StatScopes)
		{
			StatScopesIDs.Add(FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraDetailed>(StatScope.FriendlyName.ToString()));
		}
	}
}
#endif

#if WITH_EDITOR

void UNiagaraScript::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName;
	if (PropertyChangedEvent.Property)
	{
		PropertyName = PropertyChangedEvent.Property->GetFName();
	}

	CacheResourceShadersForRendering(true);

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraScript, bDeprecated) || PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraScript, DeprecationRecommendation))
	{
		if (Source)
		{
			Source->MarkNotSynchronized(TEXT("Deprecation changed."));
		}
	}

	CustomAssetRegistryTagCache.Reset();
}

#endif

#if WITH_EDITORONLY_DATA
bool UNiagaraScript::AreScriptAndSourceSynchronized() const
{
	if (Source)
	{
		FNiagaraVMExecutableDataId NewId;
		ComputeVMCompilationId(NewId);
		bool bSynchronized = (NewId.IsValid() && NewId == CachedScriptVMId);
		if (!bSynchronized && NewId.IsValid() && CachedScriptVMId.IsValid() && CachedScriptVM.IsValid())
		{
			if (NewId != LastReportedVMId)
			{
				if (GEnableVerboseNiagaraChangeIdLogging)
				{
					if (NewId.BaseScriptID != CachedScriptVMId.BaseScriptID)
					{
						UE_LOG(LogNiagara, Log, TEXT("AreScriptAndSourceSynchronized base script id's don't match. %s != %s, script %s"),
							*NewId.BaseScriptID.ToString(), *CachedScriptVMId.BaseScriptID.ToString(), *GetPathName());
					}

					if (NewId.BaseScriptCompileHash != CachedScriptVMId.BaseScriptCompileHash)
					{
						UE_LOG(LogNiagara, Log, TEXT("AreScriptAndSourceSynchronized base script compile hashes don't match. %s != %s, script %s"),
							*NewId.BaseScriptCompileHash.ToString(), *CachedScriptVMId.BaseScriptCompileHash.ToString(), *GetPathName());
					}

					if (NewId.ReferencedCompileHashes.Num() != CachedScriptVMId.ReferencedCompileHashes.Num())
					{
						UE_LOG(LogNiagara, Log, TEXT("AreScriptAndSourceSynchronized num referenced compile hashes don't match. %d != %d, script %s"),
							NewId.ReferencedCompileHashes.Num(), CachedScriptVMId.ReferencedCompileHashes.Num(), *GetPathName());
					}
					else
					{
						for (int32 i = 0; i < NewId.ReferencedCompileHashes.Num(); i++)
						{
							if (NewId.ReferencedCompileHashes[i] != CachedScriptVMId.ReferencedCompileHashes[i])
							{
								UE_LOG(LogNiagara, Log, TEXT("AreScriptAndSourceSynchronized referenced compile hash %d doesn't match. %s != %s, script %s, source %s"),
									i, *NewId.ReferencedCompileHashes[i].ToString(), *CachedScriptVMId.ReferencedCompileHashes[i].ToString(), *GetPathName(),
									NewId.ReferencedObjects[i] != nullptr ? *NewId.ReferencedObjects[i]->GetPathName() : TEXT("nullptr"));
							}
						}
					}
				}
				LastReportedVMId = NewId;
			}
		}
		return bSynchronized;
	}
	else
	{
		return false;
	}
}

void UNiagaraScript::MarkScriptAndSourceDesynchronized(FString Reason)
{
	if (Source)
	{
		Source->MarkNotSynchronized(Reason);
	}
}

bool UNiagaraScript::HandleVariableRenames(const TMap<FNiagaraVariable, FNiagaraVariable>& OldToNewVars, const FString& UniqueEmitterName)
{
	bool bConvertedAnything = false;
	auto Iter = OldToNewVars.CreateConstIterator();
	while (Iter)
	{
		// Sometimes the script is under the generic name, other times it has been converted to the unique emitter name. Handle both cases below...
		FNiagaraVariable RISrcVarA = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Iter->Key, !UniqueEmitterName.IsEmpty() ? TEXT("Emitter") : nullptr , GetUsage());
		FNiagaraVariable RISrcVarB = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Iter->Key, !UniqueEmitterName.IsEmpty() ? *UniqueEmitterName : nullptr, GetUsage());
		FNiagaraVariable RIDestVarA = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Iter->Value, !UniqueEmitterName.IsEmpty() ? TEXT("Emitter") : nullptr, GetUsage());
		FNiagaraVariable RIDestVarB = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Iter->Value, !UniqueEmitterName.IsEmpty() ? *UniqueEmitterName : nullptr, GetUsage());

		{
			if (nullptr != RapidIterationParameters.FindParameterOffset(RISrcVarA))
			{
				RapidIterationParameters.RenameParameter(RISrcVarA, RIDestVarA.GetName());
				UE_LOG(LogNiagara, Log, TEXT("Converted RI variable \"%s\" to \"%s\" in Script \"%s\""), *RISrcVarA.GetName().ToString(), *RIDestVarA.GetName().ToString(), *GetFullName());
				bConvertedAnything = true;
			}
			else if (nullptr != RapidIterationParameters.FindParameterOffset(RISrcVarB))
			{
				RapidIterationParameters.RenameParameter(RISrcVarB, RIDestVarB.GetName());
				UE_LOG(LogNiagara, Log, TEXT("Converted RI variable \"%s\" to \"%s\" in Script \"%s\""), *RISrcVarB.GetName().ToString(), *RIDestVarB.GetName().ToString(), *GetFullName());
				bConvertedAnything = true;
			}
		}

		{
			// Go ahead and convert the stored VM executable data too. I'm not 100% sure why this is necessary, since we should be recompiling.
			int32 VarIdx = GetVMExecutableData().Parameters.Parameters.IndexOfByKey(RISrcVarA);
			if (VarIdx != INDEX_NONE)
			{
				GetVMExecutableData().Parameters.Parameters[VarIdx].SetName(RIDestVarA.GetName());
				UE_LOG(LogNiagara, Log, TEXT("Converted exec param variable \"%s\" to \"%s\" in Script \"%s\""), *RISrcVarA.GetName().ToString(), *RIDestVarA.GetName().ToString(), *GetFullName());
				bConvertedAnything = true;
			}

			VarIdx = GetVMExecutableData().Parameters.Parameters.IndexOfByKey(RISrcVarB);
			if (VarIdx != INDEX_NONE)
			{
				GetVMExecutableData().Parameters.Parameters[VarIdx].SetName(RIDestVarB.GetName());
				UE_LOG(LogNiagara, Log, TEXT("Converted exec param  variable \"%s\" to \"%s\" in Script \"%s\""), *RISrcVarB.GetName().ToString(), *RIDestVarB.GetName().ToString(), *GetFullName());
				bConvertedAnything = true;
			}
		}

		{
			// Also handle any data set mappings...
			auto DS2PIterator = GetVMExecutableData().DataSetToParameters.CreateIterator();
			while (DS2PIterator)
			{
				for (int32 i = 0; i < DS2PIterator.Value().Parameters.Num(); i++)
				{
					FNiagaraVariable Var = DS2PIterator.Value().Parameters[i];
					if (Var == RISrcVarA)
					{
						DS2PIterator.Value().Parameters[i].SetName(RIDestVarA.GetName());
						bConvertedAnything = true;
					}
					else if (Var == RISrcVarB)
					{
						DS2PIterator.Value().Parameters[i].SetName(RIDestVarB.GetName());
						bConvertedAnything = true;
					}
				}
				++DS2PIterator;
			}
		}
		++Iter;
	}

	if (bConvertedAnything)
	{
		InvalidateExecutionReadyParameterStores();
	}

	return bConvertedAnything;
}

UNiagaraScript* UNiagaraScript::MakeRecursiveDeepCopy(UObject* DestOuter, TMap<const UObject*, UObject*>& ExistingConversions) const
{
	check(GetOuter() != DestOuter);

	bool bSourceConvertedAlready = ExistingConversions.Contains(Source);

	ResetLoaders(GetTransientPackage()); // Make sure that we're not going to get invalid version number linkers into the transient package. 
	GetTransientPackage()->LinkerCustomVersion.Empty();

	// For some reason, the default parameters of FObjectDuplicationParameters aren't the same as
	// StaticDuplicateObject uses internally. These are copied from Static Duplicate Object...
	EObjectFlags FlagMask = RF_AllFlags & ~RF_Standalone & ~RF_Public; // Remove Standalone and Public flags.
	EDuplicateMode::Type DuplicateMode = EDuplicateMode::Normal;
	EInternalObjectFlags InternalFlagsMask = EInternalObjectFlags::AllFlags;

	FObjectDuplicationParameters ObjParameters((UObject*)this, GetTransientPackage());
	ObjParameters.DestName = NAME_None;
	if (this->GetOuter() != DestOuter)
	{
		// try to keep the object name consistent if possible
		if (FindObjectFast<UObject>(DestOuter, GetFName()) == nullptr)
		{
			ObjParameters.DestName = GetFName();
		}
	}

	ObjParameters.DestClass = GetClass();
	ObjParameters.FlagMask = FlagMask;
	ObjParameters.InternalFlagMask = InternalFlagsMask;
	ObjParameters.DuplicateMode = DuplicateMode;
	
	// Make sure that we don't duplicate objects that we've already converted...
	TMap<const UObject*, UObject*>::TConstIterator It = ExistingConversions.CreateConstIterator();
	while (It)
	{
		ObjParameters.DuplicationSeed.Add(const_cast<UObject*>(It.Key()), It.Value());
		++It;
	}

	UNiagaraScript*	Script = CastChecked<UNiagaraScript>(StaticDuplicateObjectEx(ObjParameters));

	check(Script->HasAnyFlags(RF_Standalone) == false);
	check(Script->HasAnyFlags(RF_Public) == false);

	if (bSourceConvertedAlready)
	{
		// Confirm that we've converted these properly..
		check(Script->Source == ExistingConversions[Source]);
	}

	if (DestOuter != nullptr)
	{
		Script->Rename(nullptr, DestOuter, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
	}
	UE_LOG(LogNiagara, Warning, TEXT("MakeRecursiveDeepCopy %s"), *Script->GetFullName());
	ExistingConversions.Add(const_cast<UNiagaraScript*>(this), Script);

	// Since the Source is the only thing we subsume from UNiagaraScripts, only do the subsume if 
	// we haven't already converted it.
	if (bSourceConvertedAlready == false)
	{
		Script->SubsumeExternalDependencies(ExistingConversions);
	}
	return Script;
}

void UNiagaraScript::SubsumeExternalDependencies(TMap<const UObject*, UObject*>& ExistingConversions)
{
	Source->SubsumeExternalDependencies(ExistingConversions);
}

void WriteTextFileToDisk(FString SaveDirectory, FString FileName, FString TextToSave, bool bAllowOverwriting)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// CreateDirectoryTree returns true if the destination
	// directory existed prior to call or has been created
	// during the call.
	if (PlatformFile.CreateDirectoryTree(*SaveDirectory))
	{
		// Get absolute file path
		FString AbsoluteFilePath = SaveDirectory + "/" + FileName;

		// Allow overwriting or file doesn't already exist
		if (bAllowOverwriting || !PlatformFile.FileExists(*AbsoluteFilePath))
		{
			if (FFileHelper::SaveStringToFile(TextToSave, *AbsoluteFilePath))
			{
				UE_LOG(LogNiagara, Log, TEXT("Wrote file to %s"), *AbsoluteFilePath);
				return;
			}

		}
	}
}

UNiagaraDataInterface* UNiagaraScript::CopyDataInterface(UNiagaraDataInterface* Src, UObject* Owner)
{
	if (Src)
	{
		UNiagaraDataInterface* DI = NewObject<UNiagaraDataInterface>(Owner, const_cast<UClass*>(Src->GetClass()), NAME_None, RF_Transactional | RF_Public);
		Src->CopyTo(DI);
		return DI;
	}
	return nullptr;
}

void UNiagaraScript::SetVMCompilationResults(const FNiagaraVMExecutableDataId& InCompileId, FNiagaraVMExecutableData& InScriptVM, FNiagaraCompileRequestDataBase* InRequestData)
{
	check(InRequestData != nullptr);

	CachedScriptVMId = InCompileId;
	CachedScriptVM = InScriptVM;
	CachedParameterCollectionReferences.Empty();
	
	if (CachedScriptVM.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_Error)
	{
		// Compiler errors for Niagara will have a strong UI impact but the game should still function properly, there 
		// will just be oddities in the visuals. It should be acted upon, but in no way should the game be blocked from
		// a successful cook because of it. Therefore, we do a warning.
		UE_LOG(LogNiagara, Warning, TEXT("%s System Asset: %s"), *CachedScriptVM.ErrorMsg, *GetPathName());
	}
	else if (CachedScriptVM.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings)
	{
		// Compiler warnings for Niagara are meant for notification and should have a UI representation, but 
		// should be expected to still function properly and can be acted upon at the user's leisure. This makes
		// them best logged as Display messages, as Log will not be shown in the cook.
		UE_LOG(LogNiagara, Display, TEXT("%s System Asset: %s"), *CachedScriptVM.ErrorMsg, *GetPathName());
	}

	// The compilation process only references via soft references any parameter collections. This resolves those 
	// soft references to real references.
	for (FString& Path : CachedScriptVM.ParameterCollectionPaths)
	{
		FSoftObjectPath SoftPath(Path);
		UObject* Obj = SoftPath.TryLoad();
		UNiagaraParameterCollection* ParamCollection = Cast<UNiagaraParameterCollection>(Obj);
		if (ParamCollection != nullptr)
		{
			CachedParameterCollectionReferences.Add(ParamCollection);
		}
	}

	CachedDefaultDataInterfaces.Empty(CachedScriptVM.DataInterfaceInfo.Num());
	for (FNiagaraScriptDataInterfaceCompileInfo Info : CachedScriptVM.DataInterfaceInfo)
	{
		int32 Idx = CachedDefaultDataInterfaces.AddDefaulted();
		CachedDefaultDataInterfaces[Idx].UserPtrIdx = Info.UserPtrIdx;
		CachedDefaultDataInterfaces[Idx].Name = Info.Name;
		CachedDefaultDataInterfaces[Idx].Type = Info.Type;
		CachedDefaultDataInterfaces[Idx].RegisteredParameterMapRead = InRequestData->ResolveEmitterAlias(Info.RegisteredParameterMapRead);
		CachedDefaultDataInterfaces[Idx].RegisteredParameterMapWrite = InRequestData->ResolveEmitterAlias(Info.RegisteredParameterMapWrite);

		// We compiled it just a bit ago, so we should be able to resolve it from the table that we passed in.
		UNiagaraDataInterface*const* FindDIById = InRequestData->GetObjectNameMap().Find(Info.Name);
		if (FindDIById != nullptr && *(FindDIById) != nullptr)
		{
			CachedDefaultDataInterfaces[Idx].DataInterface = CopyDataInterface(*(FindDIById), this);
			check(CachedDefaultDataInterfaces[Idx].DataInterface != nullptr);
		}			
		
		if (CachedDefaultDataInterfaces[Idx].DataInterface == nullptr)
		{
			// Use the CDO since we didn't have a default..
			UObject* Obj = const_cast<UClass*>(Info.Type.GetClass())->GetDefaultObject(true);
			CachedDefaultDataInterfaces[Idx].DataInterface = Cast<UNiagaraDataInterface>(CopyDataInterface(CastChecked<UNiagaraDataInterface>(Obj), this));

			if (Info.bIsPlaceholder == false)
			{
				UE_LOG(LogNiagara, Error, TEXT("We somehow ended up with a data interface that we couldn't match post compile. This shouldn't happen. Creating a dummy to prevent crashes. %s"), *Info.Name.ToString());
			}
		}
		check(CachedDefaultDataInterfaces[Idx].DataInterface != nullptr);
	}

	GenerateStatScopeIDs();

	// Now go ahead and trigger the GPU script compile now that we have a compiled GPU hlsl script.
	if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		if (CachedScriptVMId.CompilerVersionID.IsValid() && CachedScriptVMId.BaseScriptID.IsValid() && CachedScriptVMId.BaseScriptCompileHash.IsValid())
		{
			CacheResourceShadersForRendering(false, true);
		}
		else
		{
			UE_LOG(LogNiagara, Error,
				TEXT("Failed to cache resource shaders for rendering for script %s because it had an invalid cached script id. This should be fixed by force recompiling the owning asset using the 'Full Rebuild' option and then saving the asset."),
				*GetPathName());
		}
	}

	InvalidateExecutionReadyParameterStores();
	
	AsyncOptimizeByteCode();

	OnVMScriptCompiled().Broadcast(this);
}

void UNiagaraScript::InvalidateExecutionReadyParameterStores()
{
#if WITH_EDITORONLY_DATA
	// Make sure that we regenerate any parameter stores, since they must be kept in sync with the layout from script compilation.
	ScriptExecutionParamStoreCPU.Empty();
	ScriptExecutionParamStoreGPU.Empty();
#endif
}

void UNiagaraScript::InvalidateCachedCompileIds()
{
	GetSource()->InvalidateCachedCompileIds();
}

void UNiagaraScript::RequestCompile(bool bForceCompile)
{
	if (!AreScriptAndSourceSynchronized() || bForceCompile)
	{
		if (IsCompilable() == false)
		{
			CachedScriptVM.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_Unknown;
			CachedScriptVMId = LastGeneratedVMId;
			return;
		}

		CachedScriptVM.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_BeingCreated;

		TArray<TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe>> DependentRequests;
		TArray<uint8> OutData;
		INiagaraModule& NiagaraModule = FModuleManager::Get().LoadModuleChecked<INiagaraModule>(TEXT("Niagara"));
		TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> RequestData = NiagaraModule.Precompile(this);

		ActiveCompileRoots.Empty();
		RequestData->GetReferencedObjects(ActiveCompileRoots);

		FNiagaraCompileOptions Options(GetUsage(), GetUsageId(), ModuleUsageBitmask, GetPathName(), GetFullName(), GetName());

		FNiagaraScriptDerivedData* CompileTask = new FNiagaraScriptDerivedData(GetFullName(), RequestData, Options, LastGeneratedVMId, false);

		// For debugging DDC/Compression issues		
		const bool bSkipDDC = false;
		if (bSkipDDC)
		{
			CompileTask->Build(OutData);

			delete CompileTask;
			CompileTask = nullptr;
		}
		else
		{
			if (CompileTask->CanBuild())
			{
				GetDerivedDataCacheRef().GetSynchronous(CompileTask, OutData);
				// Assume that once given over to the derived cache, the compile task is going to be killed by it.
				CompileTask = nullptr;
			}
			else
			{
				delete CompileTask;
				CompileTask = nullptr;
			}
		}

		if (OutData.Num() > 0)
		{
			FNiagaraVMExecutableData ExeData;
			FNiagaraScriptDerivedData::BinaryToExecData(OutData, ExeData);
			SetVMCompilationResults(LastGeneratedVMId, ExeData, RequestData.Get());
		}
		else
		{
			check(false);
		}

		ActiveCompileRoots.Empty();
	}
	else
	{
		UE_LOG(LogNiagara, Verbose, TEXT("Script '%s' is in-sync skipping compile.."), *GetFullName());
	}
}

bool UNiagaraScript::RequestExternallyManagedAsyncCompile(const TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe>& RequestData, FNiagaraVMExecutableDataId& OutCompileId, uint32& OutAsyncHandle, bool bTrulyAsync)
{
	if (!AreScriptAndSourceSynchronized())
	{
		if (IsCompilable() == false)
		{
			OutCompileId = LastGeneratedVMId;
			OutAsyncHandle = (uint32)INDEX_NONE;
			CachedScriptVM.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_Unknown;
			CachedScriptVMId = LastGeneratedVMId;
			return false;
		}

		CachedScriptVM.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_BeingCreated;

		OutCompileId = LastGeneratedVMId;
		FNiagaraCompileOptions Options(GetUsage(), GetUsageId(), ModuleUsageBitmask, GetPathName(), GetFullName(), GetName());
		FNiagaraScriptDerivedData* CompileTask = new FNiagaraScriptDerivedData(GetFullName(), RequestData, Options, LastGeneratedVMId, bTrulyAsync);
	
		check(CompileTask->CanBuild());
		OutAsyncHandle = GetDerivedDataCacheRef().GetAsynchronous(CompileTask);

		return true;
	}
	else
	{
		OutCompileId = LastGeneratedVMId;
		OutAsyncHandle = (uint32)INDEX_NONE;
		UE_LOG(LogNiagara, Verbose, TEXT("Script '%s' is in-sync skipping compile.."), *GetFullName());
		return false;
	}
}
#endif

void UNiagaraScript::OnCompilationComplete()
{
#if WITH_EDITORONLY_DATA
	FNiagaraSystemUpdateContext(this, true);
#endif
}

void UNiagaraScript::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);
#if WITH_EDITORONLY_DATA

	if (ProvidedDependencies.Num() > 0)
	{
		FName ProvidedDependenciesName = GET_MEMBER_NAME_CHECKED(UNiagaraScript, ProvidedDependencies);
		FString* ProvidedDependenciesTags;

		if (CustomAssetRegistryTagCache.IsSet() == false)
		{
			CustomAssetRegistryTagCache = TMap<FName, FString>();
		}

		ProvidedDependenciesTags = CustomAssetRegistryTagCache->Find(ProvidedDependenciesName);
		if(ProvidedDependenciesTags == nullptr)
		{
			ProvidedDependenciesTags = &CustomAssetRegistryTagCache->Add(ProvidedDependenciesName);
			for (FName ProvidedDependency : ProvidedDependencies)
			{
				ProvidedDependenciesTags->Append(ProvidedDependency.ToString() + ",");
			}
		}

		OutTags.Add(FAssetRegistryTag(ProvidedDependenciesName, *ProvidedDependenciesTags, UObject::FAssetRegistryTag::TT_Hidden));
	}

	if (Highlights.Num() > 0)
	{
		FName HighlightsName = GET_MEMBER_NAME_CHECKED(UNiagaraScript, Highlights);
		FString* HighlightsTags;

		if (CustomAssetRegistryTagCache.IsSet() == false)
		{
			CustomAssetRegistryTagCache = TMap<FName, FString>();
		}

		HighlightsTags = CustomAssetRegistryTagCache->Find(HighlightsName);
		if (HighlightsTags == nullptr)
		{
			HighlightsTags = &CustomAssetRegistryTagCache->Add(HighlightsName);
			FNiagaraScriptHighlight::ArrayToJson(Highlights, *HighlightsTags);
		}

		OutTags.Add(FAssetRegistryTag(HighlightsName, *HighlightsTags, UObject::FAssetRegistryTag::TT_Hidden));
	}
#endif
}

#if WITH_EDITOR

void UNiagaraScript::BeginCacheForCookedPlatformData(const ITargetPlatform *TargetPlatform)
{
	if (ShouldCacheShadersForCooking())
	{
		// Commandlets like DerivedDataCacheCommandlet call BeginCacheForCookedPlatformData directly on objects. This may mean that
		// we have not properly gotten the HLSL script generated by the time that we get here. This does the awkward work of 
		// waiting on the parent system to finish generating the HLSL before we can begin compiling it for the GPU.
		UNiagaraSystem* SystemOwner = FindRootSystem();
		if (SystemOwner)
		{
			SystemOwner->WaitForCompilationComplete();
		}

		if (CachedScriptVMId.CompilerVersionID.IsValid() == false || CachedScriptVMId.BaseScriptID.IsValid() == false || CachedScriptVMId.BaseScriptCompileHash.IsValid() == false)
		{
			UE_LOG(LogNiagara, Error,
				TEXT("Failed to cache cooked shader for script %s because it had an invalid cached script id.  This should be fixed by running the console command fx.PreventSystemRecompile with the owning system asset path as the argument and then resaving the assets."),
				*GetPathName());
			return;
		}

		TArray<FName> DesiredShaderFormats;
		TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);

		TArray<FNiagaraShaderScript*>& CachedScriptResourcesForPlatform = CachedScriptResourcesForCooking.FindOrAdd(TargetPlatform);

		// Cache for all the shader formats that the cooking target requires
		for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
		{
			const EShaderPlatform LegacyShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);
			if (FNiagaraUtilities::SupportsGPUParticles(LegacyShaderPlatform))
			{
				CacheResourceShadersForCooking(LegacyShaderPlatform, CachedScriptResourcesForPlatform);
			}
		}
	}
}

bool UNiagaraScript::IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform)
{
	if (ShouldCacheShadersForCooking())
	{
		bool bHasOutstandingCompilationRequests = false;
		if (UNiagaraSystem* SystemOwner = FindRootSystem())
		{
			bHasOutstandingCompilationRequests = SystemOwner->HasOutstandingCompilationRequests();
		}

		if (!bHasOutstandingCompilationRequests)
		{
			TArray<FName> DesiredShaderFormats;
			TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);

			const TArray<FNiagaraShaderScript*>* CachedScriptResourcesForPlatform = CachedScriptResourcesForCooking.Find(TargetPlatform);
			if (CachedScriptResourcesForPlatform)
			{
				for (const auto& MaterialResource : *CachedScriptResourcesForPlatform)
				{
					if (MaterialResource->IsCompilationFinished() == false)
					{
						// For now, finish compilation here until we can make sure compilation is finished in the cook commandlet asyncronously before serialize
						MaterialResource->FinishCompilation();

						if (MaterialResource->IsCompilationFinished() == false)
						{
							return false;
						}
					}
				}

				return true;
			}
		}

		return false;
	}

	return true;
}

void UNiagaraScript::CacheResourceShadersForCooking(EShaderPlatform ShaderPlatform, TArray<FNiagaraShaderScript*>& InOutCachedResources)
{
	if (CanBeRunOnGpu())
	{
		// spawn and update are combined on GPU, so we only compile spawn scripts
		if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript)
		{
			FNiagaraShaderScript *ResourceToCache = nullptr;
			ERHIFeatureLevel::Type TargetFeatureLevel = GetMaxSupportedFeatureLevel(ShaderPlatform);

			FNiagaraShaderScript* NewResource = AllocateResource();
			check(CachedScriptVMId.CompilerVersionID.IsValid());
			check(CachedScriptVMId.BaseScriptID.IsValid());
			check(CachedScriptVMId.BaseScriptCompileHash.IsValid());

			NewResource->SetScript(this, (ERHIFeatureLevel::Type)TargetFeatureLevel, CachedScriptVMId.CompilerVersionID, CachedScriptVMId.BaseScriptID, CachedScriptVMId.AdditionalDefines,
				CachedScriptVMId.BaseScriptCompileHash,	CachedScriptVMId.ReferencedCompileHashes, 
				CachedScriptVMId.bUsesRapidIterationParams, CachedScriptVMId.DetailLevelMask, GetFullName());
			ResourceToCache = NewResource;

			check(ResourceToCache);

			CacheShadersForResources(ShaderPlatform, ResourceToCache, false, false, true);

			INiagaraModule NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>(TEXT("Niagara"));
			NiagaraModule.ProcessShaderCompilationQueue();

			InOutCachedResources.Add(ResourceToCache);
		}
	}
}



void UNiagaraScript::CacheShadersForResources(EShaderPlatform ShaderPlatform, FNiagaraShaderScript *ResourceToCache, bool bApplyCompletedShaderMapForRendering, bool bForceRecompile, bool bCooking)
{
	if (CanBeRunOnGpu())
	{
		// When not running in the editor, the shaders are created in-sync (in the postload) to avoid update issues.
		const bool bSuccess = ResourceToCache->CacheShaders(ShaderPlatform, bApplyCompletedShaderMapForRendering, bForceRecompile, bCooking || !GIsEditor);

#if defined(NIAGARA_SCRIPT_COMPILE_LOGGING_MEDIUM)
		if (!bSuccess)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Failed to compile Niagara shader %s for platform %s."),
				*GetPathName(),
				*LegacyShaderPlatformToShaderFormat(ShaderPlatform).ToString());

			const TArray<FString>& CompileErrors = ResourceToCache->GetCompileErrors();
			for (int32 ErrorIndex = 0; ErrorIndex < CompileErrors.Num(); ErrorIndex++)
			{
				UE_LOG(LogNiagara, Warning, TEXT("	%s"), *CompileErrors[ErrorIndex]);
			}
		}
#endif
	}
}

void UNiagaraScript::CacheResourceShadersForRendering(bool bRegenerateId, bool bForceRecompile)
{
	if (bRegenerateId)
	{
		// Regenerate this script's Id if requested
		for (int32 Idx = 0; Idx < ERHIFeatureLevel::Num; Idx++)
		{
			if (ScriptResourcesByFeatureLevel[Idx])
			{
				ScriptResourcesByFeatureLevel[Idx]->ReleaseShaderMap();
				ScriptResourcesByFeatureLevel[Idx] = nullptr;
			}
		}
	}

	//UpdateResourceAllocations();

	if (CanBeRunOnGpu())
	{
		if (Source)
		{
			FNiagaraShaderScript* ResourceToCache;
			ERHIFeatureLevel::Type CacheFeatureLevel = GMaxRHIFeatureLevel;
			ScriptResource.SetScript(this, FeatureLevel, CachedScriptVMId.CompilerVersionID, CachedScriptVMId.BaseScriptID, CachedScriptVMId.AdditionalDefines,
				CachedScriptVMId.BaseScriptCompileHash, CachedScriptVMId.ReferencedCompileHashes, 
				CachedScriptVMId.bUsesRapidIterationParams, CachedScriptVMId.DetailLevelMask, GetFullName());

			//if (ScriptResourcesByFeatureLevel[FeatureLevel])
			{
				if (FNiagaraUtilities::SupportsGPUParticles(CacheFeatureLevel))
				{
					EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[CacheFeatureLevel];
					ResourceToCache = ScriptResourcesByFeatureLevel[CacheFeatureLevel];
					CacheShadersForResources(ShaderPlatform, &ScriptResource, true);
					ScriptResourcesByFeatureLevel[CacheFeatureLevel] = &ScriptResource;
				}
			}
		}
	}
}

void UNiagaraScript::SyncAliases(const TMap<FString, FString>& RenameMap)
{
	// First handle any rapid iteration parameters...
	{
		TArray<FNiagaraVariable> Params;
		RapidIterationParameters.GetParameters(Params);
		for (FNiagaraVariable Var : Params)
		{
			FNiagaraVariable NewVar = FNiagaraVariable::ResolveAliases(Var, RenameMap);
			if (NewVar.GetName() != Var.GetName())
			{
				RapidIterationParameters.RenameParameter(Var, NewVar.GetName());
			}
		}
	}

	InvalidateExecutionReadyParameterStores();

	// Now handle any Parameters overall..
	for (int32 i = 0; i < GetVMExecutableData().Parameters.Parameters.Num(); i++)
	{
		if (GetVMExecutableData().Parameters.Parameters[i].IsValid() == false)
		{
			const FNiagaraVariable& InvalidParameter = GetVMExecutableData().Parameters.Parameters[i];
			UE_LOG(LogNiagara, Error, TEXT("Invalid parameter found while syncing script aliases.  Script: %s Parameter Name: %s Parameter Type: %s"),
				*GetPathName(), *InvalidParameter.GetName().ToString(), InvalidParameter.GetType().IsValid() ? *InvalidParameter.GetType().GetName() : TEXT("Unknown"));
			continue;
		}

		FNiagaraVariable Var = GetVMExecutableData().Parameters.Parameters[i];
		FNiagaraVariable NewVar = FNiagaraVariable::ResolveAliases(Var, RenameMap);
		if (NewVar.GetName() != Var.GetName())
		{
			GetVMExecutableData().Parameters.Parameters[i] = NewVar;
		}
	}

	// Also handle any data set mappings...
	auto Iterator = GetVMExecutableData().DataSetToParameters.CreateIterator();
	while (Iterator)
	{
		for (int32 i = 0; i < Iterator.Value().Parameters.Num(); i++)
		{
			FNiagaraVariable Var = Iterator.Value().Parameters[i];
			FNiagaraVariable NewVar = FNiagaraVariable::ResolveAliases(Var, RenameMap);
			if (NewVar.GetName() != Var.GetName())
			{
				Iterator.Value().Parameters[i] = NewVar;
			}
		}
		++Iterator;
	}
}

bool UNiagaraScript::SynchronizeExecutablesWithMaster(const UNiagaraScript* Script, const TMap<FString, FString>& RenameMap)
{
	FNiagaraVMExecutableDataId Id;
	ComputeVMCompilationId(Id);

#if 1 // TODO Shaun... turn this on...
	if (Id == Script->GetVMExecutableDataCompilationId())
	{
		CachedScriptVM.Reset();
		ScriptResource.Invalidate();

		CachedScriptVM = Script->CachedScriptVM;
		CachedScriptVMId = Script->CachedScriptVMId;
		CachedParameterCollectionReferences = Script->CachedParameterCollectionReferences;
		CachedDefaultDataInterfaces.Empty();
		for (const FNiagaraScriptDataInterfaceInfo& Info : Script->CachedDefaultDataInterfaces)
		{
			FNiagaraScriptDataInterfaceInfo AddInfo;
			AddInfo = Info;
			AddInfo.DataInterface = CopyDataInterface(Info.DataInterface, this);
			CachedDefaultDataInterfaces.Add(AddInfo);
		}

		GenerateStatScopeIDs();

		//SyncAliases(RenameMap);

		// Now go ahead and trigger the GPU script compile now that we have a compiled GPU hlsl script.
		if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript)
		{
			CacheResourceShadersForRendering(false, true);
		}

		OnVMScriptCompiled().Broadcast(this);
		return true;
	}
#endif

	return false;
}

void UNiagaraScript::InvalidateCompileResults()
{
	UE_LOG(LogNiagara, Log, TEXT("InvalidateCompileResults %s"), *GetPathName());
	CachedScriptVM.Reset();
	ScriptResource.Invalidate();
	CachedScriptVMId.Invalidate();
	LastGeneratedVMId.Invalidate();
}


UNiagaraScript::FOnScriptCompiled& UNiagaraScript::OnVMScriptCompiled()
{
	return OnVMScriptCompiledDelegate;
}



#endif


NIAGARA_API bool UNiagaraScript::IsScriptCompilationPending(bool bGPUScript) const
{
	if (bGPUScript)
	{
		FNiagaraShader *Shader = ScriptResource.GetShaderGameThread();
		if (Shader)
		{
			return false;
		}
		return !ScriptResource.IsCompilationFinished();
	}
	else
	{
		if (CachedScriptVM.IsValid())
		{
			return (CachedScriptVM.ByteCode.Num() == 0) && (CachedScriptVM.OptimizedByteCode.Num() == 0) && (CachedScriptVM.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_BeingCreated || CachedScriptVM.LastCompileStatus == ENiagaraScriptCompileStatus::NCS_Unknown);
		}
		return false;
	}
}

NIAGARA_API bool UNiagaraScript::DidScriptCompilationSucceed(bool bGPUScript) const
{
	if (bGPUScript)
	{
		FNiagaraShader *Shader = ScriptResource.GetShaderGameThread();
		if (Shader)
		{
			return true;
		}

		if (ScriptResource.IsCompilationFinished())
		{
			// If we failed compilation, it would be finished and Shader would be null.
			return false;
		}
	}
	else
	{
		if (CachedScriptVM.IsValid())
		{
			return (CachedScriptVM.ByteCode.Num() != 0) || (CachedScriptVM.OptimizedByteCode.Num() != 0);
		}
	}

	return false;
}

void SerializeNiagaraShaderMaps(const TMap<const ITargetPlatform*, TArray<FNiagaraShaderScript*>>* PlatformScriptResourcesToSave, FArchive& Ar, TArray<FNiagaraShaderScript>& OutLoadedResources)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

//	SCOPED_LOADTIMER(SerializeInlineShaderMaps);
	if (Ar.IsSaving())
	{
		int32 NumResourcesToSave = 0;
		const TArray<FNiagaraShaderScript*>* ScriptResourcesToSavePtr = nullptr;

		if (Ar.IsCooking())
		{
			checkf(PlatformScriptResourcesToSave != nullptr, TEXT("PlatformScriptResourcesToSave must be supplied when cooking"));
			ScriptResourcesToSavePtr = PlatformScriptResourcesToSave->Find(Ar.CookingTarget());
			if (ScriptResourcesToSavePtr != nullptr)
			{
				NumResourcesToSave = ScriptResourcesToSavePtr->Num();
			}
		}

		Ar << NumResourcesToSave;

		if (ScriptResourcesToSavePtr != nullptr)
		{
			for (FNiagaraShaderScript* ScriptResourceToSave : (*ScriptResourcesToSavePtr))
			{
				checkf(ScriptResourceToSave != nullptr, TEXT("Invalid script resource was cached"));
				ScriptResourceToSave->SerializeShaderMap(Ar);
			}
		}
	}
	else if (Ar.IsLoading())
	{
		int32 NumLoadedResources = 0;
		Ar << NumLoadedResources;
		for (int32 i = 0; i < NumLoadedResources; i++)
		{
			FNiagaraShaderScript LoadedResource;
			LoadedResource.SerializeShaderMap(Ar);
			OutLoadedResources.Add(LoadedResource);
		}
	}
}

void ProcessSerializedShaderMaps(UNiagaraScript* Owner, TArray<FNiagaraShaderScript>& LoadedResources, FNiagaraShaderScript& OutResourceForCurrentPlatform, FNiagaraShaderScript* (&OutScriptResourcesLoaded)[ERHIFeatureLevel::Num])
{
	check(IsInGameThread());

	for (FNiagaraShaderScript& LoadedResource : LoadedResources)
	{
		LoadedResource.RegisterShaderMap();

		FNiagaraShaderMap* LoadedShaderMap = LoadedResource.GetGameThreadShaderMap();
		if (LoadedShaderMap && LoadedShaderMap->GetShaderPlatform() == GMaxRHIShaderPlatform)
		{
			OutResourceForCurrentPlatform = LoadedResource;

			ERHIFeatureLevel::Type LoadedFeatureLevel = LoadedShaderMap->GetShaderMapId().FeatureLevel;
			if (!OutScriptResourcesLoaded[LoadedFeatureLevel])
			{
				OutScriptResourcesLoaded[LoadedFeatureLevel] = Owner->AllocateResource();
			}

			OutScriptResourcesLoaded[LoadedFeatureLevel]->SetShaderMap(LoadedShaderMap);
			OutResourceForCurrentPlatform.SetDataInterfaceParamInfo(LoadedResource.GetShaderGameThread()->GetDIParameters());

			break;
		}
		else
		{
			LoadedResource.DiscardShaderMap();
		}
	}
}

FNiagaraShaderScript* UNiagaraScript::AllocateResource()
{
	return new FNiagaraShaderScript();
}

#if WITH_EDITORONLY_DATA
TArray<ENiagaraScriptUsage> UNiagaraScript::GetSupportedUsageContexts() const
{
	return GetSupportedUsageContextsForBitmask(ModuleUsageBitmask);
}

TArray<ENiagaraScriptUsage> UNiagaraScript::GetSupportedUsageContextsForBitmask(int32 InModuleUsageBitmask)
{
	TArray<ENiagaraScriptUsage> Supported;
	for (int32 i = 0; i <= (int32)ENiagaraScriptUsage::SystemUpdateScript; i++)
	{
		int32 TargetBit = (InModuleUsageBitmask >> (int32)i) & 1;
		if (TargetBit == 1)
		{
			Supported.Add((ENiagaraScriptUsage)i);
		}
	}
	return Supported;
}
#endif

bool UNiagaraScript::CanBeRunOnGpu()const
{

	if (Usage != ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		return false;
	}
	if (!CachedScriptVM.IsValid())
	{
		return false;
	}
	for (const FNiagaraScriptDataInterfaceCompileInfo& InterfaceInfo : CachedScriptVM.DataInterfaceInfo)
	{
		if (!InterfaceInfo.CanExecuteOnTarget(ENiagaraSimTarget::GPUComputeSim))
		{
			return false;
		}
	}
	return true;
}


bool UNiagaraScript::LegacyCanBeRunOnGpu() const
{
	if (UNiagaraEmitter* Emitter = GetTypedOuter<UNiagaraEmitter>())
	{
		if (Emitter->SimTarget == ENiagaraSimTarget::CPUSim)
		{
			return false;
		}

		if (!IsParticleSpawnScript())
		{
			return false;
		}

		return true;
	}
	return false;
}


#if WITH_EDITORONLY_DATA
FGuid UNiagaraScript::GetBaseChangeID() const
{
	return Source->GetChangeID(); 
}

ENiagaraScriptCompileStatus UNiagaraScript::GetLastCompileStatus() const
{
	if (CachedScriptVM.IsValid())
	{
		return CachedScriptVM.LastCompileStatus;
	}
	return ENiagaraScriptCompileStatus::NCS_Unknown;
}
#endif

bool UNiagaraScript::UsesCollection(const UNiagaraParameterCollection* Collection)const
{
	if (CachedScriptVM.IsValid())
	{
		return CachedParameterCollectionReferences.FindByPredicate([&](const UNiagaraParameterCollection* CheckCollection)
		{
			return CheckCollection == Collection;
		}) != NULL;
	}
	return false;
}
