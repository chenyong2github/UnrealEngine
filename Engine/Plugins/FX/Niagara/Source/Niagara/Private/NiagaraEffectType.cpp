// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraEffectType.h"
#include "NiagaraCommon.h"
#include "NiagaraComponent.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraSystem.h"

//In an effort to cut the impact of runtime perf tracking, I limit the number of fames we actually sample on.
int32 GNumFramesBetweenRuntimePerfSamples = 5;
static FAutoConsoleVariableRef CVarNumFramesBetweenRuntimePerfSamples(TEXT("fx.NumFramesBetweenRuntimePerfSamples"), GNumFramesBetweenRuntimePerfSamples, TEXT("How many frames between each sample of Niagara runtime perf. \n"), ECVF_ReadOnly);

int32 GNiagaraRuntimeCycleHistorySize = 15;
static FAutoConsoleVariableRef CVarNiagaraRuntimeCycleHistorySize(TEXT("fx.NiagaraRuntimeCycleHistorySize"), GNiagaraRuntimeCycleHistorySize, TEXT("How many frames history to use in Niagara's runtime performance trackers. \n"), ECVF_ReadOnly);

UNiagaraEffectType::UNiagaraEffectType(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, UpdateFrequency(ENiagaraScalabilityUpdateFrequency::SpawnOnly)
	, CullReaction(ENiagaraCullReaction::DeactivateImmediate)
	, SignificanceHandler(nullptr)
	, NumInstances(0)
	, bNewSystemsSinceLastScalabilityUpdate(false)
	, PerformanceBaselineController(nullptr)
{
}

void UNiagaraEffectType::BeginDestroy()
{
	Super::BeginDestroy();
}

bool UNiagaraEffectType::IsReadyForFinishDestroy()
{
	return Super::IsReadyForFinishDestroy();
}

void UNiagaraEffectType::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
}

void UNiagaraEffectType::PostLoad()
{
	Super::PostLoad();

	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);

	/** Init signficance handlers to match previous behavior. */
	if (NiagaraVer < FNiagaraCustomVersion::SignificanceHandlers)
	{
		if (UpdateFrequency == ENiagaraScalabilityUpdateFrequency::SpawnOnly)
		{
			SignificanceHandler = nullptr;
		}
		else
		{
			SignificanceHandler = NewObject<UNiagaraSignificanceHandlerDistance>(this);
		}
	}

#if !WITH_EDITOR && NIAGARA_PERF_BASELINES
	//When not in the editor we clear out the baseline so that it's regenerated for play tests.
	//We cannot use the saved editor/development config settings.
	InvalidatePerfBaseline();
#endif
}

const FNiagaraSystemScalabilitySettings& UNiagaraEffectType::GetActiveSystemScalabilitySettings()const
{
	for (const FNiagaraSystemScalabilitySettings& Settings : SystemScalabilitySettings.Settings)
	{
		if (Settings.Platforms.IsActive())
		{
			return Settings;
		}
	}

	//UE_LOG(LogNiagara, Warning, TEXT("Could not find active system scalability settings for EffectType %s"), *GetFullName());

	static FNiagaraSystemScalabilitySettings Dummy;
	return Dummy;
}

const FNiagaraEmitterScalabilitySettings& UNiagaraEffectType::GetActiveEmitterScalabilitySettings()const
{
	for (const FNiagaraEmitterScalabilitySettings& Settings : EmitterScalabilitySettings.Settings)
	{
		if (Settings.Platforms.IsActive())
		{
			return Settings;
		}
	}
	
	//UE_LOG(LogNiagara, Warning, TEXT("Could not find active emitter scalability settings for EffectType %s"), *GetFullName());

	static FNiagaraEmitterScalabilitySettings Dummy;
	return Dummy;
}

#if WITH_EDITOR

void UNiagaraEffectType::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FNiagaraSystemUpdateContext UpdateContext;
	for (TObjectIterator<UNiagaraSystem> It; It; ++It)
	{
		UNiagaraSystem* System = *It;
		if (System->GetEffectType() == this)
		{
			System->OnScalabilityCVarChanged();
			UpdateContext.Add(System, true);
		}
	}

	if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraEffectType, PerformanceBaselineController))
	{
		PerfBaselineVersion.Invalidate();
	}
}
#endif

#if NIAGARA_PERF_BASELINES
void UNiagaraEffectType::UpdatePerfBaselineStats(FNiagaraPerfBaselineStats& NewBaselineStats)
{
	PerfBaselineStats = NewBaselineStats;
	PerfBaselineVersion = CurrentPerfBaselineVersion;

#if WITH_EDITOR
	SaveConfig();
#endif
}

void UNiagaraEffectType::InvalidatePerfBaseline()
{
	PerfBaselineVersion.Invalidate();
	PerfBaselineStats = FNiagaraPerfBaselineStats();

#if WITH_EDITOR
	SaveConfig();
#endif
}
#endif

//////////////////////////////////////////////////////////////////////////

FNiagaraSystemScalabilityOverride::FNiagaraSystemScalabilityOverride()
	: bOverrideDistanceSettings(false)
	, bOverrideInstanceCountSettings(false)
	, bOverridePerSystemInstanceCountSettings(false)
	, bOverrideTimeSinceRendererSettings(false)
	, bOverrideGlobalBudgetCullingSettings(false)
{
}

FNiagaraSystemScalabilitySettings::FNiagaraSystemScalabilitySettings()
{
	Clear();
}

void FNiagaraSystemScalabilitySettings::Clear()
{
	Platforms = FNiagaraPlatformSet();
	bCullByDistance = false;
	bCullByMaxTimeWithoutRender = false;
	bCullMaxInstanceCount = false;
	bCullPerSystemMaxInstanceCount = false;
	bCullByGlobalBudget = false;
	MaxDistance = 0.0f;
	MaxInstances = 0;
	MaxSystemInstances = 0;
	MaxTimeWithoutRender = 0.0f;
	MaxGlobalBudgetUsage = 1.0f;
}

FNiagaraEmitterScalabilitySettings::FNiagaraEmitterScalabilitySettings()
{
	Clear();
}

void FNiagaraEmitterScalabilitySettings::Clear()
{
	SpawnCountScale = 1.0f;
	bScaleSpawnCount = false;
}

FNiagaraEmitterScalabilityOverride::FNiagaraEmitterScalabilityOverride()
	: bOverrideSpawnCountScale(false)
{
}


//////////////////////////////////////////////////////////////////////////

#include "NiagaraScalabilityManager.h"
void UNiagaraSignificanceHandlerDistance::CalculateSignificance(TArray<UNiagaraComponent*>& Components, TArray<FNiagaraScalabilityState>& OutState, TArray<int32>& OutIndices)
{
	const int32 ComponentCount = Components.Num();
	check(ComponentCount == OutState.Num());

	for (int32 CompIdx = 0; CompIdx < ComponentCount; ++CompIdx)
	{
		FNiagaraScalabilityState& State = OutState[CompIdx];

		const bool AddIndex = !State.bCulled || State.IsDirty();
		
		if (State.bCulled)
		{
			State.Significance = 0.0f;
		}
		else
		{
			UNiagaraComponent* Component = Components[CompIdx];

			float LODDistance = 0.0f;
#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
			if (Component->bEnablePreviewLODDistance)
			{
				LODDistance = Component->PreviewLODDistance;
			}
			else
#endif
			if(FNiagaraSystemInstance* Inst = Component->GetSystemInstance())
			{
				LODDistance = Inst->GetLODDistance();
			}

			State.Significance = 1.0f / LODDistance;
		}

		if (AddIndex)
		{
			OutIndices.Add(CompIdx);
		}
	}
}

void UNiagaraSignificanceHandlerAge::CalculateSignificance(TArray<UNiagaraComponent*>& Components, TArray<FNiagaraScalabilityState>& OutState, TArray<int32>& OutIndices)
{
	const int32 ComponentCount = Components.Num();
	check(ComponentCount == OutState.Num());

	for (int32 CompIdx = 0; CompIdx < ComponentCount; ++CompIdx)
	{
		FNiagaraScalabilityState& State = OutState[CompIdx];
		const bool AddIndex = !State.bCulled || State.IsDirty();

		if (State.bCulled)
		{
			State.Significance = 0.0f;
		}
		else
		{
			UNiagaraComponent* Component = Components[CompIdx];

			if (FNiagaraSystemInstance* Inst = Component->GetSystemInstance())
			{
				State.Significance = 1.0f / Inst->GetAge();//Newer Systems are higher significance.
			}
		}

		if (AddIndex)
		{
			OutIndices.Add(CompIdx);
		}
	}
}


#if NIAGARA_PERF_BASELINES

#include "AssetRegistryModule.h"

//Invalidate this to regenerate perf baseline info.
//For example if there are some significant code optimizations.
const FGuid UNiagaraEffectType::CurrentPerfBaselineVersion = FGuid(0xD854D103, 0x87C17A44, 0x87CA4524, 0x5F72FBC2);
UNiagaraEffectType::FGeneratePerfBaselines UNiagaraEffectType::GeneratePerfBaselinesDelegate;

void UNiagaraEffectType::GeneratePerfBaselines()
{
	if (GeneratePerfBaselinesDelegate.IsBound())
	{
		//Load all effect types so we generate all baselines at once.
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> EffectTypeAssets;
		AssetRegistryModule.Get().GetAssetsByClass(UNiagaraEffectType::StaticClass()->GetFName(), EffectTypeAssets);

		TArray<UNiagaraEffectType*> EffectTypesToGenerate;
		for (FAssetData& Asset : EffectTypeAssets)
		{
			if (UNiagaraEffectType* FXType = Cast<UNiagaraEffectType>(Asset.GetAsset()))
			{
				if (FXType->IsPerfBaselineValid() == false && FXType->GetPerfBaselineController())
				{
					EffectTypesToGenerate.Add(FXType);
				}
			}
		}

		GeneratePerfBaselinesDelegate.Execute(EffectTypesToGenerate);
	}
}

void UNiagaraEffectType::SpawnBaselineActor(UWorld* World)
{
	if (PerformanceBaselineController && World)
	{
		//Update with dummy stats so we don't try to regen them again.
		FNiagaraPerfBaselineStats DummyStats;
		UpdatePerfBaselineStats(DummyStats);

		FActorSpawnParameters SpawnParams;
		ANiagaraPerfBaselineActor* BaselineActor = CastChecked<ANiagaraPerfBaselineActor>(World->SpawnActorDeferred<ANiagaraPerfBaselineActor>(ANiagaraPerfBaselineActor::StaticClass(), FTransform::Identity));
		BaselineActor->Controller = CastChecked<UNiagaraBaselineController>(StaticDuplicateObject(PerformanceBaselineController, BaselineActor));
		BaselineActor->Controller->EffectType = this;
		BaselineActor->Controller->Owner = BaselineActor;

		BaselineActor->FinishSpawning(FTransform::Identity);
		BaselineActor->RegisterAllActorTickFunctions(true, true);
	}
}

void InvalidatePerfBaselines()
{
	for (TObjectIterator<UNiagaraEffectType> It; It; ++It)
	{
		It->InvalidatePerfBaseline();
	}
}

FAutoConsoleCommand InvalidatePerfBaselinesCommand(
	TEXT("fx.InvalidateNiagaraPerfBaselines"),
	TEXT("Invalidates all Niagara performance baseline data."),
	FConsoleCommandDelegate::CreateStatic(&InvalidatePerfBaselines)
);

#endif
//////////////////////////////////////////////////////////////////////////