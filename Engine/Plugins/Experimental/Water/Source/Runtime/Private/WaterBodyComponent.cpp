// Copyright Epic Games, Inc. All Rights Reserved.


#include "WaterBodyComponent.h"
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
#include "WaterModule.h"
#include "WaterSubsystem.h"
#include "WaterZoneActor.h"
#include "WaterBodyExclusionVolume.h"
#include "WaterBodyIslandActor.h"
#include "WaterSplineMetadata.h"
#include "WaterSplineComponent.h"
#include "WaterRuntimeSettings.h"
#include "WaterUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GerstnerWaterWaves.h"
#include "WaterMeshComponent.h"
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
DECLARE_CYCLE_STAT(TEXT("WaterBody_ComputeWaterDepth"), STAT_WaterBody_ComputeLocation, STATGROUP_Water);
DECLARE_CYCLE_STAT(TEXT("WaterBody_ComputeWaterDepth"), STAT_WaterBody_ComputeNormal, STATGROUP_Water);
DECLARE_CYCLE_STAT(TEXT("WaterBody_ComputeLandscapeDepth"), STAT_WaterBody_ComputeLandscapeDepth, STATGROUP_Water);
DECLARE_CYCLE_STAT(TEXT("WaterBody_ComputeWaveHeight"), STAT_WaterBody_ComputeWaveHeight, STATGROUP_Water);
// ----------------------------------------------------------------------------------

TAutoConsoleVariable<float> CVarWaterOceanFallbackDepth(
	TEXT("r.Water.OceanFallbackDepth"),
	3000.0f,
	TEXT("Depth to report for the ocean when no terrain is found under the query location. Not used when <= 0."),
	ECVF_Default);

const FName UWaterBodyComponent::WaterBodyIndexParamName(TEXT("WaterBodyIndex"));
const FName UWaterBodyComponent::WaterVelocityAndHeightName(TEXT("WaterVelocityAndHeight"));
const FName UWaterBodyComponent::GlobalOceanHeightName(TEXT("GlobalOceanHeight"));
const FName UWaterBodyComponent::FixedZHeightName(TEXT("FixedZHeight"));
const FName UWaterBodyComponent::WaterAreaParamName(TEXT("WaterArea"));
const FName UWaterBodyComponent::FixedVelocityName(TEXT("FixedVelocity"));
const FName UWaterBodyComponent::FixedWaterDepthName(TEXT("FixedWaterDepth"));

// ----------------------------------------------------------------------------------

UWaterBodyComponent::UWaterBodyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAffectsLandscape = true;

	CollisionProfileName = GetDefault<UWaterRuntimeSettings>()->GetDefaultWaterCollisionProfileName();

	WaterMID = nullptr;

	TargetWaveMaskDepth = 2048.f;

	bCanAffectNavigation = false;
	bFillCollisionUnderWaterBodiesForNavmesh = false;
}

void UWaterBodyComponent::OnVisibilityChanged()
{
	Super::OnVisibilityChanged();

	UpdateComponentVisibility(/* bAllowWaterMeshRebuild = */true);
}

void UWaterBodyComponent::OnHiddenInGameChanged()
{
	Super::OnHiddenInGameChanged();

	UpdateComponentVisibility(/* bAllowWaterMeshRebuild = */true);
}

bool UWaterBodyComponent::IsFlatSurface() const
{
	// Lakes and oceans have surfaces aligned with the XY plane
	return (GetWaterBodyType() == EWaterBodyType::Lake || GetWaterBodyType() == EWaterBodyType::Ocean);
}

bool UWaterBodyComponent::IsWaveSupported() const
{
	return (GetWaterBodyType() == EWaterBodyType::Lake || GetWaterBodyType() == EWaterBodyType::Ocean || GetWaterBodyType() == EWaterBodyType::Transition);
}

bool UWaterBodyComponent::HasWaves() const
{ 
	if (!IsWaveSupported())
	{
		return false;
	}
	return GetWaterWaves() ? (GetWaterWaves()->GetWaterWaves() != nullptr) : false;
}

FBox UWaterBodyComponent::GetCollisionComponentBounds() const
{
	FBox Box(ForceInit);
	for (UPrimitiveComponent* CollisionComponent : GetCollisionComponents())
	{
		if (CollisionComponent && CollisionComponent->IsRegistered())
		{
			Box += CollisionComponent->Bounds.GetBox();
		}
	}
	return Box;
}

AWaterBody* UWaterBodyComponent::GetWaterBodyActor() const
{
	// If we have an Owner, it must be an AWaterBody
	return GetOwner() ? CastChecked<AWaterBody>(GetOwner()) : nullptr;
}

UWaterSplineComponent* UWaterBodyComponent::GetWaterSpline() const
{
	if (const AWaterBody* OwningWaterBody = GetWaterBodyActor())
	{
		return OwningWaterBody->GetWaterSpline();
	}
	return nullptr;
}

bool UWaterBodyComponent::IsWaterSplineClosedLoop() const
{
	return (GetWaterBodyType() == EWaterBodyType::Lake) || (GetWaterBodyType() == EWaterBodyType::Ocean);
}

bool UWaterBodyComponent::IsHeightOffsetSupported() const
{
	return GetWaterBodyType() == EWaterBodyType::Ocean;
}

bool UWaterBodyComponent::AffectsLandscape() const
{
	return bAffectsLandscape && (GetWaterBodyType() != EWaterBodyType::Transition);
}

bool UWaterBodyComponent::AffectsWaterMesh() const
{ 
	return ShouldGenerateWaterMeshTile();
}

#if WITH_EDITOR
ETextureRenderTargetFormat UWaterBodyComponent::GetBrushRenderTargetFormat() const
{
	return (GetWaterBodyType() == EWaterBodyType::River) ? ETextureRenderTargetFormat::RTF_RGBA32f : ETextureRenderTargetFormat::RTF_RGBA16f;
}

void UWaterBodyComponent::GetBrushRenderDependencies(TSet<UObject*>& OutDependencies) const
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

void UWaterBodyComponent::SetWaterMaterial(UMaterialInterface* InMaterial)
{
	WaterMaterial = InMaterial;
	UpdateMaterialInstances();
}

UMaterialInstanceDynamic* UWaterBodyComponent::GetWaterMaterialInstance()
{
	CreateOrUpdateWaterMID(); 
	return WaterMID;
}

UMaterialInstanceDynamic* UWaterBodyComponent::GetUnderwaterPostProcessMaterialInstance()
{
	CreateOrUpdateUnderwaterPostProcessMID(); 
	return UnderwaterPostProcessMID;
}

void UWaterBodyComponent::SetUnderwaterPostProcessMaterial(UMaterialInterface* InMaterial)
{
	UnderwaterPostProcessMaterial = InMaterial;
	UpdateMaterialInstances();
}

bool UWaterBodyComponent::ShouldGenerateWaterMeshTile() const
{
	return ((GetWaterBodyType() != EWaterBodyType::Transition)
		&& (GetWaterMeshOverride() == nullptr)
		&& (GetWaterMaterial() != nullptr));
}

void UWaterBodyComponent::AddIsland(AWaterBodyIsland* Island)
{
	Islands.AddUnique(Island);
}

void UWaterBodyComponent::RemoveIsland(AWaterBodyIsland* Island)
{
	Islands.RemoveSwap(Island);
}

void UWaterBodyComponent::UpdateIslands()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Water_UpdateIslands);

	// For now, islands are not detected dynamically
#if WITH_EDITOR
	if (GetWorld())
	{
		for (AWaterBodyIsland* Island : TActorRange<AWaterBodyIsland>(GetWorld()))
		{
			Island->UpdateOverlappingWaterBodyComponents();
		}
	}
#endif // WITH_EDITOR
}

void UWaterBodyComponent::AddExclusionVolume(AWaterBodyExclusionVolume* InExclusionVolume)
{
	ExclusionVolumes.AddUnique(InExclusionVolume);
}

void UWaterBodyComponent::RemoveExclusionVolume(AWaterBodyExclusionVolume* InExclusionVolume)
{
	ExclusionVolumes.RemoveSwap(InExclusionVolume);
}

void UWaterBodyComponent::UpdateExclusionVolumes()
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

FPostProcessVolumeProperties UWaterBodyComponent::GetPostProcessProperties() const
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

void UWaterBodyComponent::PostDuplicate(bool bDuplicateForPie)
{
	Super::PostDuplicate(bDuplicateForPie);

#if WITH_EDITOR
	if (!bDuplicateForPie && GIsEditor)
	{
		// After duplication due to copy-pasting, UWaterSplineMetadata might have been edited without the spline component being made aware of that (for some reason, USplineComponent::PostDuplicate isn't called)::
		GetWaterSpline()->SynchronizeWaterProperties();

		OnWaterBodyChanged(/*bShapeOrPositionChanged*/true, /*bWeightmapSettingsChanged*/true);
	}

	RegisterOnUpdateWavesData(GetWaterWaves(), /* bRegister = */true);
#endif // WITH_EDITOR
}

float UWaterBodyComponent::FindInputKeyClosestToWorldLocation(const FVector& WorldLocation) const
{
	return GetWaterSpline()->FindInputKeyClosestToWorldLocation(WorldLocation);
}

float UWaterBodyComponent::GetConstantSurfaceZ() const
{
	const UWaterSplineComponent* const WaterSpline = GetWaterSpline();

	// A single Z doesn't really make sense for non-flat water bodies, but it can be useful for when using FixedZ post process for example. Take the first spline key in that case :
	float WaterSurfaceZ = (IsFlatSurface() || WaterSpline == nullptr) ? GetComponentLocation().Z : WaterSpline->GetLocationAtSplineInputKey(0.0f, ESplineCoordinateSpace::World).Z;
	
	// Apply body height offset if applicable (ocean)
	if (IsHeightOffsetSupported())
	{
		WaterSurfaceZ += GetHeightOffset();
	}

	return WaterSurfaceZ;
}

float UWaterBodyComponent::GetConstantDepth() const
{
	// Only makes sense when you consider the water depth to be constant for the whole water body, in which case we just use the first spline key's : 
	const UWaterSplineComponent* const WaterSpline = GetWaterSpline();
	return WaterSpline ? WaterSpline->GetFloatPropertyAtSplineInputKey(0.0f, GET_MEMBER_NAME_CHECKED(UWaterSplineMetadata, Depth)) : 0.0f;
}

FVector UWaterBodyComponent::GetConstantVelocity() const
{
	// Only makes sense when you consider the water velocity to be constant for the whole water body, in which case we just use the first spline key's : 
	return GetWaterVelocityVectorAtSplineInputKey(0.0f);
}

void UWaterBodyComponent::GetSurfaceMinMaxZ(float& OutMinZ, float& OutMaxZ) const
{
	const float SurfaceZ = GetConstantSurfaceZ();
	const float MaxWaveHeight = GetMaxWaveHeight();
	OutMaxZ = SurfaceZ + MaxWaveHeight;
	OutMinZ = SurfaceZ - MaxWaveHeight;
}

EWaterBodyQueryFlags UWaterBodyComponent::CheckAndAjustQueryFlags(EWaterBodyQueryFlags InQueryFlags) const
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

bool UWaterBodyComponent::IsWorldLocationInExclusionVolume(const FVector& InWorldLocation) const
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

FWaterBodyQueryResult UWaterBodyComponent::QueryWaterInfoClosestToWorldLocation(const FVector& InWorldLocation, EWaterBodyQueryFlags InQueryFlags, const TOptional<float>& InSplineInputKey) const
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
		SCOPE_CYCLE_COUNTER(STAT_WaterBody_ComputeLocation);
		FVector WaterPlaneLocation = InWorldLocation;
		// If in exclusion volume, force the water plane location at the query location. It is technically invalid, but it's up to the caller to check whether we're in an exclusion volume. 
		//  If the user fails to do so, at least it allows immersion depth to be 0.0f, which means the query location is NOT in water :
		if (!Result.IsInExclusionVolume())
		{
			WaterPlaneLocation.Z = bFlatSurface ? GetComponentLocation().Z : GetWaterSpline()->GetLocationAtSplineInputKey(Result.LazilyComputeSplineKey(*this, InWorldLocation), ESplineCoordinateSpace::World).Z;

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
		SCOPE_CYCLE_COUNTER(STAT_WaterBody_ComputeNormal);
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

			// If the height is invalid, we either have invalid landscape data or we're under the 
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

void UWaterBodyComponent::GetWaterSurfaceInfoAtLocation(const FVector& InLocation, FVector& OutWaterSurfaceLocation, FVector& OutWaterSurfaceNormal, FVector& OutWaterVelocity, float& OutWaterDepth, bool bIncludeDepth /* = false */) const
{
	EWaterBodyQueryFlags QueryFlags =
		EWaterBodyQueryFlags::ComputeLocation
		| EWaterBodyQueryFlags::ComputeNormal
		| EWaterBodyQueryFlags::ComputeVelocity;

	if (bIncludeDepth)
	{
		QueryFlags |= EWaterBodyQueryFlags::ComputeDepth;
	}

	FWaterBodyQueryResult QueryResult = QueryWaterInfoClosestToWorldLocation(InLocation, QueryFlags);
	OutWaterSurfaceLocation = QueryResult.GetWaterSurfaceLocation();
	OutWaterSurfaceNormal = QueryResult.GetWaterSurfaceNormal();
	OutWaterVelocity = QueryResult.GetVelocity();

	if (bIncludeDepth)
	{
		OutWaterDepth = QueryResult.GetWaterSurfaceDepth();
	}
}

float UWaterBodyComponent::GetWaterVelocityAtSplineInputKey(float InKey) const
{
	return WaterSplineMetadata ? WaterSplineMetadata->WaterVelocityScalar.Eval(InKey, 0.f) : 0.0f;
}

FVector UWaterBodyComponent::GetWaterVelocityVectorAtSplineInputKey(float InKey) const
{
	UWaterSplineComponent* WaterSpline = GetWaterSpline();
	const float WaterVelocityScalar = GetWaterVelocityAtSplineInputKey(InKey);
	const FVector SplineDirection = WaterSpline ? WaterSpline->GetDirectionAtSplineInputKey(InKey, ESplineCoordinateSpace::World) : FVector::ZeroVector;
	return SplineDirection * WaterVelocityScalar;
}

float UWaterBodyComponent::GetAudioIntensityAtSplineInputKey(float InKey) const
{
	return WaterSplineMetadata ? WaterSplineMetadata->AudioIntensity.Eval(InKey, 0.f) : 0.0f;
}

void UWaterBodyComponent::OnRegister()
{
	Super::OnRegister();

	AWaterBody* OwningWaterBodyActor = GetWaterBodyActor();
	WaterSplineMetadata = OwningWaterBodyActor->GetWaterSplineMetadata();

	check(WaterSplineMetadata);

#if WITH_EDITOR
	RegisterOnChangeWaterSplineMetadata(WaterSplineMetadata, /*bRegister = */true);
	GetWaterSpline()->OnSplineDataChanged().AddUObject(this, &UWaterBodyComponent::OnSplineDataChanged);
#endif // WITH_EDITOR
}

TArray<AWaterBodyIsland*> UWaterBodyComponent::GetIslands() const
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

TArray<AWaterBodyExclusionVolume*> UWaterBodyComponent::GetExclusionVolumes() const
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

// Our transient MIDs are per-object and shall not survive duplicating nor be exported to text when copy-pasting : 
EObjectFlags UWaterBodyComponent::GetTransientMIDFlags() const
{
	return RF_Transient | RF_NonPIEDuplicateTransient | RF_TextExportTransient;
}

void UWaterBodyComponent::UpdateMaterialInstances()
{
	CreateOrUpdateWaterMID();
	CreateOrUpdateUnderwaterPostProcessMID();
}

bool UWaterBodyComponent::UpdateWaterHeight()
{
	bool bWaterBodyChanged = false;
	const AActor* Owner = GetOwner();
	USplineComponent* WaterSpline = GetWaterSpline();
	if (IsFlatSurface() && WaterSpline && Owner)
	{
		const int32 NumSplinePoints = WaterSpline->GetNumberOfSplinePoints();

		const float ActorZ = Owner->GetActorLocation().Z;

		for (int32 PointIndex = 0; PointIndex < NumSplinePoints; ++PointIndex)
		{
			FVector WorldLoc = WaterSpline->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);

			if (WorldLoc.Z != ActorZ)
			{
				bWaterBodyChanged = true;
				WorldLoc.Z = ActorZ;
				WaterSpline->SetLocationAtSplinePoint(PointIndex, WorldLoc, ESplineCoordinateSpace::World);
			}
		}
	}

	return bWaterBodyChanged;
}

void UWaterBodyComponent::CreateOrUpdateWaterMID()
{
	// If GetWorld fails we may be in a blueprint
	if (GetWorld())
	{
		WaterMID = FWaterUtils::GetOrCreateTransientMID(WaterMID, TEXT("WaterMID"), WaterMaterial, GetTransientMIDFlags());

		SetDynamicParametersOnMID(WaterMID);
	}
}

void UWaterBodyComponent::CreateOrUpdateUnderwaterPostProcessMID()
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

void UWaterBodyComponent::PrepareCurrentPostProcessSettings()
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

ALandscapeProxy* UWaterBodyComponent::FindLandscape() const
{
	if (bAffectsLandscape && !Landscape.IsValid())
	{
		const FVector Location = GetComponentLocation();
		for (TObjectIterator<ALandscapeProxy> It; It; ++It)
		{
			if (It->GetWorld() == GetWorld())
			{
				FBox Box = It->GetComponentsBoundingBox();
				if (Box.IsInsideXY(Location))
				{
					Landscape = *It;
					return Landscape.Get();
				}
			}
		}
	}
	return Landscape.Get();
}

void UWaterBodyComponent::UpdateComponentVisibility(bool bAllowWaterMeshRebuild)
{
	if (UWorld* World = GetWorld())
	{
	 	const bool bIsWaterRenderingEnabled = FWaterUtils::IsWaterEnabled(/*bIsRenderThread = */false);
	 
		bool bIsRenderedByWaterMesh = ShouldGenerateWaterMeshTile();
		bool bLocalVisible = bIsWaterRenderingEnabled && !bIsRenderedByWaterMesh && GetVisibleFlag();
		bool bLocalHiddenInGame = !bIsWaterRenderingEnabled || bIsRenderedByWaterMesh || bHiddenInGame;

	 	for (UPrimitiveComponent* Component : GetStandardRenderableComponents())
	 	{
	 		Component->SetVisibility(bLocalVisible);
	 		Component->SetHiddenInGame(bLocalHiddenInGame);
	 	}

		// If the component is being or can be rendered by the water mesh, rebuild it in case its visibility has changed : 
		if (bAllowWaterMeshRebuild && (GetWaterBodyType() != EWaterBodyType::Transition))
		{
			if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld())) 
			{
				if (AWaterZone* WaterZone = WaterSubsystem->GetWaterZoneActor())
				{
					WaterZone->MarkWaterMeshComponentForRebuild();
				}
	 		}
	 	}
	}
}

#if WITH_EDITOR
void UWaterBodyComponent::PreEditUndo()
{
	Super::PreEditUndo();

	// On undo, when PreEditChange is called, PropertyAboutToChange is nullptr so we need to unregister from the previous object here :
	RegisterOnUpdateWavesData(GetWaterWaves(), /*bRegister = */false);
}

void UWaterBodyComponent::PostEditUndo()
{
	Super::PostEditUndo();

	OnWaterBodyChanged(/*bShapeOrPositionChanged*/true, /*bWeightmapSettingsChanged*/true);

	// On undo, when PostEditChangeProperty is called, PropertyChangedEvent is fake so we need to register to the new object here :
	RegisterOnUpdateWavesData(GetWaterWaves(), /*bRegister = */true);

	RequestGPUWaveDataUpdate();
}

void UWaterBodyComponent::PostEditImport()
{
	Super::PostEditImport();

	OnWaterBodyChanged(/*bShapeOrPositionChanged*/true, /*bWeightmapSettingsChanged*/true);

	RequestGPUWaveDataUpdate();
}

void UWaterBodyComponent::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaterBodyComponent, LayerWeightmapSettings))
	{
		bWeightmapSettingsChanged = true;
	}
	else if ((PropertyName == GET_MEMBER_NAME_CHECKED(UWaterBodyComponent, WaterMaterial)) ||
			(PropertyName == GET_MEMBER_NAME_CHECKED(UWaterBodyComponent, UnderwaterPostProcessMaterial)))
	{
		UpdateMaterialInstances();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaterBodyComponent, TargetWaveMaskDepth))
	{
		RequestGPUWaveDataUpdate();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaterBodyComponent, MaxWaveHeightOffset))
	{
		bShapeOrPositionChanged = true;
	}
	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == FName(TEXT("RelativeScale3D")) &&
		PropertyName == FName(TEXT("Z")))
	{
		// Prevent scaling on the Z axis
		FVector Scale = GetRelativeScale3D();
		Scale.Z = 1.f;
		SetRelativeScale3D(Scale);
	}
}

TArray<TSharedRef<FTokenizedMessage>> UWaterBodyComponent::CheckWaterBodyStatus() const
{
	TArray<TSharedRef<FTokenizedMessage>> Result;
	if (!IsTemplate())
	{
		if (const UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
		{
			if (AffectsWaterMesh() && (WaterSubsystem->GetWaterZoneActor() == nullptr))
			{
				Result.Add(FTokenizedMessage::Create(EMessageSeverity::Error)
					->AddToken(FUObjectToken::Create(this))
					->AddToken(FTextToken::Create(FText::Format(
						LOCTEXT("MapCheck_Message_MissingWaterZone", "Water body {0} requires a WaterZone actor to be rendered. Please add one to the map. "),
						FText::FromString(GetWaterBodyActor()->GetActorLabel())))));
			}
		}

		if (AffectsLandscape() && (FindLandscape() == nullptr))
		{
			Result.Add(FTokenizedMessage::Create(EMessageSeverity::Error)
				->AddToken(FUObjectToken::Create(this))
				->AddToken(FTextToken::Create(FText::Format(
					LOCTEXT("MapCheck_Message_MissingLandscape", "Water body {0} requires a Landscape to be rendered. Please add one to the map. "),
					FText::FromString(GetWaterBodyActor()->GetActorLabel())))));
		}
	}
	return Result;
}

void UWaterBodyComponent::CheckForErrors()
{
	Super::CheckForErrors();

	TArray<TSharedRef<FTokenizedMessage>> StatusMessages = CheckWaterBodyStatus();
	for (const TSharedRef<FTokenizedMessage>& StatusMessage : StatusMessages)
	{
		FMessageLog("MapCheck").AddMessage(StatusMessage);
	}
}

void UWaterBodyComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	bool bShapeOrPositionChanged = false;
	bool bWeightmapSettingsChanged = false;

	OnPostEditChangeProperty(PropertyChangedEvent, bShapeOrPositionChanged, bWeightmapSettingsChanged);
	
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!IsTemplate())
	{
		OnWaterBodyChanged(bShapeOrPositionChanged, bWeightmapSettingsChanged);
	}
}

void UWaterBodyComponent::OnSplineDataChanged()
{
	OnWaterBodyChanged(/*bShapeOrPositionChanged*/true);
}

void UWaterBodyComponent::RegisterOnUpdateWavesData(UWaterWavesBase* InWaterWaves, bool bRegister)
{
	if (InWaterWaves != nullptr)
	{
		if (bRegister)
		{
			InWaterWaves->OnUpdateWavesData.AddUObject(this, &UWaterBodyComponent::OnWavesDataUpdated);
		}
		else
		{
			InWaterWaves->OnUpdateWavesData.RemoveAll(this);
		}
	}
}

void UWaterBodyComponent::OnWavesDataUpdated(UWaterWavesBase* InWaterWaves, EPropertyChangeType::Type InChangeType)
{
	RequestGPUWaveDataUpdate();

	// Waves data affect the navigation : 
	OnWaterBodyChanged(/*bShapeOrPositionChanged = */true);
}

void UWaterBodyComponent::OnWaterSplineMetadataChanged(UWaterSplineMetadata* InWaterSplineMetadata, FPropertyChangedEvent& PropertyChangedEvent)
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
		GetWaterSpline()->SynchronizeWaterProperties();
	}

	// Waves data affect the navigation : 
	OnWaterBodyChanged(bShapeOrPositionChanged);
}

void UWaterBodyComponent::RegisterOnChangeWaterSplineMetadata(UWaterSplineMetadata* InWaterSplineMetadata, bool bRegister)
{
	if (InWaterSplineMetadata != nullptr)
	{
		if (bRegister)
		{
			InWaterSplineMetadata->OnChangeData.AddUObject(this, &UWaterBodyComponent::OnWaterSplineMetadataChanged);
		}
		else
		{
			InWaterSplineMetadata->OnChangeData.RemoveAll(this);
		}
	}
}

#endif // WITH_EDITOR

void UWaterBodyComponent::GetNavigationData(struct FNavigationRelevantData& Data) const
{
	if (CanAffectNavigation())
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
			// skip recursion on this component
			if (PrimComp != this)
			{
				PrimComp->GetNavigationData(Data);
			}
		}
	}
}

FBox UWaterBodyComponent::GetNavigationBounds() const
{
	return GetCollisionComponentBounds();
}

bool UWaterBodyComponent::IsNavigationRelevant() const
{
	return CanAffectNavigation() && (GetCollisionComponents().Num() > 0);
}

void UWaterBodyComponent::ApplyNavigationSettings()
{
	AActor* Owner = GetOwner();
	if (Owner)
	{
		const bool bCanAffectNav = CanAffectNavigation();

		// Make sure the engine's bCanEverAffectionNavigation matches the component's one.
		SetCanEverAffectNavigation(bCanAffectNav);

		// @todo_water: change this
		TInlineComponentArray<UActorComponent*> Components;
		Owner->GetComponents(Components);

		const TArray<UPrimitiveComponent*> LocalCollisionComponents = GetCollisionComponents();
		for (UActorComponent* ActorComp : Components)
		{
			UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ActorComp);
			if (PrimComp == this)
			{
				continue;
			}
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

void UWaterBodyComponent::RequestGPUWaveDataUpdate()
{
	if (FWaterBodyManager* Manager = UWaterSubsystem::GetWaterBodyManager(GetWorld()))
	{
		Manager->RequestWaveDataRebuild();
	}
}

void UWaterBodyComponent::BeginUpdateWaterBody()
{
	UpdateSplineComponent();
}

void UWaterBodyComponent::UpdateWaterBody(bool bWithExclusionVolumes)
{
	// The first update is without exclusion volumes : perform it.
	// The second update is with exclusion volumes but there's no need to perform it again if we don't have exclusion volumes anyway, because the result will be the same.
	if (!bWithExclusionVolumes || GetExclusionVolumes().Num() > 0)
	{
		OnUpdateBody(bWithExclusionVolumes);
	}
}

void UWaterBodyComponent::UpdateAll(bool bShapeOrPositionChanged)
{
	BeginUpdateWaterBody();

	AWaterBody* WaterBodyOwner = GetWaterBodyActor();
	check(WaterBodyOwner);
	
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
			FNavigationSystem::UpdateActorAndComponentData(*WaterBodyOwner);
		}

		UpdateComponentVisibility(/* bAllowWaterMeshRebuild = */true);

#if WITH_EDITOR
		WaterBodyOwner->UpdateActorIcon();
#endif
	}
}

void UWaterBodyComponent::UpdateSplineComponent()
{
	if (USplineComponent* WaterSpline = GetWaterSpline())
	{
		WaterSpline->SetClosedLoop(IsWaterSplineClosedLoop());
	}
}

void UWaterBodyComponent::OnWaterBodyChanged(bool bShapeOrPositionChanged, bool bWeightmapSettingsChanged)
{
	// It's possible to get called without a water spline after the Redo of a water body deletion (i.e. the water body actor gets deleted again, hence its SplineComp is restored to nullptr)
	//  This is a very-edgy case that needs to be checked everywhere that UpdateAll might hook into so it's simpler to just skip it all. The actor is in limbo by then anyway (it only survives because
	//  of the editor transaction) :
	if (GetWaterSpline())
	{
		UpdateAll(bShapeOrPositionChanged);

		// Some of the spline parameters need to be transferred to the underwater post process MID, if any : 
		if (bShapeOrPositionChanged)
		{
			SetDynamicParametersOnUnderwaterPostProcessMID(UnderwaterPostProcessMID);
		}
	}

#if WITH_EDITOR
	AWaterBody* const WaterBodyActor = GetWaterBodyActor();
	IWaterBrushActorInterface::FWaterBrushActorChangedEventParams Params(WaterBodyActor);
	Params.bShapeOrPositionChanged = bShapeOrPositionChanged;
	Params.bWeightmapSettingsChanged = bWeightmapSettingsChanged;
	WaterBodyActor->BroadcastWaterBrushActorChangedEvent(Params);
#endif
}

void UWaterBodyComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FWaterCustomVersion::GUID);
}

void UWaterBodyComponent::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	// WaterMeshOverride is now enough to override the water mesh (bOverrideWaterMesh_DEPRECATED was superfluous), so make sure to discard WaterMeshOverride (except on custom water bodies) when the boolean wasn't set :
	if (!bOverrideWaterMesh_DEPRECATED && (WaterMeshOverride != nullptr) && (GetWaterBodyType() != EWaterBodyType::Transition))
	{
		WaterMeshOverride = nullptr;
	}
#endif

#if WITH_EDITOR
	RegisterOnUpdateWavesData(GetWaterWaves(), /* bRegister = */true);
#endif
}

void UWaterBodyComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
#if WITH_EDITOR
	RegisterOnChangeWaterSplineMetadata(WaterSplineMetadata, /*bRegister = */false);
	RegisterOnUpdateWavesData(GetWaterWaves(), /*bRegister = */false);
#endif // WITH_EDITOR

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UWaterBodyComponent::MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* Hit, EMoveComponentFlags MoveFlags, ETeleportType Teleport)
{
	// Prevent Z-Scale
	FVector Scale = GetRelativeScale3D();
	Scale.Z = 1.f;
	SetRelativeScale3D(Scale);

	// Restrict rotation to the Z-axis only
	FQuat CorrectedRotation = NewRotation;
	CorrectedRotation.X = 0.f;
	CorrectedRotation.Y = 0.f;

	return Super::MoveComponentImpl(Delta, CorrectedRotation, bSweep, Hit, MoveFlags, Teleport);
}

bool UWaterBodyComponent::SetDynamicParametersOnMID(UMaterialInstanceDynamic* InMID)
{
	UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld());
	if ((InMID == nullptr) || (WaterSubsystem == nullptr))
	{
		return false;
	}

	const float GlobalOceanHeight = WaterSubsystem->GetOceanTotalHeight();
	InMID->SetScalarParameterValue(WaterBodyIndexParamName, WaterBodyIndex);
	InMID->SetScalarParameterValue(GlobalOceanHeightName, GlobalOceanHeight);
	InMID->SetScalarParameterValue(FixedZHeightName, GetConstantSurfaceZ());
	InMID->SetScalarParameterValue(FixedWaterDepthName, GetConstantDepth());
	InMID->SetVectorParameterValue(FixedVelocityName, GetConstantVelocity());

	if (AWaterZone* WaterZone = WaterSubsystem->GetWaterZoneActor())
	{
		InMID->SetTextureParameterValue(WaterVelocityAndHeightName, WaterZone->WaterVelocityTexture);

		UWaterMeshComponent* WaterMeshComponent = WaterZone->GetWaterMeshComponent();
		check(WaterMeshComponent);

		// LWC_TODO: precision loss
		FLinearColor WaterArea = FLinearColor(WaterMeshComponent->RTWorldLocation);
		WaterArea.B = WaterMeshComponent->RTWorldSizeVector.X;
		WaterArea.A = WaterMeshComponent->RTWorldSizeVector.Y;
		InMID->SetVectorParameterValue(WaterAreaParamName, WaterArea);
	}

	return true;
}

bool UWaterBodyComponent::SetDynamicParametersOnUnderwaterPostProcessMID(UMaterialInstanceDynamic* InMID)
{
	UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld());
	if ((InMID == nullptr) || (WaterSubsystem == nullptr))
	{
		return false;
	}

	// The post process MID needs the same base parameters as the water materials : 
	SetDynamicParametersOnMID(InMID);

	// Add here the list of parameters that the underwater material needs (for not nothing more than the standard material) :

	return true;
}

float UWaterBodyComponent::GetWaveReferenceTime() const
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
bool UWaterBodyComponent::GetWaveInfoAtPosition(const FVector& InPosition, float InWaterDepth, bool bInSimpleWaves, FWaveInfo& InOutWaveInfo) const
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

float UWaterBodyComponent::GetMaxWaveHeight() const
{
	return (HasWaves() ? GetWaterWaves()->GetMaxWaveHeight() : 0.0f) + MaxWaveHeightOffset;
}

float UWaterBodyComponent::GetWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime, FVector& OutNormal) const
{
	check(HasWaves());

	return GetWaterWaves()->GetWaveHeightAtPosition(InPosition, InWaterDepth, InTime, OutNormal);
}

float UWaterBodyComponent::GetSimpleWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime) const
{
	check(HasWaves());

	return GetWaterWaves()->GetSimpleWaveHeightAtPosition(InPosition, InWaterDepth, InTime);
}

float UWaterBodyComponent::GetWaveAttenuationFactor(const FVector& InPosition, float InWaterDepth) const
{
	check(HasWaves());

	return GetWaterWaves()->GetWaveAttenuationFactor(InPosition, InWaterDepth, TargetWaveMaskDepth);
}

UWaterWavesBase* UWaterBodyComponent::GetWaterWaves() const
{
	if (AWaterBody* OwningWaterBody = GetWaterBodyActor())
	{
		return OwningWaterBody->GetWaterWaves();
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
