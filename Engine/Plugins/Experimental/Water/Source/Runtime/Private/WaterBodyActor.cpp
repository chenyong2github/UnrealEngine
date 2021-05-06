// Copyright Epic Games, Inc. All Rights Reserved.


#include "WaterBodyActor.h"
#include "UObject/UObjectIterator.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavCollision.h"
#include "AI/NavigationSystemBase.h"
#include "AI/NavigationSystemHelpers.h"
#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Misc/SecureHash.h"
#include "PhysicsEngine/BodySetup.h"
#include "Components/SplineMeshComponent.h"
#include "Components/BoxComponent.h"
#include "BuoyancyComponent.h"
#include "Modules/ModuleManager.h"
#include "WaterModule.h"
#include "WaterSubsystem.h"
#include "WaterMeshActor.h"
#include "WaterBodyExclusionVolume.h"
#include "WaterBodyIslandActor.h"
#include "WaterSplineMetadata.h"
#include "WaterSplineComponent.h"
#include "WaterRuntimeSettings.h"
#include "WaterUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GerstnerWaterWaves.h"
#include "WaterVersion.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#include "Components/BillboardComponent.h"
#endif

#define LOCTEXT_NAMESPACE "Water"

// ----------------------------------------------------------------------------------

DECLARE_CYCLE_STAT(TEXT("WaterBody_ComputeWaterInfo"), STAT_WaterBody_ComputeWaterInfo, STATGROUP_Water);
DECLARE_CYCLE_STAT(TEXT("WaterBody_ComputeWaterDepth"), STAT_WaterBody_ComputeWaterDepth, STATGROUP_Water);
DECLARE_CYCLE_STAT(TEXT("WaterBody_ComputeLandscapeDepth"), STAT_WaterBody_ComputeLandscapeDepth, STATGROUP_Water);
DECLARE_CYCLE_STAT(TEXT("WaterBody_ComputeWaveHeight"), STAT_WaterBody_ComputeWaveHeight, STATGROUP_Water);

// ----------------------------------------------------------------------------------

TAutoConsoleVariable<float> CVarWaterOceanFallbackDepth(
	TEXT("r.Water.OceanFallbackDepth"),
	3000.0f,
	TEXT("Depth to report for the ocean when no terrain is found under the query location. Not used when <= 0."),
	ECVF_Default);

const FName AWaterBody::WaterBodyIndexParamName(TEXT("WaterBodyIndex"));
const FName AWaterBody::WaterVelocityAndHeightName(TEXT("WaterVelocityAndHeight"));
const FName AWaterBody::GlobalOceanHeightName(TEXT("GlobalOceanHeight"));
const FName AWaterBody::FixedZHeightName(TEXT("FixedZHeight"));
const FName AWaterBody::OverriddenWaterDepthName(TEXT("Overridden Water Depth"));

// ----------------------------------------------------------------------------------

AWaterBody::AWaterBody(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAffectsLandscape = true;

	CollisionProfileName = GetDefault<UWaterRuntimeSettings>()->GetDefaultWaterCollisionProfileName();

	SetCanBeDamaged(false);
	bNetLoadOnClient = true;

	SplineComp = CreateDefaultSubobject<UWaterSplineComponent>(TEXT("WaterSpline"));
	SplineComp->SetMobility(EComponentMobility::Static);

	WaterSplineMetadata = ObjectInitializer.CreateDefaultSubobject<UWaterSplineMetadata>(this, TEXT("WaterSplineMetadata"));
	//@todo_water: Remove once AWaterBody is not Blueprintable
	WaterSplineMetadata->Reset(3);
	WaterSplineMetadata->AddPoint(0.0f);
	WaterSplineMetadata->AddPoint(1.0f);
	WaterSplineMetadata->AddPoint(2.0f);

	WaterMID = nullptr;

	TargetWaveMaskDepth = 2048.f;

#if WITH_EDITOR
	if (!IsTemplate())
	{
		SplineComp->OnSplineDataChanged().AddUObject(this, &AWaterBody::OnSplineDataChanged);
	}

	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterSprite"));
#endif

	RootComponent = SplineComp;

	bCanAffectNavigation = false;
	bFillCollisionUnderWaterBodiesForNavmesh = false;
}

bool AWaterBody::IsFlatSurface() const
{
	// Lakes and oceans have surfaces aligned with the XY plane
	return (GetWaterBodyType() == EWaterBodyType::Lake || GetWaterBodyType() == EWaterBodyType::Ocean);
}

bool AWaterBody::IsWaveSupported() const
{
	return (GetWaterBodyType() == EWaterBodyType::Lake || GetWaterBodyType() == EWaterBodyType::Ocean || GetWaterBodyType() == EWaterBodyType::Transition);
}

bool AWaterBody::HasWaves() const
{ 
	if (!IsWaveSupported())
	{
		return false;
	}
	return WaterWaves ? (WaterWaves->GetWaterWaves() != nullptr) : false; 
}

bool AWaterBody::IsWaterSplineClosedLoop() const
{
	return (GetWaterBodyType() == EWaterBodyType::Lake) || (GetWaterBodyType() == EWaterBodyType::Ocean);
}

bool AWaterBody::IsHeightOffsetSupported() const
{
	return GetWaterBodyType() == EWaterBodyType::Ocean;
}

bool AWaterBody::AffectsLandscape() const
{
	return bAffectsLandscape && (GetWaterBodyType() != EWaterBodyType::Transition);
}

bool AWaterBody::AffectsWaterMesh() const
{ 
	return ShouldGenerateWaterMeshTile();
}

#if WITH_EDITOR
ETextureRenderTargetFormat AWaterBody::GetBrushRenderTargetFormat() const
{
	return (GetWaterBodyType() == EWaterBodyType::River) ? ETextureRenderTargetFormat::RTF_RGBA32f : ETextureRenderTargetFormat::RTF_RGBA16f;
}

void AWaterBody::GetBrushRenderDependencies(TSet<UObject*>& OutDependencies) const
{
	for (const TPair<FName, FWaterBodyWeightmapSettings>& Pair : LayerWeightmapSettings)
	{
		if (Pair.Value.ModulationTexture)
		{
			OutDependencies.Add(Pair.Value.ModulationTexture);
		}
	}

	if (WaterHeightmapSettings.Effects.Displacement.Texture)
	{
		OutDependencies.Add(WaterHeightmapSettings.Effects.Displacement.Texture);
	}
}
#endif //WITH_EDITOR

void AWaterBody::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);
	if (UBuoyancyComponent* BuoyancyComponent = OtherActor->FindComponentByClass<UBuoyancyComponent>())
	{
		BuoyancyComponent->EnteredWaterBody(this);
	}
}

void AWaterBody::NotifyActorEndOverlap(AActor* OtherActor)
{
	Super::NotifyActorEndOverlap(OtherActor);
	if (UBuoyancyComponent* BuoyancyComponent = OtherActor->FindComponentByClass<UBuoyancyComponent>())
	{
		BuoyancyComponent->ExitedWaterBody(this);
	}
}

void AWaterBody::SetWaterMaterial(UMaterialInterface* InMaterial)
{
	WaterMaterial = InMaterial;
	UpdateMaterialInstances();
}

UMaterialInstanceDynamic* AWaterBody::GetWaterMaterialInstance()
{
	CreateOrUpdateWaterMID(); 
	return WaterMID;
}

UMaterialInstanceDynamic* AWaterBody::GetUnderwaterPostProcessMaterialInstance()
{
	CreateOrUpdateUnderwaterPostProcessMID(); 
	return UnderwaterPostProcessMID;
}

void AWaterBody::SetUnderwaterPostProcessMaterial(UMaterialInterface* InMaterial)
{
	UnderwaterPostProcessMaterial = InMaterial;
	UpdateMaterialInstances();
}

bool AWaterBody::ShouldGenerateWaterMeshTile() const
{
	return ((GetWaterBodyType() != EWaterBodyType::Transition)
		&& (GetWaterMeshOverride() == nullptr)
		&& (GetWaterMaterial() != nullptr));
}

void AWaterBody::AddIsland(AWaterBodyIsland* Island)
{
	Islands.AddUnique(Island);
}

void AWaterBody::RemoveIsland(AWaterBodyIsland* Island)
{
	Islands.RemoveSwap(Island);
}

void AWaterBody::UpdateIslands()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Water_UpdateIslands);

	// For now, islands are not detected dynamically
#if WITH_EDITOR
	if (GetWorld())
	{
		for (AWaterBodyIsland* Island : TActorRange<AWaterBodyIsland>(GetWorld()))
		{
			Island->UpdateOverlappingWaterBodies();
		}
	}
#endif // WITH_EDITOR
}

void AWaterBody::AddExclusionVolume(AWaterBodyExclusionVolume* InExclusionVolume)
{
	ExclusionVolumes.AddUnique(InExclusionVolume);
}

void AWaterBody::RemoveExclusionVolume(AWaterBodyExclusionVolume* InExclusionVolume)
{
	ExclusionVolumes.RemoveSwap(InExclusionVolume);
}

void AWaterBody::UpdateExclusionVolumes()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Water_UpdateExclusionVolumes);
	if (GetWorld())
	{
		for (AWaterBodyExclusionVolume* ExclusionVolume : TActorRange<AWaterBodyExclusionVolume>(GetWorld()))
		{
			ExclusionVolume->UpdateOverlappingWaterBodies();
		}
	}
}

FPostProcessVolumeProperties AWaterBody::GetPostProcessProperties() const
{
	FPostProcessVolumeProperties Ret;
	Ret.bIsEnabled = UnderwaterPostProcessSettings.bEnabled;
	Ret.bIsUnbound = false;
	Ret.BlendRadius = UnderwaterPostProcessSettings.BlendRadius;
	Ret.BlendWeight = UnderwaterPostProcessSettings.BlendWeight;
	Ret.Priority = UnderwaterPostProcessSettings.Priority;
	Ret.Settings = &CurrentPostProcessSettings;
	return Ret;
}

void AWaterBody::PostDuplicate(bool bDuplicateForPie)
{
	Super::PostDuplicate(bDuplicateForPie);

#if WITH_EDITOR
	if (!bDuplicateForPie && GIsEditor)
	{
		// After duplication due to copy-pasting, UWaterSplineMetadata might have been edited without the spline component being made aware of that (for some reason, USplineComponent::PostDuplicate isn't called)::
		SplineComp->SynchronizeWaterProperties();

		InitializeBody();

		OnWaterBodyChanged(/*bShapeOrPositionChanged*/true, /*bWeightmapSettingsChanged*/true);
	}

	RegisterOnUpdateWavesData(WaterWaves, /* bRegister = */true);
#endif // WITH_EDITOR
}

void AWaterBody::GetNavigationData(struct FNavigationRelevantData& Data) const
{
	if (CanAffectNavigation() && IsBodyInitialized())
	{
		const TSubclassOf<UNavAreaBase> UseAreaClass = GetNavAreaClass();
		TArray<UPrimitiveComponent*> LocalCollisionComponents = GetCollisionComponents();
		for (int32 CompIdx = 0; CompIdx < LocalCollisionComponents.Num(); CompIdx++)
		{
			UPrimitiveComponent* PrimComp = LocalCollisionComponents[CompIdx];
			if (PrimComp == nullptr)
			{
				UE_LOG(LogNavigation, Warning, TEXT("%s: skipping null collision component at index %d in %s"), ANSI_TO_TCHAR(__FUNCTION__), CompIdx, *GetFullNameSafe(this));
				continue;
			}

			FCompositeNavModifier CompositeNavModifier;
			CompositeNavModifier.CreateAreaModifiers(PrimComp, WaterNavAreaClass);
			for (FAreaNavModifier& AreaNavModifier : CompositeNavModifier.GetMutableAreas())
			{
				AreaNavModifier.SetExpandTopByCellHeight(true);
			}

			Data.Modifiers.Add(CompositeNavModifier);
			PrimComp->GetNavigationData(Data);
		}
	}
}

FBox AWaterBody::GetNavigationBounds() const
{
	return GetComponentsBoundingBox(true);
}

bool AWaterBody::IsNavigationRelevant() const
{
	return CanAffectNavigation() && (GetCollisionComponents().Num() > 0);
}

float AWaterBody::FindInputKeyClosestToWorldLocation(const FVector& WorldLocation) const
{
	return GetWaterSpline()->FindInputKeyClosestToWorldLocation(WorldLocation);
}

float AWaterBody::GetConstantSurfaceZ() const
{
	// A single Z doesn't really make sense for non-flat water bodies, but it can be useful for when using FixedZ post process for example. Take the first spline key in that case : 
	float WaterSurfaceZ = IsFlatSurface() ? GetActorLocation().Z : GetWaterSpline()->GetLocationAtSplineInputKey(0.0f, ESplineCoordinateSpace::World).Z;
	
	// Apply body height offset if applicable (ocean)
	if (IsHeightOffsetSupported())
	{
		WaterSurfaceZ += GetHeightOffset();
	}

	return WaterSurfaceZ;
}

float AWaterBody::GetConstantDepth() const
{
	// Only makes sense when you consider the water depth to be constant for the whole water body, in which case we just use the first spline key's : 
	return GetWaterSpline()->GetFloatPropertyAtSplineInputKey(0.0f, GET_MEMBER_NAME_CHECKED(UWaterSplineMetadata, Depth));
}

void AWaterBody::GetSurfaceMinMaxZ(float& OutMinZ, float& OutMaxZ) const
{
	float SurfaceZ = GetConstantSurfaceZ();
	float MaxWaveHeight = GetMaxWaveHeight();
	OutMaxZ = SurfaceZ + MaxWaveHeight;
	OutMinZ = SurfaceZ - MaxWaveHeight;
}

EWaterBodyQueryFlags AWaterBody::CheckAndAjustQueryFlags(EWaterBodyQueryFlags InQueryFlags) const
{
	EWaterBodyQueryFlags Result = InQueryFlags;

	// Waves only make sense for the following queries : 
	check(!EnumHasAnyFlags(Result, EWaterBodyQueryFlags::IncludeWaves)
		|| EnumHasAnyFlags(Result, EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::ComputeNormal | EWaterBodyQueryFlags::ComputeDepth | EWaterBodyQueryFlags::ComputeImmersionDepth));

	// Simple waves only make sense when computing waves : 
	check(!EnumHasAnyFlags(Result, EWaterBodyQueryFlags::SimpleWaves)
		|| EnumHasAnyFlags(Result, EWaterBodyQueryFlags::IncludeWaves));

	if (EnumHasAnyFlags(InQueryFlags, EWaterBodyQueryFlags::ComputeDepth | EWaterBodyQueryFlags::ComputeImmersionDepth))
	{
		// We need location when querying depth : 
		Result |= EWaterBodyQueryFlags::ComputeLocation;
	}

	if (EnumHasAnyFlags(InQueryFlags, EWaterBodyQueryFlags::IncludeWaves) && HasWaves())
	{
		// We need location and water depth when computing waves :
		Result |= EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::ComputeDepth;
	}

	return Result;
}

bool AWaterBody::IsWorldLocationInExclusionVolume(const FVector& InWorldLocation) const
{
	for (const TLazyObjectPtr<AWaterBodyExclusionVolume>& ExclusionVolume : ExclusionVolumes)
	{
		if (ExclusionVolume.IsValid() && ExclusionVolume->EncompassesPoint(InWorldLocation))
		{
			return true;
		}
	}

	return false;
}

FWaterBodyQueryResult AWaterBody::QueryWaterInfoClosestToWorldLocation(const FVector& InWorldLocation, EWaterBodyQueryFlags InQueryFlags, const TOptional<float>& InSplineInputKey) const
{
	SCOPE_CYCLE_COUNTER(STAT_WaterBody_ComputeWaterInfo);

	// Use the (optional) input spline input key if it has already been computed: 
	FWaterBodyQueryResult Result(InSplineInputKey);
	Result.SetQueryFlags(CheckAndAjustQueryFlags(InQueryFlags));

	if (!EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::IgnoreExclusionVolumes))
	{
		// No early-out, so that the requested information is still set. It is expected for the caller to check for IsInExclusionVolume() because technically, the returned information will be invalid :
		Result.SetIsInExclusionVolume(IsWorldLocationInExclusionVolume(InWorldLocation));
	}

	// Lakes and oceans have surfaces aligned with the XY plane
	const bool bFlatSurface = IsFlatSurface();

	// Compute water plane location :
	if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeLocation))
	{
		FVector WaterPlaneLocation = InWorldLocation;
		// If in exclusion volume, force the water plane location at the query location. It is technically invalid, but it's up to the caller to check whether we're in an exclusion volume. 
		//  If the user fails to do so, at least it allows immersion depth to be 0.0f, which means the query location is NOT in water :
		if (!Result.IsInExclusionVolume())
		{
			WaterPlaneLocation.Z = bFlatSurface ? GetActorLocation().Z : GetWaterSpline()->GetLocationAtSplineInputKey(Result.LazilyComputeSplineKey(*this, InWorldLocation), ESplineCoordinateSpace::World).Z;

			// Apply body height offset if applicable (ocean)
			if (IsHeightOffsetSupported())
			{
				WaterPlaneLocation.Z += GetHeightOffset();
			}
		}

		Result.SetWaterPlaneLocation(WaterPlaneLocation);
		// When not including waves, water surface == water plane : 
		Result.SetWaterSurfaceLocation(WaterPlaneLocation);
	}

	// Compute water plane normal :
	FVector WaterPlaneNormal = FVector::UpVector;
	if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeNormal))
	{
		// Default to Z up for the normal
		if (!bFlatSurface)
		{
			// For rivers default to using spline up vector to account for sloping rivers
			WaterPlaneNormal = GetWaterSpline()->GetUpVectorAtSplineInputKey(Result.LazilyComputeSplineKey(*this, InWorldLocation), ESplineCoordinateSpace::World);
		}

		Result.SetWaterPlaneNormal(WaterPlaneNormal);
		// When not including waves, water surface == water plane : 
		Result.SetWaterSurfaceNormal(WaterPlaneNormal);
	}

	// Compute water plane depth : 
	float WaveAttenuationFactor = 1.0f;
	if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeDepth))
	{
		SCOPE_CYCLE_COUNTER(STAT_WaterBody_ComputeWaterDepth);

		check(EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeLocation));
		float WaterPlaneDepth = 0.0f;

		// The better option for computing water depth for ocean and lake is landscape : 
		const bool bTryUseLandscape = (GetWaterBodyType() == EWaterBodyType::Ocean || GetWaterBodyType() == EWaterBodyType::Lake);
		if (bTryUseLandscape)
		{
			TOptional<float> LandscapeHeightOptional;
			if (ALandscapeProxy* LandscapePtr = FindLandscape())
			{
				SCOPE_CYCLE_COUNTER(STAT_WaterBody_ComputeLandscapeDepth);
				LandscapeHeightOptional = LandscapePtr->GetHeightAtLocation(InWorldLocation);
			}

			bool bValidLandscapeData = LandscapeHeightOptional.IsSet();
			if (bValidLandscapeData)
			{
				WaterPlaneDepth = Result.GetWaterPlaneLocation().Z - LandscapeHeightOptional.GetValue();
				// Special case : cancel out waves for under-landscape ocean
				if ((WaterPlaneDepth < 0.0f) && (GetWaterBodyType() == EWaterBodyType::Ocean))
				{
					WaveAttenuationFactor = 0.0f;
				}
			}

			// If the height is invalid, we either have invalid landscape data or we're under the landscape
			if (!bValidLandscapeData || (WaterPlaneDepth < 0.0f))
			{
				if (GetWaterBodyType() == EWaterBodyType::Ocean)
				{
					// Fallback value when landscape is not found under the ocean water.
					WaterPlaneDepth = CVarWaterOceanFallbackDepth.GetValueOnAnyThread();
				}
				else
				{
					check(GetWaterBodyType() == EWaterBodyType::Lake);
					// For an underwater lake, consider an uniform depth across the projection segment on the lake spline :
					WaterPlaneDepth = WaterSplineMetadata->Depth.Eval(Result.LazilyComputeSplineKey(*this, InWorldLocation), 0.f);
				}
			}
		}
		else
		{
			// For rivers and transitions, depth always come from the spline :
			WaterPlaneDepth = WaterSplineMetadata->Depth.Eval(Result.LazilyComputeSplineKey(*this, InWorldLocation), 0.f);
		}

		WaterPlaneDepth = FMath::Max(WaterPlaneDepth, 0.0f);
		Result.SetWaterPlaneDepth(WaterPlaneDepth);

		// When not including waves, water surface == water plane : 
		Result.SetWaterSurfaceDepth(WaterPlaneDepth);
	}

	// Optionally compute water surface location/normal/depth for waves : 
	if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::IncludeWaves) && HasWaves())
	{
		SCOPE_CYCLE_COUNTER(STAT_WaterBody_ComputeWaveHeight);
		FWaveInfo WaveInfo;

		if (!Result.IsInExclusionVolume())
		{
			WaveInfo.AttenuationFactor = WaveAttenuationFactor;
			WaveInfo.Normal = WaterPlaneNormal;
			const bool bSimpleWaves = EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::SimpleWaves);
			GetWaveInfoAtPosition(Result.GetWaterPlaneLocation(), Result.GetWaterSurfaceDepth(), bSimpleWaves, WaveInfo);
		}

		Result.SetWaveInfo(WaveInfo);

		if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeLocation))
		{
			FVector WaterSurfaceLocation = Result.GetWaterSurfaceLocation();
			WaterSurfaceLocation.Z += WaveInfo.Height;
			Result.SetWaterSurfaceLocation(WaterSurfaceLocation);
		}

		if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeNormal))
		{
			Result.SetWaterSurfaceNormal(WaveInfo.Normal);
		}

		if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeDepth))
		{
			Result.SetWaterSurfaceDepth(Result.GetWaterSurfaceDepth() + WaveInfo.Height);
		}
	}

	if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeImmersionDepth))
	{
		check(EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeLocation));

		// Immersion depth indicates how much under the water surface is the world location. 
		//  therefore, it takes into account the waves if IncludeWaves is passed :
		Result.SetImmersionDepth(Result.GetWaterSurfaceLocation().Z - InWorldLocation.Z);
		// When in an exclusion volume, the queried location is considered out of water (immersion depth == 0.0f)
		check(!Result.IsInExclusionVolume() || (Result.GetImmersionDepth() == 0.0f));
	}

	// Compute velocity : 
	if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeVelocity))
	{
		FVector Velocity = FVector::ZeroVector;
		if (!Result.IsInExclusionVolume())
		{
			Velocity = GetWaterVelocityVectorAtSplineInputKey(Result.LazilyComputeSplineKey(*this, InWorldLocation));
		}

		Result.SetVelocity(Velocity);
	}

	return Result;
}

float AWaterBody::GetWaterVelocityAtSplineInputKey(float InKey) const
{
	return WaterSplineMetadata->WaterVelocityScalar.Eval(InKey, 0.f);
}

FVector AWaterBody::GetWaterVelocityVectorAtSplineInputKey(float InKey) const
{
	float WaterVelocityScalar = GetWaterVelocityAtSplineInputKey(InKey);
	const FVector SplineDirection = GetWaterSpline()->GetDirectionAtSplineInputKey(InKey, ESplineCoordinateSpace::World);
	return SplineDirection * WaterVelocityScalar;
}


float AWaterBody::GetAudioIntensityAtSplineInputKey(float InKey) const
{
	return WaterSplineMetadata->AudioIntensity.Eval(InKey, 0.f);
}

TArray<AWaterBodyIsland*> AWaterBody::GetIslands() const
{
	TArray<AWaterBodyIsland*> IslandActors;
	IslandActors.Reserve(Islands.Num());

	for (const TLazyObjectPtr<AWaterBodyIsland>& IslandPtr : Islands)
	{
		AWaterBodyIsland* Island = IslandPtr.Get();
		if (Island)
		{
			IslandActors.Add(Island);
		}
	}

	return IslandActors;
}

TArray<AWaterBodyExclusionVolume*> AWaterBody::GetExclusionVolumes() const
{
	TArray<AWaterBodyExclusionVolume*> Result;
	Result.Reserve(ExclusionVolumes.Num());

	for (const TLazyObjectPtr<AWaterBodyExclusionVolume>& VolumePtr : ExclusionVolumes)
	{
		if (AWaterBodyExclusionVolume* Volume = VolumePtr.Get())
		{
			Result.Add(Volume);
		}
	}

	return Result;
}

void AWaterBody::SetWaterWaves(UWaterWavesBase* InWaterWaves)
{
	SetWaterWavesInternal(InWaterWaves, /*bTriggerWaterBodyChanged = */true);
}

void AWaterBody::SetWaterWavesInternal(UWaterWavesBase* InWaterWaves, bool bTriggerWaterBodyChanged)
{
	if (InWaterWaves != WaterWaves)
	{
#if WITH_EDITOR
		RegisterOnUpdateWavesData(WaterWaves, /*bRegister = */false);
#endif // WITH_EDITOR

		WaterWaves = InWaterWaves;

#if WITH_EDITOR
		RegisterOnUpdateWavesData(WaterWaves, /*bRegister = */true);
#endif // WITH_EDITOR		

		RequestGPUWaveDataUpdate();

		// Waves data can affect the navigation: 
		if (bTriggerWaterBodyChanged)
		{
		OnWaterBodyChanged(/*bShapeOrPositionChanged = */true);
	}
}
}

// Our transient MIDs are per-object and shall not survive duplicating nor be exported to text when copy-pasting : 
EObjectFlags AWaterBody::GetTransientMIDFlags() const
{
	return RF_Transient | RF_NonPIEDuplicateTransient | RF_TextExportTransient;
}

void AWaterBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	InitializeBody();

	UpdateAll(true);

	FindLandscape();

	UpdateMaterialInstances();

	UpdateWaterComponentVisibility();
}

void AWaterBody::PreInitializeComponents()
{
	// some water bodies are dynamic (e.g. Ocean) and thus need to be regenerated at runtime :
	UpdateAll(true);
}

void AWaterBody::BeginPlay()
{
	Super::BeginPlay();

	FindLandscape();

	TArray<UPrimitiveComponent*> LocalCollisionComponents = GetCollisionComponents();
	for (UPrimitiveComponent* CollisionComponent : LocalCollisionComponents)
	{
		check(CollisionComponent != nullptr);
		CollisionComponent->SetPhysMaterialOverride(PhysicalMaterial);
	}

	UpdateMaterialInstances();

	UpdateWaterComponentVisibility();
}

void AWaterBody::UpdateMaterialInstances()
{
	CreateOrUpdateWaterMID();
	CreateOrUpdateUnderwaterPostProcessMID();
}

bool AWaterBody::UpdateWaterHeight()
{
	bool bWaterBodyChanged = false;
	if (IsFlatSurface())
	{
		const int32 NumSplinePoints = SplineComp->GetNumberOfSplinePoints();

		const float ActorZ = GetActorLocation().Z;

		for (int32 PointIndex = 0; PointIndex < NumSplinePoints; ++PointIndex)
		{
			FVector WorldLoc = SplineComp->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);

			if (WorldLoc.Z != ActorZ)
			{
				bWaterBodyChanged = true;
				WorldLoc.Z = ActorZ;
				SplineComp->SetLocationAtSplinePoint(PointIndex, WorldLoc, ESplineCoordinateSpace::World);
			}
		}
	}

	return bWaterBodyChanged;
}

void AWaterBody::CreateOrUpdateWaterMID()
{
	// If GetWorld fails we may be in a blueprint
	if (GetWorld())
	{
		WaterMID = FWaterUtils::GetOrCreateTransientMID(WaterMID, TEXT("WaterMID"), WaterMaterial, GetTransientMIDFlags());

		SetDynamicParametersOnMID(WaterMID);
	}
}

void AWaterBody::CreateOrUpdateUnderwaterPostProcessMID()
{
	// If GetWorld fails we may be in a blueprint
	if (GetWorld())
	{
		UnderwaterPostProcessMID = FWaterUtils::GetOrCreateTransientMID(UnderwaterPostProcessMID, TEXT("UnderwaterPostProcessMID"), UnderwaterPostProcessMaterial, GetTransientMIDFlags());

		SetDynamicParametersOnUnderwaterPostProcessMID(UnderwaterPostProcessMID);

		// update the transient post process settings accordingly : 
		PrepareCurrentPostProcessSettings();
	}
}

void AWaterBody::PrepareCurrentPostProcessSettings()
{
	// Prepare the transient settings that are actually used by the post-process system : 
	// - Copy all the non-transient settings :
	CurrentPostProcessSettings = UnderwaterPostProcessSettings.PostProcessSettings;

	// - Control the WeightedBlendables with the transient underwater post process MID : 
	if (UnderwaterPostProcessMID != nullptr)
	{
		if (CurrentPostProcessSettings.WeightedBlendables.Array.Num() == 0)
		{
			CurrentPostProcessSettings.WeightedBlendables.Array.Emplace();
		}
		FWeightedBlendable& Blendable = CurrentPostProcessSettings.WeightedBlendables.Array[0];
		Blendable.Object = UnderwaterPostProcessMID;
		Blendable.Weight = 1.0f;
	}
	else
	{
		CurrentPostProcessSettings.WeightedBlendables.Array.Empty();
	}
}

ALandscapeProxy* AWaterBody::FindLandscape() const
{
	UWorld* World = GetWorld();
	if (bAffectsLandscape && !Landscape.IsValid() && (World != nullptr))
	{
		FBox WaterBodyAABB = GetComponentsBoundingBox();
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			if (WaterBodyAABB.Intersect(It->GetComponentsBoundingBox()))
			{
				Landscape = *It;
				return Landscape.Get();
			}
		}
	}
	return Landscape.Get();
}
void AWaterBody::UpdateWaterComponentVisibility()
{	
	if (UWorld* World = GetWorld())
	{
		// If water rendering is enabled we dont need the components to do the rendering
		const bool bIsWaterRenderingEnabled = UWaterSubsystem::GetWaterSubsystem(World)->IsWaterRenderingEnabled();

		const bool bIsEditorWorld = GetWorld()->IsEditorWorld();

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
		GetComponents(MeshComponents);
		for (UStaticMeshComponent* Component : MeshComponents)
		{
			if (GetWaterBodyType() == EWaterBodyType::Transition)
			{
				Component->SetVisibility(bIsWaterRenderingEnabled);
				Component->SetHiddenInGame(!bIsWaterRenderingEnabled);
			}
			else if (bIsEditorWorld)
			{
				Component->SetVisibility(false);
				Component->SetHiddenInGame(true);
			}
			else
			{
				Component->SetHiddenInGame(bIsWaterRenderingEnabled);
			}
		}
	}
}

#if WITH_EDITOR
void AWaterBody::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (bFinished)
	{
		UpdateWaterHeight();
	}

	OnWaterBodyChanged(bFinished);
}

void AWaterBody::PreEditUndo()
{
	Super::PreEditUndo();

	// On undo, when PreEditChange is called, PropertyAboutToChange is nullptr so we need to unregister from the previous object here :
	RegisterOnUpdateWavesData(WaterWaves, /*bRegister = */false);
}

void AWaterBody::PostEditUndo()
{
	Super::PostEditUndo();

	OnWaterBodyChanged(/*bShapeOrPositionChanged*/true, /*bWeightmapSettingsChanged*/true);

	// On undo, when PostEditChangeProperty is called, PropertyChangedEvent is fake so we need to register to the new object here :
	RegisterOnUpdateWavesData(WaterWaves, /*bRegister = */true);

	RequestGPUWaveDataUpdate();
}

void AWaterBody::PostEditImport()
{
	Super::PostEditImport();

	OnWaterBodyChanged(/*bShapeOrPositionChanged*/true, /*bWeightmapSettingsChanged*/true);

	RequestGPUWaveDataUpdate();
}

void AWaterBody::UpdateActorIcon()
{
	if (ActorIcon && !bIsEditorPreviewActor)
	{
		// Actor icon gets in the way of meshes
		ActorIcon->SetVisibility(IsIconVisible());

		UTexture2D* IconTexture = ActorIcon->Sprite;
		IWaterModuleInterface& WaterModule = FModuleManager::GetModuleChecked<IWaterModuleInterface>("Water");
		if (const IWaterEditorServices* WaterEditorServices = WaterModule.GetWaterEditorServices())
		{
			if (CheckWaterBodyStatus() != EWaterBodyStatus::Valid)
			{
				IconTexture = WaterEditorServices->GetErrorSprite();
			}
			else
			{
				IconTexture = WaterEditorServices->GetWaterActorSprite(GetClass());
			}
		}
		FWaterIconHelper::UpdateSpriteComponent(this, IconTexture);


		if (GetWaterBodyType() == EWaterBodyType::Lake && SplineComp)
		{
			// Move the actor icon to the center of the lake
			FVector ZOffset(0.0f, 0.0f, GetDefault<UWaterRuntimeSettings>()->WaterBodyIconWorldZOffset);
			ActorIcon->SetWorldLocation(SplineComp->Bounds.Origin + ZOffset);
		}
	}
}

bool AWaterBody::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(AWaterBody, WaterMeshOverride))
		{
			return bOverrideWaterMesh || GetWaterBodyType() == EWaterBodyType::Transition;
		}
		else if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(AWaterBody, WaterBodyType))
		{
			return !IsWaterBodyTypeReadOnly();
		}
	}
	return Super::CanEditChange(InProperty);
}

bool AWaterBody::IsIconVisible() const
{
	return GetWaterBodyType() != EWaterBodyType::Transition;
}

void AWaterBody::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FName PropertyName = PropertyAboutToChange ? PropertyAboutToChange->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AWaterBody, WaterWaves))
	{
		RegisterOnUpdateWavesData(WaterWaves, /* bRegister = */false);
	}
}

void AWaterBody::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged)
{
	bool bRequestGPUWaveDataUpdate = false;
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBody, WaterBodyType))
	{
		if (ensure(!IsWaterBodyTypeReadOnly()))
		{
			InitializeBody();
			bShapeOrPositionChanged = true;
		}
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBody, LayerWeightmapSettings))
	{
		bWeightmapSettingsChanged = true;
	}
	else if ((PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBody, WaterMaterial)) ||
			 (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBody, UnderwaterPostProcessMaterial)))
	{
		UpdateMaterialInstances();
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBody, WaterWaves))
	{
		RegisterOnUpdateWavesData(WaterWaves, /* bRegister = */true);

		RequestGPUWaveDataUpdate();

		// Waves data affect the navigation : 
		bShapeOrPositionChanged = true;
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBody, TargetWaveMaskDepth))
	{
		RequestGPUWaveDataUpdate();
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBody, MaxWaveHeightOffset))
	{
		bShapeOrPositionChanged = true;
	}
}

AWaterBody::EWaterBodyStatus AWaterBody::CheckWaterBodyStatus() const
{
	if (!IsTemplate())
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(World))
			{
				if (AffectsWaterMesh() && (WaterSubsystem->GetWaterMeshActor() == nullptr))
				{
					return EWaterBodyStatus::MissingWaterMesh;
				}
			}

			if (AffectsLandscape() && FindLandscape() == nullptr)
			{
				return EWaterBodyStatus::MissingLandscape;
			}
		}
	}

	return EWaterBodyStatus::Valid;
}

void AWaterBody::CheckForErrors()
{
	Super::CheckForErrors();

	switch (CheckWaterBodyStatus())
	{
	case EWaterBodyStatus::MissingWaterMesh:
		FMessageLog("MapCheck").Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_MissingWaterMesh", "This water body requires a WaterMeshActor to be rendered. Please add one to the map. ")))
			->AddToken(FMapErrorToken::Create(TEXT("WaterBodyMissingWaterMesh")));
		break;
	case EWaterBodyStatus::MissingLandscape:
		FMessageLog("MapCheck").Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_MissingLandscape", "This water body requires a Landscape to be rendered. Please add one to the map. ")))
			->AddToken(FMapErrorToken::Create(TEXT("WaterBodyMissingLandscape")));
		break;
	}
}

void AWaterBody::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	bool bShapeOrPositionChanged = false;
	bool bWeightmapSettingsChanged = false;

	OnPostEditChangeProperty(PropertyChangedEvent, bShapeOrPositionChanged, bWeightmapSettingsChanged);
	
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnWaterBodyChanged(bShapeOrPositionChanged, bWeightmapSettingsChanged);
}

void AWaterBody::OnSplineDataChanged()
{
	OnWaterBodyChanged(/*bShapeOrPositionChanged*/true);
}

void AWaterBody::RegisterOnUpdateWavesData(UWaterWavesBase* InWaterWaves, bool bRegister)
{
	if (InWaterWaves != nullptr)
	{
		if (bRegister)
		{
			InWaterWaves->OnUpdateWavesData.AddUObject(this, &AWaterBody::OnWavesDataUpdated);
		}
		else
		{
			InWaterWaves->OnUpdateWavesData.RemoveAll(this);
		}
	}
}

void AWaterBody::OnWavesDataUpdated(UWaterWavesBase* InWaterWaves, EPropertyChangeType::Type InChangeType)
{
	RequestGPUWaveDataUpdate();

	// Waves data affect the navigation : 
	OnWaterBodyChanged(/*bShapeOrPositionChanged = */true);
}

void AWaterBody::OnWaterSplineMetadataChanged(UWaterSplineMetadata* InWaterSplineMetadata, FPropertyChangedEvent& PropertyChangedEvent)
{
	bool bShapeOrPositionChanged = false;

	FName ChangedProperty = PropertyChangedEvent.GetPropertyName();
	if ((ChangedProperty == NAME_None)
		|| (ChangedProperty == GET_MEMBER_NAME_CHECKED(UWaterSplineMetadata, Depth))
		|| (ChangedProperty == GET_MEMBER_NAME_CHECKED(UWaterSplineMetadata, RiverWidth))
		|| (ChangedProperty == GET_MEMBER_NAME_CHECKED(UWaterSplineMetadata, WaterVelocityScalar)))
	{
		// Those changes require an update of the water brush (except in interactive mode, where we only apply the change once the value is actually set): 
		bShapeOrPositionChanged = true;
	}

	if ((ChangedProperty == NAME_None)
		|| (ChangedProperty == GET_MEMBER_NAME_CHECKED(UWaterSplineMetadata, RiverWidth)))
	{ 
		// River Width is driving the spline shape, make sure the spline component is aware of the change : 
		SplineComp->SynchronizeWaterProperties();
	}

	// Waves data affect the navigation : 
	OnWaterBodyChanged(bShapeOrPositionChanged);
}

void AWaterBody::RegisterOnChangeWaterSplineMetadata(UWaterSplineMetadata* InWaterSplineMetadata, bool bRegister)
{
	if (InWaterSplineMetadata != nullptr)
	{
		if (bRegister)
		{
			InWaterSplineMetadata->OnChangeData.AddUObject(this, &AWaterBody::OnWaterSplineMetadataChanged);
		}
		else
		{
			InWaterSplineMetadata->OnChangeData.RemoveAll(this);
		}
	}
}

#endif

void AWaterBody::ApplyNavigationSettings() const
{
	if (IsBodyInitialized())
	{
		const bool bCanAffectNav = CanAffectNavigation();

		TInlineComponentArray<UActorComponent*> Components;
		GetComponents(Components);

		TArray<UPrimitiveComponent*> LocalCollisionComponents = GetCollisionComponents();
		for (UActorComponent* ActorComp : Components)
		{
			UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComp);
			if (!PrimComp || LocalCollisionComponents.Find(PrimComp) == INDEX_NONE)
			{
				ActorComp->SetCanEverAffectNavigation(false);
			}
			else
			{
				PrimComp->SetCustomNavigableGeometry(bCanAffectNav ? EHasCustomNavigableGeometry::EvenIfNotCollidable : EHasCustomNavigableGeometry::No);
				PrimComp->SetCanEverAffectNavigation(bCanAffectNav);
			}
		}
	}
}

void AWaterBody::RequestGPUWaveDataUpdate()
{
	if (FWaterBodyManager* Manager = UWaterSubsystem::GetWaterBodyManager(GetWorld()))
	{
		Manager->RequestWaveDataRebuild();
	}
}

void AWaterBody::BeginUpdateWaterBody()
{
	UpdateSplineComponent();
}

void AWaterBody::UpdateAll(bool bShapeOrPositionChanged)
{
	BeginUpdateWaterBody();

	if (GIsEditor || IsBodyDynamic())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Water_UpdateAll);

		bShapeOrPositionChanged |= UpdateWaterHeight();

		if (bShapeOrPositionChanged)
		{
			// We might be affected to a different landscape now that our shape has changed : 
			Landscape.Reset();
		}

		// First, update the water body without taking into account exclusion volumes, as those rely on the collision to detect overlapping water bodies
		UpdateWaterBody(/* bWithExclusionVolumes*/false);

		// Then, update the list of exclusion volumes after this adjustment
		if (bShapeOrPositionChanged)
		{
			UpdateIslands();

			UpdateExclusionVolumes();
		}

		// Finally, generate the body once again, this time with the updated list of exclusion volumes
		UpdateWaterBody(/*bWithExclusionVolumes*/true);

		ApplyNavigationSettings();

		if (bShapeOrPositionChanged)
		{
			FNavigationSystem::UpdateActorAndComponentData(*this);
		}

		UpdateWaterComponentVisibility();

#if WITH_EDITOR
		UpdateActorIcon();
#endif
	}
}

void AWaterBody::UpdateSplineComponent()
{
	if (SplineComp)
	{
		SplineComp->SetClosedLoop(IsWaterSplineClosedLoop());
	}
}

void AWaterBody::OnWaterBodyChanged(bool bShapeOrPositionChanged, bool bWeightmapSettingsChanged)
{
	UpdateAll(bShapeOrPositionChanged);

	// Some of the spline parameters need to be transferred to the underwater post process MID, if any : 
	if (bShapeOrPositionChanged)
	{
		SetDynamicParametersOnUnderwaterPostProcessMID(UnderwaterPostProcessMID);
	}

#if WITH_EDITOR
	FWaterBrushActorChangedEventParams Params(this);
	Params.bShapeOrPositionChanged = bShapeOrPositionChanged;
	Params.bWeightmapSettingsChanged = bWeightmapSettingsChanged;
	BroadcastWaterBrushActorChangedEvent(Params);
#endif
}

void AWaterBody::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FWaterCustomVersion::GUID);
}

void AWaterBody::PostLoad()
{
	Super::PostLoad();

	if (SplineComp)
	{
		if (GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::MoveWaterMetadataToActor)
		{
			if (SplineComp->SplineCurves.Metadata_DEPRECATED)
			{
				UWaterSplineMetadata* OldSplineMetadata = Cast<UWaterSplineMetadata>(SplineComp->SplineCurves.Metadata_DEPRECATED);
				SplineComp->SplineCurves.Metadata_DEPRECATED = nullptr;

				if (WaterSplineMetadata)
				{
					WaterSplineMetadata->Depth = OldSplineMetadata->Depth;
					WaterSplineMetadata->WaterVelocityScalar = OldSplineMetadata->WaterVelocityScalar;
					WaterSplineMetadata->RiverWidth = OldSplineMetadata->RiverWidth;
				}
			}
		}

		// Keep metadata in sync
		if (WaterSplineMetadata)
		{
			const int32 NumPoints = SplineComp->GetNumberOfSplinePoints();
			WaterSplineMetadata->Fixup(NumPoints, SplineComp);
		}
	}

	TArray<UPrimitiveComponent*> LocalCollisionComponents = GetCollisionComponents();

	if (WaterBodyType == EWaterBodyType::Lake && GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::ReplaceLakeCollision)
	{
		for (auto It = LocalCollisionComponents.CreateIterator(); It; ++It)
		{
			if (UBoxComponent* OldLakeCollision = Cast<UBoxComponent>(*It))
			{
				OldLakeCollision->ConditionalPostLoad();

				OldLakeCollision->DestroyComponent();
				// Rename it so we can use the name
				OldLakeCollision->Rename(TEXT("LakeCollision_Old"), this, REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
				It.RemoveCurrent();
			}
		}
	}

	if (GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::FixUpUnderwaterPostProcessMaterial)
	{
		// Get back the underwater post process material from where it was before : 
		// UnderwaterPostProcessMaterial_DEPRECATED takes priority as it was used to override the material from WeightedBlendables that was set via the BP : 
		if (UnderwaterPostProcessSettings.UnderwaterPostProcessMaterial_DEPRECATED)
		{
			UnderwaterPostProcessMaterial = UnderwaterPostProcessSettings.UnderwaterPostProcessMaterial_DEPRECATED;
		}
		else if (UnderwaterPostProcessSettings.PostProcessSettings.WeightedBlendables.Array.Num() > 0)
		{
			UnderwaterPostProcessMaterial = Cast<UMaterialInterface>(UnderwaterPostProcessSettings.PostProcessSettings.WeightedBlendables.Array[0].Object);
			UnderwaterPostProcessSettings.PostProcessSettings.WeightedBlendables.Array.Empty();
		}
		// If the material was actually already a MID, use its parent, we will instantiate a transient MID out of it from code anyway : 
		if (UnderwaterPostProcessMaterial)
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(UnderwaterPostProcessMaterial))
			{
				UnderwaterPostProcessMaterial = MID->GetMaterial();
			}
		}

		// don't call CreateOrUpdateUnderwaterPostProcessMID() just yet because we need the water mesh actor to be registerd
	}

	if (GetLinkerCustomVersion(FWaterCustomVersion::GUID) < FWaterCustomVersion::WaterBodyRefactor)
	{
		// Try to retrieve wave data from BP properties when it was defined in BP : 
		if (UClass* WaterBodyClass = GetClass())
		{
			if (WaterBodyClass->ClassGeneratedBy != nullptr)
			{
				FStructProperty* OldWaveStructProperty = nullptr;
				for (FProperty* BPProperty = WaterBodyClass->PropertyLink; BPProperty != nullptr; BPProperty = BPProperty->PropertyLinkNext)
				{
					const FString WaveSpectrumSettingsName(TEXT("Wave Spectrum Settings"));
					if (BPProperty->GetName() == WaveSpectrumSettingsName)
					{
						OldWaveStructProperty = CastField<FStructProperty>(BPProperty);
						break;
					}
				}

				if (OldWaveStructProperty != nullptr)
				{
					void* OldPropertyOnWaveSpectrumSettings = OldWaveStructProperty->ContainerPtrToValuePtr<void>(this);
					// We need to propagate object flags to the sub objects (if we deprecate an archetype's data, it is public and its sub-object need to be as well) :
					EObjectFlags NewFlags = GetMaskedFlags(RF_PropagateToSubObjects);
					UGerstnerWaterWaves* GerstnerWaves = NewObject<UGerstnerWaterWaves>(this, MakeUniqueObjectName(this, UGerstnerWaterWaves::StaticClass(), TEXT("GestnerWaterWaves")), NewFlags);
					UClass* NewGerstnerClass = UGerstnerWaterWaveGeneratorSimple::StaticClass();
					UGerstnerWaterWaveGeneratorSimple* GerstnerWavesGenerator = NewObject<UGerstnerWaterWaveGeneratorSimple>(this, MakeUniqueObjectName(this, NewGerstnerClass, TEXT("GestnerWaterWavesGenerator")), NewFlags);
					GerstnerWaves->GerstnerWaveGenerator = GerstnerWavesGenerator;
					SetWaterWavesInternal(GerstnerWaves, /*bTriggerWaterBodyChanged = */false); // we're in PostLoad, we don't want to send the water body changed event as it might re-enter into BP script

					for (FProperty* NewProperty = NewGerstnerClass->PropertyLink; NewProperty != nullptr; NewProperty = NewProperty->PropertyLinkNext)
					{
						void* NewPropertyOnGerstnerWavesGenerator = NewProperty->ContainerPtrToValuePtr<void>(GerstnerWavesGenerator);

						// Iterate through each property field in the lightmass settings struct that we are copying from...
						for (TFieldIterator<FProperty> OldIt(OldWaveStructProperty->Struct); OldIt; ++OldIt)
						{
							FProperty* OldProperty = *OldIt;
							void* OldPropertyToCopy = OldProperty->ContainerPtrToValuePtr<void>(OldPropertyOnWaveSpectrumSettings);
							if ((OldProperty->GetName().Contains(NewProperty->GetName()))
								|| (OldProperty->GetName().Contains(FString(TEXT("MaxWaves"))) && (NewProperty->GetName() == FString(TEXT("NumWaves")))))
							{
								OldProperty->CopySingleValue(NewPropertyOnGerstnerWavesGenerator, OldPropertyToCopy);
								break;
							}
							else if (OldProperty->GetName().Contains(FString(TEXT("DominantWaveDirection"))) && (NewProperty->GetName() == FString(TEXT("WindAngleDeg"))))
							{
								FVector2D Direction2D;
								OldProperty->CopySingleValue(&Direction2D, OldPropertyToCopy);
								FVector Direction(Direction2D, 0.0f);
								FRotator Rotator = Direction.Rotation();
								GerstnerWavesGenerator->WindAngleDeg = Rotator.Yaw;
								break;
							}
						}
					}
				}
			}
		}
	}

	if (GetLinkerCustomVersion(FWaterCustomVersion::GUID) < FWaterCustomVersion::FixupUnserializedGerstnerWaves)
	{
		// At one point, some attributes from UGerstnerWaterWaves were transient, recompute those here at load-time (nowadays, they are serialized properly so they should be properly recompute on property change)
		if (HasWaves())
		{
			check(WaterWaves != nullptr);
			if (UGerstnerWaterWaves* GerstnerWaterWaves = Cast<UGerstnerWaterWaves>(WaterWaves->GetWaterWaves()))
			{
				GerstnerWaterWaves->ConditionalPostLoad();
				GerstnerWaterWaves->RecomputeWaves(/*bAllowBPScript = */false); // We're in PostLoad, don't let BP script run, this is forbidden
			}
		}
	}

#if WITH_EDITORONLY_DATA
	if (GetLinkerCustomVersion(FWaterCustomVersion::GUID) < FWaterCustomVersion::MoveTerrainCarvingSettingsToWater)
	{
		static_assert(sizeof(WaterHeightmapSettings) == sizeof(TerrainCarvingSettings_DEPRECATED), "Both old and old water heightmap settings struct should be exactly similar");
		FMemory::Memcpy((void*)&WaterHeightmapSettings, (void*)&TerrainCarvingSettings_DEPRECATED, sizeof(WaterHeightmapSettings));
	}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		if (WaterWaves && (WaterWaves->GetOuter() != this))
		{
			WaterWaves->ClearFlags(RF_Public);
			// At one point, WaterWaves's outer was the level. We need them to be outered by the water body : 
			WaterWaves->Rename(nullptr, this, REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);
		}
	}
#endif // WITH_EDITOR

#if WITH_EDITOR
	RegisterOnUpdateWavesData(WaterWaves, /* bRegister = */true);
#endif
}

void AWaterBody::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

#if WITH_EDITOR
	// Register to data changes on the spline metadata (we only do it here because WaterSplineMetadata shouldn't ever change after creation/load/duplication)
	RegisterOnChangeWaterSplineMetadata(WaterSplineMetadata, /*bRegister = */true);

	FixupOnPostRegisterAllComponents();

	// Make sure existing collision components are marked as net-addressable (their names should already be deterministic) :
	TArray<UPrimitiveComponent*> LocalCollisionComponents = GetCollisionComponents();
	for (auto It = LocalCollisionComponents.CreateIterator(); It; ++It)
	{
		if (UActorComponent* CollisionComponent = Cast<UActorComponent>(*It))
		{
			CollisionComponent->SetNetAddressable();
		}
	}
#endif // WITH_EDITOR

	// We must check for WaterBodyIndex to see if we have already been registered because PostRegisterAllComponents can be called multiple times in a row (e.g. if the actor is a child 
	//  actor of another BP, the parent BP instance will register first, with all its child components, which will trigger registration of the child water body actor, and then 
	//  the water body actor will also get registered independently as a "standard" actor) :
	FWaterBodyManager* Manager = UWaterSubsystem::GetWaterBodyManager(GetWorld());
	if (Manager && !IsTemplate() && (WaterBodyIndex == INDEX_NONE))
	{
		WaterBodyIndex = Manager->AddWaterBody(this);
	}
	
	// At this point, the water mesh actor should be ready and we can setup the MID accordingly : 
	// Needs to be done at the end so that all data needed by the MIDs (e.g. WaterBodyIndex) is up to date :
	UpdateMaterialInstances();
}

void AWaterBody::PostUnregisterAllComponents()
{
	Super::PostUnregisterAllComponents();

	// We must check for WaterBodyIndex because PostUnregisterAllComponents can be called multiple times in a row by PostEditChangeProperty, etc.
	FWaterBodyManager* Manager = UWaterSubsystem::GetWaterBodyManager(GetWorld());
	if (Manager && !IsTemplate() && (WaterBodyIndex != INDEX_NONE))
	{
		Manager->RemoveWaterBody(this);
	}
	WaterBodyIndex = INDEX_NONE;
}

void AWaterBody::Destroyed()
{
	Super::Destroyed();

#if WITH_EDITOR
	RegisterOnChangeWaterSplineMetadata(WaterSplineMetadata, /*bRegister = */false);
	RegisterOnUpdateWavesData(WaterWaves, /*bRegister = */false);
#endif // WITH_EDITOR		
}

UWaterBodyGenerator::UWaterBodyGenerator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UWaterBodyGenerator::UpdateBody(bool bWithExclusionVolumes)
{
	AWaterBody* OwnerBody = GetOuterAWaterBody();
	// The first update is without exclusion volumes : perform it.
	// The second update is with exclusion volumes but there's no need to perform it again if we don't have exclusion volumes anyway, because the result will be the same.
	if (!bWithExclusionVolumes || (OwnerBody->GetExclusionVolumes().Num() > 0))
	{
		OnUpdateBody(bWithExclusionVolumes);
	}
}

void AWaterBody::SetDynamicParametersOnMID(UMaterialInstanceDynamic* InMID)
{
	UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld());
	if ((InMID == nullptr) || (WaterSubsystem == nullptr))
	{
		return;
	}

	const float GlobalOceanHeight = WaterSubsystem->GetOceanTotalHeight();
	InMID->SetScalarParameterValue(WaterBodyIndexParamName, WaterBodyIndex);
	InMID->SetScalarParameterValue(GlobalOceanHeightName, GlobalOceanHeight);

	if (AWaterMeshActor* WaterMesh = WaterSubsystem->GetWaterMeshActor())
	{
		InMID->SetTextureParameterValue(WaterVelocityAndHeightName, WaterMesh->WaterVelocityTexture);
	}
}

void AWaterBody::SetDynamicParametersOnUnderwaterPostProcessMID(UMaterialInstanceDynamic* InMID)
{
	UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld());
	if ((InMID == nullptr) || (WaterSubsystem == nullptr))
	{
		return;
	}

	// The post process MID needs the same base parameters as the water materials : 
	SetDynamicParametersOnMID(InMID);

	InMID->SetScalarParameterValue(FixedZHeightName, GetConstantSurfaceZ());
	InMID->SetScalarParameterValue(OverriddenWaterDepthName, GetConstantDepth());
}

float AWaterBody::GetWaveReferenceTime() const
{
	if (HasWaves())
	{
		if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
		{
			return WaterSubsystem->GetWaterTimeSeconds();
		}
	}
	return 0.0f;
}

/** Returns wave-related information at the given world position and for this water depth.
 Pass bSimpleWaves = true for the simple version (faster computation, lesser accuracy, doesn't perturb the normal) */
bool AWaterBody::GetWaveInfoAtPosition(const FVector& InPosition, float InWaterDepth, bool bInSimpleWaves, FWaveInfo& InOutWaveInfo) const
{
	if (!HasWaves())
	{
		return false; //Collision needs to be fixed for rivers
	}

	float MaxWaveHeight = GetMaxWaveHeight();

	InOutWaveInfo.ReferenceTime = GetWaveReferenceTime();
	InOutWaveInfo.AttenuationFactor *= GetWaveAttenuationFactor(InPosition, InWaterDepth);

	// No need to perform computation if we're going to cancel it out afterwards :
	if (InOutWaveInfo.AttenuationFactor > 0.0f)
	{
		// Maximum amplitude that the wave can reach at this location : 
		InOutWaveInfo.MaxHeight = MaxWaveHeight * InOutWaveInfo.AttenuationFactor;

		float WaveHeight;
		if (bInSimpleWaves)
		{
			WaveHeight = GetSimpleWaveHeightAtPosition(InPosition, InWaterDepth, InOutWaveInfo.ReferenceTime);
		}
		else
		{
			FVector ComputedNormal;
			WaveHeight = GetWaveHeightAtPosition(InPosition, InWaterDepth, InOutWaveInfo.ReferenceTime, ComputedNormal);
			// Attenuate the normal :
			ComputedNormal = FMath::Lerp(InOutWaveInfo.Normal, ComputedNormal, InOutWaveInfo.AttenuationFactor);
			if (!ComputedNormal.IsZero())
			{
				InOutWaveInfo.Normal = ComputedNormal;
			}
		}

		// Attenuate the wave amplitude :
		InOutWaveInfo.Height = WaveHeight * InOutWaveInfo.AttenuationFactor;
	}

	return true;
}

float AWaterBody::GetMaxWaveHeight() const
{
	return (HasWaves() ? WaterWaves->GetMaxWaveHeight() : 0.0f) + MaxWaveHeightOffset;
}

float AWaterBody::GetWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime, FVector& OutNormal) const
{
	check(HasWaves());

	return WaterWaves->GetWaveHeightAtPosition(InPosition, InWaterDepth, InTime, OutNormal);
}

float AWaterBody::GetSimpleWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime) const
{
	check(HasWaves());

	return WaterWaves->GetSimpleWaveHeightAtPosition(InPosition, InWaterDepth, InTime);
}

float AWaterBody::GetWaveAttenuationFactor(const FVector& InPosition, float InWaterDepth) const
{
	check(HasWaves());

	return WaterWaves->GetWaveAttenuationFactor(InPosition, InWaterDepth, TargetWaveMaskDepth);
}

#undef LOCTEXT_NAMESPACE 