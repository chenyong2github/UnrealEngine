// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/EngineTypes.h"
#include "UObject/UnrealType.h"
#include "HAL/IConsoleManager.h"
#include "Engine/EngineBaseTypes.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"
#include "GameFramework/LightWeightInstanceManager.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/MeshMerging.h"
#include "Engine/CollisionProfile.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

FAttachmentTransformRules FAttachmentTransformRules::KeepRelativeTransform(EAttachmentRule::KeepRelative, false);
FAttachmentTransformRules FAttachmentTransformRules::KeepWorldTransform(EAttachmentRule::KeepWorld, false);
FAttachmentTransformRules FAttachmentTransformRules::SnapToTargetNotIncludingScale(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
FAttachmentTransformRules FAttachmentTransformRules::SnapToTargetIncludingScale(EAttachmentRule::SnapToTarget, false);

FDetachmentTransformRules FDetachmentTransformRules::KeepRelativeTransform(EDetachmentRule::KeepRelative, true);
FDetachmentTransformRules FDetachmentTransformRules::KeepWorldTransform(EDetachmentRule::KeepWorld, true);

/** If true, origin rebasing is enabled in multiplayer games, meaning that servers and clients can have different local world origins. */
int32 FRepMovement::EnableMultiplayerWorldOriginRebasing = 0;

/** Console variable ref to enable multiplayer world origin rebasing. */
FAutoConsoleVariableRef CVarEnableMultiplayerWorldOriginRebasing(
	TEXT("p.EnableMultiplayerWorldOriginRebasing"),
	FRepMovement::EnableMultiplayerWorldOriginRebasing,
	TEXT("Enable world origin rebasing for multiplayer, meaning that servers and clients can have different world origin locations."),
	ECVF_ReadOnly);

#if WITH_EDITORONLY_DATA
void FMeshProxySettings::PostLoadDeprecated()
{
	MaterialSettings.MaterialMergeType = EMaterialMergeType::MaterialMergeType_Simplygon;
}

void FMeshMergingSettings::PostLoadDeprecated()
{
	FMeshMergingSettings DefaultObject;
	if (bImportVertexColors_DEPRECATED != DefaultObject.bImportVertexColors_DEPRECATED)
	{
		bBakeVertexDataToMesh = bImportVertexColors_DEPRECATED;
	}

	if (bExportNormalMap_DEPRECATED != DefaultObject.bExportNormalMap_DEPRECATED)
	{
		MaterialSettings.bNormalMap = bExportNormalMap_DEPRECATED;
	}

	if (bExportMetallicMap_DEPRECATED != DefaultObject.bExportMetallicMap_DEPRECATED)
	{
		MaterialSettings.bMetallicMap = bExportMetallicMap_DEPRECATED;
	}
	if (bExportRoughnessMap_DEPRECATED != DefaultObject.bExportRoughnessMap_DEPRECATED)
	{
		MaterialSettings.bRoughnessMap = bExportRoughnessMap_DEPRECATED;
	}
	if (bExportSpecularMap_DEPRECATED != DefaultObject.bExportSpecularMap_DEPRECATED)
	{
		MaterialSettings.bSpecularMap = bExportSpecularMap_DEPRECATED;
	}
	if (MergedMaterialAtlasResolution_DEPRECATED != DefaultObject.MergedMaterialAtlasResolution_DEPRECATED)
	{
		MaterialSettings.TextureSize.X = MergedMaterialAtlasResolution_DEPRECATED;
		MaterialSettings.TextureSize.Y = MergedMaterialAtlasResolution_DEPRECATED;
	}
	if (bCalculateCorrectLODModel_DEPRECATED != DefaultObject.bCalculateCorrectLODModel_DEPRECATED)
	{
		LODSelectionType = EMeshLODSelectionType::CalculateLOD;
	}

	if (ExportSpecificLOD_DEPRECATED != DefaultObject.ExportSpecificLOD_DEPRECATED)
	{
		SpecificLOD = ExportSpecificLOD_DEPRECATED;
		LODSelectionType = EMeshLODSelectionType::SpecificLOD;
	}
}
#endif

UEngineBaseTypes::UEngineBaseTypes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UEngineTypes::UEngineTypes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

ECollisionChannel UEngineTypes::ConvertToCollisionChannel(ETraceTypeQuery TraceType)
{
	return UCollisionProfile::Get()->ConvertToCollisionChannel(true, (int32)TraceType);
}

ECollisionChannel UEngineTypes::ConvertToCollisionChannel(EObjectTypeQuery ObjectType)
{
	return UCollisionProfile::Get()->ConvertToCollisionChannel(false, (int32)ObjectType);
}

EObjectTypeQuery UEngineTypes::ConvertToObjectType(ECollisionChannel CollisionChannel)
{
	return UCollisionProfile::Get()->ConvertToObjectType(CollisionChannel);
}

ETraceTypeQuery UEngineTypes::ConvertToTraceType(ECollisionChannel CollisionChannel)
{
	return UCollisionProfile::Get()->ConvertToTraceType(CollisionChannel);
}

void FDamageEvent::GetBestHitInfo(AActor const* HitActor, AActor const* HitInstigator, FHitResult& OutHitInfo, FVector& OutImpulseDir) const
{
	ensure(HitActor);
	if (HitActor)
	{
		// fill out the hitinfo as best we can
		OutHitInfo.HitObjectHandle = FActorInstanceHandle(const_cast<AActor*>(HitActor));
		OutHitInfo.bBlockingHit = true;
		OutHitInfo.BoneName = NAME_None;
		OutHitInfo.Component = Cast<UPrimitiveComponent>(HitActor->GetRootComponent());
		
		// assume the actor got hit in the center of his root component
		OutHitInfo.ImpactPoint = HitActor->GetActorLocation();
		OutHitInfo.Location = OutHitInfo.ImpactPoint;
		
		// assume hit came from instigator's location
		OutImpulseDir = HitInstigator ? 
			( OutHitInfo.ImpactPoint - HitInstigator->GetActorLocation() ).GetSafeNormal()
			: FVector::ZeroVector;

		// assume normal points back toward instigator
		OutHitInfo.ImpactNormal = -OutImpulseDir;
		OutHitInfo.Normal = OutHitInfo.ImpactNormal;
	}
}

void FPointDamageEvent::GetBestHitInfo(AActor const* HitActor, AActor const* HitInstigator, FHitResult& OutHitInfo, FVector& OutImpulseDir) const
{
	// assume the actor got hit in the center of his root component
	OutHitInfo = HitInfo;
	OutImpulseDir = ShotDirection;
}


void FRadialDamageEvent::GetBestHitInfo(AActor const* HitActor, AActor const* HitInstigator, FHitResult& OutHitInfo, FVector& OutImpulseDir) const
{
	ensure(ComponentHits.Num() > 0);

	// for now, just return the first one
	OutHitInfo = ComponentHits[0];
	OutImpulseDir = (OutHitInfo.ImpactPoint - Origin).GetSafeNormal();
}


float FRadialDamageParams::GetDamageScale(float DistanceFromEpicenter) const
{
	float const ValidatedInnerRadius = FMath::Max(0.f, InnerRadius);
	float const ValidatedOuterRadius = FMath::Max(OuterRadius, ValidatedInnerRadius);
	float const ValidatedDist = FMath::Max(0.f, DistanceFromEpicenter);

	if (ValidatedDist >= ValidatedOuterRadius)
	{
		// outside the radius, no effect
		return 0.f;
	}

	if ( (DamageFalloff == 0.f)	|| (ValidatedDist <= ValidatedInnerRadius) )
	{
		// no falloff or inside inner radius means full effect
		return 1.f;
	}

	// calculate the interpolated scale
	float DamageScale = 1.f - ( (ValidatedDist - ValidatedInnerRadius) / (ValidatedOuterRadius - ValidatedInnerRadius) );
	DamageScale = FMath::Pow(DamageScale, DamageFalloff);

	return DamageScale;
}

FLightmassDebugOptions::FLightmassDebugOptions()
	: bDebugMode(false)
	, bStatsEnabled(false)
	, bGatherBSPSurfacesAcrossComponents(true)
	, CoplanarTolerance(0.001f)
	, bUseImmediateImport(true)
	, bImmediateProcessMappings(true)
	, bSortMappings(true)
	, bDumpBinaryFiles(false)
	, bDebugMaterials(false)
	, bPadMappings(true)
	, bDebugPaddings(false)
	, bOnlyCalcDebugTexelMappings(false)
	, bUseRandomColors(false)
	, bColorBordersGreen(false)
	, bColorByExecutionTime(false)
	, ExecutionTimeDivisor(15.0f)
{}

UActorComponent* FComponentReference::GetComponent(AActor* OwningActor) const
{
	UActorComponent* Result = nullptr;

	// Component is specified directly, use that
	if(OverrideComponent.IsValid())
	{
		Result = OverrideComponent.Get();
	}
	else
	{
		// Look in Actor if specified, OwningActor if not
		AActor* SearchActor = (OtherActor != NULL) ? ToRawPtr(OtherActor) : OwningActor;
		if(SearchActor)
		{
			if(ComponentProperty != NAME_None)
			{
				FObjectPropertyBase* ObjProp = FindFProperty<FObjectPropertyBase>(SearchActor->GetClass(), ComponentProperty);
				if(ObjProp != NULL)
				{
					// .. and return the component that is there
					Result = Cast<UActorComponent>(ObjProp->GetObjectPropertyValue_InContainer(SearchActor));
				}
			}
			else if (!PathToComponent.IsEmpty())
			{
				Result = FindObject<UActorComponent>(SearchActor, *PathToComponent);
			}
			else
			{
				Result = SearchActor->GetRootComponent();
			}
		}
	}

	return Result;
}

FActorInstanceHandle::FActorInstanceHandle()
	: Actor(nullptr)
	, ManagerIndex(INDEX_NONE)
	, InstanceIndex(INDEX_NONE)
{
	// do nothing
}

FActorInstanceHandle::FActorInstanceHandle(AActor* InActor)
	: Actor(InActor)
	, ManagerIndex(INDEX_NONE)
	, InstanceIndex(INDEX_NONE)
{
	if (InActor)
	{
		if (ALightWeightInstanceManager* LWIManager = FLightWeightInstanceSubsystem::Get().FindLightWeightInstanceManager(InActor->StaticClass(), InActor->GetLevel()))
		{
			InstanceIndex = LWIManager->FindIndexForActor(InActor);
			if (InstanceIndex != INDEX_NONE)
			{
				ManagerIndex = FLightWeightInstanceSubsystem::Get().GetManagerIndex(LWIManager);
			}
		}
	}
}

FActorInstanceHandle::FActorInstanceHandle(int32 InManagerIndex, int32 InInstanceIndex)
	: Actor(nullptr)
	, ManagerIndex(InManagerIndex)
{
	const ALightWeightInstanceManager* Manager = FLightWeightInstanceSubsystem::Get().GetManagerAt(InManagerIndex);

	if (ensure(Manager))
	{
		InstanceIndex = Manager->ConvertCollisionIndexToLightWeightIndex(InInstanceIndex);

		if (AActor*const* FoundActor = Manager->Actors.Find(InInstanceIndex))
		{
			Actor = *FoundActor;
		}
	}
	else
	{
		ManagerIndex = INDEX_NONE;
		InstanceIndex = INDEX_NONE;
	}
}

FActorInstanceHandle::FActorInstanceHandle(ALightWeightInstanceManager* Manager, int32 InInstanceIndex)
	: Actor(nullptr)
	, ManagerIndex(INDEX_NONE)
	, InstanceIndex(InInstanceIndex)
{
	if (ensure(Manager))
	{
		InstanceIndex = Manager->ConvertCollisionIndexToLightWeightIndex(InInstanceIndex);
		if (AActor*const* FoundActor = Manager->Actors.Find(InstanceIndex))
		{
			Actor = *FoundActor;
		}

		ManagerIndex = FLightWeightInstanceSubsystem::Get().GetManagerIndex(Manager);
		ensure(ManagerIndex != INDEX_NONE);
	}
}

FActorInstanceHandle::FActorInstanceHandle(const FActorInstanceHandle& Other)
{
	Actor = Other.Actor;
	ManagerIndex = Other.ManagerIndex;
	InstanceIndex = Other.InstanceIndex;
}

bool FActorInstanceHandle::IsValid() const
{
	return (ManagerIndex != INDEX_NONE && InstanceIndex != INDEX_NONE) || Actor.IsValid();
}

bool FActorInstanceHandle::DoesRepresentClass(const UClass* OtherClass) const
{
	if (OtherClass == nullptr)
	{
		return false;
	}

	if (Actor.IsValid())
	{
		return Actor->IsA(OtherClass);
	}

	if (ALightWeightInstanceManager* Manager = FLightWeightInstanceSubsystem::Get().FindLightWeightInstanceManager(*this))
	{
		return Manager->DoesRepresentClass(OtherClass);
	}

	return false;
}

UClass* FActorInstanceHandle::GetRepresentedClass() const
{
	if (!IsValid())
	{
		return nullptr;
	}

	if (Actor.IsValid())
	{
		return Actor->GetClass();
	}

	if (ALightWeightInstanceManager* Manager = FLightWeightInstanceSubsystem::Get().FindLightWeightInstanceManager(*this))
	{
		return Manager->GetRepresentedClass();
	}

	return nullptr;
}

FVector FActorInstanceHandle::GetLocation() const
{
	return FVector();
}

FRotator FActorInstanceHandle::GetRotation() const
{
	return FRotator();
}

ULevel* FActorInstanceHandle::GetLevel() const
{
	return nullptr;
}

bool FActorInstanceHandle::IsInLevel(ULevel* Level) const
{
	return false;
}

FName FActorInstanceHandle::GetFName() const
{
	return NAME_None;
}

FString FActorInstanceHandle::GetName() const
{
	return FString();
}

AActor* FActorInstanceHandle::FetchActor() const
{
	if (Actor.IsValid())
	{
		return Actor.Get();
	}

	return FLightWeightInstanceSubsystem::Get().GetActor(*this);
}

bool FActorInstanceHandle::operator==(const FActorInstanceHandle& Other) const
{
	return ManagerIndex == Other.ManagerIndex && InstanceIndex == Other.InstanceIndex;
}

bool FActorInstanceHandle::operator!=(const FActorInstanceHandle& Other) const
{
	return !(*this == Other);
}

bool FActorInstanceHandle::operator==(const AActor* OtherActor) const
{
	// if we have an actor, compare the two actors
	if (Actor.IsValid())
	{
		return Actor.Get() == OtherActor;
	}

	// we don't have an actor so see if we can look up an instance associated with OtherActor and see if we refer to the same instance
	if (ALightWeightInstanceManager* Manager = FLightWeightInstanceSubsystem::Get().FindLightWeightInstanceManager(OtherActor->StaticClass(), OtherActor->GetLevel()))
	{
		if (FLightWeightInstanceSubsystem::Get().GetManagerIndex(Manager) != ManagerIndex)
		{
			return false;
		}

		return Manager->FindIndexForActor(OtherActor) == InstanceIndex;
	}
	
	return false;
}

bool FActorInstanceHandle::operator!=(const AActor* OtherActor) const
{
	return !(*this == OtherActor);
}

FArchive& operator<<(FArchive& Ar, FActorInstanceHandle& Handle)
{
	Ar << Handle.Actor;
	Ar << Handle.ManagerIndex;
	Ar << Handle.InstanceIndex;

	return Ar;
}

FString FHitResult::ToString() const
{
	return FString::Printf(TEXT("bBlockingHit:%s bStartPenetrating:%s Time:%f Location:%s ImpactPoint:%s Normal:%s ImpactNormal:%s TraceStart:%s TraceEnd:%s PenetrationDepth:%f Item:%d PhysMaterial:%s Actor:%s Component:%s BoneName:%s FaceIndex:%d"),
		bBlockingHit == true ? TEXT("True") : TEXT("False"),
		bStartPenetrating == true ? TEXT("True") : TEXT("False"),
		Time,
		*Location.ToString(),
		*ImpactPoint.ToString(),
		*Normal.ToString(),
		*ImpactNormal.ToString(),
		*TraceStart.ToString(),
		*TraceEnd.ToString(),
		PenetrationDepth,
		Item,
		PhysMaterial.IsValid() ? *PhysMaterial->GetName() : TEXT("None"),
		*FLightWeightInstanceSubsystem::Get().GetName(HitObjectHandle),
		Component.IsValid() ? *Component->GetName() : TEXT("None"),
		BoneName.IsValid() ? *BoneName.ToString() : TEXT("None"),
		FaceIndex);
}

FRepMovement::FRepMovement()
	: LinearVelocity(ForceInit)
	, AngularVelocity(ForceInit)
	, Location(ForceInit)
	, Rotation(ForceInit)
	, bSimulatedPhysicSleep(false)
	, bRepPhysics(false)
	, LocationQuantizationLevel(EVectorQuantization::RoundWholeNumber)
	, VelocityQuantizationLevel(EVectorQuantization::RoundWholeNumber)
	, RotationQuantizationLevel(ERotatorQuantization::ByteComponents)
{
}

/** Rebase zero-origin position onto local world origin value. */
FVector FRepMovement::RebaseOntoLocalOrigin(const struct FVector& Location, const struct FIntVector& LocalOrigin)
{
	if (EnableMultiplayerWorldOriginRebasing <= 0 || LocalOrigin == FIntVector::ZeroValue)
	{
		return Location;
	}

	return FVector(Location.X - LocalOrigin.X, Location.Y - LocalOrigin.Y, Location.Z - LocalOrigin.Z);
}

/** Rebase local-origin position onto zero world origin value. */
FVector FRepMovement::RebaseOntoZeroOrigin(const struct FVector& Location, const struct FIntVector& LocalOrigin)
{
	if (EnableMultiplayerWorldOriginRebasing <= 0 || LocalOrigin == FIntVector::ZeroValue)
	{
		return Location;
	}

	return FVector(Location.X + LocalOrigin.X, Location.Y + LocalOrigin.Y, Location.Z + LocalOrigin.Z);
}

/** Rebase zero-origin position onto local world origin value based on an actor's world. */
FVector FRepMovement::RebaseOntoLocalOrigin(const struct FVector& Location, const AActor* const WorldContextActor)
{
	if (WorldContextActor == nullptr || EnableMultiplayerWorldOriginRebasing <= 0)
	{
		return Location;
	}

	return RebaseOntoLocalOrigin(Location, WorldContextActor->GetWorld()->OriginLocation);
}

/** Rebase local-origin position onto zero world origin value based on an actor's world.*/
FVector FRepMovement::RebaseOntoZeroOrigin(const struct FVector& Location, const AActor* const WorldContextActor)
{
	if (WorldContextActor == nullptr || EnableMultiplayerWorldOriginRebasing <= 0)
	{
		return Location;
	}

	return RebaseOntoZeroOrigin(Location, WorldContextActor->GetWorld()->OriginLocation);
}

/// @cond DOXYGEN_WARNINGS

/** Rebase zero-origin position onto local world origin value based on an actor component's world. */
FVector FRepMovement::RebaseOntoLocalOrigin(const struct FVector& Location, const UActorComponent* const WorldContextActorComponent)
{
	if (WorldContextActorComponent == nullptr || EnableMultiplayerWorldOriginRebasing <= 0)
	{
		return Location;
	}

	return RebaseOntoLocalOrigin(Location, WorldContextActorComponent->GetWorld()->OriginLocation);
}

/** Rebase local-origin position onto zero world origin value based on an actor component's world.*/
FVector FRepMovement::RebaseOntoZeroOrigin(const struct FVector& Location, const UActorComponent* const WorldContextActorComponent)
{
	if (WorldContextActorComponent == nullptr || EnableMultiplayerWorldOriginRebasing <= 0)
	{
		return Location;
	}

	return RebaseOntoZeroOrigin(Location, WorldContextActorComponent->GetWorld()->OriginLocation);
}

/// @endcond
