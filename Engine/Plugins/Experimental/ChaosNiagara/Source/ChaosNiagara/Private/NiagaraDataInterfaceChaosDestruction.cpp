// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceChaosDestruction.h"
#include "NiagaraTypes.h"
#include "Misc/FileHelper.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "PhysicsSolver.h"
#include "Niagara/Private/NiagaraStats.h"
#include "Chaos/PBDCollisionConstraintUtil.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Chaos/PBDCollisionTypes.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterInstanceBatcher.h"

#include <memory>

#define LOCTEXT_NAMESPACE "ChaosNiagaraDestructionDataInterface"
//#pragma optimize("", off)

DECLARE_CYCLE_STAT(TEXT("CollisionCallback"), STAT_CollisionCallback, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("TrailingCallback"), STAT_TrailingCallback, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("BreakingCallback"), STAT_BreakingCallback, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("CollisionCallbackSorting"), STAT_CollisionCallbackSorting, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("BreakingCallbackSorting"), STAT_BreakingCallbackSorting, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("TrailingCallbackSorting"), STAT_TrailingCallbackSorting, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumAllCollisions"), STAT_NiagaraNumAllCollisions, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumFilteredAllCollisions"), STAT_NiagaraNumFilteredAllCollisions, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumCollisionsToSpawnParticles"), STAT_NiagaraNumCollisionsToSpawnParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumAllTrailings"), STAT_NiagaraNumAllTrailings, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumFilteredAllTrailings"), STAT_NiagaraNumFilteredAllTrailings, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumTrailingsToSpawnParticles"), STAT_NiagaraNumTrailingsToSpawnParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumAllBreakings"), STAT_NiagaraNumAllBreakings, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumFilteredAllBreakings"), STAT_NiagaraNumFilteredAllBreakings, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumBreakingsToSpawnParticles"), STAT_NiagaraNumBreakingsToSpawnParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumParticlesSpawnedFromCollisions"), STAT_NiagaraNumParticlesSpawnedFromCollisions, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumParticlesSpawnedFromTrailings"), STAT_NiagaraNumParticlesSpawnedFromTrailings, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumParticlesSpawnedFromBreaking"), STAT_NiagaraNumParticlesSpawnedFromBreakings, STATGROUP_Niagara);
DECLARE_MEMORY_STAT(TEXT("PhysicsProxyReverseMapping"), STAT_PhysicsProxyReverseMappingMemory, STATGROUP_Niagara);
DECLARE_MEMORY_STAT(TEXT("ParticleIndexReverseMapping"), STAT_ParticleIndexReverseMappingMemory, STATGROUP_Niagara);
DECLARE_MEMORY_STAT(TEXT("AllCollisionsData"), STAT_AllCollisionsDataMemory, STATGROUP_Niagara);
DECLARE_MEMORY_STAT(TEXT("AllCollisionsIndicesByPhysicsProxy"), STAT_AllCollisionsIndicesByPhysicsProxyMemory, STATGROUP_Niagara);
DECLARE_MEMORY_STAT(TEXT("AllBreakingsData"), STAT_AllBreakingsDataMemory, STATGROUP_Niagara);
DECLARE_MEMORY_STAT(TEXT("AllBreakingsIndicesByPhysicsProxy"), STAT_AllBreakingsIndicesByPhysicsProxyMemory, STATGROUP_Niagara);
DECLARE_MEMORY_STAT(TEXT("AllTrailingsData"), STAT_AllTrailingsDataMemory, STATGROUP_Niagara);
DECLARE_MEMORY_STAT(TEXT("AllTrailingsIndicesByPhysicsProxy"), STAT_AllTrailingsIndicesByPhysicsProxyMemory, STATGROUP_Niagara);

// Name of all the functions available in the data interface
static const FName GetPositionName("GetPosition");
static const FName GetNormalName("GetNormal");
static const FName GetVelocityName("GetVelocity");
static const FName GetAngularVelocityName("GetAngularVelocity");
static const FName GetExtentMinName("GetExtentMin");
static const FName GetExtentMaxName("GetExtentMax");
static const FName GetVolumeName("GetVolume");
static const FName GetParticleIdsToSpawnAtTimeName("GetParticleIdsToSpawnAtTime");
static const FName GetPointTypeName("GetPointType");
static const FName GetColorName("GetColor");
static const FName GetSolverTimeName("GetSolverTime");
static const FName GetDensityName("GetDensity");
static const FName GetFrictionName("GetFriction");
static const FName GetRestitutionName("GetRestitution");
static const FName GetSurfaceTypeName("GetSurfaceType");
static const FName GetTransformName("GetTransform");
static const FName GetSizeName("GetSize");
static const FName GetCollisionDataName("GetCollisionData");
static const FName GetBreakingDataName("GetBreakingData");
static const FName GetTrailingDataName("GetTrailingData");

UNiagaraDataInterfaceChaosDestruction::UNiagaraDataInterfaceChaosDestruction(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, DataSourceType(EDataSourceTypeEnum::ChaosNiagara_DataSourceType_Collision)
	, DataProcessFrequency(10)
	, MaxNumberOfDataEntriesToSpawn(50)
	, DoSpawn(true)
	, SpawnMultiplierMinMax(FVector2D(1, 1))
	, SpawnChance(1.f)
	, ImpulseToSpawnMinMax(FVector2D(-1.f, -1.f))
	, SpeedToSpawnMinMax(FVector2D(-1.f, -1.f))
	, MassToSpawnMinMax(FVector2D(-1.f, -1.f))
	, ExtentMinToSpawnMinMax(FVector2D(-1.f, -1.f))
	, ExtentMaxToSpawnMinMax(FVector2D(-1.f, -1.f))
	, VolumeToSpawnMinMax(FVector2D(-1.f, -1.f))
	, SolverTimeToSpawnMinMax(FVector2D(-1.f, -1.f))
	, SurfaceTypeToSpawn(-1.f)
	, LocationFilteringMode(ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive)
	, LocationXToSpawn(ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_None)
	, LocationXToSpawnMinMax(FVector2D(0.f, 0.f))
	, LocationYToSpawn(ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_None)
	, LocationYToSpawnMinMax(FVector2D(0.f, 0.f))
	, LocationZToSpawn(ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_None)
	, LocationZToSpawnMinMax(FVector2D(0.f, 0.f))
	, DataSortingType(EDataSortTypeEnum::ChaosNiagara_DataSortType_NoSorting)
	, bGetExternalCollisionData(false)
	, DoSpatialHash(false)
	, SpatialHashVolumeMin(FVector(-100.f))
	, SpatialHashVolumeMax(FVector(100.f))
	, SpatialHashVolumeCellSize(FVector(10.f))
	, MaxDataPerCell(1)
	, bApplyMaterialsFilter(false)
	, bGetExternalBreakingData(true)
	, bGetExternalTrailingData(false)
	, RandomPositionMagnitudeMinMax(FVector2D(0.f, 0.f))
	, InheritedVelocityMultiplier(1.f)
	, RandomVelocityGenerationType(ERandomVelocityGenerationTypeEnum::ChaosNiagara_RandomVelocityGenerationType_RandomDistribution)
	, RandomVelocityMagnitudeMinMax(FVector2D(1.f, 2.f))
	, SpreadAngleMax(30.f)
	, VelocityOffsetMin(FVector(ForceInitToZero))
	, VelocityOffsetMax(FVector(ForceInitToZero))
	, FinalVelocityMagnitudeMinMax(FVector2D(-1.f, -1.f))
	, MaxLatency(1.f)
	, DebugType(EDebugTypeEnum::ChaosNiagara_DebugType_NoDebug)
//	, ParticleIndexToProcess(-1)
	, LastSpawnedPointID(-1)
	, LastSpawnTime(-1.f)
	, SolverTime(0.f)
	, TimeStampOfLastProcessedData(-1.f)
	, ShouldSpawn(true)

{
	// Colors to visualize particles for debugging
	ColorArray.Add({ 1.0, 1.0, 1.0 }); // White
	ColorArray.Add({ 1.0, 0.0, 0.0 }); // Red
	ColorArray.Add({ 0.0, 1.0, 0.0 }); // Lime
	ColorArray.Add({ 0.0, 0.0, 1.0 }); // Blue
	ColorArray.Add({ 1.0, 1.0, 0.0 }); // Yellow
	ColorArray.Add({ 0.0, 1.0, 1.0 }); // Cyan
	ColorArray.Add({ 1.0, 0.0, 1.0 }); // Magenta
	ColorArray.Add({ 0.75, 0.75, 0.75 }); // Silver
	ColorArray.Add({ 0.5, 0.5, 0.5 }); // Gray
	ColorArray.Add({ 0.5, 0.0, 0.0 }); // Maroon
	ColorArray.Add({ 0.5, 0.5, 0.0 }); // Olive
	ColorArray.Add({ 0.0, 0.5, 0.0 }); // Green
	ColorArray.Add({ 0.5, 0.0, 0.5 }); // Purple
	ColorArray.Add({ 0.0, 0.5, 0.5 }); // Teal
	ColorArray.Add({ 0.0, 0.0, 0.5 }); // Navy
	ColorArray.Add({ 1.0, 165.0 / 255.0, 0.5 }); // Orange
	ColorArray.Add({ 1.0, 215.0 / 255.0, 0.5 }); // Gold
	ColorArray.Add({ 154.0 / 255.0, 205.0 / 255.0, 50.0 / 255.0 }); // Yellow green
	ColorArray.Add({ 127.0 / 255.0, 255.0 / 255.0, 212.0 / 255.0 }); // Aqua marine

#if INCLUDE_CHAOS
	Solvers.Reset();
#endif

	Proxy = MakeShared<FNiagaraDataInterfaceProxyChaosDestruction, ESPMode::ThreadSafe>();
	PushToRenderThread();
}

void UNiagaraDataInterfaceChaosDestruction::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
		FNiagaraTypeRegistry::Register(FChaosDestructionEvent::StaticStruct(), true, true, false);
	}

	LastSpawnedPointID = -1;
	LastSpawnTime = -1.f;
	TimeStampOfLastProcessedData = -1.f;
	PushToRenderThread();
}

void UNiagaraDataInterfaceChaosDestruction::PostLoad()
{
	Super::PostLoad();

	LastSpawnedPointID = -1;
	LastSpawnTime = -1.f;
	TimeStampOfLastProcessedData = -1.f;

#if WITH_CHAOS
	FPhysScene* Scene = GetWorld()->GetPhysicsScene();
	Scene->RegisterEventHandler<Chaos::FCollisionEventData>(Chaos::EEventType::Collision, this, &UNiagaraDataInterfaceChaosDestruction::HandleCollisionEvents);
	Scene->RegisterEventHandler<Chaos::FBreakingEventData>(Chaos::EEventType::Breaking, this, &UNiagaraDataInterfaceChaosDestruction::HandleBreakingEvents);
	Scene->RegisterEventHandler<Chaos::FTrailingEventData>(Chaos::EEventType::Trailing, this, &UNiagaraDataInterfaceChaosDestruction::HandleTrailingEvents);
#endif

	PushToRenderThread();
}

void UNiagaraDataInterfaceChaosDestruction::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_CHAOS
	FPhysScene* Scene = GetWorld()->GetPhysicsScene();
	if (Scene)
	{
		Scene->UnregisterEventHandler(Chaos::EEventType::Collision, this);
		Scene->UnregisterEventHandler(Chaos::EEventType::Breaking, this);
		Scene->UnregisterEventHandler(Chaos::EEventType::Trailing, this);
	}
#endif
}

#if WITH_EDITOR

void UNiagaraDataInterfaceChaosDestruction::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, ChaosSolverActorSet))
		{
			Modify();
			if (ChaosSolverActorSet.Num())
			{
				LastSpawnedPointID = -1;
				LastSpawnTime = -1.f;
				TimeStampOfLastProcessedData = -1.f;
			}
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, ChaosBreakingMaterialSet))
		{
			Modify();
			if (ChaosBreakingMaterialSet.Num())
			{
				/**/
			}
		}
		// Validate inputs
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, DataProcessFrequency))
		{
			DataProcessFrequency = FMath::Max(1, DataProcessFrequency);
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, MaxNumberOfDataEntriesToSpawn))
		{
			MaxNumberOfDataEntriesToSpawn = FMath::Max(0, MaxNumberOfDataEntriesToSpawn);
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, SpawnMultiplierMinMax))
		{
			if (PropertyChangedEvent.Property->GetFName() == FName("X"))
			{
				SpawnMultiplierMinMax.X = FMath::Max(0.f, SpawnMultiplierMinMax.X);
			}
			else if (PropertyChangedEvent.Property->GetFName() == FName("Y"))
			{
				SpawnMultiplierMinMax.Y = FMath::Max(0.f, SpawnMultiplierMinMax.Y);
			}
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, SpawnChance))
		{
			SpawnChance = FMath::Clamp(SpawnChance, 0.f, 1.f);
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, SpatialHashVolumeCellSize))
		{
			SpatialHashVolumeCellSize.X = FMath::Max(1.f, SpatialHashVolumeCellSize.X);
			SpatialHashVolumeCellSize.Y = FMath::Max(1.f, SpatialHashVolumeCellSize.Y);
			SpatialHashVolumeCellSize.Z = FMath::Max(1.f, SpatialHashVolumeCellSize.Z);
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, MaxDataPerCell))
		{
			MaxDataPerCell = FMath::Max(0, MaxDataPerCell);
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, RandomVelocityMagnitudeMinMax))
		{
			if (PropertyChangedEvent.Property->GetFName() == FName("X"))
			{
				RandomVelocityMagnitudeMinMax.X = FMath::Max(0.f, RandomVelocityMagnitudeMinMax.X);
			}
			else if (PropertyChangedEvent.Property->GetFName() == FName("Y"))
			{
				RandomVelocityMagnitudeMinMax.Y = FMath::Max(0.f, RandomVelocityMagnitudeMinMax.Y);
			}
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, SpreadAngleMax))
		{
			SpreadAngleMax = FMath::Clamp(SpreadAngleMax, 0.f, 90.f);
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceChaosDestruction, MaxLatency))
		{
			MaxLatency = FMath::Max(0.f, MaxLatency);
		}
	}

	PushToRenderThread();
}

#endif

bool UNiagaraDataInterfaceChaosDestruction::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	if (UNiagaraDataInterfaceChaosDestruction* DestinationChaosDestruction = CastChecked<UNiagaraDataInterfaceChaosDestruction>(Destination))
	{
		DestinationChaosDestruction->ChaosSolverActorSet = ChaosSolverActorSet;
		DestinationChaosDestruction->DataSourceType = DataSourceType;
		DestinationChaosDestruction->DataProcessFrequency = DataProcessFrequency;
		DestinationChaosDestruction->MaxNumberOfDataEntriesToSpawn = MaxNumberOfDataEntriesToSpawn;
		DestinationChaosDestruction->DoSpawn = DoSpawn;
		DestinationChaosDestruction->ShouldSpawn = ShouldSpawn;
		DestinationChaosDestruction->SpawnMultiplierMinMax = SpawnMultiplierMinMax;
		DestinationChaosDestruction->SpawnChance = SpawnChance;
		DestinationChaosDestruction->ImpulseToSpawnMinMax = ImpulseToSpawnMinMax;
		DestinationChaosDestruction->SpeedToSpawnMinMax = SpeedToSpawnMinMax;
		DestinationChaosDestruction->MassToSpawnMinMax = MassToSpawnMinMax;
		DestinationChaosDestruction->ExtentMinToSpawnMinMax = ExtentMinToSpawnMinMax;
		DestinationChaosDestruction->ExtentMaxToSpawnMinMax = ExtentMaxToSpawnMinMax;
		DestinationChaosDestruction->VolumeToSpawnMinMax = VolumeToSpawnMinMax;
		DestinationChaosDestruction->SolverTimeToSpawnMinMax = SolverTimeToSpawnMinMax;
		DestinationChaosDestruction->SurfaceTypeToSpawn = SurfaceTypeToSpawn;
		DestinationChaosDestruction->LocationFilteringMode = LocationFilteringMode;
		DestinationChaosDestruction->LocationXToSpawn = LocationXToSpawn;
		DestinationChaosDestruction->LocationXToSpawnMinMax = LocationXToSpawnMinMax;
		DestinationChaosDestruction->LocationYToSpawn = LocationYToSpawn;
		DestinationChaosDestruction->LocationYToSpawnMinMax = LocationYToSpawnMinMax;
		DestinationChaosDestruction->LocationZToSpawn = LocationZToSpawn;
		DestinationChaosDestruction->LocationZToSpawnMinMax = LocationZToSpawnMinMax;
		DestinationChaosDestruction->DataSortingType = DataSortingType;
		DestinationChaosDestruction->DoSpatialHash = DoSpatialHash;
		DestinationChaosDestruction->bGetExternalCollisionData = bGetExternalCollisionData;
		DestinationChaosDestruction->bGetExternalBreakingData = bGetExternalBreakingData;
		DestinationChaosDestruction->bGetExternalTrailingData = bGetExternalTrailingData;
		DestinationChaosDestruction->SpatialHashVolumeMin = SpatialHashVolumeMin;
		DestinationChaosDestruction->SpatialHashVolumeMax = SpatialHashVolumeMax;
		DestinationChaosDestruction->SpatialHashVolumeCellSize = SpatialHashVolumeCellSize;
		DestinationChaosDestruction->MaxDataPerCell = MaxDataPerCell;
		DestinationChaosDestruction->bApplyMaterialsFilter = bApplyMaterialsFilter;
		DestinationChaosDestruction->ChaosBreakingMaterialSet = ChaosBreakingMaterialSet;
		DestinationChaosDestruction->RandomPositionMagnitudeMinMax = RandomPositionMagnitudeMinMax;
		DestinationChaosDestruction->InheritedVelocityMultiplier = InheritedVelocityMultiplier;
		DestinationChaosDestruction->RandomVelocityGenerationType = RandomVelocityGenerationType;
		DestinationChaosDestruction->RandomVelocityMagnitudeMinMax = RandomVelocityMagnitudeMinMax;
		DestinationChaosDestruction->SpreadAngleMax = SpreadAngleMax;
		DestinationChaosDestruction->VelocityOffsetMin = VelocityOffsetMin;
		DestinationChaosDestruction->VelocityOffsetMax = VelocityOffsetMax;
		DestinationChaosDestruction->FinalVelocityMagnitudeMinMax = FinalVelocityMagnitudeMinMax;
		DestinationChaosDestruction->MaxLatency = MaxLatency;
		DestinationChaosDestruction->DebugType = DebugType;
		//DestinationChaosDestruction->ParticleIndexToProcess = ParticleIndexToProcess;
		DestinationChaosDestruction->LastSpawnedPointID = LastSpawnedPointID;
		DestinationChaosDestruction->LastSpawnTime = LastSpawnTime;
		DestinationChaosDestruction->TimeStampOfLastProcessedData = TimeStampOfLastProcessedData;
		DestinationChaosDestruction->SolverTime = SolverTime;
		DestinationChaosDestruction->PushToRenderThread();

		return true;
	}

	return false;
}

bool UNiagaraDataInterfaceChaosDestruction::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceChaosDestruction* OtherChaosDestruction = Cast<const UNiagaraDataInterfaceChaosDestruction>(Other);
	if (OtherChaosDestruction == nullptr)
	{
		return false;
	}

	if (OtherChaosDestruction->ChaosSolverActorSet.Num() != ChaosSolverActorSet.Num())
	{
		return false;
	}

	bool bResult = true;
	for (int32 Idx = 0; Idx < ChaosSolverActorSet.Num(); ++Idx)
	{
		bResult = bResult &&
			OtherChaosDestruction->ChaosSolverActorSet.Array()[Idx]->GetName().Equals(ChaosSolverActorSet.Array()[Idx]->GetName());
	}

	return bResult
		&& OtherChaosDestruction->DoSpawn == DoSpawn
		&& OtherChaosDestruction->ShouldSpawn == ShouldSpawn
		&& OtherChaosDestruction->DataSourceType == DataSourceType
		&& OtherChaosDestruction->DataProcessFrequency == DataProcessFrequency
		&& OtherChaosDestruction->MaxNumberOfDataEntriesToSpawn == MaxNumberOfDataEntriesToSpawn
		&& OtherChaosDestruction->SpawnMultiplierMinMax == SpawnMultiplierMinMax
		&& OtherChaosDestruction->SpawnChance == SpawnChance
		&& OtherChaosDestruction->ImpulseToSpawnMinMax == ImpulseToSpawnMinMax
		&& OtherChaosDestruction->SpeedToSpawnMinMax == SpeedToSpawnMinMax
		&& OtherChaosDestruction->MassToSpawnMinMax == MassToSpawnMinMax
		&& OtherChaosDestruction->ExtentMinToSpawnMinMax == ExtentMinToSpawnMinMax
		&& OtherChaosDestruction->ExtentMaxToSpawnMinMax == ExtentMaxToSpawnMinMax
		&& OtherChaosDestruction->VolumeToSpawnMinMax == VolumeToSpawnMinMax
		&& OtherChaosDestruction->SolverTimeToSpawnMinMax == SolverTimeToSpawnMinMax
		&& OtherChaosDestruction->SurfaceTypeToSpawn == SurfaceTypeToSpawn
		&& OtherChaosDestruction->LocationFilteringMode == LocationFilteringMode
		&& OtherChaosDestruction->LocationXToSpawn == LocationXToSpawn
		&& OtherChaosDestruction->LocationXToSpawnMinMax == LocationXToSpawnMinMax
		&& OtherChaosDestruction->LocationYToSpawn == LocationYToSpawn
		&& OtherChaosDestruction->LocationYToSpawnMinMax == LocationYToSpawnMinMax
		&& OtherChaosDestruction->LocationZToSpawn == LocationZToSpawn
		&& OtherChaosDestruction->LocationZToSpawnMinMax == LocationZToSpawnMinMax
		&& OtherChaosDestruction->DataSortingType == DataSortingType
		&& OtherChaosDestruction->DoSpatialHash == DoSpatialHash
		&& OtherChaosDestruction->bGetExternalCollisionData == bGetExternalCollisionData
		&& OtherChaosDestruction->bGetExternalBreakingData == bGetExternalBreakingData
		&& OtherChaosDestruction->bGetExternalTrailingData == bGetExternalTrailingData
		&& OtherChaosDestruction->SpatialHashVolumeMin == SpatialHashVolumeMin
		&& OtherChaosDestruction->SpatialHashVolumeMax == SpatialHashVolumeMax
		&& OtherChaosDestruction->SpatialHashVolumeCellSize == SpatialHashVolumeCellSize
		&& OtherChaosDestruction->MaxDataPerCell == MaxDataPerCell
		&& OtherChaosDestruction->bApplyMaterialsFilter == bApplyMaterialsFilter
		//&& OtherChaosDestruction->ChaosBreakingMaterialSet == ChaosBreakingMaterialSet // Error	C2678	binary '==': no operator found which takes a left - hand operand of type 'const TSet<UPhysicalMaterial *,DefaultKeyFuncs<InElementType,false>,FDefaultSetAllocator>' (or there is no acceptable conversion)	ChaosOdin	Z : \Epic\Morten.Vassvik_DESKTOP - Dev - Destruction\Engine\Plugins\Experimental\ChaosNiagara\Source\ChaosNiagara\Private\NiagaraDataInterfaceChaosDestruction.cpp	369
		&& OtherChaosDestruction->RandomPositionMagnitudeMinMax == RandomPositionMagnitudeMinMax
		&& OtherChaosDestruction->InheritedVelocityMultiplier == InheritedVelocityMultiplier
		&& OtherChaosDestruction->RandomVelocityGenerationType == RandomVelocityGenerationType
		&& OtherChaosDestruction->RandomVelocityMagnitudeMinMax == RandomVelocityMagnitudeMinMax
		&& OtherChaosDestruction->SpreadAngleMax == SpreadAngleMax
		&& OtherChaosDestruction->VelocityOffsetMin == VelocityOffsetMin
		&& OtherChaosDestruction->VelocityOffsetMax == VelocityOffsetMax
		&& OtherChaosDestruction->FinalVelocityMagnitudeMinMax == FinalVelocityMagnitudeMinMax
		&& OtherChaosDestruction->MaxLatency == MaxLatency
		&& OtherChaosDestruction->DebugType == DebugType;
		//&& OtherChaosDestruction->ParticleIndexToProcess == ParticleIndexToProcess;
}

int32 UNiagaraDataInterfaceChaosDestruction::PerInstanceDataSize()const
{
	return sizeof(FNDIChaosDestruction_InstanceData);
}

bool UNiagaraDataInterfaceChaosDestruction::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIChaosDestruction_InstanceData* InstData = new (PerInstanceData) FNDIChaosDestruction_InstanceData();

	LastSpawnedPointID = -1;
	LastSpawnTime = -1.0f;
	TimeStampOfLastProcessedData = -1.f;

#if INCLUDE_CHAOS
	// If there is no SolverActor specified need to grab the WorldSolver
	if (ChaosSolverActorSet.Num() == 0)
	{
		if (SystemInstance)
		{
			if (UNiagaraComponent* NiagaraComponent = SystemInstance->GetComponent())
			{
				if (UWorld* World = NiagaraComponent->GetWorld())
				{
					int32 NewIdx = Solvers.Add(FSolverData());

					FSolverData& SolverData = Solvers[NewIdx];
					SolverData.PhysScene = World->PhysicsScene_Chaos;
					SolverData.Solver = World->PhysicsScene_Chaos->GetSolver();
				}
			}
		}
	}
	else
	{
		for (AChaosSolverActor* SolverActor : ChaosSolverActorSet)
		{
			if (SolverActor)
			{
				if (Chaos::FPhysicsSolver* Solver = SolverActor->GetSolver())
				{
					int32 NewIdx = Solvers.Add(FSolverData());

					FSolverData& SolverData = Solvers[NewIdx];
					SolverData.PhysScene = SolverActor->GetPhysicsScene();
					SolverData.Solver = Solver;
				}
			}
		}
	}

	ResetInstData(InstData);

	TSharedPtr<FNiagaraDataInterfaceProxyChaosDestruction, ESPMode::ThreadSafe> ThisProxy = StaticCastSharedPtr<FNiagaraDataInterfaceProxyChaosDestruction, FNiagaraDataInterfaceProxy, ESPMode::ThreadSafe>(Proxy);
	ENQUEUE_RENDER_COMMAND(FNiagaraChaosDestructionDICreateRTInstance)(
		[ThisProxy, InstanceID = SystemInstance->GetId()](FRHICommandList& CmdList)
	{
		ThisProxy->CreatePerInstanceData(InstanceID);
	}
	);

#endif

	return true;
}

void UNiagaraDataInterfaceChaosDestruction::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIChaosDestruction_InstanceData* InstData = (FNDIChaosDestruction_InstanceData*)PerInstanceData;
	InstData->~FNDIChaosDestruction_InstanceData();

	check(Proxy);
	TSharedPtr<FNiagaraDataInterfaceProxyChaosDestruction, ESPMode::ThreadSafe> ThisProxy = StaticCastSharedPtr<FNiagaraDataInterfaceProxyChaosDestruction, FNiagaraDataInterfaceProxy, ESPMode::ThreadSafe>(Proxy);
	ENQUEUE_RENDER_COMMAND(FNiagaraDIChaosDestructionDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			ThisProxy->DestroyInstanceData(Batcher, InstanceID);
		}
	);
}

#if INCLUDE_CHAOS
void GetMeshExtData(FSolverData SolverData,
					const int32 ParticleIndex,
					const TArray<PhysicsProxyWrapper>& PhysicsProxyReverseMapping,
					const TArray<int32>& ParticleIndexReverseMapping,
					float& BoundingboxVolume,
					float& BoundingboxExtentMin,
					float& BoundingboxExtentMax,
					FBox& BoundingBox,
					int32& SurfaceType,
					Chaos::TRigidTransform<float, 3>& Transform,
					UPhysicalMaterial*& PhysicalMaterial)
{
	PhysicalMaterial = nullptr;
	if (ParticleIndex < 0)
	{
		BoundingboxVolume = 1000000.f;
		BoundingboxExtentMin = 100.f;
		BoundingboxExtentMax = 100.f;
		SurfaceType = 0;
	}
	else if (PhysicsProxyReverseMapping[ParticleIndex].Type == EPhysicsProxyType::GeometryCollectionType)
	{
		// Since we are touching game objects below, I want to make sure that we're not off in some random thread.
		ensure(IsInGameThread());
		if (IPhysicsProxyBase* PhysicsProxy = PhysicsProxyReverseMapping[ParticleIndex].PhysicsProxy)
		{
			if (UGeometryCollectionComponent* GeometryCollectionComponent = SolverData.PhysScene->GetOwningComponent<UGeometryCollectionComponent>(PhysicsProxy))
			{
				if (const UGeometryCollection* GeometryCollection = GeometryCollectionComponent->GetRestCollection())
				{
					if (const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionObject = GeometryCollection->GetGeometryCollection())
					{
						//int NumTransforms = GeometryCollectionObject->NumElements(FGeometryCollection::TransformGroup);
						if (!ensure(0 <= ParticleIndex && ParticleIndex < ParticleIndexReverseMapping.Num()))
						{
							return;
						}
						int32 TransformIndex = ParticleIndexReverseMapping[ParticleIndex];
						//ensure(TransformIndex < NumTransforms);

						//int NumGeoms = GeometryCollectionObject->NumElements(FGeometryCollection::GeometryGroup);
						int32 GeometryGroupIndex = GeometryCollectionObject->TransformToGeometryIndex[TransformIndex];
						//ensure(0 <= GeometryGroupIndex && GeometryGroupIndex < NumGeoms);

						if (!ensure(0 <= GeometryGroupIndex && GeometryGroupIndex < GeometryCollectionObject->BoundingBox.Num()))
						{
							return;
						}
						BoundingBox = GeometryCollectionObject->BoundingBox[GeometryGroupIndex];

						FVector Extents = BoundingBox.GetSize();
						BoundingboxExtentMin = FMath::Min3(Extents[0], Extents[1], Extents[2]);
						BoundingboxExtentMax = FMath::Max3(Extents[0], Extents[1], Extents[2]);
						BoundingboxVolume = BoundingBox.GetVolume();

						// Get data from MareialID[]
						int32 FaceStartIndex = GeometryCollectionObject->FaceStart[GeometryGroupIndex];
						int32 MaterialID = GeometryCollectionObject->MaterialID[FaceStartIndex];

						UMaterialInterface* Material = GeometryCollectionComponent->GetMaterial(MaterialID);
						ensure(Material);
						if (Material)
						{
							PhysicalMaterial = Material->GetPhysicalMaterial();
							ensure(PhysicalMaterial);
							if (PhysicalMaterial)
							{
								SurfaceType = PhysicalMaterial->SurfaceType;
							}
						}
					}
				}
				if (const FGeometryCollectionPhysicsProxy* GeomCollectionPhysicsProxy = GeometryCollectionComponent->GetPhysicsProxy())
				{
					const FGeometryCollectionResults& PhysResult = GeomCollectionPhysicsProxy->GetPhysicsResults().GetGameDataForRead();
					Transform = PhysResult.ParticleToWorldTransforms[ParticleIndex - PhysResult.BaseIndex];
				}
			}
		}
	}
}

void GetMesPhysicalData(FSolverData SolverData,
						const int32 ParticleIndex,
						const TArray<PhysicsProxyWrapper>& PhysicsProxyReverseMapping,
						const TArray<int32>& ParticleIndexReverseMapping,
						FLinearColor& Color,
						float& Friction,
						float& Restitution,
						float& Density)
{
	static FMaterialParameterInfo ChaosDestructionColorName[3] = {
		FMaterialParameterInfo(FName("ChaosDestructionColor1")),
		FMaterialParameterInfo(FName("ChaosDestructionColor2")),
		FMaterialParameterInfo(FName("ChaosDestructionColor3")),
	};
	
	if (ParticleIndex < 0)
	{
		Friction = 0.7f;
		Restitution = 0.3f;
		Density = 1.0f;
	}
	else if (PhysicsProxyReverseMapping[ParticleIndex].Type == EPhysicsProxyType::GeometryCollectionType)
	{
		// Since we are touching game objects below, I want to make sure that we're not off in some random thread.
		ensure(IsInGameThread());

		if (IPhysicsProxyBase* PhysicsProxy = PhysicsProxyReverseMapping[ParticleIndex].PhysicsProxy)
		{
			if (UGeometryCollectionComponent* GeometryCollectionComponent = SolverData.PhysScene->GetOwningComponent<UGeometryCollectionComponent>(PhysicsProxy))
			{
				if (const UGeometryCollection* GeometryCollection = GeometryCollectionComponent->GetRestCollection())
				{
					if (const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionObject = GeometryCollection->GetGeometryCollection())
					{
						//int NumTransforms = GeometryCollectionObject->NumElements(FGeometryCollection::TransformGroup);
						if (!ensure(0 <= ParticleIndex && ParticleIndex < ParticleIndexReverseMapping.Num()))
						{
							return;
						}
						int32 TransformIndex = ParticleIndexReverseMapping[ParticleIndex];
						//ensure(TransformIndex < NumTransforms);

						//int NumGeoms = GeometryCollectionObject->NumElements(FGeometryCollection::GeometryGroup);
						if (!ensure(0 <= TransformIndex && TransformIndex < GeometryCollectionObject->TransformToGeometryIndex.Num()))
						{
							return;
						}
						int32 GeometryGroupIndex = GeometryCollectionObject->TransformToGeometryIndex[TransformIndex];
						//ensure(0 <= GeometryGroupIndex && GeometryGroupIndex < NumGeoms);
						
						
						if (!ensure(0 <= GeometryGroupIndex && GeometryGroupIndex < GeometryCollectionObject->BoundingBox.Num()))
						{
							return;
						}
						FBox BoundingBox = GeometryCollectionObject->BoundingBox[GeometryGroupIndex];

						// Get data from MaterialID[]
						int32 FaceStartIndex = GeometryCollectionObject->FaceStart[GeometryGroupIndex];
						int32 MaterialID = GeometryCollectionObject->MaterialID[FaceStartIndex];
						// For now let's use the first material
						MaterialID = 0;

						UMaterialInterface* Material = GeometryCollectionComponent->GetMaterial(MaterialID);
						ensure(Material);
						if (Material)
						{
							int RandVal = FMath::RandRange(0, sizeof(ChaosDestructionColorName) / sizeof(FMaterialParameterInfo) - 1);

							FLinearColor ChaosDestructionColor;
							if (Material->GetVectorParameterValue(ChaosDestructionColorName[RandVal], ChaosDestructionColor))
							{
								Color = ChaosDestructionColor;
							}

							UPhysicalMaterial* PhysicalMaterial = Material->GetPhysicalMaterial();
							ensure(PhysicalMaterial);
							if (PhysicalMaterial)

							{
								//UE_LOG(LogScript, Warning, TEXT("GetMesPhysicalData: Name = %s"), *PhysicalMaterial->GetName());
								Friction = PhysicalMaterial->Friction;
								Restitution = PhysicalMaterial->Restitution;
								Density = PhysicalMaterial->Density;
							}
						}
					}
				}
			}
		}
	}
}
#endif

#if INCLUDE_CHAOS

void UNiagaraDataInterfaceChaosDestruction::HandleCollisionEvents(const Chaos::FCollisionEventData& Event)
{
	ensure(IsInGameThread());
	
	// Copy data from Event into AllCollisionsArray
	// Also get Boundingbox related data and SurfaceType and save it as well
	CollisionEvents.AddUninitialized(Event.CollisionData.AllCollisionsArray.Num());

	int32 Idx = 0;
	for (Chaos::TCollisionDataExt<float, 3>& Collision : CollisionEvents)
	{
		Collision = Event.CollisionData.AllCollisionsArray[Idx];

		// #GM: Disable this for now for perf
		/*
		GetMeshExtData(SolverData,
			AllCollisionsArray[Idx].ParticleIndexMesh == INDEX_NONE ? AllCollisionsArray[Idx].ParticleIndex : AllCollisionsArray[Idx].ParticleIndexMesh,
			PhysicsProxyReverseMappingArray,
			ParticleIndexReverseMappingArray,
			AllCollisionsArray[Idx].BoundingboxVolume,
			AllCollisionsArray[Idx].BoundingboxExtentMin,
			AllCollisionsArray[Idx].BoundingboxExtentMax,
			AllCollisionsArray[Idx].SurfaceType);
		*/
		CollisionEvents[Idx].BoundingboxVolume = 1000000.f;
		CollisionEvents[Idx].BoundingboxExtentMin = 100.f;
		CollisionEvents[Idx].BoundingboxExtentMax = 100.f;
		CollisionEvents[Idx].SurfaceType = 0;

		Idx++;
	}
}


void UNiagaraDataInterfaceChaosDestruction::FilterAllCollisions(TArray<Chaos::TCollisionDataExt<float, 3>>& AllCollisionsArray)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FilterAllCollisions);

	if (/*ParticleToProcess != nullptr ||*/
		ImpulseToSpawnMinMax.X > 0.f ||
		ImpulseToSpawnMinMax.Y > 0.f ||
		SpeedToSpawnMinMax.X > 0.f ||
		SpeedToSpawnMinMax.Y > 0.f ||
		MassToSpawnMinMax.X > 0.f ||
		MassToSpawnMinMax.Y > 0.f ||
		ExtentMinToSpawnMinMax.X > 0.f ||
		ExtentMinToSpawnMinMax.Y > 0.f ||
		ExtentMaxToSpawnMinMax.X > 0.f ||
		ExtentMaxToSpawnMinMax.Y > 0.f ||
		VolumeToSpawnMinMax.X > 0.f ||
		VolumeToSpawnMinMax.Y > 0.f ||
		SolverTimeToSpawnMinMax.X > 0.f ||
		SolverTimeToSpawnMinMax.Y > 0.f ||
		SurfaceTypeToSpawn != -1 ||
		LocationXToSpawn != ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_None ||
		LocationYToSpawn != ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_None ||
		LocationZToSpawn != ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_None)
	{
		TArray<Chaos::TCollisionDataExt<float, 3>> FilteredAllCollisionsArray;
		FilteredAllCollisionsArray.SetNumUninitialized(AllCollisionsArray.Num());

		int32 IdxFilteredCollisions = 0;

		float MinImpulseToSpawnSquared = ImpulseToSpawnMinMax.X * ImpulseToSpawnMinMax.X;
		float MaxImpulseToSpawnSquared = ImpulseToSpawnMinMax.Y * ImpulseToSpawnMinMax.Y;
		float MinSpeedToSpawnSquared = SpeedToSpawnMinMax.X * SpeedToSpawnMinMax.X;
		float MaxSpeedToSpawnSquared = SpeedToSpawnMinMax.Y * SpeedToSpawnMinMax.Y;

		for (int32 IdxCollision = 0; IdxCollision < AllCollisionsArray.Num(); ++IdxCollision)
		{
			float CollisionAccumulatedImpulseSquared = AllCollisionsArray[IdxCollision].AccumulatedImpulse.SizeSquared();
			float CollisionSpeedSquared = AllCollisionsArray[IdxCollision].Velocity1.SizeSquared();

			if (/*(ParticleToProcess != nullptr && AllCollisionsArrayInOut[IdxCollision].Particle != ParticleToProcess) ||*/
				(ImpulseToSpawnMinMax.X > 0.f && ImpulseToSpawnMinMax.Y < 0.f && CollisionAccumulatedImpulseSquared < MinImpulseToSpawnSquared) ||
				(ImpulseToSpawnMinMax.X < 0.f && ImpulseToSpawnMinMax.Y > 0.f && CollisionAccumulatedImpulseSquared > MaxImpulseToSpawnSquared) ||
				(ImpulseToSpawnMinMax.X > 0.f && ImpulseToSpawnMinMax.Y > 0.f && (CollisionAccumulatedImpulseSquared < MinImpulseToSpawnSquared || CollisionAccumulatedImpulseSquared > MaxImpulseToSpawnSquared)) ||
				(SpeedToSpawnMinMax.X > 0.f && SpeedToSpawnMinMax.Y < 0.f && CollisionSpeedSquared < MinSpeedToSpawnSquared) ||
				(SpeedToSpawnMinMax.X < 0.f && SpeedToSpawnMinMax.Y > 0.f && CollisionSpeedSquared > MaxSpeedToSpawnSquared) ||
				(SpeedToSpawnMinMax.X > 0.f && SpeedToSpawnMinMax.Y > 0.f && (CollisionSpeedSquared < MinSpeedToSpawnSquared || CollisionSpeedSquared > MaxSpeedToSpawnSquared)) ||
				(MassToSpawnMinMax.X > 0.f && MassToSpawnMinMax.Y < 0.f && AllCollisionsArray[IdxCollision].Mass1 < MassToSpawnMinMax.X) ||
				(MassToSpawnMinMax.X < 0.f && MassToSpawnMinMax.Y > 0.f && AllCollisionsArray[IdxCollision].Mass1 > MassToSpawnMinMax.Y) ||
				(MassToSpawnMinMax.X > 0.f && MassToSpawnMinMax.Y > 0.f && (AllCollisionsArray[IdxCollision].Mass1 < MassToSpawnMinMax.X || AllCollisionsArray[IdxCollision].Mass1 > MassToSpawnMinMax.Y)) ||
				(ExtentMinToSpawnMinMax.X > 0.f && ExtentMinToSpawnMinMax.Y < 0.f && AllCollisionsArray[IdxCollision].BoundingboxExtentMin < ExtentMinToSpawnMinMax.X) ||
				(ExtentMinToSpawnMinMax.X < 0.f && ExtentMinToSpawnMinMax.Y > 0.f && AllCollisionsArray[IdxCollision].BoundingboxExtentMin > ExtentMinToSpawnMinMax.Y) ||
				(ExtentMinToSpawnMinMax.X > 0.f && ExtentMinToSpawnMinMax.Y > 0.f && (AllCollisionsArray[IdxCollision].BoundingboxExtentMin < ExtentMinToSpawnMinMax.X || AllCollisionsArray[IdxCollision].BoundingboxExtentMin > ExtentMinToSpawnMinMax.Y)) ||
				(ExtentMaxToSpawnMinMax.X > 0.f && ExtentMaxToSpawnMinMax.Y < 0.f && AllCollisionsArray[IdxCollision].BoundingboxExtentMax < ExtentMaxToSpawnMinMax.X) ||
				(ExtentMaxToSpawnMinMax.X < 0.f && ExtentMaxToSpawnMinMax.Y > 0.f && AllCollisionsArray[IdxCollision].BoundingboxExtentMax > ExtentMaxToSpawnMinMax.Y) ||
				(ExtentMaxToSpawnMinMax.X > 0.f && ExtentMaxToSpawnMinMax.Y > 0.f && (AllCollisionsArray[IdxCollision].BoundingboxExtentMax < ExtentMaxToSpawnMinMax.X || AllCollisionsArray[IdxCollision].BoundingboxExtentMax > ExtentMaxToSpawnMinMax.Y)) ||
				(VolumeToSpawnMinMax.X > 0.f && VolumeToSpawnMinMax.Y < 0.f && AllCollisionsArray[IdxCollision].BoundingboxVolume < VolumeToSpawnMinMax.X) ||
				(VolumeToSpawnMinMax.X < 0.f && VolumeToSpawnMinMax.Y > 0.f && AllCollisionsArray[IdxCollision].BoundingboxVolume > VolumeToSpawnMinMax.Y) ||
				(VolumeToSpawnMinMax.X > 0.f && VolumeToSpawnMinMax.Y > 0.f && (AllCollisionsArray[IdxCollision].BoundingboxVolume < VolumeToSpawnMinMax.X || AllCollisionsArray[IdxCollision].BoundingboxVolume > VolumeToSpawnMinMax.Y)) ||
				(SolverTimeToSpawnMinMax.X > 0.f && SolverTimeToSpawnMinMax.Y < 0.f && SolverTime < SolverTimeToSpawnMinMax.X) ||
				(SolverTimeToSpawnMinMax.X < 0.f && SolverTimeToSpawnMinMax.Y > 0.f && SolverTime > SolverTimeToSpawnMinMax.Y) ||
				(SolverTimeToSpawnMinMax.X > 0.f && SolverTimeToSpawnMinMax.Y > 0.f && (SolverTime < SolverTimeToSpawnMinMax.X || SolverTime > SolverTimeToSpawnMinMax.Y)) ||
				(SurfaceTypeToSpawn != -1 && AllCollisionsArray[IdxCollision].SurfaceType != SurfaceTypeToSpawn) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Min && AllCollisionsArray[IdxCollision].Location.X <= LocationXToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Max && AllCollisionsArray[IdxCollision].Location.X >= LocationXToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_MinMax && (AllCollisionsArray[IdxCollision].Location.X <= LocationXToSpawnMinMax.X || AllCollisionsArray[IdxCollision].Location.X >= LocationXToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Min && AllCollisionsArray[IdxCollision].Location.Y <= LocationYToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Max && AllCollisionsArray[IdxCollision].Location.Y >= LocationYToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_MinMax && (AllCollisionsArray[IdxCollision].Location.Y <= LocationYToSpawnMinMax.X || AllCollisionsArray[IdxCollision].Location.Y >= LocationYToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Min && AllCollisionsArray[IdxCollision].Location.Z <= LocationZToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Max && AllCollisionsArray[IdxCollision].Location.Z >= LocationZToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_MinMax && (AllCollisionsArray[IdxCollision].Location.Z <= LocationZToSpawnMinMax.X || AllCollisionsArray[IdxCollision].Location.Z >= LocationZToSpawnMinMax.Y)) ||			
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Min && AllCollisionsArray[IdxCollision].Location.X > LocationXToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Max && AllCollisionsArray[IdxCollision].Location.X < LocationXToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_MinMax && !(AllCollisionsArray[IdxCollision].Location.X <= LocationXToSpawnMinMax.X || AllCollisionsArray[IdxCollision].Location.X >= LocationXToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Min && AllCollisionsArray[IdxCollision].Location.Y > LocationYToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Max && AllCollisionsArray[IdxCollision].Location.Y < LocationYToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_MinMax && !(AllCollisionsArray[IdxCollision].Location.Y <= LocationYToSpawnMinMax.X || AllCollisionsArray[IdxCollision].Location.Y >= LocationYToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Min && AllCollisionsArray[IdxCollision].Location.Z > LocationZToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Max && AllCollisionsArray[IdxCollision].Location.Z < LocationZToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_MinMax && !(AllCollisionsArray[IdxCollision].Location.Z <= LocationZToSpawnMinMax.X || AllCollisionsArray[IdxCollision].Location.Z >= LocationZToSpawnMinMax.Y)))
			{
				continue;
			}

			FilteredAllCollisionsArray[IdxFilteredCollisions] = AllCollisionsArray[IdxCollision];

			IdxFilteredCollisions++;
		}
		FilteredAllCollisionsArray.SetNum(IdxFilteredCollisions);

		// If collisions were filtered copy FilteredAllCollisionsArray back into AllCollisions
		if (FilteredAllCollisionsArray.Num() != AllCollisionsArray.Num())
		{
			AllCollisionsArray.SetNumUninitialized(FilteredAllCollisionsArray.Num());
		}
		for (int32 Idx = 0; Idx < FilteredAllCollisionsArray.Num(); ++Idx)
		{
			AllCollisionsArray[Idx] = FilteredAllCollisionsArray[Idx];
		}

		INC_DWORD_STAT_BY(STAT_NiagaraNumFilteredAllCollisions, FilteredAllCollisionsArray.Num());
	}
}

void UNiagaraDataInterfaceChaosDestruction::SortCollisions(TArray<Chaos::TCollisionDataExt<float, 3>>& CollisionsArray)
{
	SCOPE_CYCLE_COUNTER(STAT_CollisionCallbackSorting);

	if (DataSortingType == EDataSortTypeEnum::ChaosNiagara_DataSortType_SortByMassMaxToMin)
	{
		CollisionsArray.Sort(UNiagaraDataInterfaceChaosDestruction::CollisionDataSortByMassPredicateMaxToMin);
	}
	else if (DataSortingType == EDataSortTypeEnum::ChaosNiagara_DataSortType_SortByMassMinToMax)
	{
		CollisionsArray.Sort(UNiagaraDataInterfaceChaosDestruction::CollisionDataSortByMassPredicateMinToMax);
	}
	else if (DataSortingType == EDataSortTypeEnum::ChaosNiagara_DataSortType_RandomShuffle)
	{
		CollisionsArray.Sort(UNiagaraDataInterfaceChaosDestruction::CollisionDataRandomShuffleSortPredicate);
	}
}

void ComputeHashTable(const TArray<Chaos::TCollisionDataExt<float, 3>>& CollisionsArray, const FBox& SpatialHashVolume, const FVector& SpatialHashVolumeCellSize, const uint32 NumberOfCellsX, const uint32 NumberOfCellsY, const uint32 NumberOfCellsZ, TMultiMap<uint32, int32>& HashTableMap)
{
	FVector CellSizeInv(1.f / SpatialHashVolumeCellSize.X, 1.f / SpatialHashVolumeCellSize.Y, 1.f / SpatialHashVolumeCellSize.Z);

	// Create a Hash Table, but only store the cells with constraint(s) as a map HashTableMap<CellIdx, ConstraintIdx>
	uint32 NumberOfCellsXY = NumberOfCellsX * NumberOfCellsY;
	uint32 NumberOfCellsXYZ = NumberOfCellsXY * NumberOfCellsZ;

	for (int32 IdxCollision = 0; IdxCollision < CollisionsArray.Num(); ++IdxCollision)
	{
		FVector Location = (FVector)CollisionsArray[IdxCollision].Location;
		if (SpatialHashVolume.IsInsideOrOn(Location))
		{
			Location -= SpatialHashVolume.Min;
			uint32 HashTableIdx = (uint32)(Location.X * CellSizeInv.X) +
								  (uint32)(Location.Y * CellSizeInv.Y) * NumberOfCellsX +
								  (uint32)(Location.Z * CellSizeInv.Z) * NumberOfCellsXY;
			if (ensure(HashTableIdx < NumberOfCellsXYZ))
			{
				HashTableMap.Add(HashTableIdx, IdxCollision);
			}
		}
	}
}

void UNiagaraDataInterfaceChaosDestruction::GetCollisionsToSpawnFromCollisions(TArray<Chaos::TCollisionDataExt<float, 3>>& AllCollisionsArray,
	TArray<Chaos::TCollisionDataExt<float, 3>>& CollisionsToSpawnArray)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GetCollisionsToSpawnFromCollisions);

	const float SpatialHasVolumeExtentMin = 100.f;
	const float SpatialHasVolumeExtentMax = 1e8;

	if (DoSpatialHash &&
		(SpatialHashVolumeMax.X - SpatialHashVolumeMin.X) > SpatialHasVolumeExtentMin &&
		(SpatialHashVolumeMax.Y - SpatialHashVolumeMin.Y) > SpatialHasVolumeExtentMin &&
		(SpatialHashVolumeMax.Z - SpatialHashVolumeMin.Z) > SpatialHasVolumeExtentMin &&
		(SpatialHashVolumeMax.X - SpatialHashVolumeMin.X) < SpatialHasVolumeExtentMax &&
		(SpatialHashVolumeMax.Y - SpatialHashVolumeMin.Y) < SpatialHasVolumeExtentMax &&
		(SpatialHashVolumeMax.Z - SpatialHashVolumeMin.Z) < SpatialHasVolumeExtentMax &&
		SpatialHashVolumeCellSize.X >= 1.f && SpatialHashVolumeCellSize.Y >= 1.f && SpatialHashVolumeCellSize.Z >= 1.f &&
		AllCollisionsArray.Num() > 1)
	{
		// Adjust SpatialHashVolumeMin, SpatialHashVolumeMin based on SpatialHashVolumeCellSize
		uint32 NumberOfCellsX = FMath::CeilToInt((SpatialHashVolumeMax.X - SpatialHashVolumeMin.X) / SpatialHashVolumeCellSize.X);
		uint32 NumberOfCellsY = FMath::CeilToInt((SpatialHashVolumeMax.Y - SpatialHashVolumeMin.Y) / SpatialHashVolumeCellSize.Y);
		uint32 NumberOfCellsZ = FMath::CeilToInt((SpatialHashVolumeMax.Z - SpatialHashVolumeMin.Z) / SpatialHashVolumeCellSize.Z);

		float dX = ((float)NumberOfCellsX * SpatialHashVolumeCellSize.X - (SpatialHashVolumeMax.X - SpatialHashVolumeMin.X)) / 2.f;
		SpatialHashVolumeMin.X -= dX; SpatialHashVolumeMax.X += dX;
		float dY = ((float)NumberOfCellsY * SpatialHashVolumeCellSize.Y - (SpatialHashVolumeMax.Y - SpatialHashVolumeMin.Y)) / 2.f;
		SpatialHashVolumeMin.Y -= dY; SpatialHashVolumeMax.Y += dY;
		float dZ = ((float)NumberOfCellsZ * SpatialHashVolumeCellSize.Z - (SpatialHashVolumeMax.Z - SpatialHashVolumeMin.Z)) / 2.f;
		SpatialHashVolumeMin.Z -= dZ; SpatialHashVolumeMax.Z += dZ;

		FBox SpatialHashVolume(SpatialHashVolumeMin, SpatialHashVolumeMax);

		// Spatial hash the collisions
		TMultiMap<uint32, int32> HashTableMap;
		ComputeHashTable(AllCollisionsArray, SpatialHashVolume, SpatialHashVolumeCellSize, NumberOfCellsX, NumberOfCellsY, NumberOfCellsZ, HashTableMap);

		TArray<uint32> UsedCellsArray;
		HashTableMap.GetKeys(UsedCellsArray);

		for (int32 IdxCell = 0; IdxCell < UsedCellsArray.Num(); ++IdxCell)
		{
			TArray<int32> CollisionsInCellArray;
			HashTableMap.MultiFind(UsedCellsArray[IdxCell], CollisionsInCellArray);

			int32 NumCollisionsToGetFromCell = FMath::Min(MaxDataPerCell, CollisionsInCellArray.Num());
			for (int32 IdxCollision = 0; IdxCollision < NumCollisionsToGetFromCell; ++IdxCollision)
			{
				CollisionsToSpawnArray.Add(AllCollisionsArray[CollisionsInCellArray[IdxCollision]]);
			}
		}

		// CollisionsToSpawnArray has too many elements
		if (CollisionsToSpawnArray.Num() > MaxNumberOfDataEntriesToSpawn)
		{
			TArray<Chaos::TCollisionDataExt<float, 3>> CollisionsArray1;

			float FInc = (float)CollisionsToSpawnArray.Num() / (float)MaxNumberOfDataEntriesToSpawn;

			CollisionsArray1.SetNumUninitialized(MaxNumberOfDataEntriesToSpawn);
			for (int32 IdxCollision = 0; IdxCollision < MaxNumberOfDataEntriesToSpawn; ++IdxCollision)
			{
				int32 NewIdx = FMath::FloorToInt((float)IdxCollision * FInc);
				CollisionsArray1[IdxCollision] = CollisionsToSpawnArray[NewIdx];
			}

			CollisionsToSpawnArray.SetNumUninitialized(MaxNumberOfDataEntriesToSpawn);
			for (int32 IdxCollision = 0; IdxCollision < MaxNumberOfDataEntriesToSpawn; ++IdxCollision)
			{
				CollisionsToSpawnArray[IdxCollision] = CollisionsArray1[IdxCollision];
			}
		}
	}
	else
	{
		if (AllCollisionsArray.Num() <= MaxNumberOfDataEntriesToSpawn)
		{
			CollisionsToSpawnArray.SetNumUninitialized(AllCollisionsArray.Num());
			for (int32 IdxCollision = 0; IdxCollision < AllCollisionsArray.Num(); ++IdxCollision)
			{
				CollisionsToSpawnArray[IdxCollision] = AllCollisionsArray[IdxCollision];
			}
		}
		else
		{
			float FInc = (float)AllCollisionsArray.Num() / (float)MaxNumberOfDataEntriesToSpawn;

			CollisionsToSpawnArray.SetNumUninitialized(MaxNumberOfDataEntriesToSpawn);
			for (int32 IdxCollision = 0; IdxCollision < MaxNumberOfDataEntriesToSpawn; ++IdxCollision)
			{
				int32 NewIdx = FMath::FloorToInt((float)IdxCollision * FInc);
				CollisionsToSpawnArray[IdxCollision] = AllCollisionsArray[NewIdx];
			}
		}
	}

	INC_DWORD_STAT_BY(STAT_NiagaraNumCollisionsToSpawnParticles, CollisionsToSpawnArray.Num());
}

int32 UNiagaraDataInterfaceChaosDestruction::SpawnParticlesFromCollision(FSolverData SolverData,
																		 Chaos::TCollisionDataExt<float, 3>& Collision,
																		 FNDIChaosDestruction_InstanceData* InstData,
																		 float TimeData_MapsCreated,
																		 int32 IdxSolver)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_SpawnParticlesFromCollision);
	float Latency = SolverData.Solver->GetSolverTime() - TimeData_MapsCreated;
	if (Latency <= MaxLatency)
	{
		int32 NumParticles = FMath::RandRange((int)SpawnMultiplierMinMax.X, (int)FMath::Max(SpawnMultiplierMinMax.X, SpawnMultiplierMinMax.Y));
		for (int32 Idx = 0; Idx < NumParticles; ++Idx)
		{
			if (FMath::FRand() > FMath::Clamp(SpawnChance, 0.f, 1.f))
			{
				continue;
			}

			// Compute random position
			float sign[2] = { -1.f, 1.f };
			float RandomX = sign[FMath::RandRange(0, 1)] * FMath::FRandRange(FMath::Min(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y), FMath::Max(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y));
			float RandomY = sign[FMath::RandRange(0, 1)] * FMath::FRandRange(FMath::Min(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y), FMath::Max(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y));
			float RandomZ = sign[FMath::RandRange(0, 1)] * FMath::FRandRange(FMath::Min(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y), FMath::Max(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y));
			FVector RandomPosition(RandomX, RandomY, RandomZ);

			// Compute velocity offset
			FVector VelocityOffset;
			VelocityOffset.X = FMath::FRandRange(FMath::Min(VelocityOffsetMin.X, VelocityOffsetMax.X), FMath::Max(VelocityOffsetMin.X, VelocityOffsetMax.X));
			VelocityOffset.Y = FMath::FRandRange(FMath::Min(VelocityOffsetMin.Y, VelocityOffsetMax.Y), FMath::Max(VelocityOffsetMin.Y, VelocityOffsetMax.Y));
			VelocityOffset.Z = FMath::FRandRange(FMath::Min(VelocityOffsetMin.Z, VelocityOffsetMax.Z), FMath::Max(VelocityOffsetMin.Z, VelocityOffsetMax.Z));

			// Compute random velocity using the selected Velocity Model
			FVector ParticleVelocity;
			if (RandomVelocityGenerationType == ERandomVelocityGenerationTypeEnum::ChaosNiagara_RandomVelocityGenerationType_RandomDistribution)
			{
				FVector RandomVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f));
				RandomVector.Normalize();

				ParticleVelocity = RandomVector * FMath::FRandRange(RandomVelocityMagnitudeMinMax.X, FMath::Max(RandomVelocityMagnitudeMinMax.X, RandomVelocityMagnitudeMinMax.Y));
			}
			else if (RandomVelocityGenerationType == ERandomVelocityGenerationTypeEnum::ChaosNiagara_RandomVelocityGenerationType_CollisionNormalBased)
			{
				FVector RandomVector = FMath::VRandCone(Collision.Normal, FMath::DegreesToRadians(SpreadAngleMax));
				RandomVector.Normalize();

				ParticleVelocity = RandomVector * FMath::FRandRange(RandomVelocityMagnitudeMinMax.X, FMath::Max(RandomVelocityMagnitudeMinMax.X, RandomVelocityMagnitudeMinMax.Y));
			}
//			else if (RandomVelocityGenerationType == ERandomVelocityGenerationTypeEnum::ChaosNiagara_RandomVelocityGenerationType_NRandomSpread)
//			{
//			}
			else if (RandomVelocityGenerationType == ERandomVelocityGenerationTypeEnum::ChaosNiagara_RandomVelocityGenerationType_RandomDistributionWithStreamers)
			{
				FVector RandomVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f));
				RandomVector.Normalize();

				ParticleVelocity = RandomVector * FMath::FRandRange(RandomVelocityMagnitudeMinMax.X, FMath::Max(RandomVelocityMagnitudeMinMax.X, RandomVelocityMagnitudeMinMax.Y));

				float RandomMultiplier = (FMath::FRand() < 0.2) ? 1.25f : 1.f;
				ParticleVelocity *= RandomMultiplier;
			}

			// Combine all velocity components
			FVector ComputedVelocity = (Collision.Velocity1 - Collision.Velocity2) * InheritedVelocityMultiplier + ParticleVelocity + VelocityOffset;

			// Clamp velocity
			if (FinalVelocityMagnitudeMinMax.X >= 0 && FinalVelocityMagnitudeMinMax.Y < 0)
			{
				if (ComputedVelocity.SizeSquared() < FinalVelocityMagnitudeMinMax.X * FinalVelocityMagnitudeMinMax.X)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.X;
				}
			}
			else if (FinalVelocityMagnitudeMinMax.X < 0 && FinalVelocityMagnitudeMinMax.Y >= 0)
			{
				if (ComputedVelocity.SizeSquared() > FinalVelocityMagnitudeMinMax.Y * FinalVelocityMagnitudeMinMax.Y)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.Y;
				}
			}
			else if (FinalVelocityMagnitudeMinMax.X >= 0 && FinalVelocityMagnitudeMinMax.Y >= 0)
			{
				if (ComputedVelocity.SizeSquared() < FinalVelocityMagnitudeMinMax.X * FinalVelocityMagnitudeMinMax.X)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.X;
				}
				else if (ComputedVelocity.SizeSquared() > FinalVelocityMagnitudeMinMax.Y * FinalVelocityMagnitudeMinMax.Y)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.Y;
				}
			}

			// Debug
			FVector ParticleColor = FVector::OneVector;
			if (DebugType == EDebugTypeEnum::ChaosNiagara_DebugType_ColorBySolver)
			{
				ParticleColor = ColorArray[IdxSolver % ColorArray.Num()];
			}
			else if (DebugType == EDebugTypeEnum::ChaosNiagara_DebugType_ColorByParticleIndex)
			{
				//ParticleColor = ColorArray[Collision.ParticleIndex % ColorArray.Num()];
			}

			// Store principal data
			InstData->PositionArray.Add(Collision.Location + RandomPosition);
			InstData->VelocityArray.Add(ComputedVelocity);
			InstData->SolverIDArray.Add(IdxSolver);

			// Store Collision data
			InstData->IncomingLocationArray.Add(Collision.Location);
			InstData->IncomingAccumulatedImpulseArray.Add(Collision.AccumulatedImpulse);
			InstData->IncomingNormalArray.Add(Collision.Normal);
			InstData->IncomingVelocity1Array.Add(Collision.Velocity1);
			InstData->IncomingVelocity2Array.Add(Collision.Velocity2);
			InstData->IncomingAngularVelocity1Array.Add(Collision.AngularVelocity1);
			InstData->IncomingAngularVelocity2Array.Add(Collision.AngularVelocity2);
			InstData->IncomingMass1Array.Add(Collision.Mass1);
			InstData->IncomingMass2Array.Add(Collision.Mass2);
			InstData->IncomingTimeArray.Add(TimeData_MapsCreated);

			// Store GeometryCollection data
			InstData->ExtentMinArray.Add(Collision.BoundingboxExtentMin);
			InstData->ExtentMaxArray.Add(Collision.BoundingboxExtentMax);
			InstData->VolumeArray.Add(Collision.BoundingboxVolume);
		}
	
		return NumParticles;
	}
	return 0;
}

bool UNiagaraDataInterfaceChaosDestruction::CollisionCallback(FNDIChaosDestruction_InstanceData* InstData)
{
	int32 IdxSolver = 0;
	for (FSolverData SolverData : Solvers)
	{
		if (SolverData.Solver->GetEventFilters()->IsCollisionEventEnabled() && CollisionEvents.Num() > 0 && SolverData.Solver->GetSolverTime() > 0.f && MaxNumberOfDataEntriesToSpawn > 0)
		{
			TArray<Chaos::TCollisionDataExt<float, 3>>& AllCollisionsArray = CollisionEvents;
			float TimeData_MapsCreated = 0.0f;

#if STATS
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_GatherMemoryStats);
				size_t SizeOfAllCollisions = sizeof(Chaos::TCollisionData<float, 3>) * AllCollisionsArray.Num();
				SET_MEMORY_STAT(STAT_AllCollisionsDataMemory, SizeOfAllCollisions);
			}

			INC_DWORD_STAT_BY(STAT_NiagaraNumAllCollisions, AllCollisionsArray.Num());
#endif // STATS


			if (AllCollisionsArray.Num() > 0)
			{
				// Filter AllCollisions
				// In case of filtering AllCollisions will be resized and filtered data will be copied back to AllCollisions
				FilterAllCollisions(AllCollisionsArray);

				// Sort AllCollisisons
				SortCollisions(AllCollisionsArray);

				// Get the collisions which will spawn particles
				TArray<Chaos::TCollisionDataExt<float, 3>> CollisionsToSpawnArray;

				GetCollisionsToSpawnFromCollisions(AllCollisionsArray, CollisionsToSpawnArray);

				// Spawn particles for collisions in CollisionsToSpawnArray
				for (int32 IdxCollision = 0; IdxCollision < CollisionsToSpawnArray.Num(); ++IdxCollision)
				{
					int32 NumParticlesSpawned = SpawnParticlesFromCollision(SolverData,
																			CollisionsToSpawnArray[IdxCollision],
																			InstData,
																			TimeData_MapsCreated,
																			IdxSolver);

					if (NumParticlesSpawned > 0)
					{
						// Get/Store Geometry/Physical Material data
						FLinearColor Color(1.f, 1.f, 1.f, 1.f);
						float Friction = 0.7f, Restitution = 0.3f, Density = 1.f;
						// #GM: Disable this for now for perf
						/*
						GetMesPhysicalData(SolverData,
							CollisionsToSpawnArray[IdxCollision].ParticleIndexMesh == INDEX_NONE ? CollisionsToSpawnArray[IdxCollision].ParticleIndex : CollisionsToSpawnArray[IdxCollision].ParticleIndexMesh,
							PhysicsProxyReverseMappingArray,
							ParticleIndexReverseMappingArray,
							Color,
							Friction,
							Restitution,
							Density);
						*/

						// jf: optimization: presize these arrays?
						for (int32 Idx = 0; Idx < NumParticlesSpawned; ++Idx)
						{
							InstData->SurfaceTypeArray.Add(CollisionsToSpawnArray[IdxCollision].SurfaceType);
							InstData->ColorArray.Add(Color);
							InstData->FrictionArray.Add(Friction);
							InstData->RestitutionArray.Add(Restitution);
							InstData->DensityArray.Add(Density);
						}
					}
				}
			}
		}

		IdxSolver++;
	}

	INC_DWORD_STAT_BY(STAT_NiagaraNumParticlesSpawnedFromCollisions, InstData->PositionArray.Num());

	return false;
}

void UNiagaraDataInterfaceChaosDestruction::HandleBreakingEvents(const Chaos::FBreakingEventData& Event)
{
	ensure(IsInGameThread());

	// Copy data from *AllBreakingData_Maps.AllBreakingData into AllBreakingsArray
	// Also get Boundingbox related data and SurfaceType and save it as well
	BreakingEvents.InsertZeroed(0, Event.BreakingData.AllBreakingsArray.Num());
	//UE_LOG(LogScript, Warning, TEXT("(*AllBreakingData_Maps.AllBreakingData).AllBreakingsArray.Num() = %d"), (*AllBreakingData_Maps.AllBreakingData).AllBreakingsArray.Num());
	int32 Idx = 0;
	for (Chaos::TBreakingDataExt<float, 3>& Breaking : BreakingEvents)
	{
		Breaking = Event.BreakingData.AllBreakingsArray[Idx];

		// #GM: Disable this for now for perf
		// TODO(mv): Temporarily re-enable this for Jon to get materials, transform and bounding box. Will optimize in the coming week. 
		if (bGetExternalBreakingData)
		{
			//Chaos::TRigidTransform<float, 3> Transform;
			//UPhysicalMaterial* Material = nullptr;
			//GetMeshExtData(SolverData,
			//	AllBreakingsArray[Idx].ParticleIndexMesh == INDEX_NONE ? AllBreakingsArray[Idx].ParticleIndex : AllBreakingsArray[Idx].ParticleIndexMesh,
			//	PhysicsProxyReverseMappingArray,
			//	ParticleIndexReverseMappingArray,
			//	AllBreakingsArray[Idx].BoundingboxVolume,
			//	AllBreakingsArray[Idx].BoundingboxExtentMin,
			//	AllBreakingsArray[Idx].BoundingboxExtentMax,
			//	AllBreakingsArray[Idx].BoundingBox,
			//	AllBreakingsArray[Idx].SurfaceType,
			//	Transform,
			//	Material);
			////UE_LOG(LogScript, Warning, TEXT("GetAllBreakingsAndMaps[%d]: SurfaceType = %d"), Idx, AllBreakingsArray[Idx].SurfaceType);
			//AllBreakingsArray[Idx].TransformTranslation = Transform.GetTranslation();
			//AllBreakingsArray[Idx].TransformRotation = Transform.GetRotation();
			//AllBreakingsArray[Idx].TransformScale = Transform.GetScale3D();
			//if (Material)
			//{
			//	AllBreakingsArray[Idx].PhysicalMaterialName = Material->GetFName();
			//}
			//else
			//{
			//	AllBreakingsArray[Idx].PhysicalMaterialName = FName();
			//}
		}
		else
		{
			BreakingEvents[Idx].BoundingboxVolume = 1000000.f;
			BreakingEvents[Idx].BoundingboxExtentMin = 100.0f;
			BreakingEvents[Idx].BoundingboxExtentMax = 100.0f;
			BreakingEvents[Idx].BoundingBox = FBox(FVector(-100.0f, -100.0f, -100.0f), FVector(100.0f, 100.0f, 100.0f));
			BreakingEvents[Idx].SurfaceType = 0;
			BreakingEvents[Idx].TransformTranslation = FVector(0.0f, 0.0f, 0.0f);
			BreakingEvents[Idx].TransformRotation = FQuat(0.0f, 0.0f, 0.0f, 1.0f);
			BreakingEvents[Idx].TransformScale = FVector(1.0f, 1.0f, 1.0f);
			BreakingEvents[Idx].PhysicalMaterialName = FName();
		}

		Idx++;
	}
}


void UNiagaraDataInterfaceChaosDestruction::FilterAllBreakings(TArray<Chaos::TBreakingDataExt<float, 3>>& AllBreakingsArray)
{
	if (bApplyMaterialsFilter || 
	//	ParticleIndexToProcess != -1 ||
		SpeedToSpawnMinMax.X > 0.f ||
		SpeedToSpawnMinMax.Y > 0.f ||
		MassToSpawnMinMax.X > 0.f ||
		MassToSpawnMinMax.Y > 0.f ||
		ExtentMinToSpawnMinMax.X > 0.f ||
		ExtentMinToSpawnMinMax.Y > 0.f ||
		ExtentMaxToSpawnMinMax.X > 0.f ||
		ExtentMaxToSpawnMinMax.Y > 0.f ||
		VolumeToSpawnMinMax.X > 0.f ||
		VolumeToSpawnMinMax.Y > 0.f ||
		SolverTimeToSpawnMinMax.X > 0.f ||
		SolverTimeToSpawnMinMax.Y > 0.f ||
		SurfaceTypeToSpawn != -1 ||
		LocationXToSpawn != ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_None ||
		LocationYToSpawn != ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_None ||
		LocationZToSpawn != ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_None)
	{ 
		TArray<Chaos::TBreakingDataExt<float, 3>> FilteredAllBreakingsArray;
		FilteredAllBreakingsArray.SetNumUninitialized(AllBreakingsArray.Num());

		int32 IdxFilteredBreakings = 0;

		float MinSpeedToSpawnSquared = SpeedToSpawnMinMax.X * SpeedToSpawnMinMax.X;
		float MaxSpeedToSpawnSquared = SpeedToSpawnMinMax.Y * SpeedToSpawnMinMax.Y;

		auto IsMaterialInFilter = [&](const FName& InMaterialName) {
			if (!InMaterialName.IsValid())
			{
				return false;
			}

			for (const UPhysicalMaterial* Material : ChaosBreakingMaterialSet)
			{
				if (!Material)
				{
					continue;
				}

				if (Material->GetFName() == InMaterialName)
				{
					return true;
				}
			}

			return false;
		};

		for (int32 IdxBreaking = 0; IdxBreaking < AllBreakingsArray.Num(); ++IdxBreaking)
		{
			float BreakingSpeedSquared = AllBreakingsArray[IdxBreaking].Velocity.SizeSquared();

			if (!(bApplyMaterialsFilter && IsMaterialInFilter(AllBreakingsArray[IdxBreaking].PhysicalMaterialName)) ||
				//(ParticleIndexToProcess != -1 && AllBreakingsArray[IdxBreaking].ParticleIndex != ParticleIndexToProcess) ||
				(SpeedToSpawnMinMax.X > 0.f && SpeedToSpawnMinMax.Y < 0.f && BreakingSpeedSquared < MinSpeedToSpawnSquared) ||
				(SpeedToSpawnMinMax.X < 0.f && SpeedToSpawnMinMax.Y > 0.f && BreakingSpeedSquared > MaxSpeedToSpawnSquared) ||
				(SpeedToSpawnMinMax.X > 0.f && SpeedToSpawnMinMax.Y > 0.f && (BreakingSpeedSquared < MinSpeedToSpawnSquared || BreakingSpeedSquared > MaxSpeedToSpawnSquared)) ||
				(MassToSpawnMinMax.X > 0.f && MassToSpawnMinMax.Y < 0.f && AllBreakingsArray[IdxBreaking].Mass < MassToSpawnMinMax.X) ||
				(MassToSpawnMinMax.X < 0.f && MassToSpawnMinMax.Y > 0.f && AllBreakingsArray[IdxBreaking].Mass > MassToSpawnMinMax.Y) ||
				(MassToSpawnMinMax.X > 0.f && MassToSpawnMinMax.Y > 0.f && (AllBreakingsArray[IdxBreaking].Mass < MassToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].Mass > MassToSpawnMinMax.Y)) ||
				(ExtentMinToSpawnMinMax.X > 0.f && ExtentMinToSpawnMinMax.Y < 0.f && AllBreakingsArray[IdxBreaking].BoundingboxExtentMin < ExtentMinToSpawnMinMax.X) ||
				(ExtentMinToSpawnMinMax.X < 0.f && ExtentMinToSpawnMinMax.Y > 0.f && AllBreakingsArray[IdxBreaking].BoundingboxExtentMin > ExtentMinToSpawnMinMax.Y) ||
				(ExtentMinToSpawnMinMax.X > 0.f && ExtentMinToSpawnMinMax.Y > 0.f && (AllBreakingsArray[IdxBreaking].BoundingboxExtentMin < ExtentMinToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].BoundingboxExtentMin > ExtentMinToSpawnMinMax.Y)) ||
				(ExtentMaxToSpawnMinMax.X > 0.f && ExtentMaxToSpawnMinMax.Y < 0.f && AllBreakingsArray[IdxBreaking].BoundingboxExtentMax < ExtentMaxToSpawnMinMax.X) ||
				(ExtentMaxToSpawnMinMax.X < 0.f && ExtentMaxToSpawnMinMax.Y > 0.f && AllBreakingsArray[IdxBreaking].BoundingboxExtentMax > ExtentMaxToSpawnMinMax.Y) ||
				(ExtentMaxToSpawnMinMax.X > 0.f && ExtentMaxToSpawnMinMax.Y > 0.f && (AllBreakingsArray[IdxBreaking].BoundingboxExtentMax < ExtentMaxToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].BoundingboxExtentMax > ExtentMaxToSpawnMinMax.Y)) ||
				(VolumeToSpawnMinMax.X > 0.f && VolumeToSpawnMinMax.Y < 0.f && AllBreakingsArray[IdxBreaking].BoundingboxVolume < VolumeToSpawnMinMax.X) ||
				(VolumeToSpawnMinMax.X < 0.f && VolumeToSpawnMinMax.Y > 0.f && AllBreakingsArray[IdxBreaking].BoundingboxVolume > VolumeToSpawnMinMax.Y) ||
				(VolumeToSpawnMinMax.X > 0.f && VolumeToSpawnMinMax.Y > 0.f && (AllBreakingsArray[IdxBreaking].BoundingboxVolume < VolumeToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].BoundingboxVolume > VolumeToSpawnMinMax.Y)) ||
				(SolverTimeToSpawnMinMax.X > 0.f && SolverTimeToSpawnMinMax.Y < 0.f && SolverTime < SolverTimeToSpawnMinMax.X) ||
				(SolverTimeToSpawnMinMax.X < 0.f && SolverTimeToSpawnMinMax.Y > 0.f && SolverTime > SolverTimeToSpawnMinMax.Y) ||
				(SolverTimeToSpawnMinMax.X > 0.f && SolverTimeToSpawnMinMax.Y > 0.f && (SolverTime < SolverTimeToSpawnMinMax.X || SolverTime > SolverTimeToSpawnMinMax.Y)) ||
				(SurfaceTypeToSpawn != -1 && AllBreakingsArray[IdxBreaking].SurfaceType != SurfaceTypeToSpawn) ||				
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Min && AllBreakingsArray[IdxBreaking].Location.X <= LocationXToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Max && AllBreakingsArray[IdxBreaking].Location.X >= LocationXToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_MinMax && (AllBreakingsArray[IdxBreaking].Location.X <= LocationXToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].Location.X >= LocationXToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Min && AllBreakingsArray[IdxBreaking].Location.Y <= LocationYToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Max && AllBreakingsArray[IdxBreaking].Location.Y >= LocationYToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_MinMax && (AllBreakingsArray[IdxBreaking].Location.Y <= LocationYToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].Location.Y >= LocationYToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Min && AllBreakingsArray[IdxBreaking].Location.Z <= LocationZToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Max && AllBreakingsArray[IdxBreaking].Location.Z >= LocationZToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_MinMax && (AllBreakingsArray[IdxBreaking].Location.Z <= LocationZToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].Location.Z >= LocationZToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Min && AllBreakingsArray[IdxBreaking].Location.X > LocationXToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Max && AllBreakingsArray[IdxBreaking].Location.X < LocationXToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_MinMax && !(AllBreakingsArray[IdxBreaking].Location.X <= LocationXToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].Location.X >= LocationXToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Min && AllBreakingsArray[IdxBreaking].Location.Y > LocationYToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Max && AllBreakingsArray[IdxBreaking].Location.Y < LocationYToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_MinMax && !(AllBreakingsArray[IdxBreaking].Location.Y <= LocationYToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].Location.Y >= LocationYToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Min && AllBreakingsArray[IdxBreaking].Location.Z > LocationZToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Max && AllBreakingsArray[IdxBreaking].Location.Z < LocationZToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_MinMax && !(AllBreakingsArray[IdxBreaking].Location.Z <= LocationZToSpawnMinMax.X || AllBreakingsArray[IdxBreaking].Location.Z >= LocationZToSpawnMinMax.Y)))
			{
				continue;
			}

			FilteredAllBreakingsArray[IdxFilteredBreakings] = AllBreakingsArray[IdxBreaking];

			IdxFilteredBreakings++;
		}
		FilteredAllBreakingsArray.SetNum(IdxFilteredBreakings);

		// If Breakings were filtered copy FilteredAllBreakingsArray back into AllBreakings
		if (FilteredAllBreakingsArray.Num() != AllBreakingsArray.Num())
		{
			AllBreakingsArray.SetNumUninitialized(FilteredAllBreakingsArray.Num());
		}
		for (int32 Idx = 0; Idx < FilteredAllBreakingsArray.Num(); ++Idx)
		{
			AllBreakingsArray[Idx] = FilteredAllBreakingsArray[Idx];
		}

		INC_DWORD_STAT_BY(STAT_NiagaraNumFilteredAllBreakings, FilteredAllBreakingsArray.Num());
	}
}

void UNiagaraDataInterfaceChaosDestruction::SortBreakings(TArray<Chaos::TBreakingDataExt<float, 3>>& BreakingsArray)
{
	SCOPE_CYCLE_COUNTER(STAT_BreakingCallbackSorting);

	if (DataSortingType == EDataSortTypeEnum::ChaosNiagara_DataSortType_SortByMassMaxToMin)
	{
		BreakingsArray.Sort(UNiagaraDataInterfaceChaosDestruction::BreakingDataSortByMassPredicateMaxToMin);
	}
	else if (DataSortingType == EDataSortTypeEnum::ChaosNiagara_DataSortType_SortByMassMinToMax)
	{
		BreakingsArray.Sort(UNiagaraDataInterfaceChaosDestruction::BreakingDataSortByMassPredicateMinToMax);
	}
	else if (DataSortingType == EDataSortTypeEnum::ChaosNiagara_DataSortType_RandomShuffle)
	{
		BreakingsArray.Sort(UNiagaraDataInterfaceChaosDestruction::BreakingDataRandomShuffleSortPredicate);
	}
}

void ComputeHashTable(const TArray<Chaos::TBreakingDataExt<float, 3>>& BreakingsArray, const FBox& SpatialHashVolume, const FVector& SpatialHashVolumeCellSize, const uint32 NumberOfCellsX, const uint32 NumberOfCellsY, const uint32 NumberOfCellsZ, TMultiMap<uint32, int32>& HashTableMap)
{
	FVector CellSizeInv(1.f / SpatialHashVolumeCellSize.X, 1.f / SpatialHashVolumeCellSize.Y, 1.f / SpatialHashVolumeCellSize.Z);

	// Create a Hash Table, but only store the cells with constraint(s) as a map HashTableMap<CellIdx, BreakingIdx>
	uint32 NumberOfCellsXY = NumberOfCellsX * NumberOfCellsY;
	uint32 NumberOfCellsXYZ = NumberOfCellsXY * NumberOfCellsZ;

	for (int32 IdxBreaking = 0; IdxBreaking < BreakingsArray.Num(); ++IdxBreaking)
	{
		FVector Location = (FVector)BreakingsArray[IdxBreaking].Location;
		if (SpatialHashVolume.IsInsideOrOn(Location))
		{
			Location -= SpatialHashVolume.Min;
			uint32 HashTableIdx = (uint32)(Location.X * CellSizeInv.X) +
								  (uint32)(Location.Y * CellSizeInv.Y) * NumberOfCellsX +
								  (uint32)(Location.Z * CellSizeInv.Z) * NumberOfCellsXY;
			if (ensure(HashTableIdx < NumberOfCellsXYZ))
			{
				HashTableMap.Add(HashTableIdx, IdxBreaking);
			}
		}
	}
}

void UNiagaraDataInterfaceChaosDestruction::GetBreakingsToSpawnFromBreakings(TArray<Chaos::TBreakingDataExt<float, 3>>& AllBreakingsArray,
																			 TArray<Chaos::TBreakingDataExt<float, 3>>& BreakingsToSpawnArray)
{
	const float SpatialHasVolumeExtentMin = 100.f;
	const float SpatialHasVolumeExtentMax = 1e8;

	if (DoSpatialHash &&
		(SpatialHashVolumeMax.X - SpatialHashVolumeMin.X) > SpatialHasVolumeExtentMin &&
		(SpatialHashVolumeMax.Y - SpatialHashVolumeMin.Y) > SpatialHasVolumeExtentMin &&
		(SpatialHashVolumeMax.Z - SpatialHashVolumeMin.Z) > SpatialHasVolumeExtentMin &&
		(SpatialHashVolumeMax.X - SpatialHashVolumeMin.X) < SpatialHasVolumeExtentMax &&
		(SpatialHashVolumeMax.Y - SpatialHashVolumeMin.Y) < SpatialHasVolumeExtentMax &&
		(SpatialHashVolumeMax.Z - SpatialHashVolumeMin.Z) < SpatialHasVolumeExtentMax &&
		SpatialHashVolumeCellSize.X >= 1.f && SpatialHashVolumeCellSize.Y >= 1.f && SpatialHashVolumeCellSize.Z >= 1.f &&
		AllBreakingsArray.Num() > 1)
	{
		// Adjust SpatialHashVolumeMin, SpatialHashVolumeMin based on SpatialHashVolumeCellSize
		uint32 NumberOfCellsX = FMath::CeilToInt((SpatialHashVolumeMax.X - SpatialHashVolumeMin.X) / SpatialHashVolumeCellSize.X);
		uint32 NumberOfCellsY = FMath::CeilToInt((SpatialHashVolumeMax.Y - SpatialHashVolumeMin.Y) / SpatialHashVolumeCellSize.Y);
		uint32 NumberOfCellsZ = FMath::CeilToInt((SpatialHashVolumeMax.Z - SpatialHashVolumeMin.Z) / SpatialHashVolumeCellSize.Z);

		float dX = ((float)NumberOfCellsX * SpatialHashVolumeCellSize.X - (SpatialHashVolumeMax.X - SpatialHashVolumeMin.X)) / 2.f;
		SpatialHashVolumeMin.X -= dX; SpatialHashVolumeMax.X += dX;
		float dY = ((float)NumberOfCellsY * SpatialHashVolumeCellSize.Y - (SpatialHashVolumeMax.Y - SpatialHashVolumeMin.Y)) / 2.f;
		SpatialHashVolumeMin.Y -= dY; SpatialHashVolumeMax.Y += dY;
		float dZ = ((float)NumberOfCellsZ * SpatialHashVolumeCellSize.Z - (SpatialHashVolumeMax.Z - SpatialHashVolumeMin.Z)) / 2.f;
		SpatialHashVolumeMin.Z -= dZ; SpatialHashVolumeMax.Z += dZ;

		FBox SpatialHashVolume(SpatialHashVolumeMin, SpatialHashVolumeMax);

		// Spatial hash the Breakings
		TMultiMap<uint32, int32> HashTableMap;
		ComputeHashTable(AllBreakingsArray, SpatialHashVolume, SpatialHashVolumeCellSize, NumberOfCellsX, NumberOfCellsY, NumberOfCellsZ, HashTableMap);

		TArray<uint32> UsedCellsArray;
		HashTableMap.GetKeys(UsedCellsArray);

		for (int32 IdxCell = 0; IdxCell < UsedCellsArray.Num(); ++IdxCell)
		{
			TArray<int32> BreakingsInCellArray;
			HashTableMap.MultiFind(UsedCellsArray[IdxCell], BreakingsInCellArray);

			int32 NumBreakingsToGetFromCell = FMath::Min(MaxDataPerCell, BreakingsInCellArray.Num());
			for (int32 IdxBreaking = 0; IdxBreaking < NumBreakingsToGetFromCell; ++IdxBreaking)
			{
				BreakingsToSpawnArray.Add(AllBreakingsArray[BreakingsInCellArray[IdxBreaking]]);
			}
		}

		// BreakingsToSpawnArray has too many elements
		if (BreakingsToSpawnArray.Num() > MaxNumberOfDataEntriesToSpawn)
		{
			TArray<Chaos::TBreakingDataExt<float, 3>> BreakingsArray1;

			float FInc = (float)BreakingsToSpawnArray.Num() / (float)MaxNumberOfDataEntriesToSpawn;

			BreakingsArray1.SetNumUninitialized(MaxNumberOfDataEntriesToSpawn);
			for (int32 IdxBreaking = 0; IdxBreaking < MaxNumberOfDataEntriesToSpawn; ++IdxBreaking)
			{
				int32 NewIdx = FMath::FloorToInt((float)IdxBreaking * FInc);
				BreakingsArray1[IdxBreaking] = BreakingsToSpawnArray[NewIdx];
			}

			BreakingsToSpawnArray.SetNumUninitialized(MaxNumberOfDataEntriesToSpawn);
			for (int32 IdxBreaking = 0; IdxBreaking < MaxNumberOfDataEntriesToSpawn; ++IdxBreaking)
			{
				BreakingsToSpawnArray[IdxBreaking] = BreakingsArray1[IdxBreaking];
			}
		}
	}
	else
	{
		if (AllBreakingsArray.Num() <= MaxNumberOfDataEntriesToSpawn)
		{
			BreakingsToSpawnArray.SetNumUninitialized(AllBreakingsArray.Num());
			for (int32 IdxBreaking = 0; IdxBreaking < AllBreakingsArray.Num(); ++IdxBreaking)
			{
				BreakingsToSpawnArray[IdxBreaking] = AllBreakingsArray[IdxBreaking];
			}
		}
		else
		{
			float FInc = (float)AllBreakingsArray.Num() / (float)MaxNumberOfDataEntriesToSpawn;

			BreakingsToSpawnArray.SetNumUninitialized(MaxNumberOfDataEntriesToSpawn);
			for (int32 IdxBreaking = 0; IdxBreaking < MaxNumberOfDataEntriesToSpawn; ++IdxBreaking)
			{
				int32 NewIdx = FMath::FloorToInt((float)IdxBreaking * FInc);
				BreakingsToSpawnArray[IdxBreaking] = AllBreakingsArray[NewIdx];
			}
		}
	}

	INC_DWORD_STAT_BY(STAT_NiagaraNumBreakingsToSpawnParticles, BreakingsToSpawnArray.Num());
}

int32 UNiagaraDataInterfaceChaosDestruction::SpawnParticlesFromBreaking(FSolverData SolverData,
																		Chaos::TBreakingDataExt<float, 3>& Breaking,
																		FNDIChaosDestruction_InstanceData* InstData,
																		float TimeData_MapsCreated,
																		int32 IdxSolver)
{
	float Latency = SolverData.Solver->GetSolverTime() - TimeData_MapsCreated;
	if (Latency <= MaxLatency)
	{
		int32 NumParticles = FMath::RandRange((int)SpawnMultiplierMinMax.X, (int)FMath::Max(SpawnMultiplierMinMax.X, SpawnMultiplierMinMax.Y));
		for (int32 Idx = 0; Idx < NumParticles; ++Idx)
		{
			if (FMath::FRand() > FMath::Clamp(SpawnChance, 0.f, 1.f))
			{
				continue;
			}

			// Compute random position
			float sign[2] = { -1.f, 1.f };
			float RandomX = sign[FMath::RandRange(0, 1)] * FMath::FRandRange(FMath::Min(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y), FMath::Max(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y));
			float RandomY = sign[FMath::RandRange(0, 1)] * FMath::FRandRange(FMath::Min(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y), FMath::Max(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y));
			float RandomZ = sign[FMath::RandRange(0, 1)] * FMath::FRandRange(FMath::Min(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y), FMath::Max(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y));
			FVector RandomPosition(RandomX, RandomY, RandomZ);

			// Compute velocity offset
			FVector VelocityOffset;
			VelocityOffset.X = FMath::FRandRange(FMath::Min(VelocityOffsetMin.X, VelocityOffsetMax.X), FMath::Max(VelocityOffsetMin.X, VelocityOffsetMax.X));
			VelocityOffset.Y = FMath::FRandRange(FMath::Min(VelocityOffsetMin.Y, VelocityOffsetMax.Y), FMath::Max(VelocityOffsetMin.Y, VelocityOffsetMax.Y));
			VelocityOffset.Z = FMath::FRandRange(FMath::Min(VelocityOffsetMin.Z, VelocityOffsetMax.Z), FMath::Max(VelocityOffsetMin.Z, VelocityOffsetMax.Z));

			// Compute random velocity using the selected Velocity Model
			FVector ParticleVelocity;
			if (RandomVelocityGenerationType == ERandomVelocityGenerationTypeEnum::ChaosNiagara_RandomVelocityGenerationType_RandomDistribution)
			{
				FVector RandomVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f));
				RandomVector.Normalize();

				ParticleVelocity = RandomVector * FMath::FRandRange(RandomVelocityMagnitudeMinMax.X, FMath::Max(RandomVelocityMagnitudeMinMax.X, RandomVelocityMagnitudeMinMax.Y));
			}
			else if (RandomVelocityGenerationType == ERandomVelocityGenerationTypeEnum::ChaosNiagara_RandomVelocityGenerationType_RandomDistributionWithStreamers)
			{
				FVector RandomVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f));
				RandomVector.Normalize();

				ParticleVelocity = RandomVector * FMath::FRandRange(RandomVelocityMagnitudeMinMax.X, FMath::Max(RandomVelocityMagnitudeMinMax.X, RandomVelocityMagnitudeMinMax.Y));

				float RandomMultiplier = (FMath::FRand() < 0.2) ? 1.25f : 1.f;
				ParticleVelocity *= RandomMultiplier;
			}

			// Combine all velocity components
			FVector ComputedVelocity = Breaking.Velocity * InheritedVelocityMultiplier + ParticleVelocity + VelocityOffset;

			// Clamp velocity
			if (FinalVelocityMagnitudeMinMax.X >= 0 && FinalVelocityMagnitudeMinMax.Y < 0)
			{
				if (ComputedVelocity.SizeSquared() < FinalVelocityMagnitudeMinMax.X * FinalVelocityMagnitudeMinMax.X)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.X;
				}
			}
			else if (FinalVelocityMagnitudeMinMax.X < 0 && FinalVelocityMagnitudeMinMax.Y >= 0)
			{
				if (ComputedVelocity.SizeSquared() > FinalVelocityMagnitudeMinMax.Y * FinalVelocityMagnitudeMinMax.Y)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.Y;
				}
			}
			else if (FinalVelocityMagnitudeMinMax.X >= 0 && FinalVelocityMagnitudeMinMax.Y >= 0)
			{
				if (ComputedVelocity.SizeSquared() < FinalVelocityMagnitudeMinMax.X * FinalVelocityMagnitudeMinMax.X)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.X;
				}
				else if (ComputedVelocity.SizeSquared() > FinalVelocityMagnitudeMinMax.Y * FinalVelocityMagnitudeMinMax.Y)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.Y;
				}
			}

			// Debug
			FVector ParticleColor = FVector::OneVector;
			if (DebugType == EDebugTypeEnum::ChaosNiagara_DebugType_ColorBySolver)
			{
				ParticleColor = ColorArray[IdxSolver % ColorArray.Num()];
			}
			else if (DebugType == EDebugTypeEnum::ChaosNiagara_DebugType_ColorByParticleIndex)
			{
				ParticleColor = ColorArray[Breaking.ParticleIndex % ColorArray.Num()];
			}

			// Store principal data
			InstData->PositionArray.Add(Breaking.Location + RandomPosition);
			InstData->VelocityArray.Add(ComputedVelocity);
			InstData->SolverIDArray.Add(IdxSolver);

			// Store Breaking data
			InstData->IncomingLocationArray.Add(Breaking.Location);
			InstData->IncomingVelocity1Array.Add(Breaking.Velocity);
			InstData->IncomingAngularVelocity1Array.Add(Breaking.AngularVelocity);
			InstData->IncomingMass1Array.Add(Breaking.Mass);
			InstData->IncomingTimeArray.Add(TimeData_MapsCreated);

			// Store GeometryCollection data
			InstData->ExtentMinArray.Add(Breaking.BoundingboxExtentMin);
			InstData->ExtentMaxArray.Add(Breaking.BoundingboxExtentMax);
			InstData->VolumeArray.Add(Breaking.BoundingboxVolume);
			InstData->BoundsArray.Add(Breaking.BoundingBox.Max - Breaking.BoundingBox.Min);

			// Set not related to default
			InstData->IncomingAccumulatedImpulseArray.Add(FVector(ForceInitToZero));
			InstData->IncomingNormalArray.Add(FVector(ForceInitToZero));
			InstData->IncomingVelocity2Array.Add(FVector(ForceInitToZero));
			InstData->IncomingAngularVelocity2Array.Add(FVector(ForceInitToZero));
			InstData->IncomingMass2Array.Add(0.f);
		}

		return NumParticles;
	}

	return 0;
}

bool UNiagaraDataInterfaceChaosDestruction::BreakingCallback(FNDIChaosDestruction_InstanceData* InstData)
{
	int32 IdxSolver = 0;
	for (FSolverData SolverData : Solvers)
	{
		if (SolverData.Solver->GetEventFilters()->IsBreakingEventEnabled() && BreakingEvents.Num() > 0 && SolverData.Solver->GetSolverTime() > 0.f && MaxNumberOfDataEntriesToSpawn > 0)
		{
			TArray<Chaos::TBreakingDataExt<float, 3>>& AllBreakingsArray = BreakingEvents;
			TArray<PhysicsProxyWrapper> PhysicsProxyReverseMappingArray;
			TArray<int32> ParticleIndexReverseMappingArray;
			TMap<IPhysicsProxyBase*, TArray<int32>> AllBreakingsIndicesByPhysicsProxyMap;
			float TimeData_MapsCreated = 0.0f;

			{
				size_t SizeOfAllBreakings = sizeof(Chaos::TBreakingData<float, 3>) * AllBreakingsArray.Num();
				size_t SizeOfPhysicsProxyReverseMapping = sizeof(PhysicsProxyWrapper) * PhysicsProxyReverseMappingArray.Num();
				size_t SizeOfParticleIndexReverseMapping = sizeof(int32) * ParticleIndexReverseMappingArray.Num();

				size_t SizeOfAllBreakingsIndicesByPhysicsProxy = 0;
				for (auto& Elem : AllBreakingsIndicesByPhysicsProxyMap)
				{
					SizeOfAllBreakingsIndicesByPhysicsProxy += sizeof(int32) * (Elem.Value).Num();
				}
				SET_MEMORY_STAT(STAT_AllBreakingsDataMemory, SizeOfAllBreakings);
				SET_MEMORY_STAT(STAT_PhysicsProxyReverseMappingMemory, SizeOfPhysicsProxyReverseMapping);
				SET_MEMORY_STAT(STAT_ParticleIndexReverseMappingMemory, SizeOfParticleIndexReverseMapping);
				SET_MEMORY_STAT(STAT_AllBreakingsIndicesByPhysicsProxyMemory, SizeOfAllBreakingsIndicesByPhysicsProxy);
			}

			INC_DWORD_STAT_BY(STAT_NiagaraNumAllBreakings, AllBreakingsArray.Num());

			if (AllBreakingsArray.Num() > 0)
			{
				// Filter AllBreakings
				// In case of filtering AllBreakings will be resized and filtered data will be copied back to AllBreakings
				FilterAllBreakings(AllBreakingsArray);

				// Sort AllCollisisons
				SortBreakings(AllBreakingsArray);

				// Get the Breakings which will spawn particles
				TArray<Chaos::TBreakingDataExt<float, 3>> BreakingsToSpawnArray;

				GetBreakingsToSpawnFromBreakings(AllBreakingsArray, BreakingsToSpawnArray);

				// Spawn particles for Breakings in BreakingsToSpawnArray
				for (int32 IdxBreaking = 0; IdxBreaking < BreakingsToSpawnArray.Num(); ++IdxBreaking)
				{
					int32 NumParticlesSpawned = SpawnParticlesFromBreaking(SolverData,
						BreakingsToSpawnArray[IdxBreaking],
						InstData,
						TimeData_MapsCreated,
						IdxSolver);

					//UE_LOG(LogScript, Warning, TEXT("IdxBreaking = %d/%d, NumParticlesSpawned = %d"), IdxBreaking, BreakingsToSpawnArray.Num()-1, NumParticlesSpawned);
					if (NumParticlesSpawned > 0)
					{
						// Get/Store Geometry/Physical Material data
						
						FLinearColor Color(1.f, 1.f, 1.f, 1.f);
						float Friction = 0.7f, Restitution = 0.3f, Density = 1.f;
						if (bGetExternalBreakingData)
						{
							// #GM: Disable this for now for perf
							GetMesPhysicalData(SolverData,
											   BreakingsToSpawnArray[IdxBreaking].ParticleIndexMesh == INDEX_NONE ? BreakingsToSpawnArray[IdxBreaking].ParticleIndex : BreakingsToSpawnArray[IdxBreaking].ParticleIndexMesh,
											   PhysicsProxyReverseMappingArray,
											   ParticleIndexReverseMappingArray,
											   Color,
											   Friction,
											   Restitution,
											   Density);
						}
						
						for (int32 Idx = 0; Idx < NumParticlesSpawned; ++Idx)
						{
							InstData->SurfaceTypeArray.Add(BreakingsToSpawnArray[IdxBreaking].SurfaceType);
							InstData->TransformTranslationArray.Add(BreakingsToSpawnArray[IdxBreaking].TransformTranslation);
							InstData->TransformRotationArray.Add(BreakingsToSpawnArray[IdxBreaking].TransformRotation);
							InstData->TransformScaleArray.Add(BreakingsToSpawnArray[IdxBreaking].TransformScale);
							InstData->ColorArray.Add(Color);
							InstData->FrictionArray.Add(Friction);
							InstData->RestitutionArray.Add(Restitution);
							InstData->DensityArray.Add(Density);
						}
					}
				}
			}
		}

		IdxSolver++;
	}

	INC_DWORD_STAT_BY(STAT_NiagaraNumParticlesSpawnedFromBreakings, InstData->PositionArray.Num());

	return false;

}

void UNiagaraDataInterfaceChaosDestruction::HandleTrailingEvents(const Chaos::FTrailingEventData& Event)
{
	ensure(IsInGameThread());

	// Copy data from *AllTrailingData_Maps.AllTrailingData into AllTrailingsArray
	// Also get Boundingbox related data and SurfaceType and save it as well
	TrailingEvents.AddUninitialized(Event.TrailingData.AllTrailingsArray.Num());

	int32 Idx = 0;
	for (Chaos::TTrailingDataExt<float, 3>& Trailing : TrailingEvents)
	{
		Trailing = Event.TrailingData.AllTrailingsArray[Idx];

		// #GM: Disable this for now for perf
		/*
		GetMeshExtData(SolverData,
			AllTrailingsArray[Idx].ParticleIndexMesh == INDEX_NONE ? AllTrailingsArray[Idx].ParticleIndex : AllTrailingsArray[Idx].ParticleIndexMesh,
			PhysicsProxyReverseMappingArray,
			ParticleIndexReverseMappingArray,
			AllTrailingsArray[Idx].BoundingboxVolume,
			AllTrailingsArray[Idx].BoundingboxExtentMin,
			AllTrailingsArray[Idx].BoundingboxExtentMax,
			AllTrailingsArray[Idx].SurfaceType);
		*/
		TrailingEvents[Idx].BoundingboxVolume = 1000000.f;
		TrailingEvents[Idx].BoundingboxExtentMin = 100.f;
		TrailingEvents[Idx].BoundingboxExtentMax = 100.f;
		TrailingEvents[Idx].SurfaceType = 0;

		Idx++;
	}
}

void UNiagaraDataInterfaceChaosDestruction::FilterAllTrailings(TArray<Chaos::TTrailingDataExt<float, 3>>& AllTrailingsArray)
{
	if (/*ParticleIndexToProcess != -1 ||*/
		SpeedToSpawnMinMax.X > 0.f ||
		SpeedToSpawnMinMax.Y > 0.f ||
		MassToSpawnMinMax.X > 0.f ||
		MassToSpawnMinMax.Y > 0.f ||
		ExtentMinToSpawnMinMax.X > 0.f ||
		ExtentMinToSpawnMinMax.Y > 0.f ||
		ExtentMaxToSpawnMinMax.X > 0.f ||
		ExtentMaxToSpawnMinMax.Y > 0.f ||
		VolumeToSpawnMinMax.X > 0.f ||
		VolumeToSpawnMinMax.Y > 0.f ||
		SolverTimeToSpawnMinMax.X > 0.f ||
		SolverTimeToSpawnMinMax.Y > 0.f ||
		SurfaceTypeToSpawn != -1 ||
		LocationXToSpawn != ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_None ||
		LocationYToSpawn != ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_None ||
		LocationZToSpawn != ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_None)
	{
		TArray<Chaos::TTrailingDataExt<float, 3>> FilteredAllTrailingsArray;
		FilteredAllTrailingsArray.SetNumUninitialized(AllTrailingsArray.Num());

		int32 IdxFilteredTrailings = 0;

		float MinSpeedToSpawnSquared = SpeedToSpawnMinMax.X * SpeedToSpawnMinMax.X;
		float MaxSpeedToSpawnSquared = SpeedToSpawnMinMax.Y * SpeedToSpawnMinMax.Y;

		for (int32 IdxTrailing = 0; IdxTrailing < AllTrailingsArray.Num(); ++IdxTrailing)
		{
			float TrailingSpeedSquared = AllTrailingsArray[IdxTrailing].Velocity.SizeSquared();

			if (/*(ParticleIndexToProcess != -1 && AllTrailingsArray[IdxTrailing].ParticleIndex != ParticleIndexToProcess) ||*/
				(SpeedToSpawnMinMax.X > 0.f && SpeedToSpawnMinMax.Y < 0.f && TrailingSpeedSquared < MinSpeedToSpawnSquared) ||
				(SpeedToSpawnMinMax.X < 0.f && SpeedToSpawnMinMax.Y > 0.f && TrailingSpeedSquared > MaxSpeedToSpawnSquared) ||
				(SpeedToSpawnMinMax.X > 0.f && SpeedToSpawnMinMax.Y > 0.f && (TrailingSpeedSquared < MinSpeedToSpawnSquared || TrailingSpeedSquared > MaxSpeedToSpawnSquared)) ||
				(MassToSpawnMinMax.X > 0.f && MassToSpawnMinMax.Y < 0.f && AllTrailingsArray[IdxTrailing].Mass < MassToSpawnMinMax.X) ||
				(MassToSpawnMinMax.X < 0.f && MassToSpawnMinMax.Y > 0.f && AllTrailingsArray[IdxTrailing].Mass > MassToSpawnMinMax.Y) ||
				(MassToSpawnMinMax.X > 0.f && MassToSpawnMinMax.Y > 0.f && (AllTrailingsArray[IdxTrailing].Mass < MassToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].Mass > MassToSpawnMinMax.Y)) ||
				(ExtentMinToSpawnMinMax.X > 0.f && ExtentMinToSpawnMinMax.Y < 0.f && AllTrailingsArray[IdxTrailing].BoundingboxExtentMin < ExtentMinToSpawnMinMax.X) ||
				(ExtentMinToSpawnMinMax.X < 0.f && ExtentMinToSpawnMinMax.Y > 0.f && AllTrailingsArray[IdxTrailing].BoundingboxExtentMin > ExtentMinToSpawnMinMax.Y) ||
				(ExtentMinToSpawnMinMax.X > 0.f && ExtentMinToSpawnMinMax.Y > 0.f && (AllTrailingsArray[IdxTrailing].BoundingboxExtentMin < ExtentMinToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].BoundingboxExtentMin > ExtentMinToSpawnMinMax.Y)) ||
				(ExtentMaxToSpawnMinMax.X > 0.f && ExtentMaxToSpawnMinMax.Y < 0.f && AllTrailingsArray[IdxTrailing].BoundingboxExtentMax < ExtentMaxToSpawnMinMax.X) ||
				(ExtentMaxToSpawnMinMax.X < 0.f && ExtentMaxToSpawnMinMax.Y > 0.f && AllTrailingsArray[IdxTrailing].BoundingboxExtentMax > ExtentMaxToSpawnMinMax.Y) ||
				(ExtentMaxToSpawnMinMax.X > 0.f && ExtentMaxToSpawnMinMax.Y > 0.f && (AllTrailingsArray[IdxTrailing].BoundingboxExtentMax < ExtentMaxToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].BoundingboxExtentMax > ExtentMaxToSpawnMinMax.Y)) ||
				(VolumeToSpawnMinMax.X > 0.f && VolumeToSpawnMinMax.Y < 0.f && AllTrailingsArray[IdxTrailing].BoundingboxVolume < VolumeToSpawnMinMax.X) ||
				(VolumeToSpawnMinMax.X < 0.f && VolumeToSpawnMinMax.Y > 0.f && AllTrailingsArray[IdxTrailing].BoundingboxVolume > VolumeToSpawnMinMax.Y) ||
				(VolumeToSpawnMinMax.X > 0.f && VolumeToSpawnMinMax.Y > 0.f && (AllTrailingsArray[IdxTrailing].BoundingboxVolume < VolumeToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].BoundingboxVolume > VolumeToSpawnMinMax.Y)) ||
				(SolverTimeToSpawnMinMax.X > 0.f && SolverTimeToSpawnMinMax.Y < 0.f && SolverTime < SolverTimeToSpawnMinMax.X) ||
				(SolverTimeToSpawnMinMax.X < 0.f && SolverTimeToSpawnMinMax.Y > 0.f && SolverTime > SolverTimeToSpawnMinMax.Y) ||
				(SolverTimeToSpawnMinMax.X > 0.f && SolverTimeToSpawnMinMax.Y > 0.f && (SolverTime < SolverTimeToSpawnMinMax.X || SolverTime > SolverTimeToSpawnMinMax.Y)) ||
				(SurfaceTypeToSpawn != -1 && AllTrailingsArray[IdxTrailing].SurfaceType != SurfaceTypeToSpawn) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Min && AllTrailingsArray[IdxTrailing].Location.X <= LocationXToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Max && AllTrailingsArray[IdxTrailing].Location.X >= LocationXToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_MinMax && (AllTrailingsArray[IdxTrailing].Location.X <= LocationXToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].Location.X >= LocationXToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Min && AllTrailingsArray[IdxTrailing].Location.Y <= LocationYToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Max && AllTrailingsArray[IdxTrailing].Location.Y >= LocationYToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_MinMax && (AllTrailingsArray[IdxTrailing].Location.Y <= LocationYToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].Location.Y >= LocationYToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Min && AllTrailingsArray[IdxTrailing].Location.Z <= LocationZToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Max && AllTrailingsArray[IdxTrailing].Location.Z >= LocationZToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Inclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_MinMax && (AllTrailingsArray[IdxTrailing].Location.Z <= LocationZToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].Location.Z >= LocationZToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Min && AllTrailingsArray[IdxTrailing].Location.X > LocationXToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_Max && AllTrailingsArray[IdxTrailing].Location.X < LocationXToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationXToSpawn == ELocationXToSpawnEnum::ChaosNiagara_LocationXToSpawn_MinMax && !(AllTrailingsArray[IdxTrailing].Location.X <= LocationXToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].Location.X >= LocationXToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Min && AllTrailingsArray[IdxTrailing].Location.Y > LocationYToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_Max && AllTrailingsArray[IdxTrailing].Location.Y < LocationYToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationYToSpawn == ELocationYToSpawnEnum::ChaosNiagara_LocationYToSpawn_MinMax && !(AllTrailingsArray[IdxTrailing].Location.Y <= LocationYToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].Location.Y >= LocationYToSpawnMinMax.Y)) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Min && AllTrailingsArray[IdxTrailing].Location.Z > LocationZToSpawnMinMax.X) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_Max && AllTrailingsArray[IdxTrailing].Location.Z < LocationZToSpawnMinMax.Y) ||
				(LocationFilteringMode == ELocationFilteringModeEnum::ChaosNiagara_LocationFilteringMode_Exclusive && LocationZToSpawn == ELocationZToSpawnEnum::ChaosNiagara_LocationZToSpawn_MinMax && !(AllTrailingsArray[IdxTrailing].Location.Z <= LocationZToSpawnMinMax.X || AllTrailingsArray[IdxTrailing].Location.Z >= LocationZToSpawnMinMax.Y)))
			{
				continue;
			}

			FilteredAllTrailingsArray[IdxFilteredTrailings] = AllTrailingsArray[IdxTrailing];

			IdxFilteredTrailings++;
		}
		FilteredAllTrailingsArray.SetNum(IdxFilteredTrailings);

		// If Trailings were filtered copy FilteredAllTrailingsArray back into AllTrailings
		if (FilteredAllTrailingsArray.Num() != AllTrailingsArray.Num())
		{
			AllTrailingsArray.SetNumUninitialized(FilteredAllTrailingsArray.Num());
		}
		for (int32 Idx = 0; Idx < FilteredAllTrailingsArray.Num(); ++Idx)
		{
			AllTrailingsArray[Idx] = FilteredAllTrailingsArray[Idx];
		}

		INC_DWORD_STAT_BY(STAT_NiagaraNumFilteredAllTrailings, FilteredAllTrailingsArray.Num());
	}
}

void UNiagaraDataInterfaceChaosDestruction::SortTrailings(TArray<Chaos::TTrailingDataExt<float, 3>>& TrailingsArray)
{
	SCOPE_CYCLE_COUNTER(STAT_TrailingCallbackSorting);

	if (DataSortingType == EDataSortTypeEnum::ChaosNiagara_DataSortType_SortByMassMaxToMin)
	{
		TrailingsArray.Sort(UNiagaraDataInterfaceChaosDestruction::TrailingDataSortByMassPredicateMaxToMin);
	}
	else if (DataSortingType == EDataSortTypeEnum::ChaosNiagara_DataSortType_SortByMassMinToMax)
	{
		TrailingsArray.Sort(UNiagaraDataInterfaceChaosDestruction::TrailingDataSortByMassPredicateMinToMax);
	}
	else if (DataSortingType == EDataSortTypeEnum::ChaosNiagara_DataSortType_RandomShuffle)
	{
		TrailingsArray.Sort(UNiagaraDataInterfaceChaosDestruction::TrailingDataRandomShuffleSortPredicate);
	}
}

void UNiagaraDataInterfaceChaosDestruction::GetTrailingsToSpawnFromTrailings(TArray<Chaos::TTrailingDataExt<float, 3>>& AllTrailingsArray,
																			 TArray<Chaos::TTrailingDataExt<float, 3>>& TrailingsToSpawnArray)
{
	if (AllTrailingsArray.Num() <= MaxNumberOfDataEntriesToSpawn)
	{
		TrailingsToSpawnArray.SetNumUninitialized(AllTrailingsArray.Num());
		for (int32 IdxTrailing = 0; IdxTrailing < AllTrailingsArray.Num(); ++IdxTrailing)
		{
			TrailingsToSpawnArray[IdxTrailing] = AllTrailingsArray[IdxTrailing];
		}
	}
	else
	{
		float FInc = (float)AllTrailingsArray.Num() / (float)MaxNumberOfDataEntriesToSpawn;

		TrailingsToSpawnArray.SetNumUninitialized(MaxNumberOfDataEntriesToSpawn);
		for (int32 IdxTrailing = 0; IdxTrailing < MaxNumberOfDataEntriesToSpawn; ++IdxTrailing)
		{
			int32 NewIdx = FMath::FloorToInt((float)IdxTrailing * FInc);
			TrailingsToSpawnArray[IdxTrailing] = AllTrailingsArray[NewIdx];
		}
	}

	INC_DWORD_STAT_BY(STAT_NiagaraNumTrailingsToSpawnParticles, TrailingsToSpawnArray.Num());
}

int32 UNiagaraDataInterfaceChaosDestruction::SpawnParticlesFromTrailing(FSolverData SolverData,
																		Chaos::TTrailingDataExt<float, 3>& Trailing,
																		FNDIChaosDestruction_InstanceData* InstData,
																		float TimeData_MapsCreated,
																		int32 IdxSolver)
{
	float Latency = SolverData.Solver->GetSolverTime() - TimeData_MapsCreated;
	if (Latency <= MaxLatency)
	{
		int32 NumParticles = FMath::RandRange((int)SpawnMultiplierMinMax.X, (int)FMath::Max(SpawnMultiplierMinMax.X, SpawnMultiplierMinMax.Y));
		for (int32 Idx = 0; Idx < NumParticles; ++Idx)
		{
			if (FMath::FRand() > FMath::Clamp(SpawnChance, 0.f, 1.f))
			{
				continue;
			}

			// Compute random position
			float sign[2] = { -1.f, 1.f };
			float RandomX = sign[FMath::RandRange(0, 1)] * FMath::FRandRange(FMath::Min(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y), FMath::Max(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y));
			float RandomY = sign[FMath::RandRange(0, 1)] * FMath::FRandRange(FMath::Min(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y), FMath::Max(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y));
			float RandomZ = sign[FMath::RandRange(0, 1)] * FMath::FRandRange(FMath::Min(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y), FMath::Max(RandomPositionMagnitudeMinMax.X, RandomPositionMagnitudeMinMax.Y));
			FVector RandomPosition(RandomX, RandomY, RandomZ);

			// Compute velocity offset
			FVector VelocityOffset;
			VelocityOffset.X = FMath::FRandRange(FMath::Min(VelocityOffsetMin.X, VelocityOffsetMax.X), FMath::Max(VelocityOffsetMin.X, VelocityOffsetMax.X));
			VelocityOffset.Y = FMath::FRandRange(FMath::Min(VelocityOffsetMin.Y, VelocityOffsetMax.Y), FMath::Max(VelocityOffsetMin.Y, VelocityOffsetMax.Y));
			VelocityOffset.Z = FMath::FRandRange(FMath::Min(VelocityOffsetMin.Z, VelocityOffsetMax.Z), FMath::Max(VelocityOffsetMin.Z, VelocityOffsetMax.Z));

			// Compute random velocity using the selected Velocity Model
			FVector ParticleVelocity;
			if (RandomVelocityGenerationType == ERandomVelocityGenerationTypeEnum::ChaosNiagara_RandomVelocityGenerationType_RandomDistribution)
			{
				FVector RandomVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f));
				RandomVector.Normalize();

				ParticleVelocity = RandomVector * FMath::FRandRange(RandomVelocityMagnitudeMinMax.X, FMath::Max(RandomVelocityMagnitudeMinMax.X, RandomVelocityMagnitudeMinMax.Y));
			}
			else if (RandomVelocityGenerationType == ERandomVelocityGenerationTypeEnum::ChaosNiagara_RandomVelocityGenerationType_RandomDistributionWithStreamers)
			{
				FVector RandomVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f));
				RandomVector.Normalize();

				ParticleVelocity = RandomVector * FMath::FRandRange(RandomVelocityMagnitudeMinMax.X, FMath::Max(RandomVelocityMagnitudeMinMax.X, RandomVelocityMagnitudeMinMax.Y));

				float RandomMultiplier = (FMath::FRand() < 0.2) ? 1.25f : 1.f;
				ParticleVelocity *= RandomMultiplier;
			}

			// Combine all velocity components
			FVector ComputedVelocity = Trailing.Velocity * InheritedVelocityMultiplier + ParticleVelocity + VelocityOffset;

			// Clamp velocity
			if (FinalVelocityMagnitudeMinMax.X >= 0 && FinalVelocityMagnitudeMinMax.Y < 0)
			{
				if (ComputedVelocity.SizeSquared() < FinalVelocityMagnitudeMinMax.X * FinalVelocityMagnitudeMinMax.X)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.X;
				}
			}
			else if (FinalVelocityMagnitudeMinMax.X < 0 && FinalVelocityMagnitudeMinMax.Y >= 0)
			{
				if (ComputedVelocity.SizeSquared() > FinalVelocityMagnitudeMinMax.Y * FinalVelocityMagnitudeMinMax.Y)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.Y;
				}
			}
			else if (FinalVelocityMagnitudeMinMax.X >= 0 && FinalVelocityMagnitudeMinMax.Y >= 0)
			{
				if (ComputedVelocity.SizeSquared() < FinalVelocityMagnitudeMinMax.X * FinalVelocityMagnitudeMinMax.X)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.X;
				}
				else if (ComputedVelocity.SizeSquared() > FinalVelocityMagnitudeMinMax.Y * FinalVelocityMagnitudeMinMax.Y)
				{
					ComputedVelocity.Normalize();
					ComputedVelocity *= FinalVelocityMagnitudeMinMax.Y;
				}
			}

			// Debug
			FVector ParticleColor = FVector::OneVector;
			if (DebugType == EDebugTypeEnum::ChaosNiagara_DebugType_ColorBySolver)
			{
				ParticleColor = ColorArray[IdxSolver % ColorArray.Num()];
			}
			else if (DebugType == EDebugTypeEnum::ChaosNiagara_DebugType_ColorByParticleIndex)
			{
				//ParticleColor = ColorArray[Trailing.ParticleIndex % ColorArray.Num()];
			}

			// Store principal data
			InstData->PositionArray.Add(Trailing.Location + RandomPosition);
			InstData->VelocityArray.Add(ComputedVelocity);
			InstData->SolverIDArray.Add(IdxSolver);

			// Store Trailing data
			InstData->IncomingLocationArray.Add(Trailing.Location);
			InstData->IncomingVelocity1Array.Add(Trailing.Velocity);
			InstData->IncomingAngularVelocity1Array.Add(Trailing.AngularVelocity);
			InstData->IncomingMass1Array.Add(Trailing.Mass);
			InstData->IncomingTimeArray.Add(TimeData_MapsCreated);

			// Store GeometryCollection data
			InstData->ExtentMinArray.Add(Trailing.BoundingboxExtentMin);
			InstData->ExtentMaxArray.Add(Trailing.BoundingboxExtentMax);
			InstData->VolumeArray.Add(Trailing.BoundingboxVolume);

			// Set not related to default
			InstData->IncomingAccumulatedImpulseArray.Add(FVector(ForceInitToZero));
			InstData->IncomingNormalArray.Add(FVector(ForceInitToZero));
			InstData->IncomingVelocity2Array.Add(FVector(ForceInitToZero));
			InstData->IncomingAngularVelocity2Array.Add(FVector(ForceInitToZero));
			InstData->IncomingMass2Array.Add(0.f);
		}

		return NumParticles;
	}

	return 0;
}

bool UNiagaraDataInterfaceChaosDestruction::TrailingCallback(FNDIChaosDestruction_InstanceData* InstData)
{
	int32 IdxSolver = 0;
	for (FSolverData SolverData : Solvers)
	{
		if (SolverData.Solver->GetEventFilters()->IsTrailingEventEnabled() && TrailingEvents.Num() > 0 && SolverData.Solver->GetSolverTime() > 0.f && MaxNumberOfDataEntriesToSpawn > 0)
		{
			TArray<Chaos::TTrailingDataExt<float, 3>>& AllTrailingsArray = TrailingEvents;
			TArray<PhysicsProxyWrapper> PhysicsProxyReverseMappingArray;
			TArray<int32> ParticleIndexReverseMappingArray;
			TMap<IPhysicsProxyBase*, TArray<int32>> AllTrailingsIndicesByPhysicsProxyMap;
			float TimeData_MapsCreated = 0.0f;

			{
				size_t SizeOfAllTrailings = sizeof(Chaos::TTrailingData<float, 3>) * AllTrailingsArray.Num();
				size_t SizeOfPhysicsProxyReverseMapping = sizeof(PhysicsProxyWrapper) * PhysicsProxyReverseMappingArray.Num();
				size_t SizeOfParticleIndexReverseMapping = sizeof(int32) * ParticleIndexReverseMappingArray.Num();

				size_t SizeOfAllTrailingsIndicesByPhysicsProxy = 0;
				for (auto& Elem : AllTrailingsIndicesByPhysicsProxyMap)
				{
					SizeOfAllTrailingsIndicesByPhysicsProxy += sizeof(int32) * (Elem.Value).Num();
				}
				SET_MEMORY_STAT(STAT_AllTrailingsDataMemory, SizeOfAllTrailings);
				SET_MEMORY_STAT(STAT_PhysicsProxyReverseMappingMemory, SizeOfPhysicsProxyReverseMapping);
				SET_MEMORY_STAT(STAT_ParticleIndexReverseMappingMemory, SizeOfParticleIndexReverseMapping);
				SET_MEMORY_STAT(STAT_AllTrailingsIndicesByPhysicsProxyMemory, SizeOfAllTrailingsIndicesByPhysicsProxy);
			}

			INC_DWORD_STAT_BY(STAT_NiagaraNumAllTrailings, AllTrailingsArray.Num());

			if (AllTrailingsArray.Num() > 0)
			{
				// Filter AllTrailings
				// In case of filtering, AllTrailings will be resized and filtered data will be copied back to AllTrailings
				FilterAllTrailings(AllTrailingsArray);

				// Sort AllCollisisons
				SortTrailings(AllTrailingsArray);

				// Get the Trailings which will spawn particles
				TArray<Chaos::TTrailingDataExt<float, 3>> TrailingsToSpawnArray;

				GetTrailingsToSpawnFromTrailings(AllTrailingsArray, TrailingsToSpawnArray);

				// Spawn particles for Trailings in TrailingsToSpawnArray
				for (int32 IdxTrailing = 0; IdxTrailing < TrailingsToSpawnArray.Num(); ++IdxTrailing)
				{
					int32 NumParticlesSpawned = SpawnParticlesFromTrailing(SolverData,
																		   TrailingsToSpawnArray[IdxTrailing],
																		   InstData,
																		   TimeData_MapsCreated,
																		   IdxSolver);

					if (NumParticlesSpawned > 0)
					{
						// Get/Store Geometry/Physical Material data
						FLinearColor Color(1.f, 1.f, 1.f, 1.f);
						float Friction = 0.7f, Restitution = 0.3f, Density = 1.f;
						// #GM: Disable this for now for perf
						/*
						GetMesPhysicalData(SolverData,
										   TrailingsToSpawnArray[IdxTrailing].ParticleIndexMesh == INDEX_NONE ? TrailingsToSpawnArray[IdxTrailing].ParticleIndex : TrailingsToSpawnArray[IdxTrailing].ParticleIndexMesh,
										   PhysicsProxyReverseMappingArray,
										   ParticleIndexReverseMappingArray,
										   Color,
										   Friction,
										   Restitution,
										   Density);
						*/
						for (int32 Idx = 0; Idx < NumParticlesSpawned; ++Idx)
						{
							InstData->SurfaceTypeArray.Add(TrailingsToSpawnArray[IdxTrailing].SurfaceType);
							InstData->ColorArray.Add(Color);
							InstData->FrictionArray.Add(Friction);
							InstData->RestitutionArray.Add(Restitution);
							InstData->DensityArray.Add(Density);
						}
					}
				}
			}
		}

		IdxSolver++;
	}

	INC_DWORD_STAT_BY(STAT_NiagaraNumParticlesSpawnedFromTrailings, InstData->PositionArray.Num());

	return false;
}

void UNiagaraDataInterfaceChaosDestruction::ResetInstData(FNDIChaosDestruction_InstanceData* InstData)
{
	InstData->PositionArray.Reset();
	InstData->VelocityArray.Reset();
	InstData->ExtentMinArray.Reset();
	InstData->ExtentMaxArray.Reset();
	InstData->VolumeArray.Reset();
	InstData->SolverIDArray.Reset();
	InstData->DensityArray.Reset();
	InstData->FrictionArray.Reset();
	InstData->RestitutionArray.Reset();
	InstData->SurfaceTypeArray.Reset();
	InstData->ColorArray.Reset();

	InstData->IncomingLocationArray.Reset();
	InstData->IncomingAccumulatedImpulseArray.Reset();
	InstData->IncomingNormalArray.Reset();
	InstData->IncomingVelocity1Array.Reset();
	InstData->IncomingVelocity2Array.Reset();
	InstData->IncomingAngularVelocity1Array.Reset();
	InstData->IncomingAngularVelocity2Array.Reset();
	InstData->IncomingMass1Array.Reset();
	InstData->IncomingMass2Array.Reset();
	InstData->IncomingTimeArray.Reset();

	InstData->TransformTranslationArray.Reset();
	InstData->TransformRotationArray.Reset();
	InstData->TransformScaleArray.Reset();
	InstData->BoundsArray.Reset();
}
#endif

bool UNiagaraDataInterfaceChaosDestruction::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
#if INCLUDE_CHAOS
	check(SystemInstance);
	FNDIChaosDestruction_InstanceData* InstData = (FNDIChaosDestruction_InstanceData*)PerInstanceData;

	// Update SolverTime
	for (FSolverData SolverData : Solvers)
	{
		SolverTime = SolverData.Solver->GetSolverTime();
		break;
	}

	ShouldSpawn = false;
	if (SolverTime != LastSpawnTime && SolverTime - LastSpawnTime >= 1.0 / (float)DataProcessFrequency)
	{
		// We skip the tick if we receive duplicate data from Chaos. This happens if Niagara's tick rate is faster 
		// than the chaos solver's tick rate. 

		// The first time around PrevLastSpawnedPointID and LastSpawnedPointID are both -1, and InstData is empty
		// so these assignment do not change anything.
		LastSpawnTime = SolverTime;
		LastSpawnedPointID += InstData->PositionArray.Num();
		//UE_LOG(LogScript, Warning, TEXT("Tick, PrevLastSpawnedPointID = %d, LastSpawnedPointID = %d"), PrevLastSpawnedPointID, LastSpawnedPointID);
		ShouldSpawn = true;
	} 

	ResetInstData(InstData);

	if (ShouldSpawn && DoSpawn) {
		if (DataSourceType == EDataSourceTypeEnum::ChaosNiagara_DataSourceType_Collision)
		{
			SCOPE_CYCLE_COUNTER(STAT_CollisionCallback);
			return CollisionCallback(InstData);
		}
		else if (DataSourceType == EDataSourceTypeEnum::ChaosNiagara_DataSourceType_Breaking)
		{
			SCOPE_CYCLE_COUNTER(STAT_BreakingCallback);
			return BreakingCallback(InstData);
		}
		else if (DataSourceType == EDataSourceTypeEnum::ChaosNiagara_DataSourceType_Trailing)
		{
			SCOPE_CYCLE_COUNTER(STAT_TrailingCallback);
			return TrailingCallback(InstData);
		}
	}

#endif

	return false;
}

// Returns the signature of all the functions available in the data interface
void UNiagaraDataInterfaceChaosDestruction::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		// GetPosition
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetPositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));			// Vector3 Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetPosition",
			"Helper function returning the position value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetNormal
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNormalName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));			// Vector3 Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetNormal",
			"Helper function returning the normal value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetVelocity
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVelocityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));			// Vector3 Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetVelocity",
			"Helper function returning the velocity value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetAngularVelocity
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetAngularVelocityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("AngularVelocity")));	// Vector3 Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetAngularVelocity",
			"Helper function returning the angular velocity value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetExtentMin
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetExtentMinName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ExtentMin")));		// Float Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetExtentMin",
			"Helper function returning the min extent value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetExtentMax
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetExtentMaxName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("ExtentMax")));		// Float Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetExtentMax",
			"Helper function returning the max extent value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetVolume
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVolumeName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Volume")));		// Float Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetVolume",
			"Helper function returning the max extent value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetParticleIdsToSpawnAtTime
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetParticleIdsToSpawnAtTimeName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));	// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time")));		    // Time in
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("MinID")));			// Int Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("MaxID")));			// Int Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count")));		    // Int Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetParticleIdsToSpawnAtTime",
			"Returns the count and IDs of the particles that should spawn for a given time value."));

		OutFunctions.Add(Sig);
	}

	{
		// GetPointType
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetPointTypeName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Type")));				// Int Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetPointType",
			"Helper function returning the type value for a given particle when spawned.\n"));

		OutFunctions.Add(Sig);
	}

	{
		// GetColor
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetColorName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color")));			// Color Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetColor",
			"Helper function returning the color for a given particle when spawned."));

		OutFunctions.Add(Sig);
	}

	{
		// GetSolverTime
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSolverTimeName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("SolverTime")));		// SolverTime Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetSolverTime",
			"Helper function returning the SolverTime."));

		OutFunctions.Add(Sig);
	}

	{
		// GetDensity
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetDensityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Density")));		// Float Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetDensity",
			"Helper function returning the max extent value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetFriction
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetFrictionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Friction")));		// Float Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetFriction",
			"Helper function returning the max extent value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetRestitution
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetRestitutionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Restitution")));		// Float Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetRestitution",
			"Helper function returning the max extent value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetSize
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSizeName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Size")));		// Int Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetSize",
			"Helper function returning the max extent value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetTransform
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetTransformName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Translation")));		// Int Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));		// Int Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Scale")));		// Int Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetTransform",
			"Helper function returning the max extent value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetSurfaceType
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSurfaceTypeName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));		// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));			// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("SurfaceType")));		// Int Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetSurfaceType",
			"Helper function returning the max extent value for a given ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetCollisionData
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetCollisionDataName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));					// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));						// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionLocation")));				// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionAccumulatedImpulse")));	// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionNormal")));				// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionVelocity1")));			// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionVelocity2")));			// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionAngularVelocity1")));		// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CollisionAngularVelocity2")));		// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMass1")));				// Float Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionMass2")));				// Float Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("CollisionTime")));				// Float Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetCollisionData",
			"Helper function returning  ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetBreakingData
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBreakingDataName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));					// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));						// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("BreakingLocation")));				// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("BreakingVelocity")));				// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("BreakingAngularVelocity")));		// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("BreakingMass")));					// Float Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("BreakingTime")));					// Float Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetBreakingData",
			"Helper function returning  ..."));

		OutFunctions.Add(Sig);
	}

	{
		// GetTrailingData
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetTrailingDataName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("DestructionData")));					// DestructionData in
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("ParticleID")));						// ParticleID In
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TrailingLocation")));				// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TrailingVelocity")));				// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("TrailingAngularVelocity")));		// Vector3 Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("TrailingMass")));					// Float Out
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("TrailingTime")));					// Float Out

		Sig.SetDescription(LOCTEXT("DataInterfaceChaos_GetTrailingData",
			"Helper function returning  ..."));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetPosition);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetNormal);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetVelocity);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetAngularVelocity);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetExtentMin);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetExtentMax);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetVolume);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetParticleIdsToSpawnAtTime);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetPointType);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetColor);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetSolverTime);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetDensity);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetFriction);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetRestitution);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetTransform);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetSize);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetSurfaceType);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetCollisionData);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetBreakingData);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetTrailingData);

void UNiagaraDataInterfaceChaosDestruction::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == GetPositionName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetPosition)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetNormalName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetNormal)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetVelocityName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetVelocity)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetAngularVelocityName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetAngularVelocity)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetExtentMinName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetExtentMin)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetExtentMaxName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetExtentMax)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetVolumeName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetVolume)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetParticleIdsToSpawnAtTimeName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3)
	{
		TNDIParamBinder<0, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetParticleIdsToSpawnAtTime)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetPointTypeName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetPointType)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetColorName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 4)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetColor)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetSolverTimeName && BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetSolverTime)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetDensityName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetDensity)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetFrictionName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetFriction)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetRestitutionName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetRestitution)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetTransformName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 10)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetTransform)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetSizeName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetSize)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetSurfaceTypeName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetSurfaceType)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetCollisionDataName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 24)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetCollisionData)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetBreakingDataName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 11)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetBreakingData)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetTrailingDataName && BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 11)
	{
		TNDIParamBinder<0, int32, NDI_FUNC_BINDER(UNiagaraDataInterfaceChaosDestruction, GetTrailingData)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("Could not find data interface function:\n\tName: %s\n\tInputs: %i\n\tOutputs: %i"),
			*BindingInfo.Name.ToString(), BindingInfo.GetNumInputs(), BindingInfo.GetNumOutputs());
		OutFunc = FVMExternalFunction();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetPosition(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleZ(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		//		if (InstData->ParticleDataArray.Num())
		if (InstData->PositionArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			FVector V = InstData->PositionArray[ParticleID];
			//UE_LOG(LogScript, Warning, TEXT("Position: %d: %f %f %f"), ParticleID, V.X, V.Y, V.Z);
			*OutSampleX.GetDest() = V.X;
			*OutSampleY.GetDest() = V.Y;
			*OutSampleZ.GetDest() = V.Z;
		}

		ParticleIDParam.Advance();
		OutSampleX.Advance();
		OutSampleY.Advance();
		OutSampleZ.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetNormal(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleZ(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->IncomingNormalArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			FVector V = InstData->IncomingNormalArray[ParticleID];

			*OutSampleX.GetDest() = V.X;
			*OutSampleY.GetDest() = V.Y;
			*OutSampleZ.GetDest() = V.Z;
		}

		ParticleIDParam.Advance();
		OutSampleX.Advance();
		OutSampleY.Advance();
		OutSampleZ.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetVelocity(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleZ(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		//		if (InstData->ParticleDataArray.Num())
		if (InstData->VelocityArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			FVector V = InstData->VelocityArray[ParticleID];
			*OutSampleX.GetDest() = V.X;
			*OutSampleY.GetDest() = V.Y;
			*OutSampleZ.GetDest() = V.Z;
		}

		ParticleIDParam.Advance();
		OutSampleX.Advance();
		OutSampleY.Advance();
		OutSampleZ.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetAngularVelocity(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleZ(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->IncomingAngularVelocity1Array.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			FVector W = InstData->IncomingAngularVelocity1Array[ParticleID];

			*OutSampleX.GetDest() = W.X;
			*OutSampleY.GetDest() = W.Y;
			*OutSampleZ.GetDest() = W.Z;
		}

		ParticleIDParam.Advance();
		OutSampleX.Advance();
		OutSampleY.Advance();
		OutSampleZ.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetExtentMin(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutValue(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->ExtentMinArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			float Value = InstData->ExtentMinArray[ParticleID];

			*OutValue.GetDest() = Value;
		}

		ParticleIDParam.Advance();
		OutValue.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetExtentMax(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutValue(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->ExtentMaxArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			float Value = InstData->ExtentMaxArray[ParticleID];

			*OutValue.GetDest() = Value;
		}

		ParticleIDParam.Advance();
		OutValue.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetVolume(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutValue(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->VolumeArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			float Value = InstData->VolumeArray[ParticleID];

			*OutValue.GetDest() = Value;
		}

		ParticleIDParam.Advance();
		OutValue.Advance();
	}
}

template<typename TimeParamType>
void UNiagaraDataInterfaceChaosDestruction::GetParticleIdsToSpawnAtTime(FVectorVMContext& Context)
{
	TimeParamType TimeParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutMinValue(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutMaxValue(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutCountValue(Context);

	if (DoSpawn && ShouldSpawn && InstData->PositionArray.Num())
	{
		*OutMinValue.GetDest() = LastSpawnedPointID + 1;
		*OutMaxValue.GetDest() = LastSpawnedPointID + InstData->PositionArray.Num();
		*OutCountValue.GetDest() = InstData->PositionArray.Num();
		//UE_LOG(LogScript, Warning, TEXT("Min = %d, Max = %d, Count = %d"), LastSpawnedPointID + 1, LastSpawnedPointID + InstData->PositionArray.Num(), InstData->PositionArray.Num());
	} 
	else 
	{
		*OutMinValue.GetDest() = 0;
		*OutMaxValue.GetDest() = 0;
		*OutCountValue.GetDest() = 0;		
	}
	

	TimeParam.Advance();
	OutMinValue.Advance();
	OutMaxValue.Advance();
	OutCountValue.Advance();
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetPointType(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutValue(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->PositionArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;

			int32 Value = 0;

			*OutValue.GetDest() = Value;
		}

		ParticleIDParam.Advance();
		OutValue.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetColor(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleA(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->ColorArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			FLinearColor V = InstData->ColorArray[ParticleID];

			*OutSampleR.GetDest() = V.R;
			*OutSampleG.GetDest() = V.G;
			*OutSampleB.GetDest() = V.B;
			*OutSampleA.GetDest() = V.A;
		}

		ParticleIDParam.Advance();
		OutSampleR.Advance();
		OutSampleG.Advance();
		OutSampleB.Advance();
		OutSampleA.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetSolverTime(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutValue(Context);

	*OutValue.GetDest() = SolverTime;

	OutValue.Advance();
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetDensity(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutValue(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->DensityArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			float Value = InstData->DensityArray[ParticleID];

			*OutValue.GetDest() = Value;
		}

		ParticleIDParam.Advance();
		OutValue.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetFriction(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutValue(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->FrictionArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			float Value = InstData->FrictionArray[ParticleID];

			*OutValue.GetDest() = Value;
		}

		ParticleIDParam.Advance();
		OutValue.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetRestitution(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutValue(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->RestitutionArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			float Value = InstData->RestitutionArray[ParticleID];

			*OutValue.GetDest() = Value;
		}

		ParticleIDParam.Advance();
		OutValue.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetTransform(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutTranslationX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutTranslationY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutTranslationZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutRotationX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutRotationY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutRotationZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutRotationW(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutScaleX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutScaleY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutScaleZ(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->TransformTranslationArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			FVector Translation = InstData->TransformTranslationArray[ParticleID];
			FQuat Rotation = InstData->TransformRotationArray[ParticleID];
			FVector Scale = InstData->TransformScaleArray[ParticleID];
			//UE_LOG(LogScript, Warning, TEXT("%f %f %f    %f %f %f %f   %f %f %f"), Translation.X, Translation.Y, Translation.Z, Rotation.X, Rotation.Y, Rotation.Z, Rotation.W, Scale.X, Scale.X, Scale.X)
			//UE_LOG(LogScript, Warning, TEXT("Transform: %f %f %f"), Translation.X, Translation.Y, Translation.Z)

			*OutTranslationX.GetDest() = Translation.X;
			*OutTranslationY.GetDest() = Translation.Y;
			*OutTranslationZ.GetDest() = Translation.Z;
			*OutRotationX.GetDest() = Rotation.X;
			*OutRotationY.GetDest() = Rotation.Y;
			*OutRotationZ.GetDest() = Rotation.Z;
			*OutRotationW.GetDest() = Rotation.W;
			*OutScaleX.GetDest() = Scale.X;
			*OutScaleY.GetDest() = Scale.Y;
			*OutScaleZ.GetDest() = Scale.Z;
		}

		ParticleIDParam.Advance();
		OutTranslationX.Advance();
		OutTranslationY.Advance();
		OutTranslationZ.Advance();
		OutRotationX.Advance();
		OutRotationY.Advance();
		OutRotationZ.Advance();
		OutRotationW.Advance();
		OutScaleX.Advance();
		OutScaleY.Advance();
		OutScaleZ.Advance();
		//OutRotation.Advance();
		//OutScale.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetSize(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSizeX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSizeY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSizeZ(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->TransformTranslationArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			FVector Bounds = InstData->BoundsArray[ParticleID];

			*OutSizeX.GetDest() = Bounds.X;
			*OutSizeY.GetDest() = Bounds.Y;
			*OutSizeZ.GetDest() = Bounds.Z;
		}

		ParticleIDParam.Advance();
		OutSizeX.Advance();
		OutSizeY.Advance();
		OutSizeZ.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetSurfaceType(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutValue(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->SurfaceTypeArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;
			int32 Value = InstData->SurfaceTypeArray[ParticleID];

			*OutValue.GetDest() = Value;
		}

		ParticleIDParam.Advance();
		OutValue.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetCollisionData(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutLocationX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutLocationY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutLocationZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAccumulatedImpulseX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAccumulatedImpulseY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAccumulatedImpulseZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutNormalX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutNormalY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutNormalZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocity1X(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocity1Y(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocity1Z(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocity2X(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocity2Y(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocity2Z(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocity1X(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocity1Y(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocity1Z(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocity2X(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocity2Y(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocity2Z(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMass1(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMass2(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutTime(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->IncomingLocationArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;

			FVector VValue;
			float FValue;

			VValue = InstData->IncomingLocationArray[ParticleID];
			*OutLocationX.GetDest() = VValue.X;
			*OutLocationY.GetDest() = VValue.Y;
			*OutLocationZ.GetDest() = VValue.Z;

			VValue = InstData->IncomingAccumulatedImpulseArray[ParticleID];
			*OutAccumulatedImpulseX.GetDest() = VValue.X;
			*OutAccumulatedImpulseY.GetDest() = VValue.Y;
			*OutAccumulatedImpulseZ.GetDest() = VValue.Z;

			VValue = InstData->IncomingNormalArray[ParticleID];
			*OutNormalX.GetDest() = VValue.X;
			*OutNormalY.GetDest() = VValue.Y;
			*OutNormalZ.GetDest() = VValue.Z;

			VValue = InstData->IncomingVelocity1Array[ParticleID];
			*OutVelocity1X.GetDest() = VValue.X;
			*OutVelocity1Y.GetDest() = VValue.Y;
			*OutVelocity1Z.GetDest() = VValue.Z;

			VValue = InstData->IncomingVelocity2Array[ParticleID];
			*OutVelocity2X.GetDest() = VValue.X;
			*OutVelocity2Y.GetDest() = VValue.Y;
			*OutVelocity2Z.GetDest() = VValue.Z;

			VValue = InstData->IncomingAngularVelocity1Array[ParticleID];
			*OutAngularVelocity1X.GetDest() = VValue.X;
			*OutAngularVelocity1Y.GetDest() = VValue.Y;
			*OutAngularVelocity1Z.GetDest() = VValue.Z;

			VValue = InstData->IncomingAngularVelocity2Array[ParticleID];
			*OutAngularVelocity2X.GetDest() = VValue.X;
			*OutAngularVelocity2Y.GetDest() = VValue.Y;
			*OutAngularVelocity2Z.GetDest() = VValue.Z;

			FValue = InstData->IncomingMass1Array[ParticleID];
			*OutMass1.GetDest() = FValue;

			FValue = InstData->IncomingMass2Array[ParticleID];
			*OutMass2.GetDest() = FValue;

			FValue = InstData->IncomingTimeArray[ParticleID];
			*OutTime.GetDest() = FValue;
		}

		ParticleIDParam.Advance();
		OutLocationX.Advance();
		OutLocationY.Advance();
		OutLocationZ.Advance();
		OutAccumulatedImpulseX.Advance();
		OutAccumulatedImpulseY.Advance();
		OutAccumulatedImpulseZ.Advance();
		OutNormalX.Advance();
		OutNormalY.Advance();
		OutNormalZ.Advance();
		OutVelocity1X.Advance();
		OutVelocity1Y.Advance();
		OutVelocity1Z.Advance();
		OutVelocity2X.Advance();
		OutVelocity2Y.Advance();
		OutVelocity2Z.Advance();
		OutAngularVelocity1X.Advance();
		OutAngularVelocity1Y.Advance();
		OutAngularVelocity1Z.Advance();
		OutAngularVelocity2X.Advance();
		OutAngularVelocity2Y.Advance();
		OutAngularVelocity2Z.Advance();
		OutMass1.Advance();
		OutMass2.Advance();
		OutTime.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetBreakingData(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutLocationX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutLocationY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutLocationZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocityX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocityY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocityZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocityX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocityY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocityZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMass(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutTime(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->IncomingLocationArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;

			FVector VValue;
			float FValue;

			VValue = InstData->IncomingLocationArray[ParticleID];
			*OutLocationX.GetDest() = VValue.X;
			*OutLocationY.GetDest() = VValue.Y;
			*OutLocationZ.GetDest() = VValue.Z;

			VValue = InstData->IncomingVelocity1Array[ParticleID];
			*OutVelocityX.GetDest() = VValue.X;
			*OutVelocityY.GetDest() = VValue.Y;
			*OutVelocityZ.GetDest() = VValue.Z;

			VValue = InstData->IncomingAngularVelocity1Array[ParticleID];
			*OutAngularVelocityX.GetDest() = VValue.X;
			*OutAngularVelocityY.GetDest() = VValue.Y;
			*OutAngularVelocityZ.GetDest() = VValue.Z;

			FValue = InstData->IncomingMass1Array[ParticleID];
			*OutMass.GetDest() = FValue;

			FValue = InstData->IncomingTimeArray[ParticleID];
			*OutTime.GetDest() = FValue;
		}

		ParticleIDParam.Advance();
		OutLocationX.Advance();
		OutLocationY.Advance();
		OutLocationZ.Advance();
		OutVelocityX.Advance();
		OutVelocityY.Advance();
		OutVelocityZ.Advance();
		OutAngularVelocityX.Advance();
		OutAngularVelocityY.Advance();
		OutAngularVelocityZ.Advance();
		OutMass.Advance();
		OutTime.Advance();
	}
}

template<typename ParticleIDParamType>
void UNiagaraDataInterfaceChaosDestruction::GetTrailingData(FVectorVMContext& Context)
{
	ParticleIDParamType ParticleIDParam(Context);
	VectorVM::FUserPtrHandler<FNDIChaosDestruction_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutLocationX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutLocationY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutLocationZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocityX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocityY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocityZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocityX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocityY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAngularVelocityZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMass(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutTime(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		if (InstData->IncomingLocationArray.Num())
		{
			int32 ParticleID = ParticleIDParam.Get();
			// Remap ParticleID
			ParticleID -= LastSpawnedPointID + 1;

			FVector VValue;
			float FValue;

			VValue = InstData->IncomingLocationArray[ParticleID];
			*OutLocationX.GetDest() = VValue.X;
			*OutLocationY.GetDest() = VValue.Y;
			*OutLocationZ.GetDest() = VValue.Z;

			VValue = InstData->IncomingVelocity1Array[ParticleID];
			*OutVelocityX.GetDest() = VValue.X;
			*OutVelocityY.GetDest() = VValue.Y;
			*OutVelocityZ.GetDest() = VValue.Z;

			VValue = InstData->IncomingAngularVelocity1Array[ParticleID];
			*OutAngularVelocityX.GetDest() = VValue.X;
			*OutAngularVelocityY.GetDest() = VValue.Y;
			*OutAngularVelocityZ.GetDest() = VValue.Z;

			FValue = InstData->IncomingMass1Array[ParticleID];
			*OutMass.GetDest() = FValue;

			FValue = InstData->IncomingTimeArray[ParticleID];
			*OutTime.GetDest() = FValue;
		}

		ParticleIDParam.Advance();
		OutLocationX.Advance();
		OutLocationY.Advance();
		OutLocationZ.Advance();
		OutVelocityX.Advance();
		OutVelocityY.Advance();
		OutVelocityZ.Advance();
		OutAngularVelocityX.Advance();
		OutAngularVelocityY.Advance();
		OutAngularVelocityZ.Advance();
		OutMass.Advance();
		OutTime.Advance();
	}
}

//----------------------------------------------------------------------------
// GPU sim functionality
//
void UNiagaraDataInterfaceChaosDestruction::GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{

	// This will get indented in the generated HLSL, which won't look good. 
	// On the other hand, it makes it really nice and readable here.
	static const TCHAR *FormatDeclarations = TEXT(R"(
		Buffer<float3> PositionBuffer_{Symbol};
		Buffer<float3> VelocityBuffer_{Symbol};
		Buffer<float>  ExtentMinBuffer_{Symbol};
		Buffer<float>  ExtentMaxBuffer_{Symbol};
		Buffer<float>  VolumeBuffer_{Symbol};
		Buffer<int>    SolverIDBuffer_{Symbol}; // NOTE(mv): Not used?
		Buffer<float>  DensityBuffer_{Symbol};
		Buffer<float>  FrictionBuffer_{Symbol};
		Buffer<float>  RestitutionBuffer_{Symbol};
		Buffer<int>    SurfaceTypeBuffer_{Symbol};
		Buffer<float4> ColorBuffer_{Symbol};
		
		Buffer<float3> IncomingLocationBuffer_{Symbol};
		Buffer<float3> IncomingAccumulatedImpulseBuffer_{Symbol};
		Buffer<float3> IncomingNormalBuffer_{Symbol};
		Buffer<float3> IncomingVelocity1Buffer_{Symbol};
		Buffer<float3> IncomingVelocity2Buffer_{Symbol};
		Buffer<float3> IncomingAngularVelocity1Buffer_{Symbol};
		Buffer<float3> IncomingAngularVelocity2Buffer_{Symbol};
		Buffer<float>  IncomingMass1Buffer_{Symbol};
		Buffer<float>  IncomingMass2Buffer_{Symbol};
		Buffer<float>  IncomingTimeBuffer_{Symbol};

		Buffer<float3>  TransformTranslationBuffer_{Symbol};
		Buffer<float4>  TransformRotationBuffer_{Symbol};
		Buffer<float3>  TransformScaleBuffer_{Symbol};
		Buffer<float3>  BoundsBuffer_{Symbol};

		// NOTE(mv): Not implemented in the CPU-side functionality yet. 
		//           Returns 0 in GetPointType instead.
		//           
		// Buffer<int> PointTypeBuffer_{Symbol};

		int   LastSpawnedPointID_{Symbol};

		float SolverTime_{Symbol};
	)");

	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
	};

	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);

	/*
	*/
}

bool UNiagaraDataInterfaceChaosDestruction::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	if (DefinitionFunctionName == GetPositionName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float3 Out_Position) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Position = PositionBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetNormalName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float3 Out_Normal) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Normal = IncomingNormalBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetVelocityName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float3 Out_Velocity) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Velocity = VelocityBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetAngularVelocityName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float3 Out_AngularVelocity) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_AngularVelocity = IncomingAngularVelocity1Buffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetExtentMinName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float Out_ExtentMin) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_ExtentMin = ExtentMinBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetExtentMaxName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float Out_ExtentMax) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_ExtentMax = ExtentMaxBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetVolumeName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float Out_Volume) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Volume = VolumeBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetParticleIdsToSpawnAtTimeName)
	{

		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in float Time, out int Out_Min, 
			                                   out int Out_Max, 
			                                   out int Out_Count) 
			{
				// This function cannot be called on the GPU, as all spawn scripts are run on the CPU..
				// TODO: Find a way to warn/error about this.
				Out_Count = 0;
				Out_Min = 0;
				Out_Max = 0;
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetPointTypeName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out int Out_PointType) 
			{
				// NOTE(mv): Not yet part of the CPU functionality.
				Out_PointType = 0;
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetColorName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float4 Out_Color) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Color = ColorBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetSolverTimeName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(out float Out_SolverTime) 
			{
				Out_SolverTime = SolverTime_{Symbol};
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetDensityName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float Out_Density) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Density = DensityBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetFrictionName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float Out_Friction) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Friction = FrictionBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetRestitutionName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float Out_Restitution) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Restitution = RestitutionBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetTransformName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float3 Out_Translation, out float4 Out_Rotation, out float3 Out_Scale) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Translation = TransformTranslationBuffer_{Symbol}[ParticleID];
				Out_Rotation = TransformRotationBuffer_{Symbol}[ParticleID];
				Out_Scale = TransformScaleBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetSizeName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float3 Out_Size) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Size = BoundsBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetSurfaceTypeName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out int Out_SurfaceType) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_SurfaceType = SurfaceTypeBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetCollisionDataName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float3 Out_Location, 
			                                       out float3 Out_AccumulatedImpulse,
			                                       out float3 Out_Normal,
			                                       out float3 Out_Velocity1,
			                                       out float3 Out_Velocity2,
			                                       out float3 Out_AngularVelocity1,
			                                       out float3 Out_AngularVelocity2,
			                                       out float  Out_Mass1,
			                                       out float  Out_Mass2,
			                                       out float  Out_Time) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Location = IncomingLocationBuffer_{Symbol}[ParticleID];
				Out_AccumulatedImpulse = IncomingAccumulatedImpulseBuffer_{Symbol}[ParticleID];
				Out_Normal = IncomingNormalBuffer_{Symbol}[ParticleID];
				Out_Velocity1 = IncomingVelocity1Buffer_{Symbol}[ParticleID];
				Out_Velocity2 = IncomingVelocity2Buffer_{Symbol}[ParticleID];
				Out_AngularVelocity1 = IncomingAngularVelocity1Buffer_{Symbol}[ParticleID];
				Out_AngularVelocity2 = IncomingAngularVelocity2Buffer_{Symbol}[ParticleID];
				Out_Mass1 = IncomingMass1Buffer_{Symbol}[ParticleID];
				Out_Mass2 = IncomingMass2Buffer_{Symbol}[ParticleID];
				Out_Time = IncomingTimeBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetBreakingDataName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float3 Out_Location,
			                                       out float3 Out_Velocity,
			                                       out float3 Out_AngularVelocity,
			                                       out float  Out_Mass,
			                                       out float  Out_Time) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Location = IncomingLocationBuffer_{Symbol}[ParticleID];
				Out_Velocity = IncomingVelocity1Buffer_{Symbol}[ParticleID];
				Out_AngularVelocity = IncomingAngularVelocity1Buffer_{Symbol}[ParticleID];
				Out_Mass = IncomingMass1Buffer_{Symbol}[ParticleID];
				Out_Time = IncomingTimeBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}
	else if (DefinitionFunctionName == GetTrailingDataName)
	{
		static const TCHAR *Format = TEXT(R"(
			void {FunctionName}(in int ParticleID, out float3 Out_Location,
			                                       out float3 Out_Velocity,
			                                       out float3 Out_AngularVelocity,
			                                       out float  Out_Mass,
			                                       out float  Out_Time) 
			{
				ParticleID -= LastSpawnedPointID_{Symbol} + 1;
				Out_Location = IncomingLocationBuffer_{Symbol}[ParticleID];
				Out_Velocity = IncomingVelocity1Buffer_{Symbol}[ParticleID];
				Out_AngularVelocity = IncomingAngularVelocity1Buffer_{Symbol}[ParticleID];
				Out_Mass = IncomingMass1Buffer_{Symbol}[ParticleID];
				Out_Time = IncomingTimeBuffer_{Symbol}[ParticleID];
			}
		)");

		TMap<FString, FStringFormatArg> Args = {
			{ TEXT("FunctionName"), InstanceFunctionName },
			{ TEXT("Symbol"), ParamInfo.DataInterfaceHLSLSymbol },
		};
		OutHLSL += FString::Format(Format, Args);
		return true;
	}

	return false;
}

template<typename T>
void LoadGPUBufferFromArray(FDynamicReadBuffer& Buffer,
	const TArray<T>* Array,
	const EPixelFormat PixelFormat,
	FString BufferName)
{
	checkf(PixelFormat == PF_A32B32G32R32F ||
		PixelFormat == PF_R32_FLOAT ||
		PixelFormat == PF_G32R32F ||
		PixelFormat == PF_R32_UINT ||
		PixelFormat == PF_R32_SINT,
		TEXT("Unsupported PixelFormat: %d"), PixelFormat);

	// NOTE: float3's have to be padded, so we pass them as PF_A32B32G32R32F and handle them differently
	bool bIsVector = (PixelFormat == PF_A32B32G32R32F && sizeof(T) == 3 * sizeof(float));

	uint32 SizePerElement = bIsVector ? 4 * sizeof(float) : sizeof(T);

	// If not initialized, or if we need to expand the backing data
	if (Buffer.NumBytes == 0 || Buffer.NumBytes < Array->Num() * SizePerElement)
	{
		Buffer.Release();
		Buffer.Initialize(SizePerElement, Array->Num(), PixelFormat, BUF_Dynamic);
	}

	Buffer.Lock();
	if (bIsVector)
	{
		FVector4* Data = (FVector4*)Buffer.MappedBuffer;
		check(Data); // TODO: Handle gracefully.

		for (int32 i = 0; i < Array->Num(); i++) {
			Data[i] = FVector4((*Array)[i]);
		}
	}
	else
	{
		T* Data = (T*)Buffer.MappedBuffer;
		check(Data); // TODO: Handle gracefully.

		for (int32 i = 0; i < Array->Num(); i++) {
			Data[i] = (*Array)[i];
		}
	}

	Buffer.Unlock();
}

static void SetBuffer(FRHICommandList& CmdList,
	const FShaderResourceParameter& Param,
	FRHIComputeShader* Shader,
	FDynamicReadBuffer& Buffer)
{
	// Skip unbound parameters, since we won't be reading any of them
	if (!Param.IsBound()) return;

	CmdList.SetShaderResourceViewParameter(Shader, Param.GetBaseIndex(), Buffer.SRV);
}

template <typename T>
static void SetBuffer(FRHICommandList& CmdList,
	const FShaderResourceParameter& Param,
	FRHIComputeShader* Shader,
	FDynamicReadBuffer& Buffer,
	const TArray<T>& Array,
	const EPixelFormat PixelFormat,
	FString BufferName)
{
	checkf(PixelFormat == PF_A32B32G32R32F ||
		   PixelFormat == PF_R32_FLOAT || 
		   PixelFormat == PF_G32R32F || 
		   PixelFormat == PF_R32_UINT || 
		   PixelFormat == PF_R32_SINT, 
		   TEXT("Unsupported PixelFormat: %d"), PixelFormat);

	// Skip unbound parameters, since we won't be reading any of them
	if (!Param.IsBound()) return;
	
	// NOTE: float3's have to be padded, so we pass them as PF_A32B32G32R32F and handle them differently
	bool bIsVector = (PixelFormat == PF_A32B32G32R32F) && (sizeof(T) == 3*sizeof(float));

	uint32 SizePerElement = bIsVector ? 4*sizeof(float) : sizeof(T); 

	// If not initialized, or if we need to expand the backing data
	if (Buffer.NumBytes == 0 || Buffer.NumBytes < Array.Num() * SizePerElement)
	{
		Buffer.Release();
		Buffer.Initialize(SizePerElement, Array.Num(), PixelFormat, BUF_Dynamic);
	}

	Buffer.Lock();

	static_assert(TIsPODType<T>::Value, "Not POD.");
	
	// NOTE: Reading from `Array` is not thread safe since it belongs to the simulation thread.
	if (bIsVector)
	{
		FVector4* Data = (FVector4*)Buffer.MappedBuffer;
		check(Data); // TODO: Handle gracefully.

		for (int32 i = 0; i < Array.Num(); i++) {
			FPlatformMemory::Memcpy(&Data[i], &Array[i], 3 * sizeof(float)); // TODO: Undefined data in fourth component
		}
	}
	else
	{
		T* Data = (T*)Buffer.MappedBuffer;
		check(Data); // TODO: Handle gracefully.
		uint32 SizeToCopy = Array.Num() * SizePerElement;
		check(Buffer.NumBytes >= SizeToCopy);
		if (Array.Num() > 0)
		{
			FPlatformMemory::Memcpy(Data, &Array[0], SizeToCopy);
		}
	}

	Buffer.Unlock();

	CmdList.SetShaderResourceViewParameter(Shader, Param.GetBaseIndex(), Buffer.SRV);
}

void UNiagaraDataInterfaceChaosDestruction::PushToRenderThread()
{
	check(Proxy);
	TSharedPtr<FNiagaraDataInterfaceProxyChaosDestruction, ESPMode::ThreadSafe> RT_Proxy = StaticCastSharedPtr<FNiagaraDataInterfaceProxyChaosDestruction, FNiagaraDataInterfaceProxy, ESPMode::ThreadSafe>(Proxy);

	int32 RT_LastSpawnedPointID = LastSpawnedPointID;
	float RT_SolverTime = SolverTime;

	ENQUEUE_RENDER_COMMAND(FPushDIChaosDestructionToRT) (
		[RT_Proxy, RT_LastSpawnedPointID, RT_SolverTime](FRHICommandListImmediate& RHICmdList)
		{
			RT_Proxy->LastSpawnedPointID = RT_LastSpawnedPointID;
			RT_Proxy->SolverTime = RT_SolverTime;
		}
	);
}

void UNiagaraDataInterfaceChaosDestruction::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FGuid& SystemInstance)
{
	check(Proxy);

	FNiagaraDIChaosDestruction_InstanceDataToPassToRT* DataToPass = new (DataForRenderThread) FNiagaraDIChaosDestruction_InstanceDataToPassToRT;
	FNDIChaosDestruction_InstanceData* InstanceData = static_cast<FNDIChaosDestruction_InstanceData*>(PerInstanceData);
	check(InstanceData);

	DataToPass->SolverTime = GetSolverTime();
	DataToPass->LastSpawnedPointID = GetLastSpawnedPointID();

	if (InstanceData->PositionArray.Num() > 0)
	{
		DataToPass->PositionArray = new TArray<FVector>(InstanceData->PositionArray);
	}

	if (InstanceData->VelocityArray.Num() > 0)
	{
		DataToPass->VelocityArray = new TArray<FVector>(InstanceData->VelocityArray);
	}

	if (InstanceData->ExtentMinArray.Num() > 0)
	{
		DataToPass->ExtentMinArray = new TArray<float>(InstanceData->ExtentMinArray);
	}

	if (InstanceData->ExtentMaxArray.Num() > 0)
	{
		DataToPass->ExtentMaxArray = new TArray<float>(InstanceData->ExtentMaxArray);
	}

	if (InstanceData->VolumeArray.Num() > 0)
	{
		DataToPass->VolumeArray = new TArray<float>(InstanceData->VolumeArray);
	}

	if (InstanceData->SolverIDArray.Num() > 0)
	{
		DataToPass->SolverIDArray = new TArray<int32>(InstanceData->SolverIDArray);
	}

	if (InstanceData->DensityArray.Num() > 0)
	{
		DataToPass->DensityArray = new TArray<float>(InstanceData->DensityArray);
	}

	if (InstanceData->FrictionArray.Num() > 0)
	{
		DataToPass->FrictionArray = new TArray<float>(InstanceData->FrictionArray);
	}

	if (InstanceData->RestitutionArray.Num() > 0)
	{
		DataToPass->RestitutionArray = new TArray<float>(InstanceData->RestitutionArray);
	}

	if (InstanceData->TransformTranslationArray.Num() > 0)
	{
		DataToPass->TransformTranslationArray = new TArray<FVector>(InstanceData->TransformTranslationArray);
	}

	if (InstanceData->TransformRotationArray.Num() > 0)
	{
		DataToPass->TransformRotationArray = new TArray<FQuat>(InstanceData->TransformRotationArray);
	}

	if (InstanceData->TransformScaleArray.Num() > 0)
	{
		DataToPass->TransformScaleArray = new TArray<FVector>(InstanceData->TransformScaleArray);
	}

	if (InstanceData->BoundsArray.Num() > 0)
	{
		DataToPass->BoundsArray = new TArray<FVector>(InstanceData->BoundsArray);
	}

	if (InstanceData->SurfaceTypeArray.Num() > 0)
	{
		DataToPass->SurfaceTypeArray = new TArray<int32>(InstanceData->SurfaceTypeArray);
	}

	if (InstanceData->ColorArray.Num() > 0)
	{
		DataToPass->ColorArray = new TArray<FLinearColor>(InstanceData->ColorArray);
	}

	if (InstanceData->IncomingLocationArray.Num() > 0)
	{
		DataToPass->IncomingLocationArray = new TArray<FVector>(InstanceData->IncomingLocationArray);
	}

	if (InstanceData->IncomingAccumulatedImpulseArray.Num() > 0)
	{
		DataToPass->IncomingAccumulatedImpulseArray = new TArray<FVector>(InstanceData->IncomingAccumulatedImpulseArray);
	}

	if (InstanceData->IncomingNormalArray.Num() > 0)
	{
		DataToPass->IncomingNormalArray = new TArray<FVector>(InstanceData->IncomingNormalArray);
	}

	if (InstanceData->IncomingVelocity1Array.Num() > 0)
	{
		DataToPass->IncomingVelocity1Array = new TArray<FVector>(InstanceData->IncomingVelocity1Array);
	}

	if (InstanceData->IncomingVelocity2Array.Num() > 0)
	{
		DataToPass->IncomingVelocity2Array = new TArray<FVector>(InstanceData->IncomingVelocity2Array);
	}

	if (InstanceData->IncomingAngularVelocity1Array.Num() > 0)
	{
		DataToPass->IncomingAngularVelocity1Array = new TArray<FVector>(InstanceData->IncomingAngularVelocity1Array);
	}

	if (InstanceData->IncomingAngularVelocity2Array.Num() > 0)
	{
		DataToPass->IncomingAngularVelocity2Array = new TArray<FVector>(InstanceData->IncomingAngularVelocity2Array);
	}

	if (InstanceData->IncomingMass1Array.Num() > 0)
	{
		DataToPass->IncomingMass1Array = new TArray<float>(InstanceData->IncomingMass1Array);
	}

	if (InstanceData->IncomingMass2Array.Num() > 0)
	{
		DataToPass->IncomingMass2Array = new TArray<float>(InstanceData->IncomingMass2Array);
	}

	if (InstanceData->IncomingTimeArray.Num() > 0)
	{
		DataToPass->IncomingTimeArray = new TArray<float>(InstanceData->IncomingTimeArray);
	}
}

void FNiagaraDataInterfaceProxyChaosDestruction::CreatePerInstanceData(const FGuid& SystemInstance)
{
	check(IsInRenderingThread());
	if (SystemsToGPUInstanceData.Contains(SystemInstance))
	{
		InstancesToDestroy.Remove(SystemInstance);
	}
	SystemsToGPUInstanceData.Add(SystemInstance, FNiagaraDIChaosDestruction_GPUData());
}

void FNiagaraDataInterfaceProxyChaosDestruction::DestroyInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FGuid& SystemInstance)
{
	check(IsInRenderingThread());
	// @todo-threadsafety This object contains GPU buffers. This _should_ delete them safely but would we rather do so manually?
	//SystemsToGPUInstanceData.Remove(SystemInstance);
	InstancesToDestroy.Add(SystemInstance);

	Batcher->EnqueueDeferredDeletesForDI_RenderThread(this->AsShared());
}

void FNiagaraDataInterfaceProxyChaosDestruction::ConsumePerInstanceDataFromGameThread(void* PerInstanceDataFromGameThread, const FGuid& Instance) 
{ 
	FNiagaraDIChaosDestruction_GPUData* DataPtr = SystemsToGPUInstanceData.Find(Instance);
	ensure(DataPtr);
	if (!DataPtr)
	{
		return;
	}

	FNiagaraDIChaosDestruction_GPUData& Data = *DataPtr;

	FNiagaraDIChaosDestruction_InstanceDataToPassToRT* InstanceData = static_cast<FNiagaraDIChaosDestruction_InstanceDataToPassToRT*>(PerInstanceDataFromGameThread);
	
	Data.ResetAll();

	Data.SolverTime = InstanceData->SolverTime;
	Data.LastSpawnedPointID = InstanceData->LastSpawnedPointID;

	if (InstanceData->PositionArray)
	{
		Data.PositionArray = TArray<FVector>(MoveTemp(*InstanceData->PositionArray));		
		//LoadGPUBufferFromArray(Data.GPUPositionBuffer, InstanceData->PositionArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("PositionBuffer")));
		delete InstanceData->PositionArray;
	}

	if (InstanceData->VelocityArray)
	{
		Data.VelocityArray = TArray<FVector>(MoveTemp(*InstanceData->VelocityArray));
		//LoadGPUBufferFromArray(Data.GPUVelocityBuffer, InstanceData->VelocityArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("VelocityBuffer")));
		delete InstanceData->VelocityArray;
	}
	
	if (InstanceData->ExtentMinArray)
	{
		Data.ExtentMinArray = TArray<float>(MoveTemp(*InstanceData->ExtentMinArray));
		//LoadGPUBufferFromArray(Data.GPUExtentMinBuffer, InstanceData->ExtentMinArray, EPixelFormat::PF_R32_FLOAT, FString(TEXT("ExtentMinBuffer")));
		delete InstanceData->ExtentMinArray;
	}
	
	if (InstanceData->ExtentMaxArray)
	{
		Data.ExtentMaxArray = TArray<float>(MoveTemp(*InstanceData->ExtentMaxArray));
		//LoadGPUBufferFromArray(Data.GPUExtentMaxBuffer, InstanceData->ExtentMaxArray, EPixelFormat::PF_R32_FLOAT, FString(TEXT("ExtentMaxBuffer")));
		delete InstanceData->ExtentMaxArray;
	}

	if (InstanceData->VolumeArray)
	{
		Data.VolumeArray = TArray<float>(MoveTemp(*InstanceData->VolumeArray));
		//LoadGPUBufferFromArray(Data.GPUVolumeBuffer, InstanceData->VolumeArray, EPixelFormat::PF_R32_FLOAT, FString(TEXT("VolumeBuffer")));
		delete InstanceData->VolumeArray;
	}

	if (InstanceData->SolverIDArray)
	{
		Data.SolverIDArray = TArray<int32>(MoveTemp(*InstanceData->SolverIDArray));
		//LoadGPUBufferFromArray(Data.GPUSolverIDBuffer, InstanceData->SolverIDArray, EPixelFormat::PF_R32_SINT, FString(TEXT("SolverIDBuffer")));
		delete InstanceData->SolverIDArray;
	}

	if (InstanceData->DensityArray)
	{
		Data.DensityArray = TArray<float>(MoveTemp(*InstanceData->DensityArray));
		//LoadGPUBufferFromArray(Data.GPUDensityBuffer, InstanceData->DensityArray, EPixelFormat::PF_R32_FLOAT, FString(TEXT("DensityBuffer")));
		delete InstanceData->DensityArray;
	}

	if (InstanceData->FrictionArray)
	{
		Data.FrictionArray = TArray<float>(MoveTemp(*InstanceData->FrictionArray));
		//LoadGPUBufferFromArray(Data.GPUFrictionBuffer, InstanceData->FrictionArray, EPixelFormat::PF_R32_FLOAT, FString(TEXT("FrictionBuffer")));
		delete InstanceData->FrictionArray;
	}

	if (InstanceData->RestitutionArray)
	{
		Data.RestitutionArray = TArray<float>(MoveTemp(*InstanceData->RestitutionArray));
		//LoadGPUBufferFromArray(Data.GPURestitutionBuffer, InstanceData->RestitutionArray, EPixelFormat::PF_R32_FLOAT, FString(TEXT("RestitutionBuffer")));
		delete InstanceData->RestitutionArray;
	}

	if (InstanceData->TransformTranslationArray)
	{
		Data.TransformTranslationArray = TArray<FVector>(MoveTemp(*InstanceData->TransformTranslationArray));
		//LoadGPUBufferFromArray(Data.GPUSurfaceTypeBuffer, InstanceData->TransformTranslationArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("TransformTranslationBuffer")));
		delete InstanceData->TransformTranslationArray;
	}

	if (InstanceData->TransformRotationArray)
	{
		Data.TransformRotationArray = TArray<FQuat>(MoveTemp(*InstanceData->TransformRotationArray));
		//LoadGPUBufferFromArray(Data.GPUSurfaceTypeBuffer, InstanceData->TransformRotationArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("TransformRotationBuffer")));
		delete InstanceData->TransformRotationArray;
	}

	if (InstanceData->TransformScaleArray)
	{
		Data.TransformScaleArray = TArray<FVector>(MoveTemp(*InstanceData->TransformScaleArray));
		//LoadGPUBufferFromArray(Data.GPUSurfaceTypeBuffer, InstanceData->TransformScaleArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("TransformScaleBuffer")));
		delete InstanceData->TransformScaleArray;
	}

	if (InstanceData->BoundsArray)
	{
		Data.BoundsArray = TArray<FVector>(MoveTemp(*InstanceData->BoundsArray));
		//LoadGPUBufferFromArray(Data.GPUBoundsBuffer, InstanceData->BoundsArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("BoundsBuffer")));
		delete InstanceData->BoundsArray;
	}

	if (InstanceData->SurfaceTypeArray)
	{
		Data.SurfaceTypeArray = TArray<int32>(MoveTemp(*InstanceData->SurfaceTypeArray));
		//LoadGPUBufferFromArray(Data.GPUSurfaceTypeBuffer, InstanceData->SurfaceTypeArray, EPixelFormat::PF_R32_FLOAT, FString(TEXT("RestitutionBuffer")));
		delete InstanceData->SurfaceTypeArray;
	}

	if (InstanceData->ColorArray)
	{
		Data.ColorArray = TArray<FLinearColor>(MoveTemp(*InstanceData->ColorArray));
		//LoadGPUBufferFromArray(Data.GPUColorBuffer, InstanceData->ColorArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("ColorBuffer")));
		delete InstanceData->ColorArray;
	}

	if (InstanceData->IncomingLocationArray)
	{
		Data.IncomingLocationArray = TArray<FVector>(MoveTemp(*InstanceData->IncomingLocationArray));
		//LoadGPUBufferFromArray(Data.GPUIncomingLocationBuffer, InstanceData->IncomingLocationArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingLocationBuffer")));
		delete InstanceData->IncomingLocationArray;
	}

	if (InstanceData->IncomingAccumulatedImpulseArray)
	{
		Data.IncomingAccumulatedImpulseArray = TArray<FVector>(MoveTemp(*InstanceData->IncomingAccumulatedImpulseArray));
		//LoadGPUBufferFromArray(Data.GPUIncomingAccumulatedImpulseBuffer, InstanceData->IncomingAccumulatedImpulseArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingAccumulatedImpulseBuffer")));
		delete InstanceData->IncomingAccumulatedImpulseArray;
	}

	if (InstanceData->IncomingNormalArray)
	{
		Data.IncomingNormalArray = TArray<FVector>(MoveTemp(*InstanceData->IncomingNormalArray));
		//LoadGPUBufferFromArray(Data.GPUIncomingNormalBuffer, InstanceData->IncomingNormalArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingNormalBuffer")));
		delete InstanceData->IncomingNormalArray;
	}

	if (InstanceData->IncomingVelocity1Array)
	{
		Data.IncomingVelocity1Array = TArray<FVector>(MoveTemp(*InstanceData->IncomingVelocity1Array));
		//LoadGPUBufferFromArray(Data.GPUIncomingVelocity1Buffer, InstanceData->IncomingVelocity1Array, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingVelocity1Buffer")));
		delete InstanceData->IncomingVelocity1Array;
	}

	if (InstanceData->IncomingVelocity2Array)
	{
		Data.IncomingVelocity2Array = TArray<FVector>(MoveTemp(*InstanceData->IncomingVelocity2Array));
		//LoadGPUBufferFromArray(Data.GPUIncomingVelocity2Buffer, InstanceData->IncomingVelocity2Array, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingVelocity2Buffer")));
		delete InstanceData->IncomingVelocity2Array;
	}

	if (InstanceData->IncomingAngularVelocity1Array)
	{
		Data.IncomingAngularVelocity1Array = TArray<FVector>(MoveTemp(*InstanceData->IncomingAngularVelocity1Array));
		//LoadGPUBufferFromArray(Data.GPUIncomingAngularVelocity1Buffer, InstanceData->IncomingAngularVelocity1Array, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingAngularVelocity1Buffer")));
		delete InstanceData->IncomingAngularVelocity1Array;
	}

	if (InstanceData->IncomingAngularVelocity2Array)
	{
		Data.IncomingAngularVelocity2Array = TArray<FVector>(MoveTemp(*InstanceData->IncomingAngularVelocity2Array));
		//LoadGPUBufferFromArray(Data.GPUIncomingAngularVelocity2Buffer, InstanceData->IncomingAngularVelocity2Array, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingAngularVelocity2Buffer")));
		delete InstanceData->IncomingAngularVelocity2Array;
	}
	
	if (InstanceData->IncomingMass1Array)
	{
		Data.IncomingMass1Array = TArray<float>(MoveTemp(*InstanceData->IncomingMass1Array));
		//LoadGPUBufferFromArray(Data.GPUIncomingMass1Buffer, InstanceData->IncomingMass1Array, EPixelFormat::PF_R32_FLOAT, FString(TEXT("IncomingMass1Buffer")));
		delete InstanceData->IncomingMass1Array;
	}

	if (InstanceData->IncomingMass2Array)
	{
		Data.IncomingMass2Array = TArray<float>(MoveTemp(*InstanceData->IncomingMass2Array));
		//LoadGPUBufferFromArray(Data.GPUIncomingMass2Buffer, InstanceData->IncomingMass2Array, EPixelFormat::PF_R32_FLOAT, FString(TEXT("IncomingMass2Buffer")));
		delete InstanceData->IncomingMass2Array;
	}

	if (InstanceData->IncomingTimeArray)
	{
		Data.IncomingTimeArray = TArray<float>(MoveTemp(*InstanceData->IncomingTimeArray));
		//LoadGPUBufferFromArray(Data.GPUIncomingTimeBuffer, InstanceData->IncomingTimeArray, EPixelFormat::PF_R32_FLOAT, FString(TEXT("IncomingTimeBuffer")));
		delete InstanceData->IncomingTimeArray;
	}
}

struct FNiagaraDataInterfaceParametersCS_ChaosDestruction : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		PositionBuffer.Bind(ParameterMap, *("PositionBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		VelocityBuffer.Bind(ParameterMap, *("VelocityBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		ExtentMinBuffer.Bind(ParameterMap, *("ExtentMinBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		ExtentMaxBuffer.Bind(ParameterMap, *("ExtentMaxBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		VolumeBuffer.Bind(ParameterMap, *("VolumeBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		SolverIDBuffer.Bind(ParameterMap, *("SolverIDBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		DensityBuffer.Bind(ParameterMap, *("DensityBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		FrictionBuffer.Bind(ParameterMap, *("FrictionBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		RestitutionBuffer.Bind(ParameterMap, *("RestitutionBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		SurfaceTypeBuffer.Bind(ParameterMap, *("SurfaceTypeBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		ColorBuffer.Bind(ParameterMap, *("ColorBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		IncomingLocationBuffer.Bind(ParameterMap, *("IncomingLocationBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		IncomingAccumulatedImpulseBuffer.Bind(ParameterMap, *("IncomingAccumulatedImpulseBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		IncomingNormalBuffer.Bind(ParameterMap, *("IncomingNormalBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		IncomingVelocity1Buffer.Bind(ParameterMap, *("IncomingVelocity1Buffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		IncomingVelocity2Buffer.Bind(ParameterMap, *("IncomingVelocity2Buffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		IncomingAngularVelocity1Buffer.Bind(ParameterMap, *("IncomingAngularVelocity1Buffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		IncomingAngularVelocity2Buffer.Bind(ParameterMap, *("IncomingAngularVelocity2Buffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		IncomingMass1Buffer.Bind(ParameterMap, *("IncomingMass1Buffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		IncomingMass2Buffer.Bind(ParameterMap, *("IncomingMass2Buffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		IncomingTimeBuffer.Bind(ParameterMap, *("IncomingTimeBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		TransformTranslationBuffer.Bind(ParameterMap, *("TransformTranslationBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		TransformRotationBuffer.Bind(ParameterMap, *("TransformRotationBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		TransformScaleBuffer.Bind(ParameterMap, *("TransformScaleBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		BoundsBuffer.Bind(ParameterMap, *("BoundsBuffer_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		LastSpawnedPointID.Bind(ParameterMap, *("LastSpawnedPointID_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		SolverTime.Bind(ParameterMap, *("SolverTime_" + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
	}

	virtual void Serialize(FArchive& Ar)override
	{
		Ar << PositionBuffer;
		Ar << VelocityBuffer;
		Ar << ExtentMinBuffer;
		Ar << ExtentMaxBuffer;
		Ar << VolumeBuffer;
		Ar << SolverIDBuffer;
		Ar << DensityBuffer;
		Ar << FrictionBuffer;
		Ar << RestitutionBuffer;
		Ar << SurfaceTypeBuffer;
		Ar << ColorBuffer;

		Ar << IncomingLocationBuffer;
		Ar << IncomingAccumulatedImpulseBuffer;
		Ar << IncomingNormalBuffer;
		Ar << IncomingVelocity1Buffer;
		Ar << IncomingVelocity2Buffer;
		Ar << IncomingAngularVelocity1Buffer;
		Ar << IncomingAngularVelocity2Buffer;
		Ar << IncomingMass1Buffer;
		Ar << IncomingMass2Buffer;
		Ar << IncomingTimeBuffer;

		Ar << TransformTranslationBuffer;
		Ar << TransformRotationBuffer;
		Ar << TransformScaleBuffer;
		Ar << BoundsBuffer;

		Ar << LastSpawnedPointID;
		Ar << SolverTime;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();
		FNiagaraDataInterfaceProxyChaosDestruction* ChaosDestructionInterfaceProxy = static_cast<FNiagaraDataInterfaceProxyChaosDestruction*>(Context.DataInterface);
		if (ChaosDestructionInterfaceProxy)
		{
			FNiagaraDIChaosDestruction_GPUData* InstanceData = ChaosDestructionInterfaceProxy->SystemsToGPUInstanceData.Find(Context.SystemInstance);

			ensure(InstanceData);

			if (!InstanceData)
			{
				return;
			}
			if(InstanceData->PositionArray.Num() > 0)
			{
				SetBuffer(RHICmdList, PositionBuffer,    ComputeShaderRHI, InstanceData->GPUPositionBuffer,    InstanceData->PositionArray,    EPixelFormat::PF_A32B32G32R32F, FString(TEXT("PositionBuffer")));
				SetBuffer(RHICmdList, VelocityBuffer,    ComputeShaderRHI, InstanceData->GPUVelocityBuffer,    InstanceData->VelocityArray,    EPixelFormat::PF_A32B32G32R32F, FString(TEXT("VelocityBuffer")));
				SetBuffer(RHICmdList, ExtentMinBuffer,   ComputeShaderRHI, InstanceData->GPUExtentMinBuffer,   InstanceData->ExtentMinArray,   EPixelFormat::PF_R32_FLOAT,     FString(TEXT("ExtentMinBuffer")));
				SetBuffer(RHICmdList, ExtentMaxBuffer,   ComputeShaderRHI, InstanceData->GPUExtentMaxBuffer,   InstanceData->ExtentMaxArray,   EPixelFormat::PF_R32_FLOAT,     FString(TEXT("ExtentMaxBuffer")));
				SetBuffer(RHICmdList, VolumeBuffer,      ComputeShaderRHI, InstanceData->GPUVolumeBuffer,      InstanceData->VolumeArray,      EPixelFormat::PF_R32_FLOAT,     FString(TEXT("VolumeBuffer")));
				SetBuffer(RHICmdList, SolverIDBuffer,    ComputeShaderRHI, InstanceData->GPUSolverIDBuffer,    InstanceData->SolverIDArray,    EPixelFormat::PF_R32_SINT,      FString(TEXT("SolverIDBuffer")));
				SetBuffer(RHICmdList, DensityBuffer,     ComputeShaderRHI, InstanceData->GPUDensityBuffer,     InstanceData->DensityArray,     EPixelFormat::PF_R32_FLOAT,     FString(TEXT("DensityBuffer")));
				SetBuffer(RHICmdList, FrictionBuffer,    ComputeShaderRHI, InstanceData->GPUFrictionBuffer,    InstanceData->FrictionArray,    EPixelFormat::PF_R32_FLOAT,     FString(TEXT("FrictionBuffer")));
				SetBuffer(RHICmdList, RestitutionBuffer, ComputeShaderRHI, InstanceData->GPURestitutionBuffer, InstanceData->RestitutionArray, EPixelFormat::PF_R32_FLOAT,     FString(TEXT("RestitutionBuffer")));
				SetBuffer(RHICmdList, SurfaceTypeBuffer, ComputeShaderRHI, InstanceData->GPUSurfaceTypeBuffer, InstanceData->SurfaceTypeArray, EPixelFormat::PF_R32_SINT,      FString(TEXT("SurfaceTypeBuffer")));
				SetBuffer(RHICmdList, ColorBuffer,       ComputeShaderRHI, InstanceData->GPUColorBuffer,       InstanceData->ColorArray,       EPixelFormat::PF_A32B32G32R32F, FString(TEXT("ColorBuffer")));

				SetBuffer(RHICmdList, IncomingLocationBuffer,           ComputeShaderRHI, InstanceData->GPUIncomingLocationBuffer,           InstanceData->IncomingLocationArray,           EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingLocationBuffer")));
				SetBuffer(RHICmdList, IncomingAccumulatedImpulseBuffer, ComputeShaderRHI, InstanceData->GPUIncomingAccumulatedImpulseBuffer, InstanceData->IncomingAccumulatedImpulseArray, EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingAccumulatedImpulseBuffer")));
				SetBuffer(RHICmdList, IncomingNormalBuffer,             ComputeShaderRHI, InstanceData->GPUIncomingNormalBuffer,             InstanceData->IncomingNormalArray,             EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingNormalBuffer")));
				SetBuffer(RHICmdList, IncomingVelocity1Buffer,          ComputeShaderRHI, InstanceData->GPUIncomingVelocity1Buffer,          InstanceData->IncomingVelocity1Array,          EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingVelocity1Buffer")));
				SetBuffer(RHICmdList, IncomingVelocity2Buffer,          ComputeShaderRHI, InstanceData->GPUIncomingVelocity2Buffer,          InstanceData->IncomingVelocity2Array,          EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingVelocity2Buffer")));
				SetBuffer(RHICmdList, IncomingAngularVelocity1Buffer,   ComputeShaderRHI, InstanceData->GPUIncomingAngularVelocity1Buffer,   InstanceData->IncomingAngularVelocity1Array,   EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingAngularVelocity1Buffer")));
				SetBuffer(RHICmdList, IncomingAngularVelocity2Buffer,   ComputeShaderRHI, InstanceData->GPUIncomingAngularVelocity2Buffer,   InstanceData->IncomingAngularVelocity2Array,   EPixelFormat::PF_A32B32G32R32F, FString(TEXT("IncomingAngularVelocity2Buffer")));
				SetBuffer(RHICmdList, IncomingMass1Buffer,              ComputeShaderRHI, InstanceData->GPUIncomingMass1Buffer,              InstanceData->IncomingMass1Array,              EPixelFormat::PF_R32_FLOAT,     FString(TEXT("IncomingMass1Buffer")));
				SetBuffer(RHICmdList, IncomingMass2Buffer,              ComputeShaderRHI, InstanceData->GPUIncomingMass2Buffer,              InstanceData->IncomingMass2Array,              EPixelFormat::PF_R32_FLOAT,     FString(TEXT("IncomingMass2Buffer")));
				SetBuffer(RHICmdList, IncomingTimeBuffer,               ComputeShaderRHI, InstanceData->GPUIncomingTimeBuffer,               InstanceData->IncomingTimeArray,               EPixelFormat::PF_R32_FLOAT,     FString(TEXT("IncomingTimeBuffer")));
				
				SetBuffer(RHICmdList, TransformTranslationBuffer,       ComputeShaderRHI, InstanceData->GPUTransformTranslationBuffer,       InstanceData->TransformTranslationArray,       EPixelFormat::PF_A32B32G32R32F, FString(TEXT("TransformTranslationBuffer")));
				SetBuffer(RHICmdList, TransformRotationBuffer,          ComputeShaderRHI, InstanceData->GPUTransformRotationBuffer,          InstanceData->TransformRotationArray,          EPixelFormat::PF_A32B32G32R32F, FString(TEXT("TransformRotationBuffer")));
				SetBuffer(RHICmdList, TransformScaleBuffer,             ComputeShaderRHI, InstanceData->GPUTransformScaleBuffer,             InstanceData->TransformScaleArray,             EPixelFormat::PF_A32B32G32R32F, FString(TEXT("TransformScaleBuffer")));
				SetBuffer(RHICmdList, BoundsBuffer,                     ComputeShaderRHI, InstanceData->GPUBoundsBuffer,                     InstanceData->BoundsArray,                     EPixelFormat::PF_A32B32G32R32F, FString(TEXT("BoundsBuffer")));

				SetShaderValue(RHICmdList, ComputeShaderRHI, LastSpawnedPointID, InstanceData->LastSpawnedPointID);
				SetShaderValue(RHICmdList, ComputeShaderRHI, SolverTime, InstanceData->SolverTime);
			}
		}
	}

private:
	// TODO: Collect these into a small number of buffers to reduce the number of binding points
	FShaderResourceParameter PositionBuffer;
	FShaderResourceParameter VelocityBuffer;
	FShaderResourceParameter ExtentMinBuffer;
	FShaderResourceParameter ExtentMaxBuffer;
	FShaderResourceParameter VolumeBuffer;
	FShaderResourceParameter SolverIDBuffer;
	FShaderResourceParameter DensityBuffer;
	FShaderResourceParameter FrictionBuffer;
	FShaderResourceParameter RestitutionBuffer;
	FShaderResourceParameter SurfaceTypeBuffer;
	FShaderResourceParameter ColorBuffer;

	FShaderResourceParameter IncomingLocationBuffer;
	FShaderResourceParameter IncomingAccumulatedImpulseBuffer;
	FShaderResourceParameter IncomingNormalBuffer;
	FShaderResourceParameter IncomingVelocity1Buffer;
	FShaderResourceParameter IncomingVelocity2Buffer;
	FShaderResourceParameter IncomingAngularVelocity1Buffer;
	FShaderResourceParameter IncomingAngularVelocity2Buffer;
	FShaderResourceParameter IncomingMass1Buffer;
	FShaderResourceParameter IncomingMass2Buffer;
	FShaderResourceParameter IncomingTimeBuffer;

	FShaderResourceParameter TransformTranslationBuffer;
	FShaderResourceParameter TransformRotationBuffer;
	FShaderResourceParameter TransformScaleBuffer;
	FShaderResourceParameter BoundsBuffer;

	FShaderParameter LastSpawnedPointID;
	FShaderParameter SolverTime;
};

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceChaosDestruction::ConstructComputeParameters()const
{
	return new FNiagaraDataInterfaceParametersCS_ChaosDestruction();
}
//#pragma optimize("", on)



#undef LOCTEXT_NAMESPACE
