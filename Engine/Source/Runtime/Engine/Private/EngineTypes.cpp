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
	, InstanceIndex(INDEX_NONE)
	, InstanceUID(0)
{
	// do nothing
}

FActorInstanceHandle::FActorInstanceHandle(AActor* InActor)
	: Actor(InActor)
	, InstanceIndex(INDEX_NONE)
{
	if (InActor)
	{
#if WITH_EDITOR
		// use the first layer the actor is in if it's in multiple layers
		TArray<const UDataLayer*> DataLayers = InActor->GetDataLayerObjects();
		const UDataLayer* Layer = DataLayers.Num() > 0 ? DataLayers[0] : nullptr;
#else
		const UDataLayer* Layer = nullptr;
#endif // WITH_EDITOR
		Manager = FLightWeightInstanceSubsystem::Get().FindLightWeightInstanceManager(InActor->StaticClass(), Layer);
	}
}

FActorInstanceHandle::FActorInstanceHandle(ALightWeightInstanceManager* InManager, int32 InInstanceIndex)
	: Actor(nullptr)
	, InstanceIndex(InInstanceIndex)
	, InstanceUID(0)
{
	Manager = InManager;
	if (Manager.IsValid())
	{
		InstanceIndex = Manager->ConvertCollisionIndexToLightWeightIndex(InInstanceIndex);
		if (AActor* const* FoundActor = Manager->Actors.Find(InstanceIndex))
		{
			Actor = *FoundActor;
		}

		UWorld* World = Manager->GetWorld();
		if (ensure(World))
		{
			InstanceUID = World->LWILastAssignedUID++;
		}
	}
}

FActorInstanceHandle::FActorInstanceHandle(const FActorInstanceHandle& Other)
{
	Actor = Other.Actor;

	Manager = Other.Manager;
	InstanceIndex = Other.InstanceIndex;
	InstanceUID = Other.InstanceUID;
}

bool FActorInstanceHandle::IsValid() const
{
	return (Manager.IsValid() && InstanceIndex != INDEX_NONE) || IsActorValid();
}

bool FActorInstanceHandle::DoesRepresentClass(const UClass* OtherClass) const
{
	if (OtherClass == nullptr)
	{
		return false;
	}

	if (IsActorValid())
	{
		return Actor->IsA(OtherClass);
	}

	if (Manager.IsValid())
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

	if (IsActorValid())
	{
		return Actor->GetClass();
	}

	if (Manager.IsValid())
	{
		return Manager->GetRepresentedClass();
	}

	return nullptr;
}

FVector FActorInstanceHandle::GetLocation() const
{
	if (IsActorValid())
	{
		return Actor->GetActorLocation();
	}

	if (Manager.IsValid())
	{
		Manager->GetLocation(*this);
	}

	return FVector();
}

FRotator FActorInstanceHandle::GetRotation() const
{
	if (IsActorValid())
	{
		return Actor->GetActorRotation();
	}

	if (Manager.IsValid())
	{
		Manager->GetRotation(*this);
	}

	return FRotator();
}

FTransform FActorInstanceHandle::GetTransform() const
{
	if (IsActorValid())
	{
		return Actor->GetActorTransform();
	}

	if (Manager.IsValid())
	{
		Manager->GetTransform(*this);
	}

	return FTransform();
}

FName FActorInstanceHandle::GetFName() const
{
	if (IsActorValid())
	{
		return Actor->GetFName();
	}

	return NAME_None;
}

FString FActorInstanceHandle::GetName() const
{
	if (IsActorValid())
	{
		return Actor->GetName();
	}

	if (Manager.IsValid())
	{
		Manager->GetName(*this);
	}

	return FString();
}

AActor* FActorInstanceHandle::GetManagingActor() const
{
	if (IsActorValid())
	{
		return Actor.Get();
	}

	return Manager.Get();
}

USceneComponent* FActorInstanceHandle::GetRootComponent() const
{
	if (IsActorValid())
	{
		return Actor->GetRootComponent();
	}

	return Manager.IsValid() ? Manager->GetRootComponent() : nullptr;
}

AActor* FActorInstanceHandle::FetchActor() const
{
	if (IsActorValid())
	{
		return Actor.Get();
	}

	return FLightWeightInstanceSubsystem::Get().FetchActor(*this);
}

int32 FActorInstanceHandle::GetRenderingInstanceIndex() const
{
	return Manager.IsValid() ? Manager->ConvertLightWeightIndexToCollisionIndex(InstanceIndex) : INDEX_NONE;
}

UObject* FActorInstanceHandle::GetActorAsUObject()
{
	if (IsActorValid())
	{
		return Cast<UObject>(Actor.Get());
	}

	return nullptr;
}

const UObject* FActorInstanceHandle::GetActorAsUObject() const
{
	if (IsActorValid())
	{
		return Cast<UObject>(Actor.Get());
	}

	return nullptr;
}

bool FActorInstanceHandle::IsActorValid() const
{
	return Actor.IsValid();
}

FActorInstanceHandle& FActorInstanceHandle::operator=(AActor* OtherActor)
{
	Actor = OtherActor;
	Manager.Reset();
	InstanceIndex = INDEX_NONE;

	return *this;
}

bool FActorInstanceHandle::operator==(const FActorInstanceHandle& Other) const
{
	// try to compare managers and indices first if we have them
	if (Manager.IsValid() && Other.Manager.IsValid() && InstanceIndex != INDEX_NONE && Other.InstanceIndex != INDEX_NONE)
	{
		return Manager == Other.Manager && InstanceIndex == Other.InstanceIndex;
	}

	// try to compare the actors
	const AActor* MyActor = FetchActor();
	const AActor* OtherActor = Other.FetchActor();

	return MyActor == OtherActor;
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

	// if OtherActor is null then we're only equal if this doesn't refer to a valid instance
	if (OtherActor == nullptr)
	{
		return !Manager.IsValid() && InstanceIndex == INDEX_NONE;
	}

	// we don't have an actor so see if we can look up an instance associated with OtherActor and see if we refer to the same instance

#if WITH_EDITOR
	// use the first layer the actor is in if it's in multiple layers
	TArray<const UDataLayer*> DataLayers = OtherActor->GetDataLayerObjects();
	const UDataLayer* Layer = DataLayers.Num() > 0 ? DataLayers[0] : nullptr;
#else
	const UDataLayer* Layer = nullptr;
#endif // WITH_EDITOR

	if (ALightWeightInstanceManager* LWIManager = FLightWeightInstanceSubsystem::Get().FindLightWeightInstanceManager(OtherActor->StaticClass(), Layer))
	{
		if (Manager.Get() != LWIManager)
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

uint32 GetTypeHash(const FActorInstanceHandle& Handle)
{
	uint32 Hash = 0;
	if (Handle.Actor.IsValid())
	{
		FCrc::StrCrc32(*(Handle.Actor->GetPathName()), Hash);
	}
	if (Handle.Manager.IsValid())
	{
		Hash = HashCombine(Hash, GetTypeHash(Handle.Manager.Get()));
	}
	Hash = HashCombine(Hash, Handle.InstanceIndex);

	return Hash;
}

FArchive& operator<<(FArchive& Ar, FActorInstanceHandle& Handle)
{
	Ar << Handle.Actor;
	Ar << Handle.Manager;
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
FVector FRepMovement::RebaseOntoLocalOrigin(const FVector& Location, const struct FIntVector& LocalOrigin)
{
	if (EnableMultiplayerWorldOriginRebasing <= 0 || LocalOrigin == FIntVector::ZeroValue)
	{
		return Location;
	}

	return FVector(Location.X - LocalOrigin.X, Location.Y - LocalOrigin.Y, Location.Z - LocalOrigin.Z);
}

/** Rebase local-origin position onto zero world origin value. */
FVector FRepMovement::RebaseOntoZeroOrigin(const FVector& Location, const struct FIntVector& LocalOrigin)
{
	if (EnableMultiplayerWorldOriginRebasing <= 0 || LocalOrigin == FIntVector::ZeroValue)
	{
		return Location;
	}

	return FVector(Location.X + LocalOrigin.X, Location.Y + LocalOrigin.Y, Location.Z + LocalOrigin.Z);
}

/** Rebase zero-origin position onto local world origin value based on an actor's world. */
FVector FRepMovement::RebaseOntoLocalOrigin(const FVector& Location, const AActor* const WorldContextActor)
{
	if (WorldContextActor == nullptr || EnableMultiplayerWorldOriginRebasing <= 0)
	{
		return Location;
	}

	return RebaseOntoLocalOrigin(Location, WorldContextActor->GetWorld()->OriginLocation);
}

/** Rebase local-origin position onto zero world origin value based on an actor's world.*/
FVector FRepMovement::RebaseOntoZeroOrigin(const FVector& Location, const AActor* const WorldContextActor)
{
	if (WorldContextActor == nullptr || EnableMultiplayerWorldOriginRebasing <= 0)
	{
		return Location;
	}

	return RebaseOntoZeroOrigin(Location, WorldContextActor->GetWorld()->OriginLocation);
}

/// @cond DOXYGEN_WARNINGS

/** Rebase zero-origin position onto local world origin value based on an actor component's world. */
FVector FRepMovement::RebaseOntoLocalOrigin(const FVector& Location, const UActorComponent* const WorldContextActorComponent)
{
	if (WorldContextActorComponent == nullptr || EnableMultiplayerWorldOriginRebasing <= 0)
	{
		return Location;
	}

	return RebaseOntoLocalOrigin(Location, WorldContextActorComponent->GetWorld()->OriginLocation);
}

/** Rebase local-origin position onto zero world origin value based on an actor component's world.*/
FVector FRepMovement::RebaseOntoZeroOrigin(const FVector& Location, const UActorComponent* const WorldContextActorComponent)
{
	if (WorldContextActorComponent == nullptr || EnableMultiplayerWorldOriginRebasing <= 0)
	{
		return Location;
	}

	return RebaseOntoZeroOrigin(Location, WorldContextActorComponent->GetWorld()->OriginLocation);
}

const TCHAR* LexToString(const EWorldType::Type Value)
{
	switch (Value)
	{
	case EWorldType::Type::Editor:
		return TEXT("Editor");
		break;
	case EWorldType::Type::EditorPreview:
		return TEXT("EditorPreview");
		break;
	case EWorldType::Type::Game:
		return TEXT("Game");
		break;
	case EWorldType::Type::GamePreview:
		return TEXT("GamePreview");
		break;
	case EWorldType::Type::GameRPC:
		return TEXT("GameRPC");
		break;
	case EWorldType::Type::Inactive:
		return TEXT("Inactive");
		break;
	case EWorldType::Type::PIE:
		return TEXT("PIE");
		break;
	case EWorldType::Type::None:
		return TEXT("None");
		break;
	default:
		return TEXT("Unknown");
		break;
	}
}

/// @endcond
