// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "NiagaraSystem.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraRenderer.h"
#include "NiagaraConstants.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraModule.h"
#include "Modules/ModuleManager.h"
#include "NiagaraEmitter.h"
#include "UObject/Package.h"
#include "NiagaraEmitterHandle.h"
#include "AssetData.h"
#include "NiagaraStats.h"
#include "NiagaraEditorDataBase.h"
#include "INiagaraEditorOnlyDataUtlities.h"
#include "NiagaraWorldManager.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSettings.h"


#if WITH_EDITOR
#include "NiagaraScriptDerivedData.h"
#include "DerivedDataCacheInterface.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Niagara - System - Precompile"), STAT_Niagara_System_Precompile, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Niagara - System - CompileScript"), STAT_Niagara_System_CompileScript, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Niagara - System - CompileScript_ResetAfter"), STAT_Niagara_System_CompileScriptResetAfter, STATGROUP_Niagara);

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

//////////////////////////////////////////////////////////////////////////

UNiagaraSystem::UNiagaraSystem(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, bFixedBounds(false)
, FastPathMode(ENiagaraFastPathMode::ScriptVMOnly)
, ExposedParameters(this)
#if WITH_EDITORONLY_DATA
, bIsolateEnabled(false)
#endif
, FixedBounds(FBox(FVector(-100), FVector(100)))
, bAutoDeactivate(true)
, WarmupTime(0.0f)
, WarmupTickCount(0)
, WarmupTickDelta(1.0f / 15.0f)
, bHasSystemScriptDIsWithPerInstanceData(false)
{
	MaxPoolSize = 32;
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
#if WITH_EDITORONLY_DATA
	WaitForCompilationComplete();
#endif
}

#if WITH_EDITOR
void UNiagaraSystem::BeginCacheForCookedPlatformData(const ITargetPlatform *TargetPlatform)
{
	//UE_LOG(LogNiagara, Display, TEXT("UNiagaraSystem::BeginCacheForCookedPlatformData %s %s"), *GetFullName(), GIsSavingPackage ? TEXT("Saving...") : TEXT("Not Saving..."));
	Super::BeginCacheForCookedPlatformData(TargetPlatform);
	
#if WITH_EDITORONLY_DATA

	WaitForCompilationComplete();
#endif
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
#endif
	}

	ResolveScalabilityOverrides();
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

#if WITH_EDITORONLY_DATA
bool UNiagaraSystem::UsesScript(const UNiagaraScript* Script)const
{
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

	ResolveScalabilityOverrides();

	UpdateContext.CommitUpdate();
}
#endif 

void UNiagaraSystem::PostLoad()
{
	Super::PostLoad();

	ExposedParameters.PostLoad();
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
		GetObjectsWithOuter((UObject*)PackageOuter, ObjectsInPackage);
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

#if UE_EDITOR
	ExposedParameters.RecreateRedirections();
#endif

#if WITH_EDITORONLY_DATA
	if (!GetOutermost()->bIsCookedForEditor)
	{
		TArray<UNiagaraScript*> AllSystemScripts;
		UNiagaraScriptSourceBase* SystemScriptSource = nullptr;
		if (SystemSpawnScript == nullptr)
		{
			SystemSpawnScript = NewObject<UNiagaraScript>(this, "SystemSpawnScript", RF_Transactional);
			SystemSpawnScript->SetUsage(ENiagaraScriptUsage::SystemSpawnScript);
			INiagaraModule& NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>("Niagara");
			SystemScriptSource = NiagaraModule.GetEditorOnlyDataUtilities().CreateDefaultScriptSource(this);
			SystemSpawnScript->SetSource(SystemScriptSource);
		}
		else
		{
			SystemSpawnScript->ConditionalPostLoad();
			SystemScriptSource = SystemSpawnScript->GetSource();
		}
		AllSystemScripts.Add(SystemSpawnScript);

		if (SystemUpdateScript == nullptr)
		{
			SystemUpdateScript = NewObject<UNiagaraScript>(this, "SystemUpdateScript", RF_Transactional);
			SystemUpdateScript->SetUsage(ENiagaraScriptUsage::SystemUpdateScript);
			SystemUpdateScript->SetSource(SystemScriptSource);
		}
		else
		{
			SystemUpdateScript->ConditionalPostLoad();
		}
		AllSystemScripts.Add(SystemUpdateScript);

		const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);

		//TODO: This causes a crash becuase the script source ptr is null? Fix
		//For existing emitters before the lifecylce rework, ensure they have the system lifecycle module.
		if (NiagaraVer < FNiagaraCustomVersion::LifeCycleRework)
		{
			/*UNiagaraScriptSourceBase* SystemScriptSource = SystemUpdateScript->GetSource();
			if (SystemScriptSource)
			{
				bool bFoundModule;
				if (SystemScriptSource->AddModuleIfMissing(TEXT("/Niagara/Modules/System/SystemLifeCycle.SystemLifeCycle"), ENiagaraScriptUsage::SystemUpdateScript, bFoundModule))
				{
					bNeedsRecompile = true;
				}
			}*/
		}

		bool bSystemScriptsAreSynchronized = true;
		for (UNiagaraScript* SystemScript : AllSystemScripts)
		{
			bSystemScriptsAreSynchronized &= SystemScript->AreScriptAndSourceSynchronized();
		}

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
			EmitterHandle.ConditionalPostLoad(NiagaraVer);
			if (EmitterHandle.GetIsEnabled() && !EmitterHandle.GetInstance()->AreAllScriptAndSourcesSynchronized())
			{
				bEmitterScriptsAreSynchronized = false;
				break;
			}
		}

		if (EditorData == nullptr)
		{
			INiagaraModule& NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>("Niagara");
			EditorData = NiagaraModule.GetEditorOnlyDataUtilities().CreateDefaultEditorData(this);
		}
		else
		{
			EditorData->PostLoadFromOwner(this);
		}

		if (UNiagaraEmitter::GetForceCompileOnLoad())
		{
			InvalidateCachedCompileIds();
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

		if (SystemCompiledData.InstanceParamStore.GetNumParameters() == 0 ||SystemCompiledData.DataSetCompiledData.Variables.Num() == 0)
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
#endif // WITH_EDITORONLY_DATA

	if ( FPlatformProperties::RequiresCookedData() )
	{
		bIsReadyToRunCached = IsReadyToRunInternal();
	}

	ResolveScalabilityOverrides();
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
	if (!SystemSpawnScript || !SystemUpdateScript)
	{
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
	if (EmitterHandles.Num() != EmitterCompiledData.Num() || EmitterHandles.Num() != SystemCompiledData.NumParticleVars.Num() || EmitterHandles.Num() != SystemCompiledData.TotalSpawnedParticlesVars.Num() || EmitterHandles.Num() != SystemCompiledData.SpawnCountScaleVars.Num())
	{
		return false;
	}
#endif

	if (SystemSpawnScript->IsScriptCompilationPending(false) || 
		SystemUpdateScript->IsScriptCompilationPending(false))
	{
		return false;
	}

	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetInstance() && !Handle.GetInstance()->IsReadyToRun())
		{
			return false;
		}
	}
	return true;
}

bool UNiagaraSystem::IsReadyToRun() const
{
	if (FPlatformProperties::RequiresCookedData())
	{
		return bIsReadyToRunCached;
	}
	else
	{
		return IsReadyToRunInternal();
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraSystem::HasOutstandingCompilationRequests() const
{
	return ActiveCompilations.Num() > 0;
}
#endif

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

	CheckDICompileInfo(SystemSpawnScript->GetVMExecutableData().DataInterfaceInfo, bHasSystemScriptDIsWithPerInstanceData, UserDINamesReadInSystemScripts);
	CheckDICompileInfo(SystemUpdateScript->GetVMExecutableData().DataInterfaceInfo, bHasSystemScriptDIsWithPerInstanceData, UserDINamesReadInSystemScripts);
}

bool UNiagaraSystem::IsValid()const
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


	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (Handle.GetIsEnabled() && Handle.GetInstance() && !Handle.GetInstance()->IsValid())
		{
			return false;
		}
	}

	return true;
}
#if WITH_EDITORONLY_DATA

FNiagaraEmitterHandle UNiagaraSystem::AddEmitterHandle(UNiagaraEmitter& InEmitter, FName EmitterName)
{
	UNiagaraEmitter* NewEmitter = UNiagaraEmitter::CreateWithParentAndOwner(InEmitter, this, EmitterName, ~(RF_Public | RF_Standalone));
	FNiagaraEmitterHandle EmitterHandle(*NewEmitter);
	if (InEmitter.bIsTemplateAsset)
	{
		NewEmitter->bIsTemplateAsset = false;
		NewEmitter->TemplateAssetDescription = FText();
		NewEmitter->RemoveParent();
	}
	EmitterHandles.Add(EmitterHandle);
	RefreshSystemParametersFromEmitter(EmitterHandle);
	return EmitterHandle;
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

void UNiagaraSystem::InvalidateCachedCompileIds()
{
	check(SystemSpawnScript->GetSource() == SystemUpdateScript->GetSource());
	SystemSpawnScript->GetSource()->InvalidateCachedCompileIds();

	for (FNiagaraEmitterHandle Handle : EmitterHandles)
	{
		if (Handle.GetInstance())
		{
			UNiagaraScriptSourceBase* GraphSource = Handle.GetInstance()->GraphSource;
			GraphSource->InvalidateCachedCompileIds();
		}
	}
}

void UNiagaraSystem::WaitForCompilationComplete()
{
	while (ActiveCompilations.Num() > 0)
	{
		QueryCompileComplete(true, ActiveCompilations.Num() == 1);
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

bool UNiagaraSystem::QueryCompileComplete(bool bWait, bool bDoPost, bool bDoNotApply)
{
	if (ActiveCompilations.Num() > 0)
	{
		int32 ActiveCompileIdx = 0;

		bool bAreWeWaitingForAnyResults = false;

		// Check to see if ALL of the sub-requests have resolved. 
		for (FEmitterCompiledScriptPair& EmitterCompiledScriptPair : ActiveCompilations[ActiveCompileIdx].EmitterCompiledScriptPairs)
		{
			if ((uint32)INDEX_NONE == EmitterCompiledScriptPair.PendingDDCID || EmitterCompiledScriptPair.bResultsReady)
			{
				continue;
			}
			if (bWait)
			{
				GetDerivedDataCacheRef().WaitAsynchronousCompletion(EmitterCompiledScriptPair.PendingDDCID);
				EmitterCompiledScriptPair.bResultsReady = true;
			}
			else
			{
				EmitterCompiledScriptPair.bResultsReady = GetDerivedDataCacheRef().PollAsynchronousCompletion(EmitterCompiledScriptPair.PendingDDCID);
				if (!EmitterCompiledScriptPair.bResultsReady)
				{
					bAreWeWaitingForAnyResults = true;
				}
			}

			// If the results are ready, go ahead and cache them so that the pending task isn't removed prematurely...
			if (EmitterCompiledScriptPair.bResultsReady)
			{
				TArray<uint8> OutData;
				bool bBuiltLocally = false;
				if (GetDerivedDataCacheRef().GetAsynchronousResults(EmitterCompiledScriptPair.PendingDDCID, OutData, &bBuiltLocally))
				{
					if (bBuiltLocally)
					{
						UE_LOG(LogNiagara, Log, TEXT("UNiagraScript \'%s\' was built locally.."), *EmitterCompiledScriptPair.CompiledScript->GetFullName());
					}

					TSharedPtr<FNiagaraVMExecutableData> ExeData = MakeShared<FNiagaraVMExecutableData>();
					EmitterCompiledScriptPair.CompileResults = ExeData;
					if (!bDoNotApply)
					{
						FNiagaraScriptDerivedData::BinaryToExecData(OutData, *(ExeData.Get()));
					}
				}
			}
		}

		check(bWait ? (bAreWeWaitingForAnyResults == false) : true);

		// Make sure that we aren't waiting for any results to come back.
		if (bAreWeWaitingForAnyResults && !bWait)
		{
			return false;
		}

		// In the world of do not apply, we're exiting the system completely so let's just kill any active compilations altogether.
		if (bDoNotApply)
		{
			ActiveCompilations[ActiveCompileIdx].RootObjects.Empty();
			ActiveCompilations.RemoveAt(ActiveCompileIdx);
			return true;
		}


		SCOPE_CYCLE_COUNTER(STAT_Niagara_System_CompileScript);

		// Now that the above code says they are all complete, go ahead and resolve them all at once.
		float CombinedCompileTime = 0.0f;
		for (FEmitterCompiledScriptPair& EmitterCompiledScriptPair : ActiveCompilations[ActiveCompileIdx].EmitterCompiledScriptPairs)
		{
			if ((uint32)INDEX_NONE == EmitterCompiledScriptPair.PendingDDCID )
			{
				continue;
			}
			CombinedCompileTime += EmitterCompiledScriptPair.CompileResults->CompileTime;
			check(EmitterCompiledScriptPair.bResultsReady);

			TSharedPtr<FNiagaraVMExecutableData> ExeData = EmitterCompiledScriptPair.CompileResults;
			TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> PrecompData = ActiveCompilations[ActiveCompileIdx].MappedData.FindChecked(EmitterCompiledScriptPair.CompiledScript);
			EmitterCompiledScriptPair.CompiledScript->SetVMCompilationResults(EmitterCompiledScriptPair.CompileId, *(ExeData.Get()), PrecompData.Get());	
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

		// Prepare rapid iteration parameters for execution.
		TArray<UNiagaraScript*> Scripts;
		TMap<UNiagaraScript*, UNiagaraScript*> ScriptDependencyMap;
		TMap<UNiagaraScript*, FString> ScriptToEmitterNameMap;
		for (FEmitterCompiledScriptPair& EmitterCompiledScriptPair : ActiveCompilations[ActiveCompileIdx].EmitterCompiledScriptPairs)
		{
			UNiagaraEmitter* Emitter = EmitterCompiledScriptPair.Emitter;
			UNiagaraScript* CompiledScript = EmitterCompiledScriptPair.CompiledScript;

			Scripts.AddUnique(CompiledScript);
			ScriptToEmitterNameMap.Add(CompiledScript, Emitter != nullptr ? Emitter->GetUniqueEmitterName() : FString());

			if (UNiagaraScript::IsEquivalentUsage(CompiledScript->GetUsage(), ENiagaraScriptUsage::EmitterSpawnScript))
			{
				Scripts.AddUnique(SystemSpawnScript);
				ScriptDependencyMap.Add(CompiledScript, SystemSpawnScript);
				ScriptToEmitterNameMap.Add(SystemSpawnScript, FString());
			}

			if (UNiagaraScript::IsEquivalentUsage(CompiledScript->GetUsage(), ENiagaraScriptUsage::EmitterUpdateScript))
			{
				Scripts.AddUnique(SystemUpdateScript);
				ScriptDependencyMap.Add(CompiledScript, SystemUpdateScript);
				ScriptToEmitterNameMap.Add(SystemSpawnScript, FString());
			}

			if (UNiagaraScript::IsEquivalentUsage(CompiledScript->GetUsage(), ENiagaraScriptUsage::ParticleSpawnScript))
			{
				if (Emitter && Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
				{
					Scripts.AddUnique(Emitter->GetGPUComputeScript());
					ScriptDependencyMap.Add(CompiledScript, Emitter->GetGPUComputeScript());
					ScriptToEmitterNameMap.Add(Emitter->GetGPUComputeScript(), Emitter->GetUniqueEmitterName());
				}
			}

			if (UNiagaraScript::IsEquivalentUsage(CompiledScript->GetUsage(), ENiagaraScriptUsage::ParticleUpdateScript))
			{
				if (Emitter && Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
				{
					Scripts.AddUnique(Emitter->GetGPUComputeScript());
					ScriptDependencyMap.Add(CompiledScript, Emitter->GetGPUComputeScript());
					ScriptToEmitterNameMap.Add(Emitter->GetGPUComputeScript(), Emitter->GetUniqueEmitterName());
				}
				else if (Emitter && Emitter->bInterpolatedSpawning)
				{
					Scripts.AddUnique(Emitter->SpawnScriptProps.Script);
					ScriptDependencyMap.Add(CompiledScript, Emitter->SpawnScriptProps.Script);
					ScriptToEmitterNameMap.Add(Emitter->SpawnScriptProps.Script, Emitter->GetUniqueEmitterName());
				}
			}
		}

		FNiagaraUtilities::PrepareRapidIterationParameters(Scripts, ScriptDependencyMap, ScriptToEmitterNameMap);

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

		UE_LOG(LogNiagara, Verbose, TEXT("Compiling System %s took %f sec (wall time), %f sec (combined time)."), *GetFullName(), (float)(FPlatformTime::Seconds() - ActiveCompilations[ActiveCompileIdx].StartTime),
			CombinedCompileTime);

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

#if WITH_EDITORONLY_DATA
void UNiagaraSystem::InitEmitterVariableAliasNames(FNiagaraEmitterCompiledData& EmitterCompiledDataToInit, const UNiagaraEmitter* InAssociatedEmitter)
{
	EmitterCompiledDataToInit.EmitterSpawnIntervalVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_SPAWN_INTERVAL, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterInterpSpawnStartDTVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_INTERP_SPAWN_START_DT, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterAgeVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_AGE, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterSpawnGroupVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_SPAWN_GROUP, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterRandomSeedVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_EMITTER_RANDOM_SEED, InAssociatedEmitter));
	EmitterCompiledDataToInit.EmitterTotalSpawnedParticlesVar.SetName(GetEmitterVariableAliasName(SYS_PARAM_ENGINE_EMITTER_TOTAL_SPAWNED_PARTICLES, InAssociatedEmitter));
}

const FName UNiagaraSystem::GetEmitterVariableAliasName(const FNiagaraVariable& InEmitterVar, const UNiagaraEmitter* InEmitter) const
{
	return FName(*InEmitterVar.GetName().ToString().Replace(TEXT("Emitter."), *(InEmitter->GetUniqueEmitterName() + TEXT("."))));
}

void UNiagaraSystem::InitEmitterDataSetCompiledData(FNiagaraDataSetCompiledData& DataSetToInit, const UNiagaraEmitter* InAssociatedEmitter, const FNiagaraEmitterHandle& InAssociatedEmitterHandle)
{
	DataSetToInit.Empty();

	DataSetToInit.Variables = InAssociatedEmitter->UpdateScriptProps.Script->GetVMExecutableData().Attributes;
	for (const FNiagaraVariable& Var : InAssociatedEmitter->SpawnScriptProps.Script->GetVMExecutableData().Attributes)
	{
		DataSetToInit.Variables.AddUnique(Var);
	}

	DataSetToInit.bNeedsPersistentIDs = InAssociatedEmitter->RequiresPersistantIDs() || DataSetToInit.Variables.Contains(SYS_PARAM_PARTICLES_ID);
	DataSetToInit.ID = FNiagaraDataSetID(InAssociatedEmitterHandle.GetIdName(), ENiagaraDataSetType::ParticleData);
	DataSetToInit.SimTarget = InAssociatedEmitter->SimTarget;

	DataSetToInit.BuildLayout();
}
#endif

bool UNiagaraSystem::RequestCompile(bool bForce)
{
	if (bForce)
	{
		InvalidateCachedCompileIds();
		bForce = false;
	}

	if (ActiveCompilations.Num() > 0)
	{
		PollForCompilationComplete();
	}
	
	int32 ActiveCompileIdx = ActiveCompilations.AddDefaulted();
	ActiveCompilations[ActiveCompileIdx].StartTime = FPlatformTime::Seconds();

	SCOPE_CYCLE_COUNTER(STAT_Niagara_System_Precompile);
	
	check(SystemSpawnScript->GetSource() == SystemUpdateScript->GetSource());
	TArray<FNiagaraVariable> OriginalExposedParams;
	GetExposedParameters().GetParameters(OriginalExposedParams);

	INiagaraModule& NiagaraModule = FModuleManager::Get().LoadModuleChecked<INiagaraModule>(TEXT("Niagara"));
	TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> SystemPrecompiledData = NiagaraModule.Precompile(this);

	SystemPrecompiledData->GetReferencedObjects(ActiveCompilations[ActiveCompileIdx].RootObjects);

	//Compile all emitters
	bool bTrulyAsync = true;
	bool bAnyUnsynchronized = false;	

	ActiveCompilations[ActiveCompileIdx].MappedData.Add(SystemSpawnScript, SystemPrecompiledData);
	ActiveCompilations[ActiveCompileIdx].MappedData.Add(SystemUpdateScript, SystemPrecompiledData);

	check(EmitterHandles.Num() == SystemPrecompiledData->GetDependentRequestCount());

	// Grab the list of user variables that were actually encountered so that we can add to them later.
	TArray<FNiagaraVariable> EncounteredExposedVars;
	SystemPrecompiledData->GatherPreCompiledVariables(TEXT("User"), EncounteredExposedVars);

	for (int32 i = 0; i < EmitterHandles.Num(); i++)
	{
		FNiagaraEmitterHandle Handle = EmitterHandles[i];
		if (Handle.GetInstance() && Handle.GetIsEnabled())
		{
			UNiagaraScriptSourceBase* GraphSource = Handle.GetInstance()->GraphSource;
			TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> EmitterPrecompiledData = SystemPrecompiledData->GetDependentRequest(i);
			EmitterPrecompiledData->GetReferencedObjects(ActiveCompilations[ActiveCompileIdx].RootObjects);

			TArray<UNiagaraScript*> EmitterScripts;
			Handle.GetInstance()->GetScripts(EmitterScripts, false);
			check(EmitterScripts.Num() > 0);
			for (UNiagaraScript* EmitterScript : EmitterScripts)
			{
				ActiveCompilations[ActiveCompileIdx].MappedData.Add(EmitterScript, EmitterPrecompiledData);

				FEmitterCompiledScriptPair Pair;
				Pair.bResultsReady = false;
				Pair.Emitter = Handle.GetInstance();
				Pair.CompiledScript = EmitterScript;
				if (EmitterScript->RequestExternallyManagedAsyncCompile(EmitterPrecompiledData, Pair.CompileId, Pair.PendingDDCID, bTrulyAsync))
				{
					bAnyUnsynchronized = true;
				}
				ActiveCompilations[ActiveCompileIdx].EmitterCompiledScriptPairs.Add(Pair);
			}

			// Add the emitter's User variables to the encountered list to expose for later.
			EmitterPrecompiledData->GatherPreCompiledVariables(TEXT("User"), EncounteredExposedVars);
		}
	}

	bool bForceSystems = bForce || bAnyUnsynchronized;
	bool bAnyCompiled = bAnyUnsynchronized || bForce;

	// Now add the system scripts for compilation...
	{
		FEmitterCompiledScriptPair Pair;
		Pair.bResultsReady = false;
		Pair.Emitter = nullptr;
		Pair.CompiledScript = SystemSpawnScript;
		if (SystemSpawnScript->RequestExternallyManagedAsyncCompile(SystemPrecompiledData, Pair.CompileId, Pair.PendingDDCID, bTrulyAsync))
		{
			bAnyCompiled = true;
		}
		ActiveCompilations[ActiveCompileIdx].EmitterCompiledScriptPairs.Add(Pair);
	}

	{
		FEmitterCompiledScriptPair Pair;
		Pair.bResultsReady = false;
		Pair.Emitter = nullptr;
		Pair.CompiledScript = SystemUpdateScript;
		if (SystemUpdateScript->RequestExternallyManagedAsyncCompile(SystemPrecompiledData, Pair.CompileId, Pair.PendingDDCID, bTrulyAsync))
		{
			bAnyCompiled = true;
		}
		ActiveCompilations[ActiveCompileIdx].EmitterCompiledScriptPairs.Add(Pair);
	}

	// Now let's synchronize the variables that we actually encountered during compile so that we can expose them to the end user.
	for (int32 i = 0; i < EncounteredExposedVars.Num(); i++)
	{
		if (OriginalExposedParams.Contains(EncounteredExposedVars[i]) == false)
		{
			// Just in case it wasn't added previously..
			ExposedParameters.AddParameter(EncounteredExposedVars[i]);
		}
	}

	FNiagaraSystemUpdateContext UpdateCtx(this, true);

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
	SystemCompiledData.NumParticleVars.Empty();
	SystemCompiledData.TotalSpawnedParticlesVars.Empty();
	SystemCompiledData.SpawnCountScaleVars.Empty();
	SystemCompiledData.InstanceParamStore.Empty();

	SystemCompiledData.InstanceParamStore = INiagaraModule::GetFixedSystemInstanceParameterStore();
	ExposedParameters.CopyParametersTo(SystemCompiledData.InstanceParamStore, false, FNiagaraParameterStore::EDataInterfaceCopyMethod::Reference);

	for (const FNiagaraEmitterHandle& PerEmitterHandle : EmitterHandles)
	{
		const UNiagaraEmitter* Emitter = PerEmitterHandle.GetInstance();
		if (ensureMsgf(Emitter != nullptr, TEXT("Failed to get Emitter Instance from Emitter Handle when post compiling Niagara System!")))
		{
			const FString EmitterName = Emitter->GetUniqueEmitterName();
			{
				FNiagaraVariable Var = SYS_PARAM_ENGINE_EMITTER_NUM_PARTICLES;
				const FString ParamName = Var.GetName().ToString().Replace(TEXT("Emitter"), *EmitterName);
				Var.SetName(*ParamName);
				SystemCompiledData.InstanceParamStore.AddParameter(Var, true, false);
				SystemCompiledData.NumParticleVars.Add(Var);
			}
			{
				FNiagaraVariable Var = SYS_PARAM_ENGINE_EMITTER_TOTAL_SPAWNED_PARTICLES;
				const FString ParamName = Var.GetName().ToString().Replace(TEXT("Emitter"), *EmitterName);
				Var.SetName(*ParamName);
				SystemCompiledData.InstanceParamStore.AddParameter(Var, true, false);
				SystemCompiledData.TotalSpawnedParticlesVars.Add(Var);
			}
			{
				FNiagaraVariable Var = SYS_PARAM_ENGINE_EMITTER_SPAWN_COUNT_SCALE;
				const FString ParamName = Var.GetName().ToString().Replace(TEXT("Emitter"), *EmitterName);
				Var.SetName(*ParamName);
				SystemCompiledData.InstanceParamStore.AddParameter(Var, true, false);
				SystemCompiledData.SpawnCountScaleVars.Add(Var);
			}
		}
	}

	auto CreateDataSetCompiledData = [&](FNiagaraDataSetCompiledData& CompiledData, TArrayView<FNiagaraVariable> Vars)
	{
		CompiledData.Empty();

		CompiledData.Variables.Reset(Vars.Num());
		for (const FNiagaraVariable& Var : Vars)
		{
			CompiledData.Variables.AddUnique(Var);
		}

		CompiledData.bNeedsPersistentIDs = false;
		CompiledData.ID = FNiagaraDataSetID();
		CompiledData.SimTarget = ENiagaraSimTarget::CPUSim;

		CompiledData.BuildLayout();
	};

	CreateDataSetCompiledData(SystemCompiledData.DataSetCompiledData, GetSystemUpdateScript()->GetVMExecutableData().Attributes);

	FNiagaraParameters* EngineParamsSpawn = GetSystemSpawnScript()->GetVMExecutableData().DataSetToParameters.Find(TEXT("Engine"));
	CreateDataSetCompiledData(SystemCompiledData.SpawnInstanceParamsDataSetCompiledData, EngineParamsSpawn ? EngineParamsSpawn->Parameters : TArrayView<FNiagaraVariable>());
	FNiagaraParameters* EngineParamsUpdate = GetSystemUpdateScript()->GetVMExecutableData().DataSetToParameters.Find(TEXT("Engine"));
	CreateDataSetCompiledData(SystemCompiledData.UpdateInstanceParamsDataSetCompiledData, EngineParamsUpdate ? EngineParamsUpdate->Parameters : TArrayView<FNiagaraVariable>());
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

void UNiagaraSystem::GenerateStatID()const
{
#if STATS
	StatID_GT = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraSystems>(GetPathName() + TEXT("[GT]"));
	StatID_GT_CNC = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraSystems>(GetPathName() + TEXT("[GT_CNC]"));
	StatID_RT = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraSystems>(GetPathName() + TEXT("[RT]"));
	StatID_RT_CNC = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraSystems>(GetPathName() + TEXT("[RT_CNC]"));
#endif
}

UNiagaraEffectType* UNiagaraSystem::GetEffectType()const
{
	if (EffectType)
	{
		return EffectType;
	}
	
	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	check(Settings);
	return Settings->GetDefaultEffectType();
}

const FNiagaraScalabilitySettings& UNiagaraSystem::GetScalabilitySettings(int32 DetailLevel)
{
#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
	int32 UseDetailLevel = DetailLevel == INDEX_NONE ? INiagaraModule::GetDetailLevel() : DetailLevel;

	int32 SettingsIndex = ResolvedScalabilitySettings.IsValidIndex(UseDetailLevel) ? UseDetailLevel : ResolvedScalabilitySettings.Num() - 1;
	check(SettingsIndex != INDEX_NONE);//We should always have at least one entry in our settings array.
	return ResolvedScalabilitySettings[SettingsIndex];
#else
	return CurrentScalabilitySettings;
#endif
}

void UNiagaraSystem::ResolveScalabilityOverrides()
{
	ResolvedScalabilitySettings.Empty();

	UNiagaraEffectType* ActualEffectType = GetEffectType();
	int32 NumEffectTypeSettings = 0;
	if (ActualEffectType)
	{
		NumEffectTypeSettings = ActualEffectType->GetScalabilitySettings().Num();
	}
	int32 NumOverrides = ScalabilityOverrides.Num();

	int32 ResolvedCount = FMath::Max(NumEffectTypeSettings, NumOverrides);
	if (ResolvedCount == 0)
	{
		//Just add a single defaulted entry to make other calling code simpler.
		ResolvedScalabilitySettings.AddDefaulted();
	}
	else
	{

		ResolvedScalabilitySettings.Reserve(ResolvedCount);
		for (int32 i = 0; i < ResolvedCount; ++i)
		{
			int32 NewIdx = ResolvedScalabilitySettings.AddDefaulted();
			FNiagaraScalabilitySettings& NewSettings = ResolvedScalabilitySettings[NewIdx];
			if (ActualEffectType)
			{
				const TArray<FNiagaraScalabilitySettings>& EffectTypeSettings = ActualEffectType->GetScalabilitySettings();
				int32 LastIndex = EffectTypeSettings.Num() - 1;
				if (LastIndex >= 0)
				{
					NewSettings = EffectTypeSettings[FMath::Min(i, LastIndex)];
				}
			}

			if (ScalabilityOverrides.IsValidIndex(i) && bOverrideScalabilitySettings)
			{
				const FNiagaraScalabilityOverrides& Overrides = ScalabilityOverrides[i];

				if (Overrides.bOverrideDistanceSettings)
				{
					NewSettings.bCullByDistance = Overrides.bCullByDistance;
					NewSettings.MaxDistance = Overrides.MaxDistance;
				}

				if (Overrides.bOverrideOwnerLODSettings)
				{
					NewSettings.bCullByMaxOwnerLOD = Overrides.bCullByMaxOwnerLOD;
					NewSettings.MaxOwnerLOD = Overrides.MaxOwnerLOD;
				}

				if (Overrides.bOverrideInstanceCountSettings)
				{
					NewSettings.bCullMaxInstanceCount = Overrides.bCullMaxInstanceCount;
					NewSettings.MaxInstances = Overrides.MaxInstances;
				}

				if (Overrides.bOverrideTimeSinceRendererSettings)
				{
					NewSettings.bCullByMaxTimeWithoutRender = Overrides.bCullByMaxTimeWithoutRender;
					NewSettings.MaxTimeWithoutRender = Overrides.MaxTimeWithoutRender;
				}

// 				if (Overrides.bOverrideSpawnCountScale)
// 				{
// 					NewSettings.SpawnCountScale = Overrides.SpawnCountScale;
// 				}
			}
		}
	}

	OnDetailLevelChanges(INiagaraModule::GetDetailLevel());
}

void UNiagaraSystem::OnDetailLevelChanges(int32 DetailLevel)
{
	int32 SettingsIndex = ResolvedScalabilitySettings.IsValidIndex(DetailLevel) ? DetailLevel : ResolvedScalabilitySettings.Num() - 1;
	check(SettingsIndex != INDEX_NONE);//We should always have at least one entry in our settings array.
	CurrentScalabilitySettings = ResolvedScalabilitySettings[SettingsIndex];
}


FNiagaraEmitterCompiledData::FNiagaraEmitterCompiledData()
{
	EmitterSpawnIntervalVar = SYS_PARAM_EMITTER_SPAWN_INTERVAL;
	EmitterInterpSpawnStartDTVar = SYS_PARAM_EMITTER_INTERP_SPAWN_START_DT;
	EmitterAgeVar = SYS_PARAM_EMITTER_AGE;
	EmitterSpawnGroupVar = SYS_PARAM_EMITTER_SPAWN_GROUP;
	EmitterRandomSeedVar = SYS_PARAM_EMITTER_RANDOM_SEED;
	EmitterTotalSpawnedParticlesVar = SYS_PARAM_ENGINE_EMITTER_TOTAL_SPAWNED_PARTICLES;
}
