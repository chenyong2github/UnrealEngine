// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterSubsystem.h"
#include "WaterModule.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "WaterMeshActor.h"
#include "WaterMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "WaterBodyActor.h"
#include "WaterBodyIslandActor.h"
#include "WaterBodyExclusionVolume.h"
#include "WaterSplineComponent.h"
#include "CollisionShape.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "SceneView.h"
#include "Math/NumericLimits.h"
#include "BuoyancyManager.h"
#include "WaterRuntimeSettings.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"

// ----------------------------------------------------------------------------------

#define LOCTEXT_NAMESPACE "WaterSubsystem"

// ----------------------------------------------------------------------------------

DECLARE_CYCLE_STAT(TEXT("IsUnderwater Test"), STAT_WaterIsUnderwater, STATGROUP_Water);

// ----------------------------------------------------------------------------------

// General purpose CVars:
static TAutoConsoleVariable<int32> CVarWaterEnabled(
	TEXT("r.Water.Enabled"),
	1,
	TEXT("If all water rendering is enabled or disabled"),
	ECVF_RenderThreadSafe
);

static int32 FreezeWaves = 0;
static FAutoConsoleVariableRef CVarFreezeWaves(
	TEXT("r.Water.FreezeWaves"),
	FreezeWaves,
	TEXT("Freeze time for waves if non-zero"),
	ECVF_Cheat
);

static TAutoConsoleVariable<int32> CVarOverrideWavesTime(
	TEXT("r.Water.OverrideWavesTime"),
	-1.0f,
	TEXT("Forces the time used for waves if >= 0.0"),
	ECVF_Cheat
);

// Underwater post process CVars : 
static int32 EnableUnderwaterPostProcess = 1;
static FAutoConsoleVariableRef CVarEnableUnderwaterPostProcess(
	TEXT("r.Water.EnableUnderwaterPostProcess"),
	EnableUnderwaterPostProcess,
	TEXT("Controls whether the underwater post process is enabled"),
	ECVF_Scalability
);

static int32 VisualizeActiveUnderwaterPostProcess = 0;
static FAutoConsoleVariableRef CVarVisualizeUnderwaterPostProcess(
	TEXT("r.Water.VisualizeActiveUnderwaterPostProcess"),
	VisualizeActiveUnderwaterPostProcess,
	TEXT("Shows which water body is currently being picked up for underwater post process"),
	ECVF_Default
);

// Shallow water CVars : 
static int32 ShallowWaterSim = 1;
static FAutoConsoleVariableRef CVarShallowWaterSim(
	TEXT("r.Water.EnableShallowWaterSimulation"),
	ShallowWaterSim,
	TEXT("Controls whether the shallow water fluid sim is enabled"),
	ECVF_Scalability
);

static int32 ShallowWaterSimulationMaxDynamicForces = 6;
static FAutoConsoleVariableRef CVarShallowWaterSimulationMaxDynamicForces(
	TEXT("r.Water.ShallowWaterMaxDynamicForces"),
	ShallowWaterSimulationMaxDynamicForces,
	TEXT("Max number of dynamic forces that will be registered with sim at a time."),
	ECVF_Scalability
);

static int32 ShallowWaterSimulationMaxImpulseForces = 3;
static FAutoConsoleVariableRef CVarShallowWaterSimulationMaxImpulseForces(
	TEXT("r.Water.ShallowWaterMaxImpulseForces"),
	ShallowWaterSimulationMaxImpulseForces,
	TEXT("Max number of impulse forces that will be registered with sim at a time."),
	ECVF_Scalability
);

static int32 ShallowWaterSimulationRenderTargetSize = 1024;
static FAutoConsoleVariableRef CVarShallowWaterSimulationRenderTargetSize(
	TEXT("r.Water.ShallowWaterRenderTargetSize"),
	ShallowWaterSimulationRenderTargetSize,
	TEXT("Size for square shallow water fluid sim render target. Effective dimensions are SizexSize"),
	ECVF_Scalability
);

// ----------------------------------------------------------------------------------

bool IsWaterEnabled(bool bIsRenderThread)
{
	return !!(bIsRenderThread ? CVarWaterEnabled.GetValueOnRenderThread() : CVarWaterEnabled.GetValueOnGameThread());
}


// ----------------------------------------------------------------------------------

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
/** Debug-only struct for displaying some information about which post process material is being used : */
struct FUnderwaterPostProcessDebugInfo
{
	TArray<TWeakObjectPtr<AWaterBody>> OverlappedWaterBodies;
	TWeakObjectPtr<AWaterBody> ActiveWaterBody;
	FWaterBodyQueryResult ActiveWaterBodyQueryResult;
};
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

// ----------------------------------------------------------------------------------

#if WITH_EDITOR

bool UWaterSubsystem::bAllowWaterSubsystemOnPreviewWorld = false;

#endif // WITH_EDITOR

// ----------------------------------------------------------------------------------

UWaterSubsystem::UWaterSubsystem()
{
	SmoothedWorldTimeSeconds = 0.f;
	NonSmoothedWorldTimeSeconds = 0.f;
	PrevWorldTimeSeconds = 0.f;
	bUnderWaterForAudio = false;
	bPauseWaveTime = false;

	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinderOptional<UStaticMesh> LakeMesh;
		ConstructorHelpers::FObjectFinderOptional<UStaticMesh> RiverMesh;

		FConstructorStatics()
			: LakeMesh(TEXT("/Water/Meshes/LakeMesh.LakeMesh"))
			, RiverMesh(TEXT("/Water/Meshes/RiverMesh.RiverMesh"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	DefaultLakeMesh = ConstructorStatics.LakeMesh.Get();
	DefaultRiverMesh = ConstructorStatics.RiverMesh.Get();
}

UWaterSubsystem* UWaterSubsystem::GetWaterSubsystem(const UWorld* InWorld)
{
	if (InWorld)
	{
		return InWorld->GetSubsystem<UWaterSubsystem>();
	}

	return nullptr;
}

FWaterBodyManager* UWaterSubsystem::GetWaterBodyManager(UWorld* InWorld)
{
	if (UWaterSubsystem* Subsystem = GetWaterSubsystem(InWorld))
	{
		return &Subsystem->WaterBodyManager;
	}

	return nullptr;
}

void UWaterSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	check(GetWorld() != nullptr);
	if (FreezeWaves == 0 && bPauseWaveTime == false)
	{
		NonSmoothedWorldTimeSeconds += DeltaTime;
	}

	float MPCTime = GetWaterTimeSeconds();
	SetMPCTime(MPCTime, PrevWorldTimeSeconds);
	PrevWorldTimeSeconds = MPCTime;

	if (WaterMeshActor)
	{
		WaterMeshActor->Update();
	}

	WaterBodyManager.Update();

	if (!bUnderWaterForAudio && CachedDepthUnderwater > 0.0f)
	{
		bUnderWaterForAudio = true;
		OnCameraUnderwaterStateChanged.Broadcast(bUnderWaterForAudio, CachedDepthUnderwater);
	}
	else if (bUnderWaterForAudio && CachedDepthUnderwater <= 0.0f)
	{
		bUnderWaterForAudio = false;
		OnCameraUnderwaterStateChanged.Broadcast(bUnderWaterForAudio, CachedDepthUnderwater);
	}
}

TStatId UWaterSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWaterSubsystem, STATGROUP_Tickables);
}

bool UWaterSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
#if WITH_EDITOR
	// In editor, don't let preview worlds instantiate a water subsystem (except if explicitly allowed by a tool that requested it by setting bAllowWaterSubsystemOnPreviewWorld)
	if (WorldType == EWorldType::EditorPreview)
	{
		return bAllowWaterSubsystemOnPreviewWorld;
	}
#endif // WITH_EDITOR

	return WorldType == EWorldType::Game || WorldType == EWorldType::Editor || WorldType == EWorldType::PIE;
}

void UWaterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	check(World != nullptr);

	WaterBodyManager.Initialize(World);

	bUsingSmoothedTime = false;
	FConsoleVariableDelegate NotifyWaterScalabilityChanged = FConsoleVariableDelegate::CreateUObject(this, &UWaterSubsystem::NotifyWaterScalabilityChangedInternal);
	CVarShallowWaterSim->SetOnChangedCallback(NotifyWaterScalabilityChanged);
	CVarShallowWaterSimulationRenderTargetSize->SetOnChangedCallback(NotifyWaterScalabilityChanged);

	FConsoleVariableDelegate NotifyWaterEnabledChanged = FConsoleVariableDelegate::CreateUObject(this, &UWaterSubsystem::NotifyWaterEnabledChangedInternal);
	CVarWaterEnabled->SetOnChangedCallback(NotifyWaterEnabledChanged);

#if WITH_EDITOR
	GetDefault<UWaterRuntimeSettings>()->OnSettingsChange.AddUObject(this, &UWaterSubsystem::ApplyRuntimeSettings);
#endif //WITH_EDITOR
	ApplyRuntimeSettings(GetDefault<UWaterRuntimeSettings>(), EPropertyChangeType::ValueSet);

	World->OnBeginPostProcessSettings.AddUObject(this, &UWaterSubsystem::ComputeUnderwaterPostProcess);
	World->InsertPostProcessVolume(&UnderwaterPostProcessVolume);
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.ObjectFlags = RF_Transient;

#if WITH_EDITOR
		// The buoyancy manager should be a subsytem really, but for now, just hide it from the outliner : 
		SpawnInfo.bHideFromSceneOutliner = true;
#endif //WITH_EDITOR

		// Store the buoyancy manager we create for future use.
		BuoyancyManager = World->SpawnActor<ABuoyancyManager>(SpawnInfo);
	}
	UCollisionProfile::Get()->OnLoadProfileConfig.AddUObject(this, &UWaterSubsystem::OnLoadProfileConfig);
	AddWaterCollisionProfile();
}

void UWaterSubsystem::Deinitialize()
{
	UWorld* World = GetWorld();
	check(World != nullptr);

	UCollisionProfile::Get()->OnLoadProfileConfig.RemoveAll(this);

	FConsoleVariableDelegate NullCallback;
	CVarShallowWaterSimulationRenderTargetSize->SetOnChangedCallback(NullCallback);
	CVarShallowWaterSim->SetOnChangedCallback(NullCallback);
	CVarWaterEnabled->SetOnChangedCallback(NullCallback);

	World->OnBeginPostProcessSettings.RemoveAll(this);
	World->RemovePostProcessVolume(&UnderwaterPostProcessVolume);

	WaterBodyManager.Deinitialize();

#if WITH_EDITOR
	GetDefault<UWaterRuntimeSettings>()->OnSettingsChange.RemoveAll(this);
#endif //WITH_EDITOR

	Super::Deinitialize();
}

void UWaterSubsystem::ApplyRuntimeSettings(const UWaterRuntimeSettings* Settings, EPropertyChangeType::Type ChangeType)
{
	UWorld* World = GetWorld();
	check(World != nullptr);
	UnderwaterTraceChannel = Settings->CollisionChannelForWaterTraces;
	MaterialParameterCollection = Settings->MaterialParameterCollection.LoadSynchronous();

#if WITH_EDITOR
	for (TActorIterator<AWaterBody> ActorItr(World); ActorItr; ++ActorItr)
	{
		(*ActorItr)->UpdateActorIcon();
	}

	for (TActorIterator<AWaterBodyIsland> ActorItr(World); ActorItr; ++ActorItr)
	{
		(*ActorItr)->UpdateActorIcon();
	}

	for (TActorIterator<AWaterBodyExclusionVolume> ActorItr(World); ActorItr; ++ActorItr)
	{
		(*ActorItr)->UpdateActorIcon();
	}
#endif // WITH_EDITOR
}

void UWaterSubsystem::AddWaterCollisionProfile()
{
	// Make sure WaterCollisionProfileName is added to Engine's collision profiles
	const FName WaterCollisionProfileName = GetDefault<UWaterRuntimeSettings>()->GetDefaultWaterCollisionProfileName();
	FCollisionResponseTemplate WaterBodyCollisionProfile;
	if (!UCollisionProfile::Get()->GetProfileTemplate(WaterCollisionProfileName, WaterBodyCollisionProfile))
	{
		WaterBodyCollisionProfile.Name = WaterCollisionProfileName;
		WaterBodyCollisionProfile.CollisionEnabled = ECollisionEnabled::QueryOnly;
		WaterBodyCollisionProfile.ObjectType = ECollisionChannel::ECC_WorldStatic;
		WaterBodyCollisionProfile.bCanModify = false;
		WaterBodyCollisionProfile.ResponseToChannels = FCollisionResponseContainer::GetDefaultResponseContainer();
		WaterBodyCollisionProfile.ResponseToChannels.Camera = ECR_Ignore;
		WaterBodyCollisionProfile.ResponseToChannels.Visibility = ECR_Ignore;
		WaterBodyCollisionProfile.ResponseToChannels.WorldDynamic = ECR_Overlap;
		WaterBodyCollisionProfile.ResponseToChannels.Pawn = ECR_Overlap;
		WaterBodyCollisionProfile.ResponseToChannels.PhysicsBody = ECR_Overlap;
		WaterBodyCollisionProfile.ResponseToChannels.Destructible = ECR_Overlap;
		WaterBodyCollisionProfile.ResponseToChannels.Vehicle = ECR_Overlap;
#if WITH_EDITORONLY_DATA
		WaterBodyCollisionProfile.HelpMessage = TEXT("Default Water Collision Profile (Created by Water Plugin)");
#endif
		FCollisionProfilePrivateAccessor::AddProfileTemplate(WaterBodyCollisionProfile);
	}
}

void UWaterSubsystem::OnLoadProfileConfig(UCollisionProfile* CollisionProfile)
{
	check(CollisionProfile == UCollisionProfile::Get());
	AddWaterCollisionProfile();
}

bool UWaterSubsystem::IsShallowWaterSimulationEnabled() const
{
	return ShallowWaterSim != 0;
}

bool UWaterSubsystem::IsUnderwaterPostProcessEnabled() const
{
	return EnableUnderwaterPostProcess != 0;
}

int32 UWaterSubsystem::GetShallowWaterMaxDynamicForces()
{
	return ShallowWaterSimulationMaxDynamicForces;
}

int32 UWaterSubsystem::GetShallowWaterMaxImpulseForces()
{
	return ShallowWaterSimulationMaxImpulseForces;
}

int32 UWaterSubsystem::GetShallowWaterSimulationRenderTargetSize()
{
	return ShallowWaterSimulationRenderTargetSize;
}

bool UWaterSubsystem::IsWaterRenderingEnabled() const
{
	return IsWaterEnabled(/*bIsRenderThread = */ false);
}

float UWaterSubsystem::GetWaterTimeSeconds() const
{
	float ForceWavesTimeValue = CVarOverrideWavesTime.GetValueOnGameThread();
	if (ForceWavesTimeValue >= 0.0f)
	{
		return ForceWavesTimeValue;
	}

	if (UWorld* World = GetWorld())
	{
		if (World->IsGameWorld() && bUsingSmoothedTime)
		{
			return GetSmoothedWorldTimeSeconds();
		}
	}
	return NonSmoothedWorldTimeSeconds;
}

float UWaterSubsystem::GetSmoothedWorldTimeSeconds() const
{
	return bUsingOverrideWorldTimeSeconds ? OverrideWorldTimeSeconds : SmoothedWorldTimeSeconds;
}

void UWaterSubsystem::PrintToWaterLog(const FString& Message, bool bWarning)
{
	if (bWarning)
	{
		UE_LOG(LogWater, Warning, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogWater, Log, TEXT("%s"), *Message);
	}
}

void UWaterSubsystem::SetSmoothedWorldTimeSeconds(float InTime)
{
	bUsingSmoothedTime = true;
	if (FreezeWaves == 0)
	{
		SmoothedWorldTimeSeconds = InTime;
	}
}


void UWaterSubsystem::SetOverrideSmoothedWorldTimeSeconds(float InTime)
{
	OverrideWorldTimeSeconds = InTime;
}

void UWaterSubsystem::SetShouldOverrideSmoothedWorldTimeSeconds(bool bOverride)
{
	bUsingOverrideWorldTimeSeconds = bOverride;
}

void UWaterSubsystem::SetShouldPauseWaveTime(bool bInPauseWaveTime)
{
	bPauseWaveTime = bInPauseWaveTime;
}

void UWaterSubsystem::SetOceanFloodHeight(float InFloodHeight)
{
	if (UWorld* World = GetWorld())
	{
	const float ClampedFloodHeight = FMath::Max(0.0f, InFloodHeight);

	if (FloodHeight != ClampedFloodHeight)
	{
		FloodHeight = ClampedFloodHeight;
		MarkAllWaterMeshesForRebuild();

		// the ocean body is dynamic and needs to be readjusted when the flood height changes : 
		if (OceanActor.IsValid())
		{
			OceanActor->SetHeightOffset(InFloodHeight);
		}

		// All water body actors need to update their underwater post process MID as it depends on the ocean global height : 
			for (TActorIterator<AWaterBody> ActorItr(World); ActorItr; ++ActorItr)
		{
			AWaterBody* WaterBody = *ActorItr;
			WaterBody->UpdateMaterialInstances();
		}
	}
}
}

AWaterMeshActor* UWaterSubsystem::GetWaterMeshActor() const
{
	if (UWorld* World = GetWorld())
	{
	// @todo water: this assumes only one water mesh actor right now.  In the future we may need to associate a water mesh actor with a water body more directly
		TActorIterator<AWaterMeshActor> It(World);
	WaterMeshActor = It ? *It : nullptr;

	return WaterMeshActor;
}

	return nullptr;
}

float UWaterSubsystem::GetOceanBaseHeight() const
{
	if (OceanActor.IsValid())
	{
		return OceanActor->GetActorLocation().Z;
	}

	return TNumericLimits<float>::Lowest();
}

void UWaterSubsystem::MarkAllWaterMeshesForRebuild()
{
	if (UWorld* World = GetWorld())
	{
		for (AWaterMeshActor* WaterMesh : TActorRange<AWaterMeshActor>(World))
	{
		WaterMesh->MarkWaterMeshComponentForRebuild();
	}
}
}

void UWaterSubsystem::NotifyWaterScalabilityChangedInternal(IConsoleVariable* CVar)
{
	OnWaterScalabilityChanged.Broadcast();
}

void UWaterSubsystem::NotifyWaterEnabledChangedInternal(IConsoleVariable* CVar)
{
	if (UWorld* World = GetWorld())
	{
	// Water body visibility depends on CVarWaterEnabled
		for (AWaterBody* WaterBody : TActorRange<AWaterBody>(World))
	{
		WaterBody->UpdateWaterComponentVisibility();
	}
}
}

struct FWaterBodyPostProcessQuery
{
	FWaterBodyPostProcessQuery(AWaterBody& InWaterBody, const FVector& InWorldLocation, const FWaterBodyQueryResult& InQueryResult)
		: WaterBody(InWaterBody)
		, WorldLocation(InWorldLocation)
		, QueryResult(InQueryResult)
	{}

	AWaterBody& WaterBody;
	FVector WorldLocation;
	FWaterBodyQueryResult QueryResult;
};

static bool GetWaterBodyDepthUnderwater(const FWaterBodyPostProcessQuery& InQuery, float& OutDepthUnderwater)
{
	// Account for max possible wave height
	const FWaveInfo& WaveInfo = InQuery.QueryResult.GetWaveInfo();
	const float ZFudgeFactor = FMath::Max(WaveInfo.MaxHeight, WaveInfo.AttenuationFactor * 10.0f);
	const FBox BoxToCheckAgainst = FBox::BuildAABB(InQuery.WorldLocation, FVector(10, 10, ZFudgeFactor));

	float ImmersionDepth = InQuery.QueryResult.GetImmersionDepth();
	check(!InQuery.QueryResult.IsInExclusionVolume());
	if ((ImmersionDepth >= 0.0f) || BoxToCheckAgainst.IsInsideOrOn(InQuery.QueryResult.GetWaterSurfaceLocation()))
	{
		OutDepthUnderwater = ImmersionDepth;
		return true;
	}

	OutDepthUnderwater = 0.0f;
	return false;
}

void UWaterSubsystem::ComputeUnderwaterPostProcess(FVector ViewLocation, FSceneView* SceneView)
{
	SCOPE_CYCLE_COUNTER(STAT_WaterIsUnderwater);

	UWorld* World = GetWorld();
	if ((World == nullptr) || (SceneView->Family->EngineShowFlags.PostProcessing == 0))
	{
		return;
	}

	const float PrevDepthUnderwater = CachedDepthUnderwater;
	CachedDepthUnderwater = -1;

	bool bUnderwaterForPostProcess = false;

	// Trace just a small distance extra from the viewpoint to account for waves since the waves wont traced against
	static const float TraceDistance = 100.f;

	// Always force simple collision traces
	static FCollisionQueryParams TraceSimple(SCENE_QUERY_STAT(DefaultQueryParam), false);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	FUnderwaterPostProcessDebugInfo UnderwaterPostProcessDebugInfo;
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	TArray<FHitResult> Hits;
	TArray<FWaterBodyPostProcessQuery, TInlineAllocator<4>> WaterBodyQueriesToProcess;
	const AWaterMeshActor* LocalWaterMeshActor = GetWaterMeshActor();
	if ((LocalWaterMeshActor != nullptr) && World->SweepMultiByChannel(Hits, ViewLocation, ViewLocation + FVector(0, 0, TraceDistance), FQuat::Identity, UnderwaterTraceChannel, FCollisionShape::MakeSphere(TraceDistance), TraceSimple))
	{
		if (Hits.Num() > 1)
		{
			// Sort hits based on their water priority for rendering since we should prioritize evaluating waves in the order those waves will be considered for rendering. 
			Hits.Sort([](const FHitResult& A, const FHitResult& B)
			{
				const AWaterBody* ABody = Cast<AWaterBody>(A.Actor);
				const AWaterBody* BBody = Cast<AWaterBody>(B.Actor);

				const int32 APriority = ABody ? ABody->GetOverlapMaterialPriority() : -1;
				const int32 BPriority = BBody ? BBody->GetOverlapMaterialPriority() : -1;

				return APriority > BPriority;
			});
		}

		float MaxWaterLevel = TNumericLimits<float>::Lowest();
		for (const FHitResult& Result : Hits)
		{
			if (AWaterBody* WaterBody = Cast<AWaterBody>(Result.Actor))
			{
				// Don't consider water bodies with no post process material : 
				if (WaterBody->UnderwaterPostProcessMaterial != nullptr)
				{
					// Base water body info needed : 
					EWaterBodyQueryFlags QueryFlags = EWaterBodyQueryFlags::ComputeImmersionDepth
						| EWaterBodyQueryFlags::ComputeLocation
						| EWaterBodyQueryFlags::IncludeWaves;
					AdjustUnderwaterWaterInfoQueryFlags(QueryFlags);

					FWaterBodyQueryResult QueryResult = WaterBody->QueryWaterInfoClosestToWorldLocation(ViewLocation, QueryFlags);
					if (!QueryResult.IsInExclusionVolume())
					{
						// Calculate the surface max Z at the view XY location
						float WaterSurfaceZ = QueryResult.GetWaterPlaneLocation().Z + QueryResult.GetWaveInfo().MaxHeight;

						// Only add the waterbody for processing if it has a higher surface than the previous waterbody (the Hits array is sorted by priority already)
						// This also removed any duplicate waterbodies possibly returned by the sweep query
						if (WaterSurfaceZ > MaxWaterLevel)
						{
							MaxWaterLevel = WaterSurfaceZ;
							WaterBodyQueriesToProcess.Add(FWaterBodyPostProcessQuery(*WaterBody, ViewLocation, QueryResult));
						}
					}
				}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				UnderwaterPostProcessDebugInfo.OverlappedWaterBodies.AddUnique(WaterBody);
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			}
		}

		for (const FWaterBodyPostProcessQuery& Query : WaterBodyQueriesToProcess)
		{
			float LocalDepthUnderwater = 0.0f;

			// Underwater is fudged a bit for post process so its possible to get a true return here but depth underwater is < 0
			// Post process should appear under any part of the water that clips the camera but underwater audio sounds should only play if the camera is actualy under water (i.e LocalDepthUnderwater > 0)
			bUnderwaterForPostProcess = GetWaterBodyDepthUnderwater(Query, LocalDepthUnderwater);
			if (bUnderwaterForPostProcess)
			{
				CachedDepthUnderwater = FMath::Max(LocalDepthUnderwater, CachedDepthUnderwater);
				UnderwaterPostProcessVolume.PostProcessProperties = Query.WaterBody.GetPostProcessProperties();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				UnderwaterPostProcessDebugInfo.ActiveWaterBody = &Query.WaterBody;
				UnderwaterPostProcessDebugInfo.ActiveWaterBodyQueryResult = Query.QueryResult;
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)					
				break;
			}
		}
	}

	SceneView->UnderwaterDepth = CachedDepthUnderwater;

	if (!bUnderwaterForPostProcess || !IsUnderwaterPostProcessEnabled())
	{
		UnderwaterPostProcessVolume.PostProcessProperties.bIsEnabled = false;
		UnderwaterPostProcessVolume.PostProcessProperties.Settings = nullptr;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ShowOnScreenDebugInfo(ViewLocation, UnderwaterPostProcessDebugInfo);
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)						
}

void UWaterSubsystem::SetMPCTime(float Time, float PrevTime)
{
	if (UWorld* World = GetWorld())
	{
		if (MaterialParameterCollection)
		{
			UMaterialParameterCollectionInstance* MaterialParameterCollectionInstance = World->GetParameterCollectionInstance(MaterialParameterCollection);
			const static FName TimeParam(TEXT("Time"));
			const static FName PrevTimeParam(TEXT("PrevTime"));
			MaterialParameterCollectionInstance->SetScalarParameterValue(TimeParam, Time);
			MaterialParameterCollectionInstance->SetScalarParameterValue(PrevTimeParam, PrevTime);
		}
	}
}


void UWaterSubsystem::AdjustUnderwaterWaterInfoQueryFlags(EWaterBodyQueryFlags& InOutFlags)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// We might need some extra info when showing debug info for the post process : 
	if (VisualizeActiveUnderwaterPostProcess > 1)
	{
		InOutFlags |= (EWaterBodyQueryFlags::ComputeDepth | EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::IncludeWaves);
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void UWaterSubsystem::ShowOnScreenDebugInfo(const FVector& InViewLocation, const FUnderwaterPostProcessDebugInfo& InDebugInfo)
{
	// Visualize the active post process if any
	if (VisualizeActiveUnderwaterPostProcess == 0)
	{
		return;
	}

	TArray<FText, TInlineAllocator<8>> OutputStrings;

	OutputStrings.Add(FText::Format(LOCTEXT("VisualizeActiveUnderwaterPostProcess_ViewLocationDetails", "Underwater post process debug : view location : {0}"), FText::FromString(InViewLocation.ToCompactString())));

	if (InDebugInfo.ActiveWaterBody.IsValid())
	{
		UMaterialInstanceDynamic* MID = InDebugInfo.ActiveWaterBody->GetUnderwaterPostProcessMaterialInstance();
		FString MaterialName = MID ? MID->GetMaterial()->GetName() : TEXT("No material");
		OutputStrings.Add(FText::Format(LOCTEXT("VisualizeActiveUnderwaterPostProcess_ActivePostprocess", "Active underwater post process water body {0} (material: {1})"),
			FText::FromString(InDebugInfo.ActiveWaterBody->GetName()),
			FText::FromString(MaterialName)));
	}
	else
	{
		OutputStrings.Add(LOCTEXT("VisualizeActiveUnderwaterPostProcess_InactivePostprocess", "Inactive underwater post process"));
	}

	// Add more details : 
	if (VisualizeActiveUnderwaterPostProcess > 1)
	{
		// Display details about the water query that resulted in this underwater post process to picked :
		if (InDebugInfo.ActiveWaterBody.IsValid())
		{
			FText WaveDetails(LOCTEXT("VisualizeActiveUnderwaterPostProcess_WavelessDetails", "No waves"));
			if (InDebugInfo.ActiveWaterBody->HasWaves())
			{
				WaveDetails = FText::Format(LOCTEXT("VisualizeActiveUnderwaterPostProcess_WaveDetails", "- Wave Height : {0} (Max : {1}, Max here: {2}, Attenuation Factor : {3})"),
					InDebugInfo.ActiveWaterBodyQueryResult.GetWaveInfo().Height,
					InDebugInfo.ActiveWaterBody->GetMaxWaveHeight(),
					InDebugInfo.ActiveWaterBodyQueryResult.GetWaveInfo().MaxHeight,
					InDebugInfo.ActiveWaterBodyQueryResult.GetWaveInfo().AttenuationFactor);
			}

			OutputStrings.Add(FText::Format(LOCTEXT("VisualizeActiveUnderwaterPostProcess_QueryDetails", "- Water Surface Z : {0}\n- Water Depth : {1}\n{2}"),
				InDebugInfo.ActiveWaterBodyQueryResult.GetWaterSurfaceLocation().Z,
				InDebugInfo.ActiveWaterBodyQueryResult.GetWaterSurfaceDepth(),
				WaveDetails));
		}

		// Display each water body returned by the overlap query : 
		if (InDebugInfo.OverlappedWaterBodies.Num() > 0)
		{
			OutputStrings.Add(FText::Format(LOCTEXT("VisualizeActiveUnderwaterPostProcess_OverlappedWaterBodyDetailsHeader", "{0} overlapping water bodies :"),
				InDebugInfo.OverlappedWaterBodies.Num()));
			for (TWeakObjectPtr<AWaterBody> WaterBody : InDebugInfo.OverlappedWaterBodies)
			{
				if (WaterBody.IsValid())
				{
					OutputStrings.Add(FText::Format(LOCTEXT("VisualizeActiveUnderwaterPostProcess_OverlappedWaterBodyDetails", "- {0} (overlap material priority: {1})"),
						FText::FromString(WaterBody->GetName()),
						FText::AsNumber(WaterBody->GetOverlapMaterialPriority())));
				}
			}
		}
	}

	// Output a single message because multi-line texts end up overlapping over messages
	FString OutputMessage;
	for (const FText& Message : OutputStrings)
	{
		OutputMessage += Message.ToString() + "\n";
	}
	static const FName DebugMessageKeyName(TEXT("ActiveUnderwaterPostProcessMessage"));
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage((int32)DebugMessageKeyName.GetNumber(), 0.f, FColor::White, OutputMessage);
	}
}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

// ----------------------------------------------------------------------------------

#if WITH_EDITOR

UWaterSubsystem::FScopedAllowWaterSubsystemOnPreviewWorld::FScopedAllowWaterSubsystemOnPreviewWorld(bool bNewValue)
{
	bPreviousValue = UWaterSubsystem::GetAllowWaterSubsystemOnPreviewWorld();
	UWaterSubsystem::SetAllowWaterSubsystemOnPreviewWorld(bNewValue);
}

UWaterSubsystem::FScopedAllowWaterSubsystemOnPreviewWorld::~FScopedAllowWaterSubsystemOnPreviewWorld()
{
	UWaterSubsystem::SetAllowWaterSubsystemOnPreviewWorld(bPreviousValue);
}

#endif // WITH_EDITOR

// ----------------------------------------------------------------------------------

#undef LOCTEXT_NAMESPACE