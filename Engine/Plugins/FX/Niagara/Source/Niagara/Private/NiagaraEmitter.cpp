// Copyright Epic Games, Inc. All Rights Reserved.


#include "NiagaraEmitter.h"

#include "INiagaraEditorOnlyDataUtlities.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraEditorDataBase.h"
#include "NiagaraModule.h"
#include "NiagaraRenderer.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraSettings.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraStats.h"
#include "NiagaraSystem.h"
#include "NiagaraTrace.h"
#include "Interfaces/ITargetPlatform.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

#if WITH_EDITOR
const FName UNiagaraEmitter::PrivateMemberNames::EventHandlerScriptProps = GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, EventHandlerScriptProps);

const FString InitialNotSynchronizedReason("Emitter created");
#endif

static int32 GbForceNiagaraCompileOnLoad = 0;
static FAutoConsoleVariableRef CVarForceNiagaraCompileOnLoad(
	TEXT("fx.ForceCompileOnLoad"),
	GbForceNiagaraCompileOnLoad,
	TEXT("If > 0 emitters will be forced to compile on load. \n"),
	ECVF_Default
	);

static int32 GbForceNiagaraMergeOnLoad = 0;
static FAutoConsoleVariableRef CVarForceNiagaraMergeOnLoad(
	TEXT("fx.ForceMergeOnLoad"),
	GbForceNiagaraMergeOnLoad,
	TEXT("If > 0 emitters will be forced to merge on load. \n"),
	ECVF_Default
);

static int32 GbForceNiagaraFailToCompile = 0;
static FAutoConsoleVariableRef CVarForceNiagaraCompileToFail(
	TEXT("fx.ForceNiagaraCompileToFail"),
	GbForceNiagaraFailToCompile,
	TEXT("If > 0 emitters will go through the motions of a compile, but will never set valid bytecode. \n"),
	ECVF_Default
);

static int32 GbEnableEmitterChangeIdMergeLogging = 0;
static FAutoConsoleVariableRef CVarEnableEmitterChangeIdMergeLogging(
	TEXT("fx.EnableEmitterMergeChangeIdLogging"),
	GbEnableEmitterChangeIdMergeLogging,
	TEXT("If > 0 verbose change id information will be logged to help with debuggin merge issues. \n"),
	ECVF_Default
);

static int32 GDebugForcedMaxGPUBufferElements = 0;
static FAutoConsoleVariableRef CVarNiagaraDebugForcedMaxGPUBufferElements(
	TEXT("fx.NiagaraDebugForcedMaxGPUBufferElements"),
	GDebugForcedMaxGPUBufferElements,
	TEXT("Force the maximum buffer size supported by the GPU to this value, for debugging purposes."),
	ECVF_Default
);

FNiagaraDetailsLevelScaleOverrides::FNiagaraDetailsLevelScaleOverrides()
{
	Low = 0.125f;
	Medium = 0.25f;
	High = 0.5f;
	Epic = 1.0f;
	Cine = 1.0f;
}

void FNiagaraEmitterScriptProperties::InitDataSetAccess()
{
	EventReceivers.Empty();
	EventGenerators.Empty();

	if (Script && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim))
	{
		//UE_LOG(LogNiagara, Log, TEXT("InitDataSetAccess: %s %d %d"), *Script->GetPathName(), Script->ReadDataSets.Num(), Script->WriteDataSets.Num());
		// TODO: add event receiver and generator lists to the script properties here
		//
		for (FNiagaraDataSetID &ReadID : Script->GetVMExecutableData().ReadDataSets)
		{
			EventReceivers.Add( FNiagaraEventReceiverProperties(ReadID.Name, NAME_None, NAME_None) );
		}

		for (FNiagaraDataSetProperties &WriteID : Script->GetVMExecutableData().WriteDataSets)
		{
			FNiagaraEventGeneratorProperties Props(WriteID, NAME_None);
			EventGenerators.Add(Props);
		}
	}
}

bool FNiagaraEmitterScriptProperties::DataSetAccessSynchronized() const
{
	if (Script && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim))
	{
		if (Script->GetVMExecutableData().ReadDataSets.Num() != EventReceivers.Num())
		{
			return false;
		}
		if (Script->GetVMExecutableData().WriteDataSets.Num() != EventGenerators.Num())
		{
			return false;
		}
		return true;
	}
	else
	{
		return EventReceivers.Num() == 0 && EventGenerators.Num() == 0;
	}
}

//////////////////////////////////////////////////////////////////////////

UNiagaraEmitter::UNiagaraEmitter(const FObjectInitializer& Initializer)
: Super(Initializer)
, PreAllocationCount(0)
, FixedBounds(FBox(FVector(-100), FVector(100)))
, MinDetailLevel_DEPRECATED(0)
, MaxDetailLevel_DEPRECATED(4)
, bInterpolatedSpawning(false)
, bFixedBounds(false)
, bUseMinDetailLevel_DEPRECATED(false)
, bUseMaxDetailLevel_DEPRECATED(false)
, bRequiresPersistentIDs(false)
, bCombineEventSpawn(false)
, MaxDeltaTimePerTick(0.125)
, DefaultShaderStageIndex(0)
, MaxUpdateIterations(1)
, bLimitDeltaTime(true)
#if WITH_EDITORONLY_DATA
, bBakeOutRapidIteration(true)
, ThumbnailImageOutOfDate(true)
#endif
{
}

void UNiagaraEmitter::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad) == false)
	{
		SpawnScriptProps.Script = NewObject<UNiagaraScript>(this, "SpawnScript", EObjectFlags::RF_Transactional);
		SpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::ParticleSpawnScript);

		UpdateScriptProps.Script = NewObject<UNiagaraScript>(this, "UpdateScript", EObjectFlags::RF_Transactional);
		UpdateScriptProps.Script->SetUsage(ENiagaraScriptUsage::ParticleUpdateScript);

#if WITH_EDITORONLY_DATA
		EmitterSpawnScriptProps.Script = NewObject<UNiagaraScript>(this, "EmitterSpawnScript", EObjectFlags::RF_Transactional);
		EmitterSpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::EmitterSpawnScript);
		
		EmitterUpdateScriptProps.Script = NewObject<UNiagaraScript>(this, "EmitterUpdateScript", EObjectFlags::RF_Transactional);
		EmitterUpdateScriptProps.Script->SetUsage(ENiagaraScriptUsage::EmitterUpdateScript);
#endif

		GPUComputeScript = NewObject<UNiagaraScript>(this, "GPUComputeScript", EObjectFlags::RF_Transactional);
		GPUComputeScript->SetUsage(ENiagaraScriptUsage::ParticleGPUComputeScript);

#if WITH_EDITORONLY_DATA && WITH_EDITOR
		if (EditorParameters == nullptr)
		{
			INiagaraModule& NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>("Niagara");
			EditorParameters = NiagaraModule.GetEditorOnlyDataUtilities().CreateDefaultEditorParameters(this);
		}
#endif
	}

#if WITH_EDITORONLY_DATA
	if (GPUComputeScript)
	{
		GPUComputeScript->OnGPUScriptCompiled().AddUObject(this, &UNiagaraEmitter::RaiseOnEmitterGPUCompiled);
	}
#endif

	UniqueEmitterName = TEXT("Emitter");

	ResolveScalabilitySettings();
}

#if WITH_EDITORONLY_DATA
bool UNiagaraEmitter::GetForceCompileOnLoad()
{
	return GbForceNiagaraCompileOnLoad > 0;
}

bool UNiagaraEmitter::IsSynchronizedWithParent() const
{
	if (Parent == nullptr)
	{
		// If the emitter has no parent than it is synchronized by default.
		return true;
	}

	if (ParentAtLastMerge == nullptr)
	{
		// If the parent was valid but the parent at last merge isn't, they we don't know if it's up to date so we say it's not, and let 
		// the actual merge code print an appropriate message to the log.
		return false;
	}

	if (Parent->GetChangeId().IsValid() == false ||
		ParentAtLastMerge->GetChangeId().IsValid() == false)
	{
		// If any of the change Ids aren't valid then we assume we're out of sync.
		return false;
	}

	// Otherwise check the change ids, and the force flag.
	return Parent->GetChangeId() == ParentAtLastMerge->GetChangeId() && GbForceNiagaraMergeOnLoad <= 0;
}

INiagaraMergeManager::FMergeEmitterResults UNiagaraEmitter::MergeChangesFromParent()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MergeEmitter);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*GetPathName(), NiagaraChannel);

	if (GbEnableEmitterChangeIdMergeLogging)
	{
		UE_LOG(LogNiagara, Log, TEXT("Emitter %s is merging changes from parent %s because its Change ID was updated."), *GetPathName(),
			Parent != nullptr ? *Parent->GetPathName() : TEXT("(null)"));

		UE_LOG(LogNiagara, Log, TEXT("\nEmitter %s Id=%s \nParentAtLastMerge %s id=%s \nParent %s Id=%s."), 
			*GetPathName(), *ChangeId.ToString(),
			ParentAtLastMerge != nullptr ? *ParentAtLastMerge->GetPathName() : TEXT("(null)"), ParentAtLastMerge != nullptr ? *ParentAtLastMerge->GetChangeId().ToString() : TEXT("(null)"),
			Parent != nullptr ? *Parent->GetPathName() : TEXT("(null)"), Parent != nullptr ? *Parent->GetChangeId().ToString() : TEXT("(null)"));
	}

	if (Parent == nullptr)
	{
		// If we don't have a copy of the parent emitter, this emitter can't safely be merged.
		INiagaraMergeManager::FMergeEmitterResults MergeResults;
		MergeResults.MergeResult = INiagaraMergeManager::EMergeEmitterResult::FailedToDiff;
		MergeResults.bModifiedGraph = false;
		MergeResults.ErrorMessages.Add(NSLOCTEXT("NiagaraEmitter", "NoParentErrorMessage", "This emitter has no 'Parent' so changes can't be merged in."));
		return MergeResults;
	}

	const bool bNoParentAtLastMerge = (ParentAtLastMerge == nullptr);

	INiagaraModule& NiagaraModule = FModuleManager::Get().GetModuleChecked<INiagaraModule>("Niagara");
	const INiagaraMergeManager& MergeManager = NiagaraModule.GetMergeManager();
	INiagaraMergeManager::FMergeEmitterResults MergeResults = MergeManager.MergeEmitter(*Parent, ParentAtLastMerge, *this);
	if (MergeResults.MergeResult == INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied || MergeResults.MergeResult == INiagaraMergeManager::EMergeEmitterResult::SucceededNoDifferences)
	{
		if (MergeResults.MergeResult == INiagaraMergeManager::EMergeEmitterResult::SucceededDifferencesApplied)
		{
			UpdateFromMergedCopy(MergeManager, MergeResults.MergedInstance);
		}

		// Update the last merged source and clear it's stand alone and public flags since it's not an asset.
		ParentAtLastMerge = Parent->DuplicateWithoutMerging(this);
		ParentAtLastMerge->ClearFlags(RF_Standalone | RF_Public);
	}
	else
	{
		UE_LOG(LogNiagara, Warning, TEXT("Failed to merge changes for parent emitter.  Emitter: %s  Parent Emitter: %s  Error Message: %s"),
			*GetPathName(), Parent != nullptr ? *Parent->GetPathName() : TEXT("(null)"), *MergeResults.GetErrorMessagesString());
	}

	return MergeResults;
}

bool UNiagaraEmitter::UsesEmitter(const UNiagaraEmitter& InEmitter) const
{
	return Parent == &InEmitter || (Parent != nullptr && Parent->UsesEmitter(InEmitter));
}

UNiagaraEmitter* UNiagaraEmitter::DuplicateWithoutMerging(UObject* InOuter)
{
	UNiagaraEmitter* Duplicate;
	{
		TGuardValue<UNiagaraEmitter*> ParentGuard(Parent, nullptr);
		TGuardValue<UNiagaraEmitter*> ParentAtLastMergeGuard(ParentAtLastMerge, nullptr);
		Duplicate = Cast<UNiagaraEmitter>(StaticDuplicateObject(this, InOuter));
	}
	return Duplicate;
}
#endif

void UNiagaraEmitter::Serialize(FArchive& Ar)
{

#if WITH_EDITORONLY_DATA
	for (UNiagaraSimulationStageBase* Stage : SimulationStages)
	{
		if (Stage)
		{
			if (Stage->Script)
			{
				if (!HasAnyFlags(RF_Transient))
				{
					if (Stage->Script->HasAnyFlags(RF_Transient))
					{
						UE_LOG(LogNiagara, Error, TEXT("Emitter \"%s\" has a simulation stage with a Transient script and the emitter itself isn't transient!"), *GetPathName());
					}
				}
			}
			else
			{
				UE_LOG(LogNiagara, Error, TEXT("Emitter \"%s\" has a simulation stage with a null Script entry!"), *GetPathName());
			}

		}
		else
		{
			UE_LOG(LogNiagara, Error, TEXT("Emitter \"%s\" has a simulation stage with a null entry!"), *GetPathName());
		}
	}
#endif
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
}

void UNiagaraEmitter::EnsureScriptsPostLoaded()
{
	TArray<UNiagaraScript*> AllScripts;
	GetScripts(AllScripts, false);

	// Post load scripts for use below.
	for (UNiagaraScript* Script : AllScripts)
	{
		Script->ConditionalPostLoad();
	}

	// Additionally we want to make sure that the GPUComputeScript, if it exists, is also post loaded immediately even if we're not using it.
	// Currently an unused GPUComputeScript will cause the cached data to be invalidated and rebuilt because it will never get
	// a valid CompilerVersionID assigned to it (since it's not being compiled because it's not being used).  The side effect of this is that
	// the invalidation occurs in a non-deterministic location (based on PostLoad order) and can mess up with the cooking process
	if (GPUComputeScript)
	{
		GPUComputeScript->ConditionalPostLoad();
	}
}

void UNiagaraEmitter::PostLoad()
{
	Super::PostLoad();

	if (GIsEditor)
	{
		SetFlags(RF_Transactional);
	}

	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);
	if (NiagaraVer < FNiagaraCustomVersion::PlatformScalingRefactor)
	{
		int32 MinDetailLevel = bUseMaxDetailLevel_DEPRECATED ? MinDetailLevel_DEPRECATED : 0;
		int32 MaxDetailLevel = bUseMaxDetailLevel_DEPRECATED ? MaxDetailLevel_DEPRECATED : 4;
		int32 NewQLMask = 0;
		//Currently all detail levels were direct mappings to quality level so just transfer them over to the new mask in PlatformSet.
		for (int32 QL = MinDetailLevel; QL <= MaxDetailLevel; ++QL)
		{
			NewQLMask |= (1 << QL);
		}

		Platforms = FNiagaraPlatformSet(NewQLMask);

		//Transfer spawn rate scaling overrides
		if (bOverrideGlobalSpawnCountScale_DEPRECATED)
		{
			FNiagaraEmitterScalabilityOverride& LowOverride = ScalabilityOverrides.Overrides.AddDefaulted_GetRef();
			LowOverride.Platforms = FNiagaraPlatformSet(FNiagaraPlatformSet::CreateQualityLevelMask(0));
			LowOverride.bOverrideSpawnCountScale = true;
			LowOverride.bScaleSpawnCount = true;
			LowOverride.SpawnCountScale = GlobalSpawnCountScaleOverrides_DEPRECATED.Low;

			FNiagaraEmitterScalabilityOverride& MediumOverride = ScalabilityOverrides.Overrides.AddDefaulted_GetRef();
			MediumOverride.Platforms = FNiagaraPlatformSet(FNiagaraPlatformSet::CreateQualityLevelMask(1));
			MediumOverride.bOverrideSpawnCountScale = true;
			MediumOverride.bScaleSpawnCount = true;
			MediumOverride.SpawnCountScale = GlobalSpawnCountScaleOverrides_DEPRECATED.Medium;

			FNiagaraEmitterScalabilityOverride& HighOverride = ScalabilityOverrides.Overrides.AddDefaulted_GetRef();
			HighOverride.Platforms = FNiagaraPlatformSet(FNiagaraPlatformSet::CreateQualityLevelMask(2));
			HighOverride.bOverrideSpawnCountScale = true;
			HighOverride.bScaleSpawnCount = true;
			HighOverride.SpawnCountScale = GlobalSpawnCountScaleOverrides_DEPRECATED.High;

			FNiagaraEmitterScalabilityOverride& EpicOverride = ScalabilityOverrides.Overrides.AddDefaulted_GetRef();
			EpicOverride.Platforms = FNiagaraPlatformSet(FNiagaraPlatformSet::CreateQualityLevelMask(3));
			EpicOverride.bOverrideSpawnCountScale = true;
			EpicOverride.bScaleSpawnCount = true;
			EpicOverride.SpawnCountScale = GlobalSpawnCountScaleOverrides_DEPRECATED.Epic;

			FNiagaraEmitterScalabilityOverride& CineOverride = ScalabilityOverrides.Overrides.AddDefaulted_GetRef();
			CineOverride.Platforms = FNiagaraPlatformSet(FNiagaraPlatformSet::CreateQualityLevelMask(4));
			CineOverride.bOverrideSpawnCountScale = true;
			CineOverride.bScaleSpawnCount = true;
			CineOverride.SpawnCountScale = GlobalSpawnCountScaleOverrides_DEPRECATED.Cine;
		}
	}

#if WITH_EDITORONLY_DATA
	if (EditorParameters == nullptr)
	{
		INiagaraModule& NiagaraModule = FModuleManager::GetModuleChecked<INiagaraModule>("Niagara");
		EditorParameters = NiagaraModule.GetEditorOnlyDataUtilities().CreateDefaultEditorParameters(this);
	}

	// this can only ever be true for old assets that haven't been loaded yet, so this won't overwrite subsequent changes to the template specification
	if(bIsTemplateAsset_DEPRECATED)
	{
		TemplateSpecification = ENiagaraScriptTemplateSpecification::Template;
	}
#endif

	for (int32 RendererIndex = RendererProperties.Num() - 1; RendererIndex >= 0; --RendererIndex)
	{
#if WITH_EDITOR
		if (ensureMsgf(RendererProperties[RendererIndex] != nullptr, TEXT("Null renderer found in %s at index %i, removing it to prevent crashes."), *GetPathName(), RendererIndex) == false)
#else
		if(RendererProperties[RendererIndex] == nullptr)
#endif
		//In cooked builds these can be cooked out and null on purpose.
		{
			RendererProperties.RemoveAt(RendererIndex);
		}
		else
		{
			RendererProperties[RendererIndex]->ConditionalPostLoad();
		}
	}

	for (int32 SimulationStageIndex = SimulationStages.Num() - 1; SimulationStageIndex >= 0; --SimulationStageIndex)
	{
		if (ensureMsgf(SimulationStages[SimulationStageIndex] != nullptr && SimulationStages[SimulationStageIndex]->Script != nullptr, TEXT("Null simulation stage, or simulation stage with a null script found in %s at index %i, removing it to prevent crashes."), *GetPathName(), SimulationStageIndex) == false)
		{
			SimulationStages.RemoveAt(SimulationStageIndex);
		}
		else
		{
			SimulationStages[SimulationStageIndex]->ConditionalPostLoad();
		}
	}

	if (SpawnScriptProps.Script)
	{
		SpawnScriptProps.Script->ConditionalPostLoad();
	}
	
#if WITH_EDITORONLY_DATA
	if (EditorData != nullptr)
	{
		EditorData->ConditionalPostLoad();
	}
	
	if (!GPUComputeScript)
	{
		GPUComputeScript = NewObject<UNiagaraScript>(this, "GPUComputeScript", EObjectFlags::RF_Transactional);
		GPUComputeScript->SetUsage(ENiagaraScriptUsage::ParticleGPUComputeScript);
		GPUComputeScript->SetLatestSource(SpawnScriptProps.Script ? SpawnScriptProps.Script->GetLatestSource() : nullptr);
	}
	GPUComputeScript->OnGPUScriptCompiled().AddUObject(this, &UNiagaraEmitter::RaiseOnEmitterGPUCompiled);

	if (EmitterSpawnScriptProps.Script == nullptr || EmitterUpdateScriptProps.Script == nullptr)
	{
		EmitterSpawnScriptProps.Script = NewObject<UNiagaraScript>(this, "EmitterSpawnScript", EObjectFlags::RF_Transactional);
		EmitterSpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::EmitterSpawnScript);

		EmitterUpdateScriptProps.Script = NewObject<UNiagaraScript>(this, "EmitterUpdateScript", EObjectFlags::RF_Transactional);
		EmitterUpdateScriptProps.Script->SetUsage(ENiagaraScriptUsage::EmitterUpdateScript);

		if (SpawnScriptProps.Script)
		{
			EmitterSpawnScriptProps.Script->SetLatestSource(SpawnScriptProps.Script->GetLatestSource());
			EmitterUpdateScriptProps.Script->SetLatestSource(SpawnScriptProps.Script->GetLatestSource());
		}
	}

	if (!GetOutermost()->bIsCookedForEditor)
	{
		GraphSource->ConditionalPostLoad();
		GraphSource->PostLoadFromEmitter(*this);
		
		// Prepare for emitter inheritance.
		if (Parent != nullptr)
		{
			Parent->ConditionalPostLoad();
		}
		if (ParentAtLastMerge != nullptr)
		{
			ParentAtLastMerge->ConditionalPostLoad();
		}

		for (auto ScratchPadScript : ScratchPadScripts)
		{
			if (ScratchPadScript)
			{
				ScratchPadScript->ConditionalPostLoad();
			}
		}

		for (auto ParentScratchPadScript : ParentScratchPadScripts)
		{
			if (ParentScratchPadScript)
			{
				ParentScratchPadScript->ConditionalPostLoad();
			}
		}

		if (IsSynchronizedWithParent() == false && IsRunningCommandlet())
		{
			// Modify here so that the asset will be marked dirty when using the resave commandlet.  This will be ignored during regular post load.
			Modify();
		}
	}
#else
	check(GPUComputeScript == nullptr || SimTarget == ENiagaraSimTarget::GPUComputeSim);
#endif

	//Temporarily disabling interpolated spawn if the script type and flag don't match.
	if (SpawnScriptProps.Script)
	{
		bool bActualInterpolatedSpawning = SpawnScriptProps.Script->IsInterpolatedParticleSpawnScript();
		if (bInterpolatedSpawning != bActualInterpolatedSpawning)
		{
			bInterpolatedSpawning = false;
			if (bActualInterpolatedSpawning)
			{
#if WITH_EDITORONLY_DATA
				//clear out the script as it was compiled with interpolated spawn.
				SpawnScriptProps.Script->InvalidateCompileResults(TEXT("Interpolated spawn changed."));
#endif
				SpawnScriptProps.Script->SetUsage(ENiagaraScriptUsage::ParticleSpawnScript);
			}
			UE_LOG(LogNiagara, Warning, TEXT("Disabling interpolated spawn because emitter flag and script type don't match. Did you adjust this value in the UI? Emitter may need recompile.. %s"), *GetFullName());
		}
	}

	EnsureScriptsPostLoaded();

#if !WITH_EDITOR
	// When running without the editor in a cooked build we run the update immediately in post load since
	// there will be no merging or compiling which makes it safe to do so.
	UpdateEmitterAfterLoad();
#endif
}

bool UNiagaraEmitter::IsEditorOnly() const
{
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	// if the emitter does not have a system as it's outer than it is likely a standalone emitter/parent emitter and so is editor only
	if (const UNiagaraSystem* SystemOwner = Cast<const UNiagaraSystem>(GetOuter()))
	{
		for (const auto& SystemEmitterHandle : SystemOwner->GetEmitterHandles())
		{
			if (SystemEmitterHandle.GetInstance() == this)
			{
				return false;
			}
		}
	}

	return true;
#else
	return Super::IsEditorOnly();
#endif
}


void UNiagaraEmitter::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
#if WITH_EDITOR
	OutTags.Add(FAssetRegistryTag("HasGPUEmitter", (SimTarget == ENiagaraSimTarget::GPUComputeSim) ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical));

	const float BoundsSize = FixedBounds.GetSize().GetMax();
	OutTags.Add(FAssetRegistryTag("FixedBoundsSize", bFixedBounds ? FString::Printf(TEXT("%.2f"), BoundsSize) : FString(TEXT("None")), FAssetRegistryTag::TT_Numerical));


	uint32 NumActiveRenderers = 0;
	TArray<const UNiagaraRendererProperties*> ActiveRenderers;

	for (const UNiagaraRendererProperties* Props : GetRenderers())
	{
		if (Props)
		{
			NumActiveRenderers++;
			ActiveRenderers.Add(Props);
		}
	}

	OutTags.Add(FAssetRegistryTag("ActiveRenderers", LexToString(NumActiveRenderers), FAssetRegistryTag::TT_Numerical));

	// Gather up NumActive emitters based off of quality level.
	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	if (Settings)
	{
		int32 NumQualityLevels = Settings->QualityLevels.Num();
		TArray<int32> QualityLevelsNumActive;
		QualityLevelsNumActive.AddZeroed(NumQualityLevels);

		// Keeping structure from UNiagaraSystem for easy code comparison
		for (int32 i = 0; i < NumQualityLevels; i++)
		{
			if (Platforms.IsEffectQualityEnabled(i))
			{
				QualityLevelsNumActive[i]++;
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

	
		TArray<UNiagaraScript*> Scripts;
		GetScripts(Scripts);
		for (UNiagaraScript* Script : Scripts)
		{
			AddDIs(Script);
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
	FName TemplateSpecificationName = GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, TemplateSpecification);
	FText TemplateSpecializationValueString = StaticEnum<ENiagaraScriptTemplateSpecification>()->GetDisplayNameTextByValue((int64) TemplateSpecification);
	OutTags.Add(FAssetRegistryTag(TemplateSpecificationName, TemplateSpecializationValueString.ToString(), FAssetRegistryTag::TT_Alphabetical));
	
#endif
	Super::GetAssetRegistryTags(OutTags);
}

bool UNiagaraEmitter::NeedsLoadForTargetPlatform(const ITargetPlatform* TargetPlatform)const
{
	// Don't load disabled emitters.
	// Awkwardly, this requires us to look for ourselves in the owning system.
	if (const UNiagaraSystem* OwnerSystem = GetTypedOuter<const UNiagaraSystem>())
	{
		for (const FNiagaraEmitterHandle& EmitterHandle : OwnerSystem->GetEmitterHandles())
		{
			if (EmitterHandle.GetInstance() == this)
			{
				if (!EmitterHandle.GetIsEnabled())
				{
					return false;
				}
				break;
			}
		}
	}

	if (!FNiagaraPlatformSet::ShouldPruneEmittersOnCook(TargetPlatform->IniPlatformName()))
	{
		return true;
	}

	bool bIsEnabled = IsEnabledOnPlatform(TargetPlatform->IniPlatformName());
	if(!bIsEnabled)
	{
		UE_LOG(LogNiagara, Verbose, TEXT("Pruned emitter %s for platform %s"), *GetFullName(), *TargetPlatform->DisplayName().ToString())
	}
	return bIsEnabled;
}

#if WITH_EDITOR
/** Creates a new emitter with the supplied emitter as a parent emitter and the supplied system as it's owner. */
UNiagaraEmitter* UNiagaraEmitter::CreateWithParentAndOwner(UNiagaraEmitter& InParentEmitter, UObject* InOwner, FName InName, EObjectFlags FlagMask)
{
	UNiagaraEmitter* NewEmitter = Cast<UNiagaraEmitter>(StaticDuplicateObject(&InParentEmitter, InOwner, InName, FlagMask));
	NewEmitter->Parent = &InParentEmitter;
	NewEmitter->ParentAtLastMerge = Cast<UNiagaraEmitter>(StaticDuplicateObject(&InParentEmitter, NewEmitter));
	NewEmitter->ParentAtLastMerge->ClearFlags(RF_Standalone | RF_Public);
	NewEmitter->ParentScratchPadScripts.Append(NewEmitter->ScratchPadScripts);
	NewEmitter->ScratchPadScripts.Empty();
	NewEmitter->SetUniqueEmitterName(InName.GetPlainNameString());
	NewEmitter->GraphSource->MarkNotSynchronized(InitialNotSynchronizedReason);
	NewEmitter->BindNotifications();

	return NewEmitter;
}

/** Creates a new emitter by duplicating an existing emitter.  The new emitter  will reference the same parent emitter if one is available. */
UNiagaraEmitter* UNiagaraEmitter::CreateAsDuplicate(const UNiagaraEmitter& InEmitterToDuplicate, FName InDuplicateName, UNiagaraSystem& InDuplicateOwnerSystem)
{
	UNiagaraEmitter* NewEmitter = Cast<UNiagaraEmitter>(StaticDuplicateObject(&InEmitterToDuplicate, &InDuplicateOwnerSystem));
	NewEmitter->ClearFlags(RF_Standalone | RF_Public);
	NewEmitter->Parent = InEmitterToDuplicate.Parent;
	if (InEmitterToDuplicate.ParentAtLastMerge != nullptr)
	{
		NewEmitter->ParentAtLastMerge = Cast<UNiagaraEmitter>(StaticDuplicateObject(InEmitterToDuplicate.ParentAtLastMerge, NewEmitter));
		NewEmitter->ParentAtLastMerge->ClearFlags(RF_Standalone | RF_Public);
	}
	NewEmitter->SetUniqueEmitterName(InDuplicateName.GetPlainNameString());
	NewEmitter->GraphSource->MarkNotSynchronized(InitialNotSynchronizedReason);
	NewEmitter->BindNotifications();

	return NewEmitter;
}


void UNiagaraEmitter::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

	if (IsAsset() && DuplicateMode == EDuplicateMode::Normal)
	{
		SetUniqueEmitterName(GetFName().GetPlainNameString());
	}
}

void UNiagaraEmitter::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);

	if (IsAsset())
	{
		SetUniqueEmitterName(GetFName().GetPlainNameString());
	}
}

void UNiagaraEmitter::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName;
	if (PropertyChangedEvent.Property)
	{
		PropertyName = PropertyChangedEvent.Property->GetFName();
	}

	bool bNeedsRecompile = false;
	bool bRecomputeExecutionOrder = false;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bInterpolatedSpawning))
	{
		bool bActualInterpolatedSpawning = SpawnScriptProps.Script->IsInterpolatedParticleSpawnScript();
		if (bInterpolatedSpawning != bActualInterpolatedSpawning)
		{
			//Recompile spawn script if we've altered the interpolated spawn property.
			SpawnScriptProps.Script->SetUsage(bInterpolatedSpawning ? ENiagaraScriptUsage::ParticleSpawnScriptInterpolated : ENiagaraScriptUsage::ParticleSpawnScript);
			UE_LOG(LogNiagara, Log, TEXT("Updating script usage: Script->IsInterpolatdSpawn %d Emitter->bInterpolatedSpawning %d"), (int32)SpawnScriptProps.Script->IsInterpolatedParticleSpawnScript(), bInterpolatedSpawning);
			if (GraphSource != nullptr)
			{
				GraphSource->MarkNotSynchronized(TEXT("Emitter interpolated spawn changed"));
			}
			bNeedsRecompile = true;
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, SimTarget))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("Emitter simulation target changed."));
		}
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bRequiresPersistentIDs))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("Emitter Requires Persistent IDs changed."));
		}
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bLocalSpace))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("Emitter LocalSpace changed."));
		}

		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bDeterminism))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("Emitter Determinism changed."));
		}

		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bSimulationStagesEnabled))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("SimulationStagesEnabled changed."));
		}
		bNeedsRecompile = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bDeprecatedShaderStagesEnabled))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("DeprecatedShaderStagesEnabled changed."));
		}
		bNeedsRecompile = true;

	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FNiagaraEventScriptProperties, SourceEmitterID))
	{
		bRecomputeExecutionOrder = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, AttributesToPreserve))
	{
		if (GraphSource != nullptr)
		{
			GraphSource->MarkNotSynchronized(TEXT("AttributesToPreserve changed."));
		}
		bNeedsRecompile = true;
	}

	ResolveScalabilitySettings();

	ThumbnailImageOutOfDate = true;
	UpdateChangeId(TEXT("PostEditChangeProperty"));
	OnPropertiesChangedDelegate.Broadcast();

#if WITH_EDITORONLY_DATA
	if (bNeedsRecompile)
	{
		UNiagaraSystem::RequestCompileForEmitter(this);
	}
	else if (bRecomputeExecutionOrder)
	{
		UNiagaraSystem::RecomputeExecutionOrderForEmitter(this);
	}
#endif
}

void UNiagaraEmitter::PreSave(const ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);
	UpdateEmitterAfterLoad();
}


UNiagaraEmitter::FOnPropertiesChanged& UNiagaraEmitter::OnPropertiesChanged()
{
	return OnPropertiesChangedDelegate;
}

UNiagaraEmitter::FOnPropertiesChanged& UNiagaraEmitter::OnRenderersChanged()
{
	return OnRenderersChangedDelegate;
}

void UNiagaraEmitter::HandleVariableRenamed(const FNiagaraVariable& InOldVariable, const FNiagaraVariable& InNewVariable, bool bUpdateContexts)
{
	// Rename the variable if it is in use by any renderer properties
	for (UNiagaraRendererProperties* Prop : GetRenderers())
	{
		Prop->Modify(false);
		Prop->RenameVariable(InOldVariable, InNewVariable, this);
	}

	// Rename any simulation stage iteration sources
	for (UNiagaraSimulationStageBase* SimStage : SimulationStages)
	{
		UNiagaraSimulationStageGeneric* GenericStage = Cast<UNiagaraSimulationStageGeneric>(SimStage);
		if (GenericStage && GenericStage->DataInterface.BoundVariable.GetName() == InOldVariable.GetName())
		{
			GenericStage->Modify(false);
			GenericStage->DataInterface.BoundVariable = InNewVariable;
		}
	}

	if (bUpdateContexts)
	{
		FNiagaraSystemUpdateContext UpdateCtx(this, true);
	}
}

void UNiagaraEmitter::HandleVariableRemoved(const FNiagaraVariable& InOldVariable, bool bUpdateContexts)
{
	// Reset the variable if it is in use by any renderer properties
	for (UNiagaraRendererProperties* Prop : GetRenderers())
	{
		Prop->Modify(false);
		Prop->RemoveVariable(InOldVariable, this);
	}

	if (bUpdateContexts)
	{
		FNiagaraSystemUpdateContext UpdateCtx(this, true);
	}
}
#endif

#if WITH_EDITORONLY_DATA
TArray<UNiagaraScriptSourceBase*> UNiagaraEmitter::GetAllSourceScripts()
{
	TArray<UNiagaraScriptSourceBase*> OutScriptSources;
	TArray<UNiagaraScript*> Scripts;
	GetScripts(Scripts, false);
	for (UNiagaraScript* Script : Scripts)
	{
		OutScriptSources.Add(Script->GetLatestSource());
	}
	return OutScriptSources;
}

FString UNiagaraEmitter::GetSourceObjectPathName() const
{
	return GetPathName();
}

TArray<UNiagaraEditorParametersAdapterBase*> UNiagaraEmitter::GetEditorOnlyParametersAdapters()
{
	return { GetEditorParameters() };
}
#endif // WITH_EDITORONLY_DATA

bool UNiagaraEmitter::IsEnabledOnPlatform(const FString& PlatformName)const
{
	return Platforms.IsEnabledForPlatform(PlatformName);
}

bool UNiagaraEmitter::IsValid()const
{
	if (!SpawnScriptProps.Script || !UpdateScriptProps.Script)
	{
		return false;
	}

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		if (!SpawnScriptProps.Script->IsScriptCompilationPending(false) && !SpawnScriptProps.Script->DidScriptCompilationSucceed(false))
		{
			return false;
		}
		if (!UpdateScriptProps.Script->IsScriptCompilationPending(false) && !UpdateScriptProps.Script->DidScriptCompilationSucceed(false))
		{
			return false;
		}
		if (EventHandlerScriptProps.Num() != 0)
		{
			for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
			{
				if (!EventHandlerScriptProps[i].Script->IsScriptCompilationPending(false) &&
					!EventHandlerScriptProps[i].Script->DidScriptCompilationSucceed(false))
				{
					return false;
				}
			}
		}
	}

	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		if (!GPUComputeScript->IsScriptCompilationPending(true) && 
			!GPUComputeScript->DidScriptCompilationSucceed(true))
		{
			return false;
		}
	}
	return true;
}

bool UNiagaraEmitter::IsReadyToRun() const
{
	//Check for various failure conditions and bail.
	if (!UpdateScriptProps.Script || !SpawnScriptProps.Script)
	{
		return false;
	}

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		if (SpawnScriptProps.Script->IsScriptCompilationPending(false))
		{
			return false;
		}
		if (UpdateScriptProps.Script->IsScriptCompilationPending(false))
		{
			return false;
		}
		if (EventHandlerScriptProps.Num() != 0)
		{
			for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
			{
				if (EventHandlerScriptProps[i].Script->IsScriptCompilationPending(false))
				{
					return false;
				}
			}
		}
	}

	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		if (GPUComputeScript->IsScriptCompilationPending(true))
		{
			return false;
		}
	}

	return true;
}

void UNiagaraEmitter::GetScripts(TArray<UNiagaraScript*>& OutScripts, bool bCompilableOnly, bool bEnabledOnly) const
{
	OutScripts.Add(SpawnScriptProps.Script);
	OutScripts.Add(UpdateScriptProps.Script);
	if (!bCompilableOnly)
	{
#if WITH_EDITORONLY_DATA
		OutScripts.Add(EmitterSpawnScriptProps.Script);
		OutScripts.Add(EmitterUpdateScriptProps.Script);
#endif
	}

	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script)
		{
			OutScripts.Add(EventHandlerScriptProps[i].Script);
		}
	}

	if (!bCompilableOnly)
	{
		for (int32 i = 0; i < SimulationStages.Num(); i++)
		{
			if (SimulationStages[i] && SimulationStages[i]->Script)
			{
				if (bEnabledOnly && (!SimulationStages[i]->bEnabled || !bSimulationStagesEnabled))
					continue;
				OutScripts.Add(SimulationStages[i]->Script);
			}
		}
	}

	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		OutScripts.Add(GPUComputeScript);
	}
}

UNiagaraScript* UNiagaraEmitter::GetScript(ENiagaraScriptUsage Usage, FGuid UsageId)
{
	TArray<UNiagaraScript*> Scripts;
	GetScripts(Scripts, false);
	for (UNiagaraScript* Script : Scripts)
	{
		if (Script->IsEquivalentUsage(Usage) && Script->GetUsageId() == UsageId)
		{
			return Script;
		}
	}
	return nullptr;
}

void UNiagaraEmitter::CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData)
{
	bRequiresViewUniformBuffer = false;

	MaxInstanceCount = 0;
	BoundsCalculators.Empty();

	// Allow renderers to cache the bindings also
	for (UNiagaraRendererProperties* Renderer : RendererProperties)
	{
		Renderer->CacheFromCompiledData(CompiledData);
	}

	// Initialize bounds calculators - skip creating if we won't ever use it.  We leave the GPU sims in there with the editor so that we can
	// generate the bounds from the readback in the tool.
#if !WITH_EDITOR
	bool bUseDynamicBounds = !bFixedBounds && SimTarget == ENiagaraSimTarget::CPUSim;
	if (bUseDynamicBounds)
#endif
	{
		BoundsCalculators.Reserve(RendererProperties.Num());
		for (UNiagaraRendererProperties* Renderer : RendererProperties)
		{
			if ((Renderer != nullptr) && Renderer->GetIsEnabled())
			{
				FNiagaraBoundsCalculator* BoundsCalculator = Renderer->CreateBoundsCalculator();
				if (BoundsCalculator != nullptr)
				{
					BoundsCalculator->InitAccessors(CompiledData);
					BoundsCalculators.Emplace(BoundsCalculator);
				}
			}
		}
	}

	// Cache information for GPU compute sims
	CacheFromShaderCompiled();

	// Find number maximum number of instance we can support for this emitter
	if (CompiledData != nullptr)
	{
		// Prevent division by 0 in case there are no renderers.
		uint32 MaxGPUBufferComponents = 1;
		if (SimTarget == ENiagaraSimTarget::CPUSim)
		{
			// CPU emitters only upload the data needed by the renderers to the GPU. Compute the maximum number of components per particle
			// among all the enabled renderers, since this will decide how many particles we can upload.
			ForEachEnabledRenderer(
				[&](UNiagaraRendererProperties* RendererProperty)
				{
					const uint32 RendererMaxNumComponents = RendererProperty->ComputeMaxUsedComponents(CompiledData);
					MaxGPUBufferComponents = FMath::Max(MaxGPUBufferComponents, RendererMaxNumComponents);
				}
			);
		}
		else
		{
			// GPU emitters must store the entire particle payload on GPU buffers, so get the maximum component count from the dataset.
			MaxGPUBufferComponents = FMath::Max(MaxGPUBufferComponents, FMath::Max3(CompiledData->TotalFloatComponents, CompiledData->TotalInt32Components, CompiledData->TotalHalfComponents));
		}

		// See how many particles we can fit in a GPU buffer. This number can be quite small on some platforms.
		uint64 MaxBufferElements = (GDebugForcedMaxGPUBufferElements > 0) ? (uint64)GDebugForcedMaxGPUBufferElements : GetMaxBufferDimension();
		// Don't just cast the result of the division to 32-bit, since that will produce garbage if MaxNumInstances is larger than UINT_MAX. Saturate instead.
		MaxInstanceCount = (uint32)FMath::Min(MaxBufferElements / MaxGPUBufferComponents, (uint64)UINT_MAX);

		if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			// On GPU, the size of the allocated buffers must be a multiple of NiagaraComputeMaxThreadGroupSize, so round down.
			MaxInstanceCount = FMath::DivideAndRoundDown(MaxInstanceCount, NiagaraComputeMaxThreadGroupSize) * NiagaraComputeMaxThreadGroupSize;
			// We will need an extra scratch instance, so the maximum number of usable instances is one less than the value we computed.
			MaxInstanceCount -= 1;
		}
	}
	else
	{
		MaxInstanceCount = 0;
	}
}

void UNiagaraEmitter::CacheFromShaderCompiled()
{
	bRequiresViewUniformBuffer = false;
	if (GPUComputeScript && (SimTarget == ENiagaraSimTarget::GPUComputeSim))
	{
		if (const FNiagaraShaderScript* NiagaraShaderScript = GPUComputeScript->GetRenderThreadScript())
		{
			for (int i=0; i < NiagaraShaderScript->GetNumPermutations(); ++i)
			{
				FNiagaraShaderRef NiagaraShaderRef = NiagaraShaderScript->GetShaderGameThread(i);
				if (NiagaraShaderRef.IsValid() && NiagaraShaderRef->ViewUniformBufferParam.IsBound())
				{
					bRequiresViewUniformBuffer = true;
					break;
				}
			}
		}
	}
}

void UNiagaraEmitter::UpdateEmitterAfterLoad()
{
	if (bFullyLoaded)
	{
		return;
	}
	bFullyLoaded = true;
	
#if WITH_EDITORONLY_DATA
	check(IsInGameThread());

	// Synchronize with definitions before merging.
	PostLoadDefinitionsSubscriptions();

	// Merge with parent if necessary.
	if (GetOuter()->IsA<UNiagaraEmitter>())
	{
		// If this emitter is owned by another emitter, remove it's inheritance information so that it doesn't try to merge changes.
		Parent = nullptr;
		ParentAtLastMerge = nullptr;
	}

	if (Parent != nullptr)
	{
		Parent->UpdateEmitterAfterLoad();
	}
	if (ParentAtLastMerge != nullptr)
	{
		ParentAtLastMerge->UpdateEmitterAfterLoad();
	}
	
	if (!GetOutermost()->bIsCookedForEditor)
	{
		if (IsSynchronizedWithParent() == false)
		{
			bool bIsPackageDirty = GetOutermost()->IsDirty();
			MergeChangesFromParent();
			if (bIsPackageDirty == false)
			{
				// we do not want to dirty the system from the merge on load
				GetOutermost()->SetDirtyFlag(false);
			}
		}

		// Reset scripts if recompile is forced.
		bool bGenerateNewChangeId = false;
		FString GenerateNewChangeIdReason;
		if (GetForceCompileOnLoad())
		{
			// If we are a standalone emitter, then we invalidate id's, which should cause systems dependent on us to regenerate.
			UObject* OuterObj = GetOuter();
			if (OuterObj == GetOutermost())
			{
				GraphSource->ForceGraphToRecompileOnNextCheck();
				bGenerateNewChangeId = true;
				GenerateNewChangeIdReason = TEXT("PostLoad - Force compile on load");
				if (GEnableVerboseNiagaraChangeIdLogging)
				{
					UE_LOG(LogNiagara, Log, TEXT("InvalidateCachedCompileIds for %s because GbForceNiagaraCompileOnLoad = %d"), *GetPathName(), GbForceNiagaraCompileOnLoad);
				}
			}
		}
	
		if (ChangeId.IsValid() == false)
		{
			// If the change id is already invalid we need to generate a new one, and can skip checking the owned scripts.
			bGenerateNewChangeId = true;
			GenerateNewChangeIdReason = TEXT("PostLoad - Change id was invalid.");
			if (GEnableVerboseNiagaraChangeIdLogging)
			{
				UE_LOG(LogNiagara, Log, TEXT("Change ID updated for emitter %s because the ID was invalid."), *GetPathName());
			}
		}

		if (bGenerateNewChangeId)
		{
			UpdateChangeId(GenerateNewChangeIdReason);
		}

		BindNotifications();
	}
#endif

	ResolveScalabilitySettings();

#if !UE_BUILD_SHIPPING
	DebugSimName.Empty();
	if (const UNiagaraSystem* SystemOwner = Cast<const UNiagaraSystem>(GetOuter()))
	{
		DebugSimName = SystemOwner->GetName();
		DebugSimName.AppendChar(':');
	}
	DebugSimName.Append(GetName());
#endif

}

bool UNiagaraEmitter::IsAllowedByScalability()const
{
	return Platforms.IsActive();
}

bool UNiagaraEmitter::RequiresPersistentIDs() const
{
	return bRequiresPersistentIDs;
}

#if WITH_EDITORONLY_DATA

FGuid UNiagaraEmitter::GetChangeId() const
{
	return ChangeId;
}

UNiagaraEditorDataBase* UNiagaraEmitter::GetEditorData() const
{
	return EditorData;
}

UNiagaraEditorParametersAdapterBase* UNiagaraEmitter::GetEditorParameters()
{
	return EditorParameters;
}

void UNiagaraEmitter::SetEditorData(UNiagaraEditorDataBase* InEditorData)
{
	if (EditorData == InEditorData)
	{
		return;
	}
	if (EditorData != nullptr)
	{
		EditorData->OnPersistentDataChanged().RemoveAll(this);
	}

	EditorData = InEditorData;
	
	if (EditorData != nullptr)
	{
		EditorData->OnPersistentDataChanged().AddUObject(this, &UNiagaraEmitter::PersistentEditorDataChanged);
	}
}

bool UNiagaraEmitter::AreAllScriptAndSourcesSynchronized() const
{
	if (SpawnScriptProps.Script->IsCompilable() && !SpawnScriptProps.Script->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	if (UpdateScriptProps.Script->IsCompilable() && !UpdateScriptProps.Script->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	if (EmitterSpawnScriptProps.Script->IsCompilable() && !EmitterSpawnScriptProps.Script->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	if (EmitterUpdateScriptProps.Script->IsCompilable() && !EmitterUpdateScriptProps.Script->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script && EventHandlerScriptProps[i].Script->IsCompilable() && !EventHandlerScriptProps[i].Script->AreScriptAndSourceSynchronized())
		{
			return false;
		}
	}

	for (int32 i = 0; i < SimulationStages.Num(); i++)
	{
		if (SimulationStages[i] && SimulationStages[i]->Script  && SimulationStages[i]->Script->IsCompilable() && SimulationStages[i]->bEnabled && !SimulationStages[i]->Script->AreScriptAndSourceSynchronized())
		{
			return false;
		}
	}

	if (SimTarget == ENiagaraSimTarget::GPUComputeSim && GPUComputeScript->IsCompilable() && !GPUComputeScript->AreScriptAndSourceSynchronized())
	{
		return false;
	}

	return true;
}


UNiagaraEmitter::FOnEmitterCompiled& UNiagaraEmitter::OnEmitterVMCompiled()
{
	return OnVMScriptCompiledDelegate;
}

UNiagaraEmitter::FOnEmitterCompiled& UNiagaraEmitter::OnEmitterGPUCompiled()
{
	return OnGPUScriptCompiledDelegate;
}

void  UNiagaraEmitter::InvalidateCompileResults()
{
	TArray<UNiagaraScript*> Scripts;
	GetScripts(Scripts, false);
	for (int32 i = 0; i < Scripts.Num(); i++)
	{
		Scripts[i]->InvalidateCompileResults(TEXT("Emitter compile invalidated."));
	}
}

void UNiagaraEmitter::OnPostCompile()
{
	SyncEmitterAlias(TEXT("Emitter"), UniqueEmitterName);

	SpawnScriptProps.InitDataSetAccess();
	UpdateScriptProps.InitDataSetAccess();

	TSet<FName> SpawnIds;
	TSet<FName> UpdateIds;
	for (const FNiagaraEventGeneratorProperties& SpawnGeneratorProps : SpawnScriptProps.EventGenerators)
	{
		SpawnIds.Add(SpawnGeneratorProps.ID);
	}
	for (const FNiagaraEventGeneratorProperties& UpdateGeneratorProps : UpdateScriptProps.EventGenerators)
	{
		UpdateIds.Add(UpdateGeneratorProps.ID);
	}

	SharedEventGeneratorIds.Empty();
	SharedEventGeneratorIds.Append(SpawnIds.Intersect(UpdateIds).Array());

	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script)
		{
			EventHandlerScriptProps[i].InitDataSetAccess();
		}
	}

	if (GbForceNiagaraFailToCompile != 0)
	{
		TArray<UNiagaraScript*> Scripts;
		GetScripts(Scripts, false);
		for (int32 i = 0; i < Scripts.Num(); i++)
		{
			Scripts[i]->InvalidateCompileResults(TEXT("Console variable forced recompile.")); 
		}
	}

	// If we have a GPU script but the SimTarget isn't GPU, we need to clear out the old results.
	if (SimTarget != ENiagaraSimTarget::GPUComputeSim && GPUComputeScript->GetLastCompileStatus() != ENiagaraScriptCompileStatus::NCS_Unknown)
	{
		GPUComputeScript->InvalidateCompileResults(TEXT("Not a GPU emitter."));
	}

	RuntimeEstimation = MemoryRuntimeEstimation();
#if STATS
	StatDatabase.ClearStatCaptures();
#endif

	OnEmitterVMCompiled().Broadcast(this);
}

void UNiagaraEmitter::BindNotifications()
{
	if (GraphSource)
	{
		GraphSource->OnChanged().AddUObject(this, &UNiagaraEmitter::GraphSourceChanged);
	}

	if (EmitterSpawnScriptProps.Script)
	{
		EmitterSpawnScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}

	if (EmitterUpdateScriptProps.Script)
	{
		EmitterUpdateScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}

	if (SpawnScriptProps.Script)
	{
		SpawnScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}

	if (UpdateScriptProps.Script)
	{
		UpdateScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}

	for (FNiagaraEventScriptProperties& EventScriptProperties : EventHandlerScriptProps)
	{
		if (EventScriptProperties.Script)
		{
			EventScriptProperties.Script->RapidIterationParameters.AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
		}
	}

	for (UNiagaraSimulationStageBase* SimulationStage : SimulationStages)
	{
		if (SimulationStage)
		{
			SimulationStage->OnChanged().AddUObject(this, &UNiagaraEmitter::SimulationStageChanged);

			if (SimulationStage->Script)
			{
				SimulationStage->Script->RapidIterationParameters.AddOnChangedHandler(
					FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
			}
		}
	}

	for (UNiagaraRendererProperties* Renderer : RendererProperties)
	{
		if (Renderer)
		{
			Renderer->OnChanged().AddUObject(this, &UNiagaraEmitter::RendererChanged);
		}
	}

	if (EditorData != nullptr)
	{
		EditorData->OnPersistentDataChanged().AddUObject(this, &UNiagaraEmitter::PersistentEditorDataChanged);
	}
}

#endif

bool UNiagaraEmitter::UsesScript(const UNiagaraScript* Script)const
{
	if (SpawnScriptProps.Script == Script || UpdateScriptProps.Script == Script)
	{
		return true;
	}
#if WITH_EDITORONLY_DATA
	if (EmitterSpawnScriptProps.Script == Script || EmitterUpdateScriptProps.Script == Script)
	{
		return true;
	}
#endif
	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script == Script)
		{
			return true;
		}
	}
	return false;
}

bool UNiagaraEmitter::UsesCollection(const class UNiagaraParameterCollection* Collection)const
{
	if (SpawnScriptProps.Script && SpawnScriptProps.Script->UsesCollection(Collection))
	{
		return true;
	}
	if (UpdateScriptProps.Script && UpdateScriptProps.Script->UsesCollection(Collection))
	{
		return true;
	}
	for (int32 i = 0; i < EventHandlerScriptProps.Num(); i++)
	{
		if (EventHandlerScriptProps[i].Script && EventHandlerScriptProps[i].Script->UsesCollection(Collection))
		{
			return true;
		}
	}
	return false;
}


bool UNiagaraEmitter::CanObtainParticleAttribute(const FNiagaraVariableBase& InVar) const
{
	check(!HasAnyFlags(RF_NeedPostLoad));

	if (SpawnScriptProps.Script)
	{
		// make sure that this isn't called before our dependents are fully loaded
		check(!SpawnScriptProps.Script->HasAnyFlags(RF_NeedPostLoad));

		return SpawnScriptProps.Script->GetVMExecutableData().Attributes.Contains(InVar);
	}
	return false;
}
bool UNiagaraEmitter::CanObtainEmitterAttribute(const FNiagaraVariableBase& InVarWithUniqueNameNamespace) const
{
	check(!HasAnyFlags(RF_NeedPostLoad));

	const UNiagaraSystem* Sys = GetTypedOuter<UNiagaraSystem>();
	if (Sys)
	{
		// make sure that this isn't called before our dependents are fully loaded
		check(!Sys->HasAnyFlags(RF_NeedPostLoad));

		return Sys->CanObtainEmitterAttribute(InVarWithUniqueNameNamespace);
	}
	return false;
}
bool UNiagaraEmitter::CanObtainSystemAttribute(const FNiagaraVariableBase& InVar) const
{
	check(!HasAnyFlags(RF_NeedPostLoad));

	const UNiagaraSystem* Sys = GetTypedOuter<UNiagaraSystem>();
	if (Sys)
	{
		// make sure that this isn't called before our dependents are fully loaded
		check(!Sys->HasAnyFlags(RF_NeedPostLoad));

		return Sys->CanObtainSystemAttribute(InVar);
	}
	return false;
}
bool UNiagaraEmitter::CanObtainUserVariable(const FNiagaraVariableBase& InVar) const
{
	check(!HasAnyFlags(RF_NeedPostLoad));

	const UNiagaraSystem* Sys = GetTypedOuter<UNiagaraSystem>();
	if (Sys)
	{
		// make sure that this isn't called before our dependents are fully loaded
		check(!Sys->HasAnyFlags(RF_NeedPostLoad));

		return Sys->CanObtainUserVariable(InVar);
	}
	return false;
}

FString UNiagaraEmitter::GetUniqueEmitterName()const
{
	return UniqueEmitterName;
}

#if WITH_EDITORONLY_DATA

void UNiagaraEmitter::UpdateFromMergedCopy(const INiagaraMergeManager& MergeManager, UNiagaraEmitter* MergedEmitter)
{
	auto ReouterMergedObject = [](UObject* NewOuter, UObject* TargetObject)
	{
		FName MergedObjectUniqueName = MakeUniqueObjectName(NewOuter, TargetObject->GetClass(), TargetObject->GetFName());
		TargetObject->Rename(*MergedObjectUniqueName.ToString(), NewOuter, REN_ForceNoResetLoaders);
	};

	// The merged copy was based on the parent emitter so its name might be wrong, check and fix that first,
	// otherwise the rapid iteration parameter names will be wrong from the copied scripts.
	if (MergedEmitter->GetUniqueEmitterName() != UniqueEmitterName)
	{
		MergedEmitter->SetUniqueEmitterName(UniqueEmitterName);
	}

	// Copy base editable emitter properties.
	TArray<FProperty*> DifferentProperties;
	MergeManager.DiffEditableProperties(this, MergedEmitter, *UNiagaraEmitter::StaticClass(), DifferentProperties);
	MergeManager.CopyPropertiesToBase(this, MergedEmitter, DifferentProperties);

	// Copy source and scripts
	ReouterMergedObject(this, MergedEmitter->GraphSource);
	GraphSource->OnChanged().RemoveAll(this);
	GraphSource = MergedEmitter->GraphSource;
	GraphSource->OnChanged().AddUObject(this, &UNiagaraEmitter::GraphSourceChanged);

	ReouterMergedObject(this, MergedEmitter->SpawnScriptProps.Script);
	SpawnScriptProps.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	SpawnScriptProps.Script = MergedEmitter->SpawnScriptProps.Script;
	SpawnScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	ReouterMergedObject(this, MergedEmitter->UpdateScriptProps.Script);
	UpdateScriptProps.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	UpdateScriptProps.Script = MergedEmitter->UpdateScriptProps.Script;
	UpdateScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	ReouterMergedObject(this, MergedEmitter->EmitterSpawnScriptProps.Script);
	EmitterSpawnScriptProps.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	EmitterSpawnScriptProps.Script = MergedEmitter->EmitterSpawnScriptProps.Script;
	EmitterSpawnScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	ReouterMergedObject(this, MergedEmitter->EmitterUpdateScriptProps.Script);
	EmitterUpdateScriptProps.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	EmitterUpdateScriptProps.Script = MergedEmitter->EmitterUpdateScriptProps.Script;
	EmitterUpdateScriptProps.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	ReouterMergedObject(this, MergedEmitter->GPUComputeScript);
	GPUComputeScript->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	GPUComputeScript = MergedEmitter->GPUComputeScript;
	GPUComputeScript->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));

	// Copy event handlers
	for (FNiagaraEventScriptProperties& EventScriptProperties : EventHandlerScriptProps)
	{
		EventScriptProperties.Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	}
	EventHandlerScriptProps.Empty();

	for (FNiagaraEventScriptProperties& MergedEventScriptProperties : MergedEmitter->EventHandlerScriptProps)
	{
		EventHandlerScriptProps.Add(MergedEventScriptProperties);
		ReouterMergedObject(this, MergedEventScriptProperties.Script);
		MergedEventScriptProperties.Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}

	// Copy shader stages
	for (UNiagaraSimulationStageBase*& SimulationStage : SimulationStages)
	{
		SimulationStage->OnChanged().RemoveAll(this);
		SimulationStage->Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	}
	SimulationStages.Empty();

	for (UNiagaraSimulationStageBase* MergedSimulationStage : MergedEmitter->SimulationStages)
	{
		ReouterMergedObject(this, MergedSimulationStage);
		SimulationStages.Add(MergedSimulationStage);
		MergedSimulationStage->OnChanged().AddUObject(this, &UNiagaraEmitter::SimulationStageChanged);
		MergedSimulationStage->Script->RapidIterationParameters.AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	}

	// Copy renderers
	for (UNiagaraRendererProperties* Renderer : RendererProperties)
	{
		Renderer->OnChanged().RemoveAll(this);

		// some renderer properties have been incorrectly flagged as RF_Public meaning that even if we remove them here with the merge
		// they will be included in a cook; so clear the flag while we're removing them
		Renderer->ClearFlags(RF_Public);
	}
	RendererProperties.Empty();

	for (UNiagaraRendererProperties* MergedRenderer : MergedEmitter->RendererProperties)
	{
		ReouterMergedObject(this, MergedRenderer);
		RendererProperties.Add(MergedRenderer);
		MergedRenderer->OnChanged().AddUObject(this, &UNiagaraEmitter::RendererChanged);
	}

	// Copy scratch pad scripts.
	ParentScratchPadScripts.Empty();
	ScratchPadScripts.Empty();

	for (UNiagaraScript* MergedParentScratchPadScript : MergedEmitter->ParentScratchPadScripts)
	{
		ReouterMergedObject(this, MergedParentScratchPadScript);
		ParentScratchPadScripts.Add(MergedParentScratchPadScript);
	}

	for (UNiagaraScript* MergedScratchPadScript : MergedEmitter->ScratchPadScripts)
	{
		ReouterMergedObject(this, MergedScratchPadScript);
		ScratchPadScripts.Add(MergedScratchPadScript);
	}

	SetEditorData(MergedEmitter->GetEditorData());

	// Update the change id since we don't know what's changed.
	UpdateChangeId(TEXT("Updated from merged copy"));
}

void UNiagaraEmitter::SyncEmitterAlias(const FString& InOldName, const FString& InNewName)
{
	TArray<UNiagaraScript*> Scripts;
	GetScripts(Scripts, false, true); // Get all the scripts...

	for (UNiagaraScript* Script : Scripts)
	{
		// We don't mark the package dirty here because this can happen as a result of a compile and we don't want to dirty files
		// due to compilation, in cases where the package should be marked dirty an previous modify would have already done this.
		Script->Modify(false);
		Script->SyncAliases(FNiagaraAliasContext(Script->GetUsage())
			.ChangeEmitterName(InOldName, InNewName));
	}

	// if we haven't yet been postloaded then we'll hold off on updating the renderers as they are dependent on everything
	// (System/Emitter/Scripts) being fully loaded.
	if (!HasAnyFlags(RF_NeedPostLoad))
	{
		for (UNiagaraRendererProperties* Renderer : RendererProperties)
		{
			if (Renderer)
			{
				Renderer->Modify(false);
				Renderer->RenameEmitter(*InOldName, this);
			}
		}
	}
}
#endif
bool UNiagaraEmitter::SetUniqueEmitterName(const FString& InName)
{
	if (InName != UniqueEmitterName)
	{
		Modify();
		FString OldName = UniqueEmitterName;
		UniqueEmitterName = InName;

		if (GetName() != InName)
		{
			// Also rename the underlying uobject to keep things consistent.
			FName UniqueObjectName = MakeUniqueObjectName(GetOuter(), UNiagaraEmitter::StaticClass(), *InName);
			Rename(*UniqueObjectName.ToString(), GetOuter(), REN_ForceNoResetLoaders);
		}

#if WITH_EDITORONLY_DATA
		SyncEmitterAlias(OldName, UniqueEmitterName);
#endif
		return true;
	}

	return false;
}

//void UNiagaraEmitter::ForEachEnabledRenderer(const TFunction<void(UNiagaraRendererProperties*)>& Func) const
//{
//	for (UNiagaraRendererProperties* Renderer : RendererProperties)
//	{
//		if (Renderer && Renderer->GetIsEnabled() && Renderer->IsSimTargetSupported(this->SimTarget))
//		{
//			Func(Renderer);
//		}
//	}
//}

void UNiagaraEmitter::AddRenderer(UNiagaraRendererProperties* Renderer)
{
	Modify();
	RendererProperties.Add(Renderer);
#if WITH_EDITOR
	Renderer->OnChanged().AddUObject(this, &UNiagaraEmitter::RendererChanged);
	UpdateChangeId(TEXT("Renderer added"));
	OnRenderersChangedDelegate.Broadcast();
#endif
	if (UNiagaraSystem* Owner = GetTypedOuter<UNiagaraSystem>())
	{
		Owner->ComputeRenderersDrawOrder();
		Owner->CacheFromCompiledData();
	}
}

void UNiagaraEmitter::RemoveRenderer(UNiagaraRendererProperties* Renderer)
{
	Modify();
	RendererProperties.Remove(Renderer);
#if WITH_EDITOR
	Renderer->OnChanged().RemoveAll(this);
	UpdateChangeId(TEXT("Renderer removed"));
	OnRenderersChangedDelegate.Broadcast();
#endif
	if (UNiagaraSystem* Owner = GetTypedOuter<UNiagaraSystem>())
	{
		Owner->ComputeRenderersDrawOrder();
	}
}

FNiagaraEventScriptProperties* UNiagaraEmitter::GetEventHandlerByIdUnsafe(FGuid ScriptUsageId)
{
	for (FNiagaraEventScriptProperties& EventScriptProperties : EventHandlerScriptProps)
	{
		if (EventScriptProperties.Script->GetUsageId() == ScriptUsageId)
		{
			return &EventScriptProperties;
		}
	}
	return nullptr;
}

void UNiagaraEmitter::AddEventHandler(FNiagaraEventScriptProperties EventHandler)
{
	Modify();
	EventHandlerScriptProps.Add(EventHandler);
#if WITH_EDITOR
	EventHandler.Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	UpdateChangeId(TEXT("Event handler added"));
#endif
}

void UNiagaraEmitter::RemoveEventHandlerByUsageId(FGuid EventHandlerUsageId)
{
	Modify();
	auto FindEventHandlerById = [=](const FNiagaraEventScriptProperties& EventHandler) { return EventHandler.Script->GetUsageId() == EventHandlerUsageId; };
#if WITH_EDITOR
	FNiagaraEventScriptProperties* EventHandler = EventHandlerScriptProps.FindByPredicate(FindEventHandlerById);
	if (EventHandler != nullptr)
	{
		EventHandler->Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
	}
#endif
	EventHandlerScriptProps.RemoveAll(FindEventHandlerById);
#if WITH_EDITOR
	UpdateChangeId(TEXT("Event handler removed"));
#endif
}

UNiagaraSimulationStageBase* UNiagaraEmitter::GetSimulationStageById(FGuid ScriptUsageId) const
{
	UNiagaraSimulationStageBase*const* FoundSimulationStagePtr = SimulationStages.FindByPredicate([&ScriptUsageId](UNiagaraSimulationStageBase* SimulationStage) { return SimulationStage->Script->GetUsageId() == ScriptUsageId; });
	return FoundSimulationStagePtr != nullptr ? *FoundSimulationStagePtr : nullptr;
}

void UNiagaraEmitter::AddSimulationStage(UNiagaraSimulationStageBase* SimulationStage)
{
	Modify();
	SimulationStages.Add(SimulationStage);
#if WITH_EDITOR
	SimulationStage->OnChanged().AddUObject(this, &UNiagaraEmitter::SimulationStageChanged);
	SimulationStage->Script->RapidIterationParameters.AddOnChangedHandler(
		FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraEmitter::ScriptRapidIterationParameterChanged));
	UpdateChangeId(TEXT("Shader stage added"));
#endif
}

void UNiagaraEmitter::RemoveSimulationStage(UNiagaraSimulationStageBase* SimulationStage)
{
	Modify();
	bool bRemoved = SimulationStages.Remove(SimulationStage) != 0;
#if WITH_EDITOR
	if (bRemoved)
	{
		SimulationStage->OnChanged().RemoveAll(this);
		SimulationStage->Script->RapidIterationParameters.RemoveAllOnChangedHandlers(this);
		UpdateChangeId(TEXT("Simulation stage removed"));
	}
#endif
}

void UNiagaraEmitter::MoveSimulationStageToIndex(UNiagaraSimulationStageBase* SimulationStageToMove, int32 TargetIndex)
{
	int32 CurrentIndex = SimulationStages.IndexOfByKey(SimulationStageToMove);
	checkf(CurrentIndex != INDEX_NONE, TEXT("Simulation stage could not be moved because it is not owned by this emitter."));
	if (TargetIndex != CurrentIndex)
	{
		int32 AdjustedTargetIndex = CurrentIndex < TargetIndex
			? TargetIndex - 1 // If the current index is less than the target index, the target index needs to be decreased to make up for the item being removed.
			: TargetIndex;

		SimulationStages.Remove(SimulationStageToMove);
		SimulationStages.Insert(SimulationStageToMove, AdjustedTargetIndex);
#if WITH_EDITOR
		UpdateChangeId("Simulation stage moved.");
#endif
	}
}

bool UNiagaraEmitter::IsEventGeneratorShared(FName EventGeneratorId) const
{
	return SharedEventGeneratorIds.Contains(EventGeneratorId);
}

void UNiagaraEmitter::BeginDestroy()
{
#if WITH_EDITOR
	if (GraphSource != nullptr)
	{
		GraphSource->OnChanged().RemoveAll(this);
	}
	if (GPUComputeScript)
	{
		GPUComputeScript->OnGPUScriptCompiled().RemoveAll(this);
	}
#endif
	Super::BeginDestroy();
}

#if WITH_EDITORONLY_DATA

void UNiagaraEmitter::UpdateChangeId(const FString& Reason)
{
	// We don't mark the package dirty here because this can happen as a result of a compile and we don't want to dirty files
	// due to compilation, in cases where the package should be marked dirty an previous modify would have already done this.
	Modify(false);
	FGuid OldId = ChangeId;
	ChangeId = FGuid::NewGuid();
	if (GbEnableEmitterChangeIdMergeLogging)
	{
		UE_LOG(LogNiagara, Log, TEXT("Emitter %s change id updated. Reason: %s OldId: %s NewId: %s"),
			*GetPathName(), *Reason, *OldId.ToString(), *ChangeId.ToString());
	}
#if STATS
	StatDatabase.ClearStatCaptures();
#endif
}

void UNiagaraEmitter::ScriptRapidIterationParameterChanged()
{
	UpdateChangeId(TEXT("Script rapid iteration parameter changed."));
}

void UNiagaraEmitter::SimulationStageChanged()
{
	UpdateChangeId(TEXT("Simulation Stage Changed"));
}

void UNiagaraEmitter::RendererChanged()
{
	UpdateChangeId(TEXT("Renderer changed."));
}

void UNiagaraEmitter::GraphSourceChanged()
{
	UpdateChangeId(TEXT("Graph source changed."));
}

void UNiagaraEmitter::RaiseOnEmitterGPUCompiled(UNiagaraScript* InScript, const FGuid& ScriptVersion)
{
	OnGPUScriptCompiledDelegate.Broadcast(this);
}

void UNiagaraEmitter::PersistentEditorDataChanged()
{
	UpdateChangeId(TEXT("Persistent editor data changed."));
}
#endif

TStatId UNiagaraEmitter::GetStatID(bool bGameThread, bool bConcurrent)const
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
		if (bConcurrent)
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

void UNiagaraEmitter::ClearRuntimeAllocationEstimate(uint64 ReportHandle)
{
	FScopeLock Lock(&EstimationCriticalSection);
	if (ReportHandle == INDEX_NONE)
	{
		RuntimeEstimation.AllocationEstimate = 0;
		RuntimeEstimation.RuntimeAllocations.Empty();
		RuntimeEstimation.IsEstimationDirty = true;
	}
	else
	{
		RuntimeEstimation.RuntimeAllocations.Remove(ReportHandle);
		RuntimeEstimation.IsEstimationDirty = true;
	}
}

int32 UNiagaraEmitter::AddRuntimeAllocation(uint64 ReporterHandle, int32 AllocationCount)
{
	FScopeLock Lock(&EstimationCriticalSection);
	int32* Estimate = RuntimeEstimation.RuntimeAllocations.Find(ReporterHandle);
	if (!Estimate || *Estimate < AllocationCount)
	{
		RuntimeEstimation.RuntimeAllocations.Add(ReporterHandle, AllocationCount);
		RuntimeEstimation.IsEstimationDirty = true;

		// Remove a random entry when there are enough logged allocations already
		if (RuntimeEstimation.RuntimeAllocations.Num() > 10)
		{
			TArray<uint64> Keys;
			RuntimeEstimation.RuntimeAllocations.GetKeys(Keys);
			RuntimeEstimation.RuntimeAllocations.Remove(Keys[FMath::RandHelper(Keys.Num())]);
		}
	}
	return RuntimeEstimation.RuntimeAllocations.Num();
}

int32 UNiagaraEmitter::GetMaxParticleCountEstimate()
{
	if (AllocationMode == EParticleAllocationMode::ManualEstimate)
	{
		return PreAllocationCount;
	}
	
	if (RuntimeEstimation.IsEstimationDirty)
	{
		FScopeLock lock(&EstimationCriticalSection);
		int32 EstimationCount = RuntimeEstimation.RuntimeAllocations.Num();
		RuntimeEstimation.AllocationEstimate = 0;
		if (EstimationCount > 0)
		{
			RuntimeEstimation.RuntimeAllocations.ValueSort(TGreater<int32>());
			int32 i = 0;
			for (TPair<uint64, int32> pair : RuntimeEstimation.RuntimeAllocations)
			{
				if (i >= (EstimationCount - 1) / 2)
				{
					// to prevent overallocation from outliers we take the median instead of the global max
					RuntimeEstimation.AllocationEstimate = pair.Value;
					break;
				}
				i++;
			}
			RuntimeEstimation.IsEstimationDirty = false;
		}
	}
	return RuntimeEstimation.AllocationEstimate;
}

void UNiagaraEmitter::GenerateStatID()const
{
#if STATS
	FString Name = GetOuter() ? GetOuter()->GetFName().ToString() : TEXT("");
	Name += TEXT("/") + UniqueEmitterName;
	StatID_GT = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraEmitters>(Name + TEXT("[GT]"));
	StatID_GT_CNC = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraEmitters>(Name + TEXT("[GT_CNC]"));
	StatID_RT = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraEmitters>(Name + TEXT("[RT]"));
	StatID_RT_CNC = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraEmitters>(Name + TEXT("[RT_CNC]"));
#endif
}

#if WITH_EDITORONLY_DATA
UNiagaraEmitter* UNiagaraEmitter::GetParent() const
{
	return Parent;
}

UNiagaraEmitter* UNiagaraEmitter::GetParentAtLastMerge() const
{
	return ParentAtLastMerge;
}

void UNiagaraEmitter::RemoveParent()
{
	Parent = nullptr;
	ParentAtLastMerge = nullptr;
}

void UNiagaraEmitter::SetParent(UNiagaraEmitter& InParent)
{
	Parent = &InParent;
	ParentAtLastMerge = InParent.DuplicateWithoutMerging(this);
	ParentAtLastMerge->ClearFlags(RF_Standalone | RF_Public);

	// Since this API is only valid for the "Create duplicate parent" operation we move the emitters scratch pad script to the parent array since that's where they're defined now.
	ParentScratchPadScripts.Append(ScratchPadScripts);
	ScratchPadScripts.Empty();
	UpdateChangeId(TEXT("Parent Set"));

	GraphSource->MarkNotSynchronized(TEXT("Emitter parent changed"));
}

void UNiagaraEmitter::Reparent(UNiagaraEmitter& InParent)
{
	Parent = &InParent;
	ParentAtLastMerge = nullptr;
	GraphSource->MarkNotSynchronized(TEXT("Emitter parent changed"));
}

void UNiagaraEmitter::NotifyScratchPadScriptsChanged()
{
	UpdateChangeId(TEXT("Scratch pad scripts changed."));
}
#endif

void UNiagaraEmitter::ResolveScalabilitySettings()
{
	CurrentScalabilitySettings.Clear();

	if (UNiagaraSystem* Owner = GetTypedOuter<UNiagaraSystem>())
	{
		if(UNiagaraEffectType* ActualEffectType = Owner->GetEffectType())
		{
			CurrentScalabilitySettings = ActualEffectType->GetActiveEmitterScalabilitySettings();
		}
	}

	for (FNiagaraEmitterScalabilityOverride& Override : ScalabilityOverrides.Overrides)
	{
		if (Override.Platforms.IsActive())
		{
			if (Override.bOverrideSpawnCountScale)
			{
				CurrentScalabilitySettings.bScaleSpawnCount = Override.bScaleSpawnCount;
				CurrentScalabilitySettings.SpawnCountScale = Override.SpawnCountScale;
			}
		}
	}
}

void UNiagaraEmitter::OnScalabilityCVarChanged()
{
	ResolveScalabilitySettings();
}
