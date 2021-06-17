// Copyright Epic Games, Inc. All Rights Reserved.


#include "NiagaraSystem.h"

#include "INiagaraEditorOnlyDataUtlities.h"
#include "NiagaraConstants.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraEditorDataBase.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraModule.h"
#include "NiagaraPrecompileContainer.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraSettings.h"
#include "NiagaraStats.h"
#include "NiagaraTrace.h"
#include "NiagaraTypes.h"
#include "NiagaraWorldManager.h"
#include "Algo/RemoveIf.h"
#include "Async/Async.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CookStats.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "NiagaraSystem"

#if WITH_EDITOR
#include "DerivedDataCacheInterface.h"
static uint32 CompileGuardSlot = 0;

#endif

DECLARE_CYCLE_STAT(TEXT("Niagara - System - Precompile"), STAT_Niagara_System_Precompile, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Niagara - System - CompileScript"), STAT_Niagara_System_CompileScript, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Niagara - System - CompileScript_ResetAfter"), STAT_Niagara_System_CompileScriptResetAfter, STATGROUP_Niagara);

#if ENABLE_COOK_STATS
namespace NiagaraScriptCookStats
{
	extern FCookStats::FDDCResourceUsageStats UsageStats;
}
#endif

//Disable for now until we can spend more time on a good method of applying the data gathered.
int32 GEnableNiagaraRuntimeCycleCounts = 0;
static FAutoConsoleVariableRef CVarEnableNiagaraRuntimeCycleCounts(TEXT("fx.EnableNiagaraRuntimeCycleCounts"), GEnableNiagaraRuntimeCycleCounts, TEXT("Toggle for runtime cylce counts tracking Niagara's frame time. \n"), ECVF_ReadOnly);

static int GNiagaraForceSystemsToCookOutRapidIterationOnLoad = 0;
static FAutoConsoleVariableRef CVarNiagaraForceSystemsToCookOutRapidIterationOnLoad(
	TEXT("fx.NiagaraForceSystemsToCookOutRapidIterationOnLoad"),
	GNiagaraForceSystemsToCookOutRapidIterationOnLoad,
	TEXT("When enabled UNiagaraSystem's bBakeOutRapidIteration will be forced to true on PostLoad of the system."),
	ECVF_Default
);

static int GNiagaraLogDDCStatusForSystems = 0;
static FAutoConsoleVariableRef CVarLogDDCStatusForSystems(
	TEXT("fx.NiagaraLogDDCStatusForSystems"),
	GNiagaraLogDDCStatusForSystems,
	TEXT("When enabled UNiagaraSystems will log out when their subscripts are pulled from the DDC or not."),
	ECVF_Default
);

static float GNiagaraScalabiltiyMinumumMaxDistance = 1.0f;
static FAutoConsoleVariableRef CVarNiagaraScalabiltiyMinumumMaxDistance(
	TEXT("fx.Niagara.Scalability.MinMaxDistance"),
	GNiagaraScalabiltiyMinumumMaxDistance,
	TEXT("Minimum value for Niagara's Max distance value. Primariy to prevent divide by zero issues and ensure a sensible distance value for sorted significance culling."),
	ECVF_Default
);

//////////////////////////////////////////////////////////////////////////

UNiagaraSystem::UNiagaraSystem(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
, bBakeOutRapidIterationOnCook(true)
, bTrimAttributes(false)
, bTrimAttributesOnCook(true)
, bDisableAllDebugSwitches(false)
#endif
, bFixedBounds(false)
#if WITH_EDITORONLY_DATA
, bIsolateEnabled(false)
#endif
, FixedBounds(FBox(FVector(-100), FVector(100)))
, bAutoDeactivate(true)
, WarmupTime(0.0f)
, WarmupTickCount(0)
, WarmupTickDelta(1.0f / 15.0f)
, bHasSystemScriptDIsWithPerInstanceData(false)
, bNeedsGPUContextInitForDataInterfaces(false)
, bHasAnyGPUEmitters(false)
, bNeedsSortedSignificanceCull(false)
, ActiveInstances(0)
{
	ExposedParameters.SetOwner(this);
#if WITH_EDITORONLY_DATA
	EditorOnlyAddedParameters.SetOwner(this);
#endif
	MaxPoolSize = 32;

	EffectType = nullptr;
	bOverrideScalabilitySettings = false;

#if WITH_EDITORONLY_DATA
	AssetGuid = FGuid::NewGuid();
#endif
}

UNiagaraSystem::UNiagaraSystem(FVTableHelper& Helper)
	: Super(Helper)
{
}

void UNiagaraSystem::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_EDITORONLY_DATA
	while (ActiveCompilations.Num() > 0)
	{
		QueryCompileComplete(true, false, true);
	}
#endif

	//Should we just destroy all system sims here to simplify cleanup?
	//FNiagaraWorldManager::DestroyAllSystemSimulations(this);
}

void UNiagaraSystem::PreSave(const class ITargetPlatform * TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	EnsureFullyLoaded();
#if WITH_EDITORONLY_DATA
	WaitForCompilationComplete();
#endif
}

#if WITH_EDITOR
void UNiagaraSystem::BeginCacheForCookedPlatformData(const ITargetPlatform *TargetPlatform)
{
	//UE_LOG(LogNiagara, Display, TEXT("UNiagaraSystem::BeginCacheForCookedPlatformData %s %s"), *GetFullName(), GIsSavingPackage ? TEXT("Saving...") : TEXT("Not Saving..."));
	Super::BeginCacheForCookedPlatformData(TargetPlatform);
	
	EnsureFullyLoaded();
#if WITH_EDITORONLY_DATA
	WaitForCompilationComplete();
#endif
}

void UNiagaraSystem::HandleVariableRenamed(const FNiagaraVariable& InOldVariable, const FNiagaraVariable& InNewVariable, bool bUpdateContexts)
{
	if (InOldVariable.IsInNameSpace(FNiagaraConstants::UserNamespace))
	{
		if (GetExposedParameters().IndexOf(InOldVariable) != INDEX_NONE)
			GetExposedParameters().RenameParameter(InOldVariable, InNewVariable.GetName());
		InitSystemCompiledData();
	}

	for (const FNiagaraEmitterHandle& Handle : GetEmitterHandles())
	{
		UNiagaraEmitter* Emitter = Handle.GetInstance();
		if (Emitter)
		{
			Emitter->HandleVariableRenamed(InOldVariable, InNewVariable, false);
		}
	}

	if (bUpdateContexts)
	{
		FNiagaraSystemUpdateContext UpdateCtx(this, true);
	}
}


void UNiagaraSystem::HandleVariableRemoved(const FNiagaraVariable& InOldVariable, bool bUpdateContexts)
{
	if (InOldVariable.IsInNameSpace(FNiagaraConstants::UserNamespace))
	{
		if (GetExposedParameters().IndexOf(InOldVariable) != INDEX_NONE)
			GetExposedParameters().RemoveParameter(InOldVariable);
		InitSystemCompiledData();
	}
	for (const FNiagaraEmitterHandle& Handle : GetEmitterHandles())
	{
		UNiagaraEmitter* Emitter = Handle.GetInstance();
		if (Emitter)
		{
			Emitter->HandleVariableRemoved(InOldVariable, false);
		}
	}
	if (bUpdateContexts)
	{
		FNiagaraSystemUpdateContext UpdateCtx(this, true);
	}
}

TArray<UNiagaraScriptSourceBase*> UNiagaraSystem::GetAllSourceScripts()
{
	EnsureFullyLoaded();
	return { SystemSpawnScript->GetLatestSource(), SystemUpdateScript->GetLatestSource() };
}

FString UNiagaraSystem::GetSourceObjectPathName() const
{
	return GetPathName();
}

TArray<UNiagaraEditorParametersAdapterBase*> UNiagaraSystem::GetEditorOnlyParametersAdapters()
{
	return { GetEditorParameters() };
}

TArray<INiagaraParameterDefinitionsSubscriber*> UNiagaraSystem::GetOwnedParameterDefinitionsSubscribers()
{
	TArray<INiagaraParameterDefinitionsSubscriber*> OutSubscribers;
	for (const FNiagaraEmitterHandle& EmitterHandle : GetEmitterHandles())
	{
		OutSubscribers.Add(EmitterHandle.GetInstance());
	}
	return OutSubscribers;
}

#endif

void UNiagaraSystem::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITORONLY_DATA
	ThumbnailImageOutOfDate = true;
#endif
	if (HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad) == false)
	{
		SystemSpawnScript = NewObject<UNiagaraScript>(this, "SystemSpawnScript", RF_Transactional);
		SystemSpawnScript->SetUsage(ENiagaraScriptUsage::SystemSpawnScript);

		SystemUpdateScript = NewObject<UNiagaraScript>(this, "SystemUpdateScript", RF_Transactional);
		SystemUpdateScript->SetUsage(ENiagaraScriptUsage::SystemUpdateScript);

#if WITH_EDITORONLY_DATA && WITH_EDITOR
		INiagaraModule& NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>("Niagara");
		EditorData = NiagaraModule.GetEditorOnlyDataUtilities().CreateDefaultEditorData(this);

		if (EditorParameters == nullptr)
		{
			EditorParameters = NiagaraModule.GetEditorOnlyDataUtilities().CreateDefaultEditorParameters(this);
		}
#endif
	}

	ResolveScalabilitySettings();
	UpdateDITickFlags();
	UpdateHasGPUEmitters();
}

bool UNiagaraSystem::IsLooping() const
{ 
	return false; 
} //sckime todo fix this!

bool UNiagaraSystem::UsesCollection(const UNiagaraParameterCollection* Collection)const
{
	if (SystemSpawnScript->UsesCollection(Collection) ||
		SystemUpdateScript->UsesCollection(Collection))
	{
		return true;
	}

	for (const FNiagaraEmitterHandle& EmitterHandle : GetEmitterHandles())
	{
		if (EmitterHandle.GetInstance() && EmitterHandle.GetInstance()->UsesCollection(Collection))
		{
			return true;
		}
	}

	return false;
}

void UNiagaraSystem::UpdateSystemAfterLoad()
{
	// guard against deadlocks by having wait called on it during the update
	if (bFullyLoaded)
	{
		return;
	}
	bFullyLoaded = true;

	for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
	{
		if (EmitterHandle.GetInstance() != nullptr)
		{
			EmitterHandle.GetInstance()->UpdateEmitterAfterLoad();
		}
	}

#if WITH_EDITORONLY_DATA
	// We remove emitters and scripts on dedicated servers, so skip further work.
	const bool bIsDedicatedServer = !GIsClient && GIsServer;

	if (!GetOutermost()->bIsCookedForEditor && !bIsDedicatedServer)
	{
		TArray<UNiagaraScript*> AllSystemScripts;
		UNiagaraScriptSourceBase* SystemScriptSource;
		if (SystemSpawnScript == nullptr)
		{
			SystemSpawnScript = NewObject<UNiagaraScript>(this, "SystemSpawnScript", RF_Transactional);
			SystemSpawnScript->SetUsage(ENiagaraScriptUsage::SystemSpawnScript);
			INiagaraModule& NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>("Niagara");
			SystemScriptSource = NiagaraModule.GetEditorOnlyDataUtilities().CreateDefaultScriptSource(this);
			SystemSpawnScript->SetLatestSource(SystemScriptSource);
		}
		else
		{
			SystemSpawnScript->ConditionalPostLoad();
			SystemScriptSource = SystemSpawnScript->GetLatestSource();
		}
		AllSystemScripts.Add(SystemSpawnScript);

		if (SystemUpdateScript == nullptr)
		{
			SystemUpdateScript = NewObject<UNiagaraScript>(this, "SystemUpdateScript", RF_Transactional);
			SystemUpdateScript->SetUsage(ENiagaraScriptUsage::SystemUpdateScript);
			SystemUpdateScript->SetLatestSource(SystemScriptSource);
		}
		else
		{
			SystemUpdateScript->ConditionalPostLoad();
		}
		AllSystemScripts.Add(SystemUpdateScript);

		bool bSystemScriptsAreSynchronized = true;
		for (UNiagaraScript* SystemScript : AllSystemScripts)
		{
			bSystemScriptsAreSynchronized &= SystemScript->AreScriptAndSourceSynchronized();
		}

		// Synchronize with parameter definitions
		PostLoadDefinitionsSubscriptions();

		bool bEmitterScriptsAreSynchronized = true;
#if 0
		UE_LOG(LogNiagara, Log, TEXT("PreMerger"));
		for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			UE_LOG(LogNiagara, Log, TEXT("Emitter Handle: %s"), *EmitterHandle.GetUniqueInstanceName());
			UNiagaraScript* UpdateScript = EmitterHandle.GetInstance()->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
			UNiagaraScript* SpawnScript = EmitterHandle.GetInstance()->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
			UE_LOG(LogNiagara, Log, TEXT("Spawn Parameters"));
			SpawnScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagara, Log, TEXT("Spawn RI Parameters"));
			SpawnScript->RapidIterationParameters.DumpParameters();
			UE_LOG(LogNiagara, Log, TEXT("Update Parameters"));
			UpdateScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagara, Log, TEXT("Update RI Parameters"));
			UpdateScript->RapidIterationParameters.DumpParameters();
		}
#endif

		for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			if (EmitterHandle.GetIsEnabled() && EmitterHandle.GetInstance() && !EmitterHandle.GetInstance()->AreAllScriptAndSourcesSynchronized())
			{
				bEmitterScriptsAreSynchronized = false;
			}
		}

		if (UNiagaraEmitter::GetForceCompileOnLoad())
		{
			ForceGraphToRecompileOnNextCheck();
			UE_LOG(LogNiagara, Log, TEXT("System %s being rebuilt because UNiagaraEmitter::GetForceCompileOnLoad() == true."), *GetPathName());
		}

		if (bSystemScriptsAreSynchronized == false && GEnableVerboseNiagaraChangeIdLogging)
		{
			UE_LOG(LogNiagara, Log, TEXT("System %s being compiled because there were changes to a system script Change ID."), *GetPathName());
		}

		if (bEmitterScriptsAreSynchronized == false && GEnableVerboseNiagaraChangeIdLogging)
		{
			UE_LOG(LogNiagara, Log, TEXT("System %s being compiled because there were changes to an emitter script Change ID."), *GetPathName());
		}

		if (EmitterCompiledData.Num() == 0 || EmitterCompiledData[0]->DataSetCompiledData.Variables.Num() == 0)
		{
			InitEmitterCompiledData();
		}

		if (SystemCompiledData.InstanceParamStore.ReadParameterVariables().Num() == 0 ||SystemCompiledData.DataSetCompiledData.Variables.Num() == 0)
		{
			InitSystemCompiledData();
		}

#if 0
		UE_LOG(LogNiagara, Log, TEXT("Before"));
		for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			UE_LOG(LogNiagara, Log, TEXT("Emitter Handle: %s"), *EmitterHandle.GetUniqueInstanceName());
			UNiagaraScript* UpdateScript = EmitterHandle.GetInstance()->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
			UNiagaraScript* SpawnScript = EmitterHandle.GetInstance()->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
			UE_LOG(LogNiagara, Log, TEXT("Spawn Parameters"));
			SpawnScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagara, Log, TEXT("Spawn RI Parameters"));
			SpawnScript->RapidIterationParameters.DumpParameters();
			UE_LOG(LogNiagara, Log, TEXT("Update Parameters"));
			UpdateScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagara, Log, TEXT("Update RI Parameters"));
			UpdateScript->RapidIterationParameters.DumpParameters();
		}
#endif

		if (bSystemScriptsAreSynchronized == false || bEmitterScriptsAreSynchronized == false)
		{
			if (IsRunningCommandlet())
			{
				// Call modify here so that the system will resave the compile ids and script vm when running the resave
				// commandlet. We don't need it for normal post-loading.
				Modify();
			}
			RequestCompile(false);
		}

#if 0
		UE_LOG(LogNiagara, Log, TEXT("After"));
		for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			UE_LOG(LogNiagara, Log, TEXT("Emitter Handle: %s"), *EmitterHandle.GetUniqueInstanceName());
			UNiagaraScript* UpdateScript = EmitterHandle.GetInstance()->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
			UNiagaraScript* SpawnScript = EmitterHandle.GetInstance()->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid());
			UE_LOG(LogNiagara, Log, TEXT("Spawn Parameters"));
			SpawnScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagara, Log, TEXT("Spawn RI Parameters"));
			SpawnScript->RapidIterationParameters.DumpParameters();
			UE_LOG(LogNiagara, Log, TEXT("Update Parameters"));
			UpdateScript->GetVMExecutableData().Parameters.DumpParameters();
			UE_LOG(LogNiagara, Log, TEXT("Update RI Parameters"));
			UpdateScript->RapidIterationParameters.DumpParameters();
		}
#endif
	}
	if (GNiagaraForceSystemsToCookOutRapidIterationOnLoad == 1 && !bBakeOutRapidIteration)
	{
		WaitForCompilationComplete();
		bBakeOutRapidIteration = true;
		RequestCompile(false);
	}
#endif

	if ( FPlatformProperties::RequiresCookedData() )
	{
		bIsValidCached = IsValidInternal();
		bIsReadyToRunCached = IsReadyToRunInternal();
	}

	ResolveScalabilitySettings();

	ComputeEmittersExecutionOrder();

	ComputeRenderersDrawOrder();

	CacheFromCompiledData();

	//TODO: Move to serialized properties?
	UpdateDITickFlags();
	UpdateHasGPUEmitters();

	// Run task to prime pools this must happen on the GameThread
	if (PoolPrimeSize > 0 && MaxPoolSize > 0)
	{
		FNiagaraWorldManager::PrimePoolForAllWorlds(this);
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraSystem::UsesScript(const UNiagaraScript* Script) const
{
	EnsureFullyLoaded();
	if (SystemSpawnScript == Script ||
		SystemUpdateScript == Script)
	{
		return true;
	}

	for (FNiagaraEmitterHandle EmitterHandle : GetEmitterHandles())
	{
		if (EmitterHandle.GetInstance() && EmitterHandle.GetInstance()->UsesScript(Script))
		{
			return true;
		}
	}
	
	return false;
}

bool UNiagaraSystem::UsesEmitter(const UNiagaraEmitter* Emitter) const
{
	if (Emitter != nullptr)
	{
		for (const FNiagaraEmitterHandle& EmitterHandle : GetEmitterHandles())
		{
			if (EmitterHandle.UsesEmitter(*Emitter))
			{
				return true;
			}
		}
	}
	return false;
}

void UNiagaraSystem::RequestCompileForEmitter(UNiagaraEmitter* InEmitter)
{
	for (TObjectIterator<UNiagaraSystem> It; It; ++It)
	{
		UNiagaraSystem* Sys = *It;
		if (Sys && Sys->UsesEmitter(InEmitter))
		{
			Sys->RequestCompile(false);
		}
	}
}

void UNiagaraSystem::RecomputeExecutionOrderForEmitter(UNiagaraEmitter* InEmitter)
{
	for (TObjectIterator<UNiagaraSystem> It; It; ++It)
	{
		UNiagaraSystem* Sys = *It;
		if (Sys && Sys->UsesEmitter(InEmitter))
		{
			Sys->ComputeEmittersExecutionOrder();
		}
	}
}

void UNiagaraSystem::RecomputeExecutionOrderForDataInterface(class UNiagaraDataInterface* DataInterface)
{
	if (UNiagaraEmitter* Emitter = DataInterface->GetTypedOuter<UNiagaraEmitter>())
	{
		RecomputeExecutionOrderForEmitter(Emitter);
	}
	else
	{
		// In theory we should never hit this, but just incase let's handle it
		for (TObjectIterator<UNiagaraSystem> It; It; ++It)
		{
			if ( UNiagaraSystem* Sys = *It )
			{
				Sys->ComputeEmittersExecutionOrder();
			}
		}
	}
}

#endif

void UNiagaraSystem::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);

	if (Ar.CustomVer(FNiagaraCustomVersion::GUID) >= FNiagaraCustomVersion::ChangeEmitterCompiledDataToSharedRefs)
	{
		UScriptStruct* NiagaraEmitterCompiledDataStruct = FNiagaraEmitterCompiledData::StaticStruct();

		int32 EmitterCompiledDataNum = 0;
		if (Ar.IsSaving())
		{
			EmitterCompiledDataNum = EmitterCompiledData.Num();
		}
		Ar << EmitterCompiledDataNum;

		if (Ar.IsLoading())
		{
			// Clear out EmitterCompiledData when loading or else we will end up with duplicate entries. 
			EmitterCompiledData.Reset();
		}
		for (int32 EmitterIndex = 0; EmitterIndex < EmitterCompiledDataNum; ++EmitterIndex)
		{
			if (Ar.IsLoading())
			{
				EmitterCompiledData.Add(MakeShared<FNiagaraEmitterCompiledData>());
			}

			NiagaraEmitterCompiledDataStruct->SerializeTaggedProperties(Ar, (uint8*)&ConstCastSharedRef<FNiagaraEmitterCompiledData>(EmitterCompiledData[EmitterIndex]).Get(), NiagaraEmitterCompiledDataStruct, nullptr);
		}
	}

#if WITH_EDITOR
	if (GIsCookerLoadingPackage && Ar.IsLoading())
	{
		// start temp fix
		// we will disable the default behavior of baking out the rapid iteration parameters on cook if one of the emitters
		// is using the old experimental sim stages as FHlslNiagaraTranslator::RegisterFunctionCall hardcodes the use of the
		// symbolic constants that are being stripped out
		bool UsingOldSimStages = false;

		for (const FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			if (const UNiagaraEmitter* Emitter = EmitterHandle.GetInstance())
			{
				if (Emitter->bDeprecatedShaderStagesEnabled)
				{
					UsingOldSimStages = true;
					break;
				}
			}
		}

		bBakeOutRapidIterationOnCook = bBakeOutRapidIterationOnCook && !UsingOldSimStages;
		// end temp fix

		bBakeOutRapidIteration = bBakeOutRapidIteration || bBakeOutRapidIterationOnCook;
		bTrimAttributes = bTrimAttributes || bTrimAttributesOnCook;

		bDisableAllDebugSwitches = true;
	}
#endif
}

#if WITH_EDITOR

void UNiagaraSystem::PreEditChange(FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);

	if (PropertyThatWillChange && PropertyThatWillChange->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraSystem, EffectType))
	{
		UpdateContext.SetDestroyOnAdd(true);
		UpdateContext.Add(this, false);
	}
}

void UNiagaraSystem::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	ThumbnailImageOutOfDate = true;

	if (PropertyChangedEvent.Property != nullptr)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraSystem, WarmupTickCount))
		{
			//Set the WarmupTime to feed back to the user.
			WarmupTime = WarmupTickCount * WarmupTickDelta;
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraSystem, WarmupTime))
		{
			//Set the WarmupTickCount to feed back to the user.
			if (FMath::IsNearlyZero(WarmupTickDelta))
			{
				WarmupTickDelta = 0.0f;
			}
			else
			{
				WarmupTickCount = WarmupTime / WarmupTickDelta;
				WarmupTime = WarmupTickDelta * WarmupTickCount;
			}
		}
	}
	else
	{
		// User parameter values may have changed off of Undo/Redo, which calls this with a nullptr, so we need to propagate those. 
		// The editor may no longer be open, so we should do this within the system to properly propagate.
		ExposedParameters.PostGenericEditChange();
	}

	UpdateDITickFlags();
	UpdateHasGPUEmitters();
	ResolveScalabilitySettings();

	UpdateContext.CommitUpdate();

	static FName SkipReset = TEXT("SkipSystemResetOnChange");
	bool bPropertyHasSkip = PropertyChangedEvent.Property && PropertyChangedEvent.Property->HasMetaData(SkipReset);
	bool bMemberHasSkip = PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->HasMetaData(SkipReset);
	if (!bPropertyHasSkip && !bMemberHasSkip)
	{
		OnSystemPostEditChangeDelegate.Broadcast(this);
	}
}
#endif 

void UNiagaraSystem::PostLoad()
{
	Super::PostLoad();

	// Workaround for UE-104235 where a CDO loads a NiagaraSystem before the NiagaraModule has had a chance to load
	// We force the module to load here we makes sure the type registry, etc, is all setup in time.
	static bool bLoadChecked = false;
	if ( !bLoadChecked )
	{
		// We don't implement IsPostLoadThreadSafe so should be on the GT, but let's not assume.
		if ( ensure(IsInGameThread()) )
		{
			bLoadChecked = true;
			FModuleManager::LoadModuleChecked<INiagaraModule>("Niagara");
		}
	}

	ExposedParameters.PostLoad();
	ExposedParameters.SanityCheckData();

	SystemCompiledData.InstanceParamStore.PostLoad();

	if (GIsEditor)
	{
		SetFlags(RF_Transactional);
	}

	// Previously added emitters didn't have their stand alone and public flags cleared so
	// they 'leak' into the system package.  Clear the flags here so they can be collected
	// during the next save.
	UPackage* PackageOuter = Cast<UPackage>(GetOuter());
	if (PackageOuter != nullptr && HasAnyFlags(RF_Public | RF_Standalone))
	{
		TArray<UObject*> ObjectsInPackage;
		GetObjectsWithPackage(PackageOuter, ObjectsInPackage);
		for (UObject* ObjectInPackage : ObjectsInPackage)
		{
			UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(ObjectInPackage);
			if (Emitter != nullptr)
			{
				Emitter->ConditionalPostLoad();
				Emitter->ClearFlags(RF_Standalone | RF_Public);
			}
		}
	}

	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);
	if (NiagaraVer < FNiagaraCustomVersion::PlatformScalingRefactor)
	{
		for (int32 DL = 0; DL < ScalabilityOverrides_DEPRECATED.Num(); ++DL)
		{
			FNiagaraSystemScalabilityOverride& LegacyOverride = ScalabilityOverrides_DEPRECATED[DL];
			FNiagaraSystemScalabilityOverride& NewOverride = SystemScalabilityOverrides.Overrides.AddDefaulted_GetRef();
			NewOverride = LegacyOverride;
			NewOverride.Platforms = FNiagaraPlatformSet(FNiagaraPlatformSet::CreateQualityLevelMask(DL));
		}
	}

#if UE_EDITOR
	ExposedParameters.RecreateRedirections();
#endif

	for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
	{
#if WITH_EDITORONLY_DATA
		EmitterHandle.ConditionalPostLoad(NiagaraVer);
#else
		if (UNiagaraEmitter* NiagaraEmitter = EmitterHandle.GetInstance())
		{
			NiagaraEmitter->ConditionalPostLoad();
		}
#endif
	}

#if WITH_EDITORONLY_DATA
	if (EditorData == nullptr)
	{
		INiagaraModule& NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>("Niagara");
		EditorData = NiagaraModule.GetEditorOnlyDataUtilities().CreateDefaultEditorData(this);
	}
	else
	{
		EditorData->PostLoadFromOwner(this);
	}

	if (EditorParameters == nullptr)
	{
		INiagaraModule& NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>("Niagara");
		EditorParameters = NiagaraModule.GetEditorOnlyDataUtilities().CreateDefaultEditorParameters(this);
	}

	// see the equivalent in NiagaraEmitter for details
	if (bIsTemplateAsset_DEPRECATED)
	{
		TemplateSpecification = bIsTemplateAsset_DEPRECATED ? ENiagaraScriptTemplateSpecification::Template : ENiagaraScriptTemplateSpecification::None;
	}
#endif // WITH_EDITORONLY_DATA

#if !WITH_EDITOR
	// When running without the editor in a cooked build we run the update immediately in post load since
	// there will be no merging or compiling which makes it safe to do so.
	UpdateSystemAfterLoad();
#endif
}

#if WITH_EDITORONLY_DATA

UNiagaraEditorDataBase* UNiagaraSystem::GetEditorData()
{
	return EditorData;
}

const UNiagaraEditorDataBase* UNiagaraSystem::GetEditorData() const
{
	return EditorData;
}

UNiagaraEditorParametersAdapterBase* UNiagaraSystem::GetEditorParameters()
{
	return EditorParameters;
}

bool UNiagaraSystem::ReferencesInstanceEmitter(UNiagaraEmitter& Emitter)
{
	if (&Emitter == nullptr)
	{
		return false;
	}

	for (FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (&Emitter == Handle.GetInstance())
		{
			return true;
		}
	}
	return false;
}

void UNiagaraSystem::RefreshSystemParametersFromEmitter(const FNiagaraEmitterHandle& EmitterHandle)
{
	InitEmitterCompiledData();
	if (ensureMsgf(EmitterHandles.ContainsByPredicate([=](const FNiagaraEmitterHandle& OwnedEmitterHandle) { return OwnedEmitterHandle.GetId() == EmitterHandle.GetId(); }),
		TEXT("Can't refresh parameters from an emitter handle this system doesn't own.")))
	{
		if (EmitterHandle.GetInstance())
		{
			EmitterHandle.GetInstance()->EmitterSpawnScriptProps.Script->RapidIterationParameters.CopyParametersTo(
				SystemSpawnScript->RapidIterationParameters, false, FNiagaraParameterStore::EDataInterfaceCopyMethod::None);
			EmitterHandle.GetInstance()->EmitterUpdateScriptProps.Script->RapidIterationParameters.CopyParametersTo(
				SystemUpdateScript->RapidIterationParameters, false, FNiagaraParameterStore::EDataInterfaceCopyMethod::None);
		}
	}
}

void UNiagaraSystem::RemoveSystemParametersForEmitter(const FNiagaraEmitterHandle& EmitterHandle)
{
	InitEmitterCompiledData();
	if (ensureMsgf(EmitterHandles.ContainsByPredicate([=](const FNiagaraEmitterHandle& OwnedEmitterHandle) { return OwnedEmitterHandle.GetId() == EmitterHandle.GetId(); }),
		TEXT("Can't remove parameters for an emitter handle this system doesn't own.")))
	{
		if (EmitterHandle.GetInstance())
		{
			EmitterHandle.GetInstance()->EmitterSpawnScriptProps.Script->RapidIterationParameters.RemoveParameters(SystemSpawnScript->RapidIterationParameters);
			EmitterHandle.GetInstance()->EmitterUpdateScriptProps.Script->RapidIterationParameters.RemoveParameters(SystemUpdateScript->RapidIterationParameters);
		}
	}
}
#endif


const TArray<FNiagaraEmitterHandle>& UNiagaraSystem::GetEmitterHandles()
{
	return EmitterHandles;
}

const TArray<FNiagaraEmitterHandle>& UNiagaraSystem::GetEmitterHandles()const
{
	return EmitterHandles;
}

bool UNiagaraSystem::IsReadyToRunInternal() const
{
	//TODO: Ideally we'd never even load Niagara assets on the server but this is a larger issue. Tracked in FORT-342580
	if (!FApp::CanEverRender())
	{
		return false;
	}

	EnsureFullyLoaded();
	if (!SystemSpawnScript || !SystemUpdateScript)
	{
		if (FPlatformProperties::RequiresCookedData())
		{
			UE_LOG(LogNiagara, Warning, TEXT("%s IsReadyToRunInternal() failed due to missing SystemScript.  Spawn[%s] Update[%s]"),
				*GetFullName(),
				SystemSpawnScript ? *SystemSpawnScript->GetName() : TEXT("<none>"),
				SystemUpdateScript ? *SystemUpdateScript->GetName() : TEXT("<none>"));
		}

		return false;
	}

#if WITH_EDITORONLY_DATA
	if (HasOutstandingCompilationRequests())
	{
		return false;
	}

	/* Check that our post compile data is in sync with the current emitter handles count. If we have just added a new emitter handle, we will not have any outstanding compilation requests as the new compile
	 * will not be added to the outstanding compilation requests until the next tick.
	 */
	if (EmitterHandles.Num() != EmitterCompiledData.Num())
	{
		return false;
	}
#endif

	if (SystemSpawnScript->IsScriptCompilationPending(false) || 
		SystemUpdateScript->IsScriptCompilationPending(false))
	{
		return false;
	}

	const int32 EmitterCount = EmitterHandles.Num();
	for (int32 EmitterIt = 0; EmitterIt < EmitterCount; ++EmitterIt)
	{
		const FNiagaraEmitterHandle& Handle = EmitterHandles[EmitterIt];
		if (Handle.GetInstance() && !Handle.GetInstance()->IsReadyToRun())
		{
			if (FPlatformProperties::RequiresCookedData())
			{
				UE_LOG(LogNiagara, Warning, TEXT("%s IsReadyToRunInternal() failed due to Emitter not being ready to run.  Emitter #%d - %s"),
					*GetFullName(),
					EmitterIt,
					Handle.GetInstance() ? *Handle.GetInstance()->GetUniqueEmitterName() : TEXT("<none>"));
			}

			return false;
		}
	}

	// SystemSpawnScript and SystemUpdateScript needs to agree on the attributes of the datasets
	// Outside of DDC weirdness it's unclear how they can get out of sync, but this is a precaution to make sure that mismatched scripts won't run
	if (SystemSpawnScript->GetVMExecutableData().Attributes != SystemUpdateScript->GetVMExecutableData().Attributes)
	{
		if (FPlatformProperties::RequiresCookedData())
		{
			UE_LOG(LogNiagara, Warning, TEXT("%s IsReadyToRunInternal() failed due to mismatch between System spawn and update script attributes."), *GetFullName());
		}

		return false;
	}

	return true;
}

void UNiagaraSystem::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		EnsureFullyLoaded();
	}

#if WITH_EDITOR
	OutTags.Add(FAssetRegistryTag("HasGPUEmitter", HasAnyGPUEmitters() ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical));

	const float BoundsSize = FixedBounds.GetSize().GetMax();
	OutTags.Add(FAssetRegistryTag("FixedBoundsSize", bFixedBounds ? FString::Printf(TEXT("%.2f"), BoundsSize) : FString(TEXT("None")), FAssetRegistryTag::TT_Numerical));

	OutTags.Add(FAssetRegistryTag("NumEmitters", LexToString(EmitterHandles.Num()), FAssetRegistryTag::TT_Numerical));

	uint32 GPUSimsMissingFixedBounds = 0;

	// Gather up generic NumActive values
	uint32 NumActiveEmitters = 0;
	uint32 NumActiveRenderers = 0;
	TArray<const UNiagaraRendererProperties*> ActiveRenderers;
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetIsEnabled())
		{

			NumActiveEmitters++;
			const UNiagaraEmitter* Emitter = Handle.GetInstance();
			if (Emitter)
			{
				// Only register fixed bounds requirement for GPU if the system itself isn't fixed bounds.
				if (bFixedBounds == false && Emitter->bFixedBounds == false && Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
				{
					GPUSimsMissingFixedBounds++;
				}

				for (const UNiagaraRendererProperties* Props : Emitter->GetRenderers())
				{
					if (Props)
					{
						NumActiveRenderers++;
						ActiveRenderers.Add(Props);
					}
				}
			}
		}
	}

	OutTags.Add(FAssetRegistryTag("ActiveEmitters", LexToString(NumActiveEmitters), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("ActiveRenderers", LexToString(NumActiveRenderers), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("GPUSimsMissingFixedBounds", LexToString(GPUSimsMissingFixedBounds), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("EffectType", EffectType != nullptr ? EffectType->GetName() : FString(TEXT("None")), FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add(FAssetRegistryTag("WarmupTime", LexToString(WarmupTime), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag("HasOverrideScalabilityForSystem", bOverrideScalabilitySettings ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add(FAssetRegistryTag("HasDIsWithPostSimulateTick", bHasDIsWithPostSimulateTick ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add(FAssetRegistryTag("NeedsSortedSignificanceCull", bNeedsSortedSignificanceCull ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical));

	// Gather up NumActive emitters based off of quality level.
	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	if (Settings)
	{
		int32 NumQualityLevels = Settings->QualityLevels.Num();
		TArray<int32> QualityLevelsNumActive;
		QualityLevelsNumActive.AddZeroed(NumQualityLevels);

		for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
		{
			if (Handle.GetIsEnabled())
			{
				const UNiagaraEmitter* Emitter = Handle.GetInstance();
				if (Emitter)
				{
					for (int32 i = 0; i < NumQualityLevels; i++)
					{
						if (Emitter->Platforms.IsEffectQualityEnabled(i))
						{
							QualityLevelsNumActive[i]++;
						}
					}
				}
			}
		}

		for (int32 i = 0; i < NumQualityLevels; i++)
		{
			FString QualityLevelKey = Settings->QualityLevels[i].ToString() + TEXT("Emitters");
			OutTags.Add(FAssetRegistryTag(*QualityLevelKey, LexToString(QualityLevelsNumActive[i]), FAssetRegistryTag::TT_Numerical));
		}
	}


	TMap<FName, uint32> NumericKeys;
	TMap<FName, FString> StringKeys;

	// Gather up custom asset tags for  RendererProperties
	{
		TArray<UClass*> RendererClasses;
		GetDerivedClasses(UNiagaraRendererProperties::StaticClass(), RendererClasses);

		for (UClass* RendererClass : RendererClasses)
		{
			const UNiagaraRendererProperties* PropDefault = RendererClass->GetDefaultObject< UNiagaraRendererProperties>();
			if (PropDefault)
			{
				PropDefault->GetAssetTagsForContext(this, ActiveRenderers, NumericKeys, StringKeys);
			}
		}
	}

	// Gather up custom asset tags for DataInterfaces
	{
		TArray<const UNiagaraDataInterface*> DataInterfaces;
		auto AddDIs = [&](UNiagaraScript* Script)
		{
			if (Script)
			{
				for (FNiagaraScriptDataInterfaceCompileInfo& Info : Script->GetVMExecutableData().DataInterfaceInfo)
				{
					UNiagaraDataInterface* DefaultDataInterface = Info.GetDefaultDataInterface();
					DataInterfaces.AddUnique(DefaultDataInterface);
				}
			}
		};

		AddDIs(SystemSpawnScript);
		AddDIs(SystemUpdateScript);
		for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
		{
			if (Handle.GetIsEnabled())
			{
				if (UNiagaraEmitter* Emitter = Handle.GetInstance())
				{
					TArray<UNiagaraScript*> Scripts;
					Emitter->GetScripts(Scripts);
					for (UNiagaraScript* Script : Scripts)
					{
						AddDIs(Script);
					}
				}
			}
		}

		TArray<UClass*> DIClasses;
		GetDerivedClasses(UNiagaraDataInterface::StaticClass(), DIClasses);

		for (UClass* DIClass : DIClasses)
		{
			const UNiagaraDataInterface* PropDefault = DIClass->GetDefaultObject< UNiagaraDataInterface>();
			if (PropDefault)
			{
				PropDefault->GetAssetTagsForContext(this, DataInterfaces, NumericKeys, StringKeys);
			}
		}
		OutTags.Add(FAssetRegistryTag("ActiveDIs", LexToString(DataInterfaces.Num()), FAssetRegistryTag::TT_Numerical));
	}


	// Now propagate the custom numeric and string tags from the DataInterfaces and RendererProperties above
	auto NumericIter = NumericKeys.CreateConstIterator();
	while (NumericIter)
	{

		OutTags.Add(FAssetRegistryTag(NumericIter.Key(), LexToString(NumericIter.Value()), FAssetRegistryTag::TT_Numerical));
		++NumericIter;
	}

	auto StringIter = StringKeys.CreateConstIterator();
	while (StringIter)
	{

		OutTags.Add(FAssetRegistryTag(StringIter.Key(), LexToString(StringIter.Value()), FAssetRegistryTag::TT_Alphabetical));
		++StringIter;
	}

	// TemplateSpecialization
	FName TemplateSpecificationName = GET_MEMBER_NAME_CHECKED(UNiagaraSystem, TemplateSpecification);
	FText TemplateSpecializationValueString = StaticEnum<ENiagaraScriptTemplateSpecification>()->GetDisplayNameTextByValue((int64) TemplateSpecification);
	OutTags.Add(FAssetRegistryTag(TemplateSpecificationName, TemplateSpecializationValueString.ToString(), FAssetRegistryTag::TT_Alphabetical));

	/*for (const UNiagaraDataInterface* DI : DataInterfaces)
	{
		FString ClassName;
		DI->GetClass()->GetName(ClassName);
		OutTags.Add(FAssetRegistryTag(*(TEXT("bHas")+ClassName), TEXT("True"), FAssetRegistryTag::TT_Alphabetical));
	}*/


	//OutTags.Add(FAssetRegistryTag("CPUCollision", UsesCPUCollision() ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical));
	//OutTags.Add(FAssetRegistryTag("Looping", bAnyEmitterLoopsForever ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical));
	//OutTags.Add(FAssetRegistryTag("Immortal", IsImmortal() ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical));
	//OutTags.Add(FAssetRegistryTag("Becomes Zombie", WillBecomeZombie() ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical));
	//OutTags.Add(FAssetRegistryTag("CanBeOccluded", OcclusionBoundsMethod == EParticleSystemOcclusionBoundsMethod::EPSOBM_None ? TEXT("False") : TEXT("True"), FAssetRegistryTag::TT_Alphabetical));

#endif
	Super::GetAssetRegistryTags(OutTags);
}

#if WITH_EDITORONLY_DATA
bool UNiagaraSystem::HasOutstandingCompilationRequests(bool bIncludingGPUShaders) const
{
	if (ActiveCompilations.Num() > 0)
	{
		return true;
	}

	// the above check only handles the VM script generation, and so GPU compute script compilation can still
	// be underway, so we'll check for that explicitly, only when needed, so that we don't burden the user with excessive compiles
	if (bIncludingGPUShaders)
	{
		for (const FNiagaraEmitterHandle& EmitterHandle : GetEmitterHandles())
		{
			if (const UNiagaraEmitter* Emitter = EmitterHandle.GetInstance())
			{
				if (const UNiagaraScript* GPUComputeScript = Emitter->GetGPUComputeScript())
				{
					if (const FNiagaraShaderScript* ShaderScript = GPUComputeScript->GetRenderThreadScript())
					{
						if (!ShaderScript->IsCompilationFinished())
						{
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}
#endif

bool UNiagaraSystem::ComputeEmitterPriority(int32 EmitterIdx, TArray<int32, TInlineAllocator<32>>& EmitterPriorities, const TBitArray<TInlineAllocator<32>>& EmitterDependencyGraph)
{
	// Mark this node as being evaluated.
	EmitterPriorities[EmitterIdx] = 0;

	int32 MaxPriority = 0;

	// Examine all the nodes we depend on. We must run after all of them, so our priority
	// will be 1 higher than the maximum priority of all our dependencies.
	const int32 NumEmitters = EmitterHandles.Num();
	int32 DepStartIndex = EmitterIdx * NumEmitters;
	TConstSetBitIterator<TInlineAllocator<32>> DepIt(EmitterDependencyGraph, DepStartIndex);
	while (DepIt.GetIndex() < DepStartIndex + NumEmitters)
	{
		int32 OtherEmitterIdx = DepIt.GetIndex() - DepStartIndex;

		// This can't happen, because we explicitly skip self-dependencies when building the edge table.
		checkSlow(OtherEmitterIdx != EmitterIdx);

		if (EmitterPriorities[OtherEmitterIdx] == 0)
		{
			// This node is currently being evaluated, which means we've found a cycle.
			return false;
		}

		if (EmitterPriorities[OtherEmitterIdx] < 0)
		{
			// Node not evaluated yet, recurse.
			if (!ComputeEmitterPriority(OtherEmitterIdx, EmitterPriorities, EmitterDependencyGraph))
			{
				return false;
			}
		}

		if (MaxPriority < EmitterPriorities[OtherEmitterIdx])
		{
			MaxPriority = EmitterPriorities[OtherEmitterIdx];
		}

		++DepIt;
	}

	EmitterPriorities[EmitterIdx] = MaxPriority + 1;
	return true;
}

void UNiagaraSystem::FindEventDependencies(UNiagaraEmitter* Emitter, TArray<UNiagaraEmitter*>& Dependencies)
{
	if (!Emitter)
	{
		return;
	}

	const TArray<FNiagaraEventScriptProperties>& EventHandlers = Emitter->GetEventHandlers();
	for (const FNiagaraEventScriptProperties& Handler : EventHandlers)
	{
		// An empty ID means the event reads from the same emitter, so we don't need to record a dependency.
		if (!Handler.SourceEmitterID.IsValid())
		{
			continue;
		}

		// Look for the ID in the list of emitter handles from the system object.
		FString SourceEmitterIDName = Handler.SourceEmitterID.ToString();
		for (int EmitterIdx = 0; EmitterIdx < EmitterHandles.Num(); ++EmitterIdx)
		{
			FName EmitterIDName = EmitterHandles[EmitterIdx].GetIdName();
			if (EmitterIDName.ToString() == SourceEmitterIDName)
			{
				// The Emitters array is in the same order as the EmitterHandles array.
				UNiagaraEmitter* Sender = EmitterHandles[EmitterIdx].GetInstance();
				Dependencies.Add(Sender);
				break;
			}
		}
	}
}

void UNiagaraSystem::FindDataInterfaceDependencies(UNiagaraEmitter* Emitter, UNiagaraScript* Script, TArray<UNiagaraEmitter*>& Dependencies)
{
	if (const FNiagaraScriptExecutionParameterStore* ParameterStore = Script->GetExecutionReadyParameterStore(Emitter->SimTarget))
	{
		if (Emitter->SimTarget == ENiagaraSimTarget::CPUSim)
		{
			for (UNiagaraDataInterface* DataInterface : ParameterStore->GetDataInterfaces())
			{
				DataInterface->GetEmitterDependencies(this, Dependencies);
			}
		}
		else
		{
			const TArray<UNiagaraDataInterface*>& StoreDataInterfaces = ParameterStore->GetDataInterfaces();
			if (StoreDataInterfaces.Num() > 0)
			{
				auto FindCachedDefaultDI =
					[](UNiagaraScript* Script, const FNiagaraVariable& Variable) -> UNiagaraDataInterface*
				{
					if (Script)
					{
						for (FNiagaraScriptDataInterfaceInfo& DataInterfaceInfo : Script->GetCachedDefaultDataInterfaces())
						{
							if ((Variable.GetType() == DataInterfaceInfo.Type) && (Variable.GetName() == DataInterfaceInfo.RegisteredParameterMapWrite))
							{
								return DataInterfaceInfo.DataInterface;
							}
						}
					}
					return nullptr;
				};

				for (const FNiagaraVariableWithOffset& Variable : ParameterStore->ReadParameterVariables())
				{
					if (!Variable.IsDataInterface())
					{
						continue;
					}

					if (UNiagaraDataInterface* DefaultDI = FindCachedDefaultDI(SystemSpawnScript, Variable))
					{
						DefaultDI->GetEmitterDependencies(this, Dependencies);
						continue;
					}

					if (UNiagaraDataInterface* DefaultDI = FindCachedDefaultDI(SystemUpdateScript, Variable))
					{
						DefaultDI->GetEmitterDependencies(this, Dependencies);
						continue;
					}

					StoreDataInterfaces[Variable.Offset]->GetEmitterDependencies(this, Dependencies);
				}
			}
		}
	}
}

void UNiagaraSystem::ComputeEmittersExecutionOrder()
{
	const int32 NumEmitters = EmitterHandles.Num();

	TArray<int32, TInlineAllocator<32>> EmitterPriorities;
	TBitArray<TInlineAllocator<32>> EmitterDependencyGraph;

	EmitterExecutionOrder.SetNum(NumEmitters);
	EmitterPriorities.SetNum(NumEmitters);
	EmitterDependencyGraph.Init(false, NumEmitters * NumEmitters);

	TArray<UNiagaraEmitter*> EmitterDependencies;
	EmitterDependencies.Reserve(3 * NumEmitters);

	RendererPostTickOrder.Reset();
	RendererCompletionOrder.Reset();

	bool bHasEmitterDependencies = false;
	uint32 SystemRendererIndex = 0;
	for (int32 EmitterIdx = 0; EmitterIdx < NumEmitters; ++EmitterIdx)
	{
		const FNiagaraEmitterHandle& EmitterHandle = EmitterHandles[EmitterIdx];
		UNiagaraEmitter* Emitter = EmitterHandle.GetInstance();

		EmitterExecutionOrder[EmitterIdx].EmitterIndex = EmitterIdx;
		EmitterPriorities[EmitterIdx] = -1;

		if (Emitter == nullptr)
		{
			continue;
		}

		if (!EmitterHandle.GetIsEnabled())
		{
			Emitter->ForEachEnabledRenderer([&] (const UNiagaraRendererProperties*) { ++SystemRendererIndex; });
			continue;
		}

		EmitterDependencies.SetNum(0, false);

		if (Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && Emitter->GetGPUComputeScript())
		{
			// GPU emitters have a combined execution context for spawn and update.
			FindDataInterfaceDependencies(Emitter, Emitter->GetGPUComputeScript(), EmitterDependencies);
		}
		else
		{
			// CPU emitters have separate contexts for spawn and update, so we need to gather DIs from both. They also support events,
			// so we need to look at the event sources for extra dependencies.
			FindDataInterfaceDependencies(Emitter, Emitter->SpawnScriptProps.Script, EmitterDependencies);
			FindDataInterfaceDependencies(Emitter, Emitter->UpdateScriptProps.Script, EmitterDependencies);
			FindEventDependencies(Emitter, EmitterDependencies);
		}

		// Map the pointers returned by the emitter to indices inside the Emitters array. This is O(N^2), but we expect
		// to have few dependencies, so in practice it should be faster than a TMap. If it gets out of hand, we can also
		// ask the DIs to give us indices directly, since they probably got the pointers by scanning the array we gave them
		// through GetEmitters() anyway.
		for (int32 DepIdx = 0; DepIdx < EmitterDependencies.Num(); ++DepIdx)
		{
			for (int32 OtherEmitterIdx = 0; OtherEmitterIdx < NumEmitters; ++OtherEmitterIdx)
			{
				if (EmitterDependencies[DepIdx] == EmitterHandles[OtherEmitterIdx].GetInstance())
				{
					const bool HasSourceEmitter = EmitterHandles[EmitterIdx].GetInstance() != nullptr;
					const bool HasDependentEmitter = EmitterHandles[OtherEmitterIdx].GetInstance() != nullptr;

					// check to see if the emitter we're dependent on may have been culled during the cook
					if (HasSourceEmitter && !HasDependentEmitter)
					{
						UE_LOG(LogNiagara, Error, TEXT("Emitter[%s] depends on Emitter[%s] which is not available (has scalability removed it during a cook?)."),
							*EmitterHandles[EmitterIdx].GetName().ToString(),
							*EmitterHandles[OtherEmitterIdx].GetName().ToString());
					}

					// Some DIs might read from the same emitter they're applied to. We don't care about dependencies on self.
					if (EmitterIdx != OtherEmitterIdx)
					{
						EmitterDependencyGraph.SetRange(EmitterIdx * NumEmitters + OtherEmitterIdx, 1, true);
						bHasEmitterDependencies = true;
					}
					break;
				}
			}
		}

		// Determine renderer execution order for PostTick and Completion for any renderers that opt into it
		for (int32 RendererIndex = 0; RendererIndex < Emitter->GetRenderers().Num(); ++RendererIndex)
		{
			const UNiagaraRendererProperties* Renderer = Emitter->GetRenderers()[RendererIndex];
			if (Renderer && Renderer->GetIsEnabled() && Renderer->IsSimTargetSupported(Emitter->SimTarget))
			{
				FNiagaraRendererExecutionIndex ExecutionIndex;
				ExecutionIndex.EmitterIndex = EmitterIdx;
				ExecutionIndex.EmitterRendererIndex = RendererIndex;
				ExecutionIndex.SystemRendererIndex = SystemRendererIndex;

				if (Renderer->NeedsSystemPostTick())
				{
					RendererPostTickOrder.Add(ExecutionIndex);
				}
				if (Renderer->NeedsSystemCompletion())
				{
					RendererCompletionOrder.Add(ExecutionIndex);
				}
				++SystemRendererIndex;
			}
		}
	}

	if (bHasEmitterDependencies)
	{
		for (int32 EmitterIdx = 0; EmitterIdx < NumEmitters; ++EmitterIdx)
		{
			if (EmitterPriorities[EmitterIdx] < 0)
			{
				if (!ComputeEmitterPriority(EmitterIdx, EmitterPriorities, EmitterDependencyGraph))
				{
					FName EmitterName = EmitterHandles[EmitterIdx].GetName();
					UE_LOG(LogNiagara, Error, TEXT("Found circular dependency involving emitter '%s' in system '%s'. The execution order will be undefined."), *EmitterName.ToString(), *GetName());
					break;
				}
			}
		}

		// Sort the emitter indices in the execution order array so that dependencies are satisfied.
		Algo::Sort(EmitterExecutionOrder, [&EmitterPriorities](FNiagaraEmitterExecutionIndex IdxA, FNiagaraEmitterExecutionIndex IdxB) { return EmitterPriorities[IdxA.EmitterIndex] < EmitterPriorities[IdxB.EmitterIndex]; });

		// Emitters with the same priority value can execute in parallel. Look for the emitters where the priority increases and mark them as needing to start a new
		// overlap group. This informs the execution code about where to insert synchronization points to satisfy data dependencies.
		// Note that we don't want to set the flag on the first emitter, since on the GPU all the systems are bunched together, and we don't mind overlapping the
		// first emitter from a system with the previous emitters from a different system, as we don't have inter-system dependencies.
		int32 PrevIdx = EmitterExecutionOrder[0].EmitterIndex;
		for (int32 i = 1; i < EmitterExecutionOrder.Num(); ++i)
		{
			int32 CurrentIdx = EmitterExecutionOrder[i].EmitterIndex;
			// A bit of paranoia never hurt anyone. Check that the priorities are monotonically increasing.
			checkSlow(EmitterPriorities[PrevIdx] <= EmitterPriorities[CurrentIdx]);
			if (EmitterPriorities[PrevIdx] != EmitterPriorities[CurrentIdx])
			{
				EmitterExecutionOrder[i].bStartNewOverlapGroup = true;
			}
			PrevIdx = CurrentIdx;
		}
	}

	// go through and remove any entries in the EmitterExecutionOrder array for emitters where we don't have a CachedEmitter, they have
	// likely been cooked out because of scalability
	EmitterExecutionOrder.SetNum(Algo::StableRemoveIf(EmitterExecutionOrder, [this](FNiagaraEmitterExecutionIndex EmitterExecIdx)
	{
		return EmitterHandles[EmitterExecIdx.EmitterIndex].GetInstance() == nullptr;
	}));
}

void UNiagaraSystem::ComputeRenderersDrawOrder()
{
	struct FSortInfo
	{
		FSortInfo(int32 InSortHint, int32 InRendererIdx) : SortHint(InSortHint), RendererIdx(InRendererIdx) {}
		int32 SortHint;
		int32 RendererIdx;
	};
	TArray<FSortInfo, TInlineAllocator<8>> RendererSortInfo;

	for (const FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
	{
		if (UNiagaraEmitter* Emitter = EmitterHandle.GetInstance())
		{
			Emitter->ForEachEnabledRenderer(
				[&](UNiagaraRendererProperties* Properties)
				{
					RendererSortInfo.Emplace(Properties->SortOrderHint, RendererSortInfo.Num());
				}
			);
		}
	}

	// We sort by the sort hint in order to guarantee that we submit according to the preferred sort order..
	RendererSortInfo.Sort([](const FSortInfo& A, const FSortInfo& B) { return A.SortHint < B.SortHint; });

	RendererDrawOrder.Reset(RendererSortInfo.Num());

	for (const FSortInfo& SortInfo : RendererSortInfo)
	{
		RendererDrawOrder.Add(SortInfo.RendererIdx);
	}
}

void UNiagaraSystem::CacheFromCompiledData()
{
	const FNiagaraDataSetCompiledData& SystemDataSet = SystemCompiledData.DataSetCompiledData;

	// Cache system data accessors
	static const FName NAME_System_ExecutionState = "System.ExecutionState";
	SystemExecutionStateAccessor.Init(SystemDataSet, NAME_System_ExecutionState);

	// Cache emitter data set accessors
	EmitterSpawnInfoAccessors.Reset();
	EmitterExecutionStateAccessors.Reset();
	EmitterSpawnInfoAccessors.SetNum(GetNumEmitters());

	// reset the MaxDeltaTime so we get the most up to date values from the emitters
	MaxDeltaTime.Reset();

	TStringBuilder<128> ExecutionStateNameBuilder;
	for (int32 i=0; i < EmitterHandles.Num(); ++i)
	{
		FNiagaraEmitterHandle& Handle = EmitterHandles[i];
		UNiagaraEmitter* NiagaraEmitter = EmitterHandles[i].GetInstance();
		if (Handle.GetIsEnabled() && NiagaraEmitter)
		{
			// Cache system instance accessors
			ExecutionStateNameBuilder.Reset();
			ExecutionStateNameBuilder << NiagaraEmitter->GetUniqueEmitterName();
			ExecutionStateNameBuilder << TEXT(".ExecutionState");
			const FName ExecutionStateName(ExecutionStateNameBuilder.ToString());

			EmitterExecutionStateAccessors.AddDefaulted_GetRef().Init(SystemDataSet, ExecutionStateName);

			// Cache emitter data set accessors, for things like bounds, etc
			const FNiagaraDataSetCompiledData* DataSetCompiledData = nullptr;
			if (EmitterCompiledData.IsValidIndex(i))
			{
				for (const FName& SpawnName : EmitterCompiledData[i]->SpawnAttributes)
				{
					EmitterSpawnInfoAccessors[i].Emplace(SystemDataSet, SpawnName);
				}

				DataSetCompiledData = &EmitterCompiledData[i]->DataSetCompiledData;

				if (NiagaraEmitter->bLimitDeltaTime)
				{
					MaxDeltaTime = MaxDeltaTime.IsSet() ? FMath::Min(MaxDeltaTime.GetValue(), NiagaraEmitter->MaxDeltaTimePerTick) : NiagaraEmitter->MaxDeltaTimePerTick;
				}
			}
			NiagaraEmitter->ConditionalPostLoad();
			NiagaraEmitter->CacheFromCompiledData(DataSetCompiledData);
		}
		else
		{
			EmitterExecutionStateAccessors.AddDefaulted();
		}
	}
}

bool UNiagaraSystem::HasSystemScriptDIsWithPerInstanceData() const
{
	return bHasSystemScriptDIsWithPerInstanceData;
}

const TArray<FName>& UNiagaraSystem::GetUserDINamesReadInSystemScripts() const
{
	return UserDINamesReadInSystemScripts;
}

FBox UNiagaraSystem::GetFixedBounds() const
{
	return FixedBounds;
}

void CheckDICompileInfo(const TArray<FNiagaraScriptDataInterfaceCompileInfo>& ScriptDICompileInfos, bool& bOutbHasSystemDIsWithPerInstanceData, TArray<FName>& OutUserDINamesReadInSystemScripts)
{
	for (const FNiagaraScriptDataInterfaceCompileInfo& ScriptDICompileInfo : ScriptDICompileInfos)
	{
		UNiagaraDataInterface* DefaultDataInterface = ScriptDICompileInfo.GetDefaultDataInterface();
		if (DefaultDataInterface != nullptr && DefaultDataInterface->PerInstanceDataSize() > 0)
		{
			bOutbHasSystemDIsWithPerInstanceData = true;
		}

		if (ScriptDICompileInfo.RegisteredParameterMapRead.ToString().StartsWith(TEXT("User.")))
		{
			OutUserDINamesReadInSystemScripts.AddUnique(ScriptDICompileInfo.RegisteredParameterMapRead);
		}
	}
}

void UNiagaraSystem::UpdatePostCompileDIInfo()
{
	bHasSystemScriptDIsWithPerInstanceData = false;
	UserDINamesReadInSystemScripts.Empty();
	bNeedsGPUContextInitForDataInterfaces = false;

	CheckDICompileInfo(SystemSpawnScript->GetVMExecutableData().DataInterfaceInfo, bHasSystemScriptDIsWithPerInstanceData, UserDINamesReadInSystemScripts);
	CheckDICompileInfo(SystemUpdateScript->GetVMExecutableData().DataInterfaceInfo, bHasSystemScriptDIsWithPerInstanceData, UserDINamesReadInSystemScripts);

	for (const FNiagaraEmitterHandle& EmitterHandle : GetEmitterHandles())
	{
		if (EmitterHandle.GetIsEnabled() == false || !EmitterHandle.GetInstance())
		{
			continue;
		}
		if (EmitterHandle.GetInstance()->SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			UNiagaraScript* GPUScript = EmitterHandle.GetInstance()->GetGPUComputeScript();
			if (GPUScript)
			{
				FNiagaraVMExecutableData& VMData = GPUScript->GetVMExecutableData();
				if (VMData.IsValid() && VMData.bNeedsGPUContextInit)
				{
					bNeedsGPUContextInitForDataInterfaces = true;
				}
			}
		}
	}
}

void UNiagaraSystem::UpdateDITickFlags()
{
	bHasDIsWithPostSimulateTick = false;
	auto CheckPostSimTick = [&](UNiagaraScript* Script)
	{
		if (Script)
		{
			for (FNiagaraScriptDataInterfaceCompileInfo& Info : Script->GetVMExecutableData().DataInterfaceInfo)
			{
				UNiagaraDataInterface* DefaultDataInterface = Info.GetDefaultDataInterface();
				if (DefaultDataInterface && DefaultDataInterface->HasPostSimulateTick())
				{
					bHasDIsWithPostSimulateTick |= true;
				}
			}
		}
	};

	CheckPostSimTick(SystemSpawnScript);
	CheckPostSimTick(SystemUpdateScript);
	for (FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetIsEnabled())
		{
			if (UNiagaraEmitter* Emitter = Handle.GetInstance())
			{
				TArray<UNiagaraScript*> Scripts;
				Emitter->GetScripts(Scripts);
				for (UNiagaraScript* Script : Scripts)
				{
					CheckPostSimTick(Script);
				}
			}
		}
	}
}

void UNiagaraSystem::UpdateHasGPUEmitters()
{
	bHasAnyGPUEmitters = 0;
	for (FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetIsEnabled())
		{
			if (UNiagaraEmitter* Emitter = Handle.GetInstance())
			{
				bHasAnyGPUEmitters |= Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim;
			}
		}
	}
}

bool UNiagaraSystem::IsValidInternal() const
{
	if (!SystemSpawnScript || !SystemUpdateScript)
	{
		return false;
	}

	if ((!SystemSpawnScript->IsScriptCompilationPending(false) && !SystemSpawnScript->DidScriptCompilationSucceed(false)) ||
		(!SystemUpdateScript->IsScriptCompilationPending(false) && !SystemUpdateScript->DidScriptCompilationSucceed(false)))
	{
		return false;
	}

	if (EmitterHandles.Num() == 0)
	{
		return false;
	}

	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetIsEnabled() && Handle.GetInstance() && !Handle.GetInstance()->IsValid())
		{
			return false;
		}
	}

	return true;
}

void UNiagaraSystem::EnsureFullyLoaded() const
{
	UNiagaraSystem* System = const_cast<UNiagaraSystem*>(this);
	System->UpdateSystemAfterLoad();
}


bool UNiagaraSystem::CanObtainEmitterAttribute(const FNiagaraVariableBase& InVarWithUniqueNameNamespace) const
{
	if (SystemSpawnScript)
		return SystemSpawnScript->GetVMExecutableData().Attributes.Contains(InVarWithUniqueNameNamespace);
	return false;
}
bool UNiagaraSystem::CanObtainSystemAttribute(const FNiagaraVariableBase& InVar) const
{
	if (SystemSpawnScript)
		return SystemSpawnScript->GetVMExecutableData().Attributes.Contains(InVar);
	return false;
}
bool UNiagaraSystem::CanObtainUserVariable(const FNiagaraVariableBase& InVar) const
{
	return ExposedParameters.IndexOf(InVar) != INDEX_NONE;
}

#if WITH_EDITORONLY_DATA

FNiagaraEmitterHandle UNiagaraSystem::AddEmitterHandle(UNiagaraEmitter& InEmitter, FName EmitterName)
{
	UNiagaraEmitter* NewEmitter = UNiagaraEmitter::CreateWithParentAndOwner(InEmitter, this, EmitterName, ~(RF_Public | RF_Standalone));
	FNiagaraEmitterHandle EmitterHandle(*NewEmitter);
	if (InEmitter.TemplateSpecification == ENiagaraScriptTemplateSpecification::Template || InEmitter.TemplateSpecification == ENiagaraScriptTemplateSpecification::Behavior)
	{
		NewEmitter->TemplateSpecification = ENiagaraScriptTemplateSpecification::None;
		NewEmitter->TemplateAssetDescription = FText();
		NewEmitter->RemoveParent();
	}
	EmitterHandles.Add(EmitterHandle);
	RefreshSystemParametersFromEmitter(EmitterHandle);
	return EmitterHandle;
}

void UNiagaraSystem::AddEmitterHandleDirect(FNiagaraEmitterHandle& EmitterHandle)
{
	EmitterHandles.Add(EmitterHandle);
	RefreshSystemParametersFromEmitter(EmitterHandle);
}

FNiagaraEmitterHandle UNiagaraSystem::DuplicateEmitterHandle(const FNiagaraEmitterHandle& EmitterHandleToDuplicate, FName EmitterName)
{
	UNiagaraEmitter* DuplicateEmitter = UNiagaraEmitter::CreateAsDuplicate(*EmitterHandleToDuplicate.GetInstance(), EmitterName, *this);
	FNiagaraEmitterHandle EmitterHandle(*DuplicateEmitter);
	EmitterHandle.SetIsEnabled(EmitterHandleToDuplicate.GetIsEnabled(), *this, false);
	EmitterHandles.Add(EmitterHandle);
	RefreshSystemParametersFromEmitter(EmitterHandle);
	return EmitterHandle;
}

void UNiagaraSystem::RemoveEmitterHandle(const FNiagaraEmitterHandle& EmitterHandleToDelete)
{
	UNiagaraEmitter* EditableEmitter = EmitterHandleToDelete.GetInstance();
	RemoveSystemParametersForEmitter(EmitterHandleToDelete);
	auto RemovePredicate = [&](const FNiagaraEmitterHandle& EmitterHandle) { return EmitterHandle.GetId() == EmitterHandleToDelete.GetId(); };
	EmitterHandles.RemoveAll(RemovePredicate);
}

void UNiagaraSystem::RemoveEmitterHandlesById(const TSet<FGuid>& HandlesToRemove)
{
	auto RemovePredicate = [&](const FNiagaraEmitterHandle& EmitterHandle)
	{
		return HandlesToRemove.Contains(EmitterHandle.GetId());
	};
	EmitterHandles.RemoveAll(RemovePredicate);

	InitEmitterCompiledData();
}
#endif


UNiagaraScript* UNiagaraSystem::GetSystemSpawnScript()
{
	return SystemSpawnScript;
}

UNiagaraScript* UNiagaraSystem::GetSystemUpdateScript()
{
	return SystemUpdateScript;
}

const UNiagaraScript* UNiagaraSystem::GetSystemSpawnScript() const
{
	return SystemSpawnScript;
}

const UNiagaraScript* UNiagaraSystem::GetSystemUpdateScript() const
{
	return SystemUpdateScript;
}

#if WITH_EDITORONLY_DATA

bool UNiagaraSystem::GetIsolateEnabled() const
{
	return bIsolateEnabled;
}

void UNiagaraSystem::SetIsolateEnabled(bool bIsolate)
{
	bIsolateEnabled = bIsolate;
}

UNiagaraSystem::FOnSystemCompiled& UNiagaraSystem::OnSystemCompiled()
{
	return OnSystemCompiledDelegate;
}

UNiagaraSystem::FOnSystemPostEditChange& UNiagaraSystem::OnSystemPostEditChange()
{
	return OnSystemPostEditChangeDelegate;
}

void UNiagaraSystem::ForceGraphToRecompileOnNextCheck()
{
	check(SystemSpawnScript->GetLatestSource() == SystemUpdateScript->GetLatestSource());
	SystemSpawnScript->GetLatestSource()->ForceGraphToRecompileOnNextCheck();

	for (FNiagaraEmitterHandle Handle : EmitterHandles)
	{
		if (Handle.GetInstance())
		{
			UNiagaraScriptSourceBase* GraphSource = Handle.GetInstance()->GraphSource;
			GraphSource->ForceGraphToRecompileOnNextCheck();
		}
	}
}

void UNiagaraSystem::WaitForCompilationComplete(bool bIncludingGPUShaders, bool bShowProgress)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WaitForNiagaraCompilation);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*GetPathName(), NiagaraChannel);

	// Calculate the slow progress for notifying via UI
	TArray<FNiagaraShaderScript*, TInlineAllocator<16>> GPUScripts;
	if (bIncludingGPUShaders)
	{
		for (FNiagaraEmitterHandle& EmitterHandle : EmitterHandles)
		{
			if (UNiagaraEmitter* Emitter = EmitterHandle.GetInstance())
			{
				if (UNiagaraScript* GPUComputeScript = Emitter->GetGPUComputeScript())
				{
					if (FNiagaraShaderScript* ShaderScript = GPUComputeScript->GetRenderThreadScript())
					{
						if (!ShaderScript->IsCompilationFinished())
							GPUScripts.Add(ShaderScript);
					}
				}
			}
		}
	}
	
	const int32 TotalCompiles = ActiveCompilations.Num() + GPUScripts.Num();
	FScopedSlowTask Progress(TotalCompiles, LOCTEXT("WaitingForCompile", "Waiting for compilation to complete"));
	if (bShowProgress && TotalCompiles > 0)
	{
		Progress.MakeDialog();
	}

	while (ActiveCompilations.Num() > 0)
	{
		if (QueryCompileComplete(true, ActiveCompilations.Num() == 1))
		{
			// make sure to only mark progress if we actually have accomplished something in the QueryCompileComplete
			Progress.EnterProgressFrame();
		}
	}
	
	for (FNiagaraShaderScript* ShaderScript : GPUScripts)
	{
		Progress.EnterProgressFrame();
		ShaderScript->FinishCompilation();
	}
}

void UNiagaraSystem::InvalidateActiveCompiles()
{
	for (FNiagaraSystemCompileRequest& ActiveCompilation : ActiveCompilations)
	{
		ActiveCompilation.bIsValid = false;
	}
}

bool UNiagaraSystem::PollForCompilationComplete()
{
	if (ActiveCompilations.Num() > 0)
	{
		return QueryCompileComplete(false, true);
	}
	return true;
}

bool InternalCompileGuardCheck(void* TestValue)
{
	// We need to make sure that we don't re-enter this function on the same thread as it might update things behind our backs.
	// Am slightly concerened about PostLoad happening on a worker thread, so am not using a generic static variable here, just
	// a thread local storage variable. The initialized TLS value should be nullptr. When we are doing a compile request, we 
	// will set the TLS to our this pointer. If the TLS is already this when requesting a compile, we will just early out.
	if (!CompileGuardSlot)
	{
		CompileGuardSlot = FPlatformTLS::AllocTlsSlot();
	}
	check(CompileGuardSlot != 0);
	bool bCompileGuardInProgress = FPlatformTLS::GetTlsValue(CompileGuardSlot) == TestValue;
	return bCompileGuardInProgress;
}

bool UNiagaraSystem::CompilationResultsValid(FNiagaraSystemCompileRequest& CompileRequest) const
{
	// for now the only thing we're concerned about is if we've got results for SystemSpawn and SystemUpdate scripts
	// then we need to make sure that they agree in terms of the dataset attributes
	const FEmitterCompiledScriptPair* SpawnScriptRequest =
		Algo::FindBy(CompileRequest.EmitterCompiledScriptPairs, SystemSpawnScript, &FEmitterCompiledScriptPair::CompiledScript);
	const FEmitterCompiledScriptPair* UpdateScriptRequest =
		Algo::FindBy(CompileRequest.EmitterCompiledScriptPairs, SystemUpdateScript, &FEmitterCompiledScriptPair::CompiledScript);

	const bool SpawnScriptValid = SpawnScriptRequest
		&& SpawnScriptRequest->CompileResults.IsValid()
		&& SpawnScriptRequest->CompileResults->LastCompileStatus != ENiagaraScriptCompileStatus::NCS_Error;

	const bool UpdateScriptValid = UpdateScriptRequest
		&& UpdateScriptRequest->CompileResults.IsValid()
		&& UpdateScriptRequest->CompileResults->LastCompileStatus != ENiagaraScriptCompileStatus::NCS_Error;

	if (SpawnScriptValid && UpdateScriptValid)
	{
		if (SpawnScriptRequest->CompileResults->Attributes != UpdateScriptRequest->CompileResults->Attributes)
		{
			// if we had requested a full rebuild, then we've got a case where the generated scripts are not compatible.  This indicates
			// a significant issue where we're allowing graphs to generate invalid collections of scripts.  One known example is using
			// the Script.Context static switch that isn't fully processed in all scripts, leading to attributes differing between the
			// SystemSpawnScript and the SystemUpdateScript
			if (CompileRequest.bForced)
			{
				FString MissingAttributes;
				FString AdditionalAttributes;

				for (const auto& SpawnAttrib : SpawnScriptRequest->CompileResults->Attributes)
				{
					if (!UpdateScriptRequest->CompileResults->Attributes.Contains(SpawnAttrib))
					{
						MissingAttributes.Appendf(TEXT("%s%s"), MissingAttributes.Len() ? TEXT(", ") : TEXT(""), *SpawnAttrib.GetName().ToString());
					}
				}

				for (const auto& UpdateAttrib : UpdateScriptRequest->CompileResults->Attributes)
				{
					if (!SpawnScriptRequest->CompileResults->Attributes.Contains(UpdateAttrib))
					{
						AdditionalAttributes.Appendf(TEXT("%s%s"), AdditionalAttributes.Len() ? TEXT(", ") : TEXT(""), *UpdateAttrib.GetName().ToString());
					}
				}

				FNiagaraCompileEvent AttributeMismatchEvent(
					FNiagaraCompileEventSeverity::Error,
					FText::Format(LOCTEXT("SystemScriptAttributeMismatchError", "System Spawn/Update scripts have attributes which don't match!\n\tMissing update attributes: {0}\n\tAdditional update attributes: {1}"),
						FText::FromString(MissingAttributes),
						FText::FromString(AdditionalAttributes))
					.ToString());

				SpawnScriptRequest->CompileResults->LastCompileStatus = ENiagaraScriptCompileStatus::NCS_Error;
				SpawnScriptRequest->CompileResults->LastCompileEvents.Add(AttributeMismatchEvent);
			}
			else
			{
				UE_LOG(LogNiagara, Log, TEXT("Failed to generate consistent results for System spawn and update scripts for system %s."), *GetFullName());
			}

			return false;
		}
	}

	// Now iterate over all dependencies and verify that they are met. If not, emit an error.
	for (FEmitterCompiledScriptPair& CompilePair : CompileRequest.EmitterCompiledScriptPairs)
	{
		bool bValid = CompilePair.CompileResults.IsValid()
			&& CompilePair.CompileResults->LastCompileStatus != ENiagaraScriptCompileStatus::NCS_Error;

		if (CompilePair.CompileResults.IsValid() && CompilePair.CompileResults->ExternalDependencies.Num() != 0)
		{
			for (const FNiagaraCompileDependency& Dependency : CompilePair.CompileResults->ExternalDependencies)
			{
				FNiagaraVariable TestVar = Dependency.DependentVariable;
				ensure(TestVar.GetName() != NAME_None);
				if (CompilePair.Emitter)
				{
					FName NewName = GetEmitterVariableAliasName(TestVar, CompilePair.Emitter);
					TestVar.SetName(NewName);
				}

				bool bDependencyMet = false;
				int32 TestIdx = CompilePair.ParentIndex;
				while (TestIdx != INDEX_NONE && bDependencyMet == false)
				{
					if (CompileRequest.EmitterCompiledScriptPairs.IsValidIndex(TestIdx))
					{
						const FEmitterCompiledScriptPair& TestPair = CompileRequest.EmitterCompiledScriptPairs[TestIdx];
						if (TestPair.CompileResults.IsValid() && TestPair.CompileResults->AttributesWritten.Num() > 0)
						{
							if (TestPair.CompileResults->AttributesWritten.Contains(TestVar))
							{
								bDependencyMet = true;
								break;
							}
						}
						TestIdx = TestPair.ParentIndex;
					}
				}
				if (!bDependencyMet)
				{
					FNiagaraCompileEvent LinkerErrorEvent(
						FNiagaraCompileEventSeverity::Error, Dependency.LinkerErrorMessage, FString(), false, Dependency.NodeGuid, Dependency.PinGuid, Dependency.StackGuids);
					CompilePair.CompileResults->LastCompileEvents.Add(LinkerErrorEvent);
					CompilePair.CompileResults->LastCompileStatus = ENiagaraScriptCompileStatus::NCS_Error;
				}
			}
		}
	}
	
	

	return true;
}

bool UNiagaraSystem::QueryCompileComplete(bool bWait, bool bDoPost, bool bDoNotApply)
{

	bool bCompileGuardInProgress = InternalCompileGuardCheck(this);

	if (ActiveCompilations.Num() > 0 && !bCompileGuardInProgress)
	{
		int32 ActiveCompileIdx = 0;

		bool bAreWeWaitingForAnyResults = false;

		// Check to see if ALL of the sub-requests have resolved. 
		for (FEmitterCompiledScriptPair& EmitterCompiledScriptPair : ActiveCompilations[ActiveCompileIdx].EmitterCompiledScriptPairs)
		{
			if ((uint32)INDEX_NONE == EmitterCompiledScriptPair.PendingJobID || EmitterCompiledScriptPair.bResultsReady)
			{
				continue;
			}
			EmitterCompiledScriptPair.bResultsReady = ProcessCompilationResult(EmitterCompiledScriptPair, bWait, bDoNotApply);
			if (!EmitterCompiledScriptPair.bResultsReady)
			{
				bAreWeWaitingForAnyResults = true;
			}
		}

		check(bWait ? (bAreWeWaitingForAnyResults == false) : true);

		// Make sure that we aren't waiting for any results to come back.
		if (bAreWeWaitingForAnyResults)
		{
			if (!bWait)
			{
				return false;
			}
		}
		else
		{
			// if we've gotten all the results, run a quick check to see if the data is valid, if it's not then that indicates that
			// we've run into a compatibility issue and so we should see if we should issue a full rebuild
			const bool ResultsValid = CompilationResultsValid(ActiveCompilations[ActiveCompileIdx]);
			if (!ResultsValid && !ActiveCompilations[ActiveCompileIdx].bForced)
			{
				ActiveCompilations[ActiveCompileIdx].RootObjects.Empty();
				ActiveCompilations.RemoveAt(ActiveCompileIdx);
				RequestCompile(true, nullptr);
				return false;
			}
		}

		// In the world of do not apply, we're exiting the system completely so let's just kill any active compilations altogether.
		if (bDoNotApply || ActiveCompilations[ActiveCompileIdx].bIsValid == false)
		{
			ActiveCompilations[ActiveCompileIdx].RootObjects.Empty();
			ActiveCompilations.RemoveAt(ActiveCompileIdx);
			return true;
		}


		SCOPE_CYCLE_COUNTER(STAT_Niagara_System_CompileScript);

		// Now that the above code says they are all complete, go ahead and resolve them all at once.
		float CombinedCompileTime = 0.0f;
		bool HasCompiledJobs = false;
		for (FEmitterCompiledScriptPair& EmitterCompiledScriptPair : ActiveCompilations[ActiveCompileIdx].EmitterCompiledScriptPairs)
		{
			if ((uint32)INDEX_NONE == EmitterCompiledScriptPair.PendingJobID)
			{
				if (!EmitterCompiledScriptPair.bResultsReady)
				{
					continue;
				}
			}
			else
			{
				HasCompiledJobs = true;
			}

			CombinedCompileTime += EmitterCompiledScriptPair.CompileResults->CompileTime;
			check(EmitterCompiledScriptPair.bResultsReady);

			TSharedPtr<FNiagaraVMExecutableData> ExeData = EmitterCompiledScriptPair.CompileResults;
			TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> PrecompData = ActiveCompilations[ActiveCompileIdx].MappedData.FindChecked(EmitterCompiledScriptPair.CompiledScript);
			EmitterCompiledScriptPair.CompiledScript->SetVMCompilationResults(EmitterCompiledScriptPair.CompileId, *ExeData, PrecompData.Get());
		}

		if (bDoPost)
		{
			for (FNiagaraEmitterHandle Handle : EmitterHandles)
			{
				if (Handle.GetInstance())
				{
					if (Handle.GetIsEnabled())
					{
						Handle.GetInstance()->OnPostCompile();
					}
					else
					{
						Handle.GetInstance()->InvalidateCompileResults();
					}
				}
			}
		}

		InitEmitterCompiledData();
		InitSystemCompiledData();

		// HACK: This is a temporary hack to fix an issue where data interfaces used by modules and dynamic inputs in the
		// particle update script aren't being shared by the interpolated spawn script when accessed directly.  This works
		// properly if the data interface is assigned to a named particle parameter and then linked to an input.
		// TODO: Bind these data interfaces the same way parameter data interfaces are bound.
		for (FEmitterCompiledScriptPair& EmitterCompiledScriptPair : ActiveCompilations[ActiveCompileIdx].EmitterCompiledScriptPairs)
		{
			UNiagaraEmitter* Emitter = EmitterCompiledScriptPair.Emitter;
			UNiagaraScript* CompiledScript = EmitterCompiledScriptPair.CompiledScript;

			if (UNiagaraScript::IsEquivalentUsage(CompiledScript->GetUsage(), ENiagaraScriptUsage::ParticleUpdateScript))
			{
				UNiagaraScript* SpawnScript = Emitter->SpawnScriptProps.Script;
				for (const FNiagaraScriptDataInterfaceInfo& UpdateDataInterfaceInfo : CompiledScript->GetCachedDefaultDataInterfaces())
				{
					if (UpdateDataInterfaceInfo.RegisteredParameterMapRead == NAME_None && UpdateDataInterfaceInfo.RegisteredParameterMapWrite == NAME_None)
					{
						// If the data interface isn't being read or written to a parameter map then it won't be bound properly so we
						// assign the update scripts copy of the data interface to the spawn scripts copy by pointer so that they will share
						// the data interface at runtime and will both be updated in the editor.
						for (FNiagaraScriptDataInterfaceInfo& SpawnDataInterfaceInfo : SpawnScript->GetCachedDefaultDataInterfaces())
						{
							if (UpdateDataInterfaceInfo.Name == SpawnDataInterfaceInfo.Name)
							{
								SpawnDataInterfaceInfo.DataInterface = UpdateDataInterfaceInfo.DataInterface;
							}
						}
					}
				}
			}
		}

		ActiveCompilations[ActiveCompileIdx].RootObjects.Empty();

		UpdatePostCompileDIInfo();

		ComputeEmittersExecutionOrder();

		ComputeRenderersDrawOrder();

		CacheFromCompiledData();
		
		UpdateHasGPUEmitters();
		UpdateDITickFlags();

		ResolveScalabilitySettings();

		const float ElapsedWallTime = (float)(FPlatformTime::Seconds() - ActiveCompilations[ActiveCompileIdx].StartTime);

		if (HasCompiledJobs)
		{
			UE_LOG(LogNiagara, Log, TEXT("Compiling System %s took %f sec (time since issued), %f sec (combined shader worker time)."),
				*GetFullName(), ElapsedWallTime, CombinedCompileTime);
		}
		else
		{
			UE_LOG(LogNiagara, Verbose, TEXT("Compiling System %s took %f sec."), *GetFullName(), ElapsedWallTime);
		}

		ActiveCompilations.RemoveAt(ActiveCompileIdx);

		if (bDoPost)
		{
			SCOPE_CYCLE_COUNTER(STAT_Niagara_System_CompileScriptResetAfter);

			OnSystemCompiled().Broadcast(this);
		}

		return true;
	}

	return false;
}

bool UNiagaraSystem::ProcessCompilationResult(FEmitterCompiledScriptPair& ScriptPair, bool bWait, bool bDoNotApply)
{
	COOK_STAT(auto Timer = NiagaraScriptCookStats::UsageStats.TimeAsyncWait());

	INiagaraModule& NiagaraModule = FModuleManager::Get().LoadModuleChecked<INiagaraModule>(TEXT("Niagara"));
	TSharedPtr<FNiagaraVMExecutableData> ExeData = NiagaraModule.GetCompileJobResult(ScriptPair.PendingJobID, bWait);

	if (!bWait && !ExeData.IsValid())
	{
		COOK_STAT(Timer.TrackCyclesOnly());
		return false;
	}
	check(ExeData.IsValid());
	if (!bDoNotApply)
	{
		ScriptPair.CompileResults = ExeData;
	}

	// save result to the ddc
	TArray<uint8> OutData;
	if (UNiagaraScript::ExecToBinaryData(ScriptPair.CompiledScript, OutData, *ExeData))
	{
		COOK_STAT(Timer.AddMiss(OutData.Num()));

		// be sure to use the CompileId that is associated with the compilation
		const FString DDCKey = UNiagaraScript::BuildNiagaraDDCKeyString(ScriptPair.CompileId);

		GetDerivedDataCacheRef().Put(*DDCKey, OutData, GetPathName());
		return true;
	}

	COOK_STAT(Timer.TrackCyclesOnly());
	return false;
}

bool UNiagaraSystem::GetFromDDC(FEmitterCompiledScriptPair& ScriptPair)
{
	if (!ScriptPair.CompiledScript->IsCompilable())
	{
		return false;
	}

	COOK_STAT(auto Timer = NiagaraScriptCookStats::UsageStats.TimeSyncWork());

	FNiagaraVMExecutableDataId NewID;
	ScriptPair.CompiledScript->ComputeVMCompilationId(NewID, FGuid());
	ScriptPair.CompileId = NewID;

	TArray<uint8> Data;
	if (GetDerivedDataCacheRef().GetSynchronous(*ScriptPair.CompiledScript->GetNiagaraDDCKeyString(FGuid()), Data, GetPathName()))
	{
		TSharedPtr<FNiagaraVMExecutableData> ExeData = MakeShared<FNiagaraVMExecutableData>();
		if (ScriptPair.CompiledScript->BinaryToExecData(ScriptPair.CompiledScript, Data, *ExeData))
		{
			COOK_STAT(Timer.AddHit(Data.Num()));
			ExeData->CompileTime = 0; // since we didn't actually compile anything
			ScriptPair.CompileResults = ExeData;
			ScriptPair.bResultsReady = true;
			if (GNiagaraLogDDCStatusForSystems != 0)
			{
				UE_LOG(LogNiagara, Verbose, TEXT("Niagara Script pulled from DDC ... %s"), *ScriptPair.CompiledScript->GetPathName());
			}
			return true;
		}
	}
	
	if (GNiagaraLogDDCStatusForSystems != 0)
	{
	    UE_LOG(LogNiagara, Verbose, TEXT("Need Compile! Niagara Script GotFromDDC could not find ... %s"), *ScriptPair.CompiledScript->GetPathName());
	}

	COOK_STAT(Timer.TrackCyclesOnly());
	return false;
}


#if WITH_EDITORONLY_DATA

void UNiagaraSystem::InitEmitterVariableAliasNames(FNiagaraEmitterCompiledData& EmitterCompiledDataToInit, const UNiagaraEmitter* InAssociatedEmitter)
{
	EmitterCompiledDataToInit.EmitterSpawnIntervalVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_SPAWN_INTERVAL, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterInterpSpawnStartDTVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_INTERP_SPAWN_START_DT, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterAgeVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_AGE, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterSpawnGroupVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_SPAWN_GROUP, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterRandomSeedVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_RANDOM_SEED, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterInstanceSeedVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_ENGINE_EMITTER_INSTANCE_SEED, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterTotalSpawnedParticlesVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_ENGINE_EMITTER_TOTAL_SPAWNED_PARTICLES, InAssociatedEmitter));
}

const FName UNiagaraSystem::GetEmitterVariableAliasName(const FNiagaraVariable& InEmitterVar, const UNiagaraEmitter* InEmitter) const
{
	return FName(*InEmitterVar.GetName().ToString().Replace(TEXT("Emitter."), *(InEmitter->GetUniqueEmitterName() + TEXT("."))));
}

void UNiagaraSystem::InitEmitterDataSetCompiledData(FNiagaraDataSetCompiledData& DataSetToInit, const UNiagaraEmitter* InAssociatedEmitter, const FNiagaraEmitterHandle& InAssociatedEmitterHandle)
{
	DataSetToInit.Empty();

	if (InAssociatedEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		DataSetToInit.Variables = InAssociatedEmitter->GetGPUComputeScript()->GetVMExecutableData().Attributes;
	}
	else
	{
		DataSetToInit.Variables = InAssociatedEmitter->UpdateScriptProps.Script->GetVMExecutableData().Attributes;

		for (const FNiagaraVariable& Var : InAssociatedEmitter->SpawnScriptProps.Script->GetVMExecutableData().Attributes)
		{
			DataSetToInit.Variables.AddUnique(Var);
		}
	}

	DataSetToInit.bRequiresPersistentIDs = InAssociatedEmitter->RequiresPersistentIDs() || DataSetToInit.Variables.Contains(SYS_PARAM_PARTICLES_ID);
	DataSetToInit.ID = FNiagaraDataSetID(InAssociatedEmitterHandle.GetIdName(), ENiagaraDataSetType::ParticleData);
	DataSetToInit.SimTarget = InAssociatedEmitter->SimTarget;

	DataSetToInit.BuildLayout();
}
#endif

bool UNiagaraSystem::RequestCompile(bool bForce, FNiagaraSystemUpdateContext* OptionalUpdateContext)
{
	// We remove emitters and scripts on dedicated servers, so skip further work.
	const bool bIsDedicatedServer = !GIsClient && GIsServer;
	if (bIsDedicatedServer)
	{
		return false;
	}

	static const bool bNoShaderCompile = FParse::Param(FCommandLine::Get(), TEXT("NoShaderCompile"));
	if (bNoShaderCompile)
	{
		return false;
	}

	bool bCompileGuardInProgress = InternalCompileGuardCheck(this);

	if (bForce)
	{
		ForceGraphToRecompileOnNextCheck();
	}

	if (bCompileGuardInProgress)
	{
		return false;
	}

	if (ActiveCompilations.Num() > 0)
	{
		PollForCompilationComplete();
	}
	

	// Record that we entered this function already.
	FPlatformTLS::SetTlsValue(CompileGuardSlot, this);

	FNiagaraSystemCompileRequest& ActiveCompilation = ActiveCompilations.AddDefaulted_GetRef();
	ActiveCompilation.bForced = bForce;
	ActiveCompilation.StartTime = FPlatformTime::Seconds();

	SCOPE_CYCLE_COUNTER(STAT_Niagara_System_Precompile);
	
	check(SystemSpawnScript->GetLatestSource() == SystemUpdateScript->GetLatestSource());
	TArray<FNiagaraVariable> OriginalExposedParams;
	GetExposedParameters().GetParameters(OriginalExposedParams);

	TArray<UNiagaraScript*> ScriptsNeedingCompile;
	bool bAnyCompiled = false;
	{
		COOK_STAT(auto Timer = NiagaraScriptCookStats::UsageStats.TimeSyncWork());
		COOK_STAT(Timer.TrackCyclesOnly());
		INiagaraModule& NiagaraModule = FModuleManager::Get().LoadModuleChecked<INiagaraModule>(TEXT("Niagara"));


		//Compile all emitters
		bool bAnyUnsynchronized = false;	

		// Pass one... determine if any need to be compiled.
		{

			for (int32 i = 0; i < EmitterHandles.Num(); i++)
			{
				FNiagaraEmitterHandle Handle = EmitterHandles[i];
				if (Handle.GetInstance() && Handle.GetIsEnabled())
				{
					TArray<UNiagaraScript*> EmitterScripts;
					Handle.GetInstance()->GetScripts(EmitterScripts, false, true);
					check(EmitterScripts.Num() > 0);
					int32 Parent = INDEX_NONE;
					for (UNiagaraScript* EmitterScript : EmitterScripts)
					{

						FEmitterCompiledScriptPair Pair;
						Pair.bResultsReady = false;
						Pair.Emitter = Handle.GetInstance();
						Pair.CompiledScript = EmitterScript;
						Pair.ParentIndex = Parent;
						if (!GetFromDDC(Pair) && EmitterScript->IsCompilable() && !EmitterScript->AreScriptAndSourceSynchronized())
						{
							ScriptsNeedingCompile.Add(EmitterScript);
							bAnyUnsynchronized = true;
						}
						Parent = ActiveCompilation.EmitterCompiledScriptPairs.Add(Pair);
					}

				}
			}

			bAnyCompiled = bAnyUnsynchronized || bForce;

			// Now add the system scripts for compilation...
			int32 Parent = INDEX_NONE;
			{
				FEmitterCompiledScriptPair Pair;
				Pair.bResultsReady = false;
				Pair.Emitter = nullptr;
				Pair.CompiledScript = SystemSpawnScript;
				if (!GetFromDDC(Pair) && !SystemSpawnScript->AreScriptAndSourceSynchronized())
				{
					ScriptsNeedingCompile.Add(SystemSpawnScript);
					bAnyCompiled = true;
				}
				Parent = ActiveCompilation.EmitterCompiledScriptPairs.Add(Pair);
			}

			{
				FEmitterCompiledScriptPair Pair;
				Pair.bResultsReady = false;
				Pair.Emitter = nullptr;
				Pair.CompiledScript = SystemUpdateScript;
				Pair.ParentIndex = Parent;
				if (!GetFromDDC(Pair) && !SystemUpdateScript->AreScriptAndSourceSynchronized())
				{
					ScriptsNeedingCompile.Add(SystemUpdateScript);
					bAnyCompiled = true;
				}
				Parent = ActiveCompilation.EmitterCompiledScriptPairs.Add(Pair);
			}

			// Need to set the EmitterParent on the emitter spawn scripts
			for (FEmitterCompiledScriptPair & Pair: ActiveCompilation.EmitterCompiledScriptPairs)
			{
				if (Pair.Emitter != nullptr && Pair.ParentIndex == INDEX_NONE)
				{
					Pair.ParentIndex = Parent;
				}
			}
		}


		// We found things needing compilation, now we have to go through an static duplicate everything that will be translated...
		{
			UNiagaraPrecompileContainer* Container = NewObject<UNiagaraPrecompileContainer>(GetTransientPackage());
			Container->System = this;
			Container->Scripts = ScriptsNeedingCompile;
			TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> SystemPrecompiledData = NiagaraModule.Precompile(Container, FGuid());

			if (SystemPrecompiledData.IsValid() == false)
			{
				UE_LOG(LogNiagara, Error, TEXT("Failed to precompile %s.  This is due to unexpected invalid or broken data.  Additional details should be in the log."), *GetPathName());
				return false;
			}

			SystemPrecompiledData->GetReferencedObjects(ActiveCompilation.RootObjects);
			ActiveCompilation.MappedData.Add(SystemSpawnScript, SystemPrecompiledData);
			ActiveCompilation.MappedData.Add(SystemUpdateScript, SystemPrecompiledData);

			check(EmitterHandles.Num() == SystemPrecompiledData->GetDependentRequestCount());


			// Grab the list of user variables that were actually encountered so that we can add to them later.
			TArray<FNiagaraVariable> EncounteredExposedVars;
			SystemPrecompiledData->GatherPreCompiledVariables(TEXT("User"), EncounteredExposedVars);

			for (int32 i = 0; i < EmitterHandles.Num(); i++)
			{
				FNiagaraEmitterHandle Handle = EmitterHandles[i];
				if (Handle.GetInstance() && Handle.GetIsEnabled())
				{
					TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> EmitterPrecompiledData = SystemPrecompiledData->GetDependentRequest(i);
					EmitterPrecompiledData->GetReferencedObjects(ActiveCompilation.RootObjects);

					TArray<UNiagaraScript*> EmitterScripts;
					Handle.GetInstance()->GetScripts(EmitterScripts, false, true);
					check(EmitterScripts.Num() > 0);
					for (UNiagaraScript* EmitterScript : EmitterScripts)
					{
						ActiveCompilation.MappedData.Add(EmitterScript, EmitterPrecompiledData);
					}

					// Add the emitter's User variables to the encountered list to expose for later.
					EmitterPrecompiledData->GatherPreCompiledVariables(TEXT("User"), EncounteredExposedVars);
				}
			}


			// Now let's synchronize the variables that we actually encountered during precompile so that we can expose them to the end user.
			for (int32 i = 0; i < EncounteredExposedVars.Num(); i++)
			{
				if (OriginalExposedParams.Contains(EncounteredExposedVars[i]) == false)
				{
					// Just in case it wasn't added previously..
					ExposedParameters.AddParameter(EncounteredExposedVars[i]);
				}
			}
		}
		

		// We have previously duplicated all that is needed for compilation, so let's now issue the compile requests!
		for (UNiagaraScript* CompiledScript : ScriptsNeedingCompile)
		{

			const auto InPairs = [&CompiledScript](const FEmitterCompiledScriptPair& Other) -> bool
			{
				return CompiledScript == Other.CompiledScript;
			};

			TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> EmitterPrecompiledData = ActiveCompilation.MappedData.FindChecked(CompiledScript);
			FEmitterCompiledScriptPair* Pair = ActiveCompilation.EmitterCompiledScriptPairs.FindByPredicate(InPairs);
			check(Pair);

			// now that we've done the precompile check with the DDC again as our key may have changed.  Currently the Precompile can update the rapid
			// iteration parameters, which if they are baked out, will impact the DDC key.
			// TODO - Handling of the rapid iteration parameters should move to follow merging of emitter sripts rather than be a part of the precompile.
			if (GetFromDDC(*Pair))
			{
				continue;
			}

			if (!CompiledScript->RequestExternallyManagedAsyncCompile(EmitterPrecompiledData, Pair->CompileId, Pair->PendingJobID))
			{
				UE_LOG(LogNiagara, Warning, TEXT("For some reason we are reporting that %s is in sync even though AreScriptAndSourceSynchronized returned false!"), *CompiledScript->GetPathName())
			}
		}

	}


	// Now record that we are done with this function.
	FPlatformTLS::SetTlsValue(CompileGuardSlot, nullptr);


	// We might be able to just complete compilation right now if nothing needed compilation.
	if (ScriptsNeedingCompile.Num() == 0)
	{
		PollForCompilationComplete();
	}

	
	if (OptionalUpdateContext)
	{
		OptionalUpdateContext->Add(this, true);
	}
	else
	{
		FNiagaraSystemUpdateContext UpdateCtx(this, true);
	}

	return bAnyCompiled;
}


#endif

#if WITH_EDITORONLY_DATA
void UNiagaraSystem::InitEmitterCompiledData()
{
	EmitterCompiledData.Empty();
	if (SystemSpawnScript->GetVMExecutableData().IsValid() && SystemUpdateScript->GetVMExecutableData().IsValid())
	{
		TArray<TSharedRef<FNiagaraEmitterCompiledData>> NewEmitterCompiledData;
		for (int32 EmitterIdx = 0; EmitterIdx < EmitterHandles.Num(); ++EmitterIdx)
		{
			NewEmitterCompiledData.Add(MakeShared<FNiagaraEmitterCompiledData>());
		}

		FNiagaraTypeDefinition SpawnInfoDef = FNiagaraTypeDefinition(FNiagaraSpawnInfo::StaticStruct());

		for (FNiagaraVariable& Var : SystemSpawnScript->GetVMExecutableData().Attributes)
		{
			for (int32 EmitterIdx = 0; EmitterIdx < EmitterHandles.Num(); ++EmitterIdx)
			{
				UNiagaraEmitter* Emitter = EmitterHandles[EmitterIdx].GetInstance();
				if (Emitter)
				{
					FString EmitterName = Emitter->GetUniqueEmitterName() + TEXT(".");
					if (Var.GetType() == SpawnInfoDef && Var.GetName().ToString().StartsWith(EmitterName))
					{
						NewEmitterCompiledData[EmitterIdx]->SpawnAttributes.AddUnique(Var.GetName());
					}
				}
			}
		}

		for (FNiagaraVariable& Var : SystemUpdateScript->GetVMExecutableData().Attributes)
		{
			for (int32 EmitterIdx = 0; EmitterIdx < EmitterHandles.Num(); ++EmitterIdx)
			{
				UNiagaraEmitter* Emitter = EmitterHandles[EmitterIdx].GetInstance();
				if (Emitter)
				{
					FString EmitterName = Emitter->GetUniqueEmitterName() + TEXT(".");
					if (Var.GetType() == SpawnInfoDef && Var.GetName().ToString().StartsWith(EmitterName))
					{
						NewEmitterCompiledData[EmitterIdx]->SpawnAttributes.AddUnique(Var.GetName());
					}
				}
			}
		}

		for (int32 EmitterIdx = 0; EmitterIdx < EmitterHandles.Num(); ++EmitterIdx)
		{
			const FNiagaraEmitterHandle& EmitterHandle = EmitterHandles[EmitterIdx];
			const UNiagaraEmitter* Emitter = EmitterHandle.GetInstance();
			FNiagaraDataSetCompiledData& EmitterDataSetCompiledData = NewEmitterCompiledData[EmitterIdx]->DataSetCompiledData;
			FNiagaraDataSetCompiledData& GPUCaptureCompiledData = NewEmitterCompiledData[EmitterIdx]->GPUCaptureDataSetCompiledData;
			if ensureMsgf(Emitter != nullptr, TEXT("Failed to get Emitter Instance from Emitter Handle in post compile, please investigate."))
			{
				static FName GPUCaptureDataSetName = TEXT("GPU Capture Dataset");
				InitEmitterVariableAliasNames(NewEmitterCompiledData[EmitterIdx].Get(), Emitter);
				InitEmitterDataSetCompiledData(EmitterDataSetCompiledData, Emitter, EmitterHandle);
				GPUCaptureCompiledData.ID = FNiagaraDataSetID(GPUCaptureDataSetName, ENiagaraDataSetType::ParticleData);
				GPUCaptureCompiledData.Variables = EmitterDataSetCompiledData.Variables;
				GPUCaptureCompiledData.SimTarget = ENiagaraSimTarget::CPUSim;
				GPUCaptureCompiledData.BuildLayout();				
			}
		}

		for (int32 EmitterIdx = 0; EmitterIdx < EmitterHandles.Num(); ++EmitterIdx)
		{
			EmitterCompiledData.Add(NewEmitterCompiledData[EmitterIdx]);
		}
	}
}

void UNiagaraSystem::InitSystemCompiledData()
{
	SystemCompiledData.InstanceParamStore.Empty();

	ExposedParameters.CopyParametersTo(SystemCompiledData.InstanceParamStore, false, FNiagaraParameterStore::EDataInterfaceCopyMethod::Reference);

	auto CreateDataSetCompiledData = [&](FNiagaraDataSetCompiledData& CompiledData, TConstArrayView<FNiagaraVariable> Vars)
	{
		CompiledData.Empty();

		CompiledData.Variables.Reset(Vars.Num());
		for (const FNiagaraVariable& Var : Vars)
		{
			CompiledData.Variables.AddUnique(Var);
		}

		CompiledData.bRequiresPersistentIDs = false;
		CompiledData.ID = FNiagaraDataSetID();
		CompiledData.SimTarget = ENiagaraSimTarget::CPUSim;

		CompiledData.BuildLayout();
	};

	const FNiagaraVMExecutableData& SystemSpawnScriptData = GetSystemSpawnScript()->GetVMExecutableData();
	const FNiagaraVMExecutableData& SystemUpdateScriptData = GetSystemUpdateScript()->GetVMExecutableData();

	CreateDataSetCompiledData(SystemCompiledData.DataSetCompiledData, SystemUpdateScriptData.Attributes);

	const FNiagaraParameters* EngineParamsSpawn = SystemSpawnScriptData.DataSetToParameters.Find(TEXT("Engine"));
	CreateDataSetCompiledData(SystemCompiledData.SpawnInstanceParamsDataSetCompiledData, EngineParamsSpawn ? TConstArrayView<FNiagaraVariable>(EngineParamsSpawn->Parameters) : TArrayView<FNiagaraVariable>());
	const FNiagaraParameters* EngineParamsUpdate = SystemUpdateScriptData.DataSetToParameters.Find(TEXT("Engine"));
	CreateDataSetCompiledData(SystemCompiledData.UpdateInstanceParamsDataSetCompiledData, EngineParamsUpdate ? TConstArrayView<FNiagaraVariable>(EngineParamsUpdate->Parameters) : TArrayView<FNiagaraVariable>());

	// create the bindings to be used with our constant buffers; geenrating the offsets to/from the data sets; we need
	// editor data to build these bindings because of the constant buffer structs only having their variable definitions
	// with editor data.
	SystemCompiledData.SpawnInstanceGlobalBinding.Build<FNiagaraGlobalParameters>(SystemCompiledData.SpawnInstanceParamsDataSetCompiledData);
	SystemCompiledData.SpawnInstanceSystemBinding.Build<FNiagaraSystemParameters>(SystemCompiledData.SpawnInstanceParamsDataSetCompiledData);
	SystemCompiledData.SpawnInstanceOwnerBinding.Build<FNiagaraOwnerParameters>(SystemCompiledData.SpawnInstanceParamsDataSetCompiledData);

	SystemCompiledData.UpdateInstanceGlobalBinding.Build<FNiagaraGlobalParameters>(SystemCompiledData.UpdateInstanceParamsDataSetCompiledData);
	SystemCompiledData.UpdateInstanceSystemBinding.Build<FNiagaraSystemParameters>(SystemCompiledData.UpdateInstanceParamsDataSetCompiledData);
	SystemCompiledData.UpdateInstanceOwnerBinding.Build<FNiagaraOwnerParameters>(SystemCompiledData.UpdateInstanceParamsDataSetCompiledData);

	const int32 EmitterCount = EmitterHandles.Num();

	SystemCompiledData.SpawnInstanceEmitterBindings.SetNum(EmitterHandles.Num());
	SystemCompiledData.UpdateInstanceEmitterBindings.SetNum(EmitterHandles.Num());

	const FString EmitterNamespace = TEXT("Emitter");
	for (int32 EmitterIdx = 0; EmitterIdx < EmitterCount; ++EmitterIdx)
	{
		const FNiagaraEmitterHandle& PerEmitterHandle = EmitterHandles[EmitterIdx];
		const UNiagaraEmitter* Emitter = PerEmitterHandle.GetInstance();
		if (ensureMsgf(Emitter != nullptr, TEXT("Failed to get Emitter Instance from Emitter Handle when post compiling Niagara System %s!"), *GetPathNameSafe(this)))
		{
			const FString EmitterName = Emitter->GetUniqueEmitterName();

			SystemCompiledData.SpawnInstanceEmitterBindings[EmitterIdx].Build<FNiagaraEmitterParameters>(SystemCompiledData.SpawnInstanceParamsDataSetCompiledData, EmitterNamespace, EmitterName);
			SystemCompiledData.UpdateInstanceEmitterBindings[EmitterIdx].Build<FNiagaraEmitterParameters>(SystemCompiledData.UpdateInstanceParamsDataSetCompiledData, EmitterNamespace, EmitterName);
		}
	}

}
#endif

TStatId UNiagaraSystem::GetStatID(bool bGameThread, bool bConcurrent)const
{
#if STATS
	if (!StatID_GT.IsValidStat())
	{
		GenerateStatID();
	}

	if (bGameThread)
	{
		if (bConcurrent)
		{
			return StatID_GT_CNC;
		}
		else
		{
			return StatID_GT;
		}
	}
	else
	{
		if(bConcurrent)
		{
			return StatID_RT_CNC;
		}
		else
		{
			return StatID_RT;
		}
	}
#endif
	return TStatId();
}

void UNiagaraSystem::AddToInstanceCountStat(int32 NumInstances, bool bSolo)const
{
#if STATS
	if (!StatID_GT.IsValidStat())
	{
		GenerateStatID();
	}

	if (FThreadStats::IsCollectingData())
	{
		if (bSolo)
		{
			FThreadStats::AddMessage(StatID_InstanceCountSolo.GetName(), EStatOperation::Add, int64(NumInstances));
			TRACE_STAT_ADD(StatID_InstanceCountSolo.GetName(), int64(NumInstances));
		}
		else
		{
			FThreadStats::AddMessage(StatID_InstanceCount.GetName(), EStatOperation::Add, int64(NumInstances));
			TRACE_STAT_ADD(StatID_InstanceCount.GetName(), int64(NumInstances));
		}
	}
#endif
}

void UNiagaraSystem::GenerateStatID()const
{
#if STATS
	StatID_GT = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraSystems>(GetPathName() + TEXT(" [GT]"));
	StatID_GT_CNC = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraSystems>(GetPathName() + TEXT(" [GT_CNC]"));
	StatID_RT = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraSystems>(GetPathName() + TEXT(" [RT]"));
	StatID_RT_CNC = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraSystems>(GetPathName() + TEXT(" [RT_CNC]"));

	StatID_InstanceCount = FDynamicStats::CreateStatIdInt64<FStatGroup_STATGROUP_NiagaraSystemCounts>(GetPathName());
	StatID_InstanceCountSolo = FDynamicStats::CreateStatIdInt64<FStatGroup_STATGROUP_NiagaraSystemCounts>(GetPathName() + TEXT(" [SOLO]"));

#endif
}

UNiagaraEffectType* UNiagaraSystem::GetEffectType()const
{
	return EffectType;
}

#if WITH_EDITOR
void UNiagaraSystem::SetEffectType(UNiagaraEffectType* InEffectType)
{
	if (InEffectType != EffectType)
	{
		Modify();
		EffectType = InEffectType;
		ResolveScalabilitySettings();
		FNiagaraSystemUpdateContext UpdateCtx;
		UpdateCtx.Add(this, true);
	}	
}
#endif

void UNiagaraSystem::ResolveScalabilitySettings()
{
	CurrentScalabilitySettings.Clear();
	if (UNiagaraEffectType* ActualEffectType = GetEffectType())
	{
		CurrentScalabilitySettings = ActualEffectType->GetActiveSystemScalabilitySettings();
	}

	if (bOverrideScalabilitySettings)
	{
		for (FNiagaraSystemScalabilityOverride& Override : SystemScalabilityOverrides.Overrides)
		{
			if (Override.Platforms.IsActive())
			{
				if (Override.bOverrideDistanceSettings)
				{
					CurrentScalabilitySettings.bCullByDistance = Override.bCullByDistance;
					CurrentScalabilitySettings.MaxDistance = Override.MaxDistance;
				}

				if (Override.bOverrideInstanceCountSettings)
				{
					CurrentScalabilitySettings.bCullMaxInstanceCount = Override.bCullMaxInstanceCount;
					CurrentScalabilitySettings.MaxInstances = Override.MaxInstances;
				}

				if (Override.bOverridePerSystemInstanceCountSettings)
				{
					CurrentScalabilitySettings.bCullPerSystemMaxInstanceCount = Override.bCullPerSystemMaxInstanceCount;
					CurrentScalabilitySettings.MaxSystemInstances = Override.MaxSystemInstances;
				}

				if (Override.bOverrideTimeSinceRendererSettings)
				{
					CurrentScalabilitySettings.bCullByMaxTimeWithoutRender = Override.bCullByMaxTimeWithoutRender;
					CurrentScalabilitySettings.MaxTimeWithoutRender = Override.MaxTimeWithoutRender;
				}

 				if (Override.bOverrideGlobalBudgetCullingSettings)
				{
 					CurrentScalabilitySettings.bCullByGlobalBudget = Override.bCullByGlobalBudget;
 					CurrentScalabilitySettings.MaxGlobalBudgetUsage = Override.MaxGlobalBudgetUsage;
 				}

				break;//These overrides *should* be for orthogonal platform sets so we can exit after we've found a match.
			}
		}
	}

	CurrentScalabilitySettings.MaxDistance = FMath::Max(GNiagaraScalabiltiyMinumumMaxDistance, CurrentScalabilitySettings.MaxDistance);

	//Work out if this system needs to have sorted significance culling done.
	bNeedsSortedSignificanceCull = false;

	if (CurrentScalabilitySettings.bCullMaxInstanceCount || CurrentScalabilitySettings.bCullPerSystemMaxInstanceCount)
	{
		bNeedsSortedSignificanceCull = true;
	}
	else
	{
		//If we're not using it at the system level, maybe one of the emitters is.
		auto ScriptUsesSigIndex = [&](UNiagaraScript* Script)
		{
			if (Script && bNeedsSortedSignificanceCull == false)//Skip if we've already found one using it.
			{
				bNeedsSortedSignificanceCull = Script->GetVMExecutableData().bReadsSignificanceIndex;
			}
		};
		ForEachScript(ScriptUsesSigIndex);
	}
}

void UNiagaraSystem::OnScalabilityCVarChanged()
{
	ResolveScalabilitySettings();

	for (FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetInstance())
		{
			Handle.GetInstance()->OnScalabilityCVarChanged();
		}
	}

	// Update components
	{
		FNiagaraSystemUpdateContext UpdateCtx;
		UpdateCtx.SetDestroyOnAdd(true);
		UpdateCtx.SetOnlyActive(true);
		UpdateCtx.Add(this, true);
	}

	// Re-prime the component pool
	if (PoolPrimeSize > 0 && MaxPoolSize > 0)
	{
		FNiagaraWorldManager::PrimePoolForAllWorlds(this);
	}
}

const FString& UNiagaraSystem::GetCrashReporterTag()const
{
	if (CrashReporterTag.IsEmpty())
	{
		CrashReporterTag = FString::Printf(TEXT("| System: %s |"), *GetFullName());
	}
	return CrashReporterTag;
}

FNiagaraEmitterCompiledData::FNiagaraEmitterCompiledData()
{
	EmitterSpawnIntervalVar = SYS_PARAM_EMITTER_SPAWN_INTERVAL;
	EmitterInterpSpawnStartDTVar = SYS_PARAM_EMITTER_INTERP_SPAWN_START_DT;
	EmitterAgeVar = SYS_PARAM_EMITTER_AGE;
	EmitterSpawnGroupVar = SYS_PARAM_EMITTER_SPAWN_GROUP;
	EmitterRandomSeedVar = SYS_PARAM_EMITTER_RANDOM_SEED;
	EmitterInstanceSeedVar = SYS_PARAM_ENGINE_EMITTER_INSTANCE_SEED;
	EmitterTotalSpawnedParticlesVar = SYS_PARAM_ENGINE_EMITTER_TOTAL_SPAWNED_PARTICLES;
}

#if WITH_EDITORONLY_DATA
void FNiagaraParameterDataSetBindingCollection::BuildInternal(const TArray<FNiagaraVariable>& ParameterVars, const FNiagaraDataSetCompiledData& DataSet, const FString& NamespaceBase, const FString& NamespaceReplacement)
{
	// be sure to reset the offsets first
	FloatOffsets.Empty();
	Int32Offsets.Empty();

	const bool DoNameReplacement = !NamespaceBase.IsEmpty() && !NamespaceReplacement.IsEmpty();

	int32 ParameterOffset = 0;
	for (FNiagaraVariable Var : ParameterVars)
	{
		if (DoNameReplacement)
		{
			const FString ParamName = Var.GetName().ToString().Replace(*NamespaceBase, *NamespaceReplacement);
			Var.SetName(*ParamName);
		}

		int32 VariableIndex = DataSet.Variables.IndexOfByKey(Var);

		if (DataSet.VariableLayouts.IsValidIndex(VariableIndex))
		{
			const FNiagaraVariableLayoutInfo& Layout = DataSet.VariableLayouts[VariableIndex];
			int32 NumFloats = 0;
			int32 NumInts = 0;

			for (uint32 CompIdx = 0; CompIdx < Layout.GetNumFloatComponents(); ++CompIdx)
			{
				int32 ParamOffset = ParameterOffset + Layout.LayoutInfo.FloatComponentByteOffsets[CompIdx];
				int32 DataSetOffset = Layout.FloatComponentStart + NumFloats++;
				auto& Binding = FloatOffsets.AddDefaulted_GetRef();
				Binding.ParameterOffset = ParamOffset;
				Binding.DataSetComponentOffset = DataSetOffset;
			}
			for (uint32 CompIdx = 0; CompIdx < Layout.GetNumInt32Components(); ++CompIdx)
			{
				int32 ParamOffset = ParameterOffset + Layout.LayoutInfo.Int32ComponentByteOffsets[CompIdx];
				int32 DataSetOffset = Layout.Int32ComponentStart + NumInts++;
				auto& Binding = Int32Offsets.AddDefaulted_GetRef();
				Binding.ParameterOffset = ParamOffset;
				Binding.DataSetComponentOffset = DataSetOffset;
			}
		}

		// we need to take into account potential padding that is in the constant buffers based similar to what is done
		// in the NiagaraHlslTranslator, where Vec2/Vec3 are treated as Vec4.
		int32 ParameterSize = Var.GetSizeInBytes();
		const FNiagaraTypeDefinition& Type = Var.GetType();
		if (Type == FNiagaraTypeDefinition::GetVec2Def() || Type == FNiagaraTypeDefinition::GetVec3Def())
		{
			ParameterSize = Align(ParameterSize, FNiagaraTypeDefinition::GetVec4Def().GetSize());
		}

		ParameterOffset += ParameterSize;
	}

	FloatOffsets.Shrink();
	Int32Offsets.Shrink();
}

UNiagaraBakerSettings* UNiagaraSystem::GetBakerSettings()
{
	if ( BakerSettings == nullptr )
	{
		BakerSettings = NewObject<UNiagaraBakerSettings>(this, "BakerSettings", RF_Transactional);
	}
	return BakerSettings;
}
#endif

#undef LOCTEXT_NAMESPACE // NiagaraSystem
