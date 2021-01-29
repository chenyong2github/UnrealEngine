// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Field/FieldSystem.h"
#include "GeometryCollection/RecordedTransformTrack.h"
#include "GeometryCollectionSimulationTypes.h"
#include "Chaos/ClusterCreationParameters.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"

class FGeometryCollection;
class FGeometryDynamicCollection;

struct FSharedSimulationSizeSpecificData
{
	FSharedSimulationSizeSpecificData()
		: MaxSize(0.f)
		, CollisionType(ECollisionTypeEnum::Chaos_Surface_Volumetric)
		, ImplicitType(EImplicitTypeEnum::Chaos_Implicit_Sphere)
		, MinLevelSetResolution(5)
		, MaxLevelSetResolution(10)
		, MinClusterLevelSetResolution(25)
		, MaxClusterLevelSetResolution(50)
		, CollisionObjectReductionPercentage(0.f)
		, CollisionParticlesFraction(1.f)
		, MaximumCollisionParticles(60)
		, DamageThreshold(250.f)
	{
	}

	float MaxSize;
	ECollisionTypeEnum CollisionType;
	EImplicitTypeEnum ImplicitType;
	int32 MinLevelSetResolution;
	int32 MaxLevelSetResolution;
	int32 MinClusterLevelSetResolution;
	int32 MaxClusterLevelSetResolution;
	float CollisionObjectReductionPercentage;
	float CollisionParticlesFraction;
	int32 MaximumCollisionParticles;
	float DamageThreshold;

	bool operator<(const FSharedSimulationSizeSpecificData& Rhs) const { return MaxSize < Rhs.MaxSize; }
};

//
//
//
enum ESimulationInitializationState { Unintialized = 0, Activated, Created, Initialized };


/**
*  Simulation Parameters
*/
struct FSharedSimulationParameters
{
	FSharedSimulationParameters()
	: bMassAsDensity(false)
	, Mass(1.0)
	, MinimumMassClamp(0.1)								// todo : Expose to users with better initial values
	, MaximumMassClamp(1e5f)							// todo : Expose to users with better initial values
	, MinimumBoundingExtentClamp(0.1)					// todo : Expose to users with better initial values
	, MaximumBoundingExtentClamp(1e6f)					// todo : Expose to users with better initial values
	, MinimumInertiaTensorDiagonalClamp(SMALL_NUMBER)	// todo : Expose to users with better initial values
	, MaximumInertiaTensorDiagonalClamp(1e20f)			// todo : Expose to users with better initial values
	, MaximumCollisionParticleCount(60)
	{
		SizeSpecificData.AddDefaulted();
	}

	FSharedSimulationParameters(ECollisionTypeEnum InCollisionType
		,EImplicitTypeEnum InImplicitType
		,int32 InMinLevelSetResolution
		,int32 InMaxLevelSetResolution
		,int32 InMinClusterLevelSetResolution
		,int32 InMaxClusterLevelSetResolution
		,bool InMassAsDensity
		,float InMass
		, float InMinimumMassClamp
		, float InMaximumMassClamp
		, float InMinimumBoundingExtentClamp
		, float InMaximumBoundingExtentClamp
		, float InMinimumInertiaTensorDiagonalClamp
		, float InMaximumInertiaTensorDiagonalClamp
		,float InCollisionParticlesFraction
		,int32 InMaximumCollisionParticleCount)
	: bMassAsDensity(InMassAsDensity)
	, Mass(InMass)
	, MinimumMassClamp(InMinimumMassClamp)
	, MaximumMassClamp(InMinimumMassClamp)
	, MinimumBoundingExtentClamp(InMinimumBoundingExtentClamp)
	, MaximumBoundingExtentClamp(InMinimumBoundingExtentClamp)
	, MinimumInertiaTensorDiagonalClamp(InMinimumInertiaTensorDiagonalClamp)
	, MaximumInertiaTensorDiagonalClamp(InMaximumInertiaTensorDiagonalClamp)
	, MaximumCollisionParticleCount(InMaximumCollisionParticleCount)
	{
		SizeSpecificData.AddDefaulted();
		SizeSpecificData[0].CollisionType = InCollisionType;
		SizeSpecificData[0].ImplicitType = InImplicitType;
		SizeSpecificData[0].MinLevelSetResolution = InMinLevelSetResolution;
		SizeSpecificData[0].MaxLevelSetResolution = InMaxLevelSetResolution;
		SizeSpecificData[0].MinClusterLevelSetResolution = InMinClusterLevelSetResolution;
		SizeSpecificData[0].MaxClusterLevelSetResolution = InMaxClusterLevelSetResolution;
		SizeSpecificData[0].CollisionParticlesFraction = InCollisionParticlesFraction;
		SizeSpecificData[0].MaximumCollisionParticles = InMaximumCollisionParticleCount;
	}

	bool bMassAsDensity;
	float Mass;
	float MinimumMassClamp;
	float MaximumMassClamp;
	float MinimumBoundingExtentClamp;
	float MaximumBoundingExtentClamp;
	float MinimumInertiaTensorDiagonalClamp;
	float MaximumInertiaTensorDiagonalClamp;

	float MinimumVolumeClamp() const { return MinimumBoundingExtentClamp * MinimumBoundingExtentClamp * MinimumBoundingExtentClamp; }
	float MaximumVolumeClamp() const { return MaximumBoundingExtentClamp * MaximumBoundingExtentClamp * MaximumBoundingExtentClamp; }

	TArray<FSharedSimulationSizeSpecificData> SizeSpecificData;
	TArray<int32> RemoveOnFractureIndices;
	int32 MaximumCollisionParticleCount;
};

struct FCollisionDataSimulationParameters
{
	FCollisionDataSimulationParameters()
		: DoGenerateCollisionData(false)
		, SaveCollisionData(false)
		, CollisionDataSizeMax(512)
		, DoCollisionDataSpatialHash(false)
		, CollisionDataSpatialHashRadius(50.f)
		, MaxCollisionPerCell(1)
	{}

	FCollisionDataSimulationParameters(bool InDoGenerateCollisionData
		, bool InSaveCollisionData
		, int32 InCollisionDataSizeMax
		, bool InDoCollisionDataSpatialHash
		, float InCollisionDataSpatialHashRadius
		, int32 InMaxCollisionPerCell)
		: DoGenerateCollisionData(InDoGenerateCollisionData)
		, SaveCollisionData(InSaveCollisionData)
		, CollisionDataSizeMax(InCollisionDataSizeMax)
		, DoCollisionDataSpatialHash(InDoCollisionDataSpatialHash)
		, CollisionDataSpatialHashRadius(InCollisionDataSpatialHashRadius)
		, MaxCollisionPerCell(InMaxCollisionPerCell)
	{}

	bool DoGenerateCollisionData;
	bool SaveCollisionData;
	int32 CollisionDataSizeMax;
	bool DoCollisionDataSpatialHash;
	float CollisionDataSpatialHashRadius;
	int32 MaxCollisionPerCell;

	FCollisionFilterData QueryData;
	FCollisionFilterData SimData;
};

struct FBreakingDataSimulationParameters
{
	FBreakingDataSimulationParameters()
		: DoGenerateBreakingData(false)
		, SaveBreakingData(false)
		, BreakingDataSizeMax(512)
		, DoBreakingDataSpatialHash(false)
		, BreakingDataSpatialHashRadius(15.f)
		, MaxBreakingPerCell(1)
	{}

	FBreakingDataSimulationParameters(bool InDoGenerateBreakingData
		, bool InSaveBreakingData
		, int32 InBreakingDataSizeMax
		, bool InDoBreakingDataSpatialHash
		, float InBreakingDataSpatialHashRadius
		, int32 InMaxBreakingPerCell)
		: DoGenerateBreakingData(InDoGenerateBreakingData)
		, SaveBreakingData(InSaveBreakingData)
		, BreakingDataSizeMax(InBreakingDataSizeMax)
		, DoBreakingDataSpatialHash(InDoBreakingDataSpatialHash)
		, BreakingDataSpatialHashRadius(InBreakingDataSpatialHashRadius)
		, MaxBreakingPerCell(InMaxBreakingPerCell)
	{}

	bool DoGenerateBreakingData;
	bool SaveBreakingData;
	int32 BreakingDataSizeMax;
	bool DoBreakingDataSpatialHash;
	float BreakingDataSpatialHashRadius;
	int32 MaxBreakingPerCell;
};

struct FTrailingDataSimulationParameters
{
	FTrailingDataSimulationParameters()
		: DoGenerateTrailingData(false)
		, SaveTrailingData(false)
		, TrailingDataSizeMax(512)
		, TrailingMinSpeedThreshold(200.f)
		, TrailingMinVolumeThreshold(10000.f)
	{}

	FTrailingDataSimulationParameters(bool InDoGenerateTrailingData
		, bool InSaveTrailingData
		, int32 InTrailingDataSizeMax
		, float InTrailingMinSpeedThreshold
		, float InTrailingMinVolumeThreshold)
		: DoGenerateTrailingData(InDoGenerateTrailingData)
		, SaveTrailingData(InSaveTrailingData)
		, TrailingDataSizeMax(InTrailingDataSizeMax)
		, TrailingMinSpeedThreshold(InTrailingMinSpeedThreshold)
		, TrailingMinVolumeThreshold(InTrailingMinVolumeThreshold)
	{}

	bool DoGenerateTrailingData;
	bool SaveTrailingData;
	int32 TrailingDataSizeMax;
	float TrailingMinSpeedThreshold;
	float TrailingMinVolumeThreshold;
};

struct FSimulationParameters
{
	FSimulationParameters()
		: Name("")
		, RestCollection(nullptr)
		, RecordedTrack(nullptr)
		, bOwnsTrack(false)
		, Simulating(false)
		, WorldTransform(FTransform::Identity)
		, EnableClustering(true)
		, ClusterGroupIndex(0)
		, MaxClusterLevel(100)
		, bUseSizeSpecificDamageThresholds(false)
		, DamageThreshold({250.f})
		, ClusterConnectionMethod(Chaos::FClusterCreationParameters<float>::EConnectionMethod::PointImplicit)
		, CollisionGroup(0)
		, CollisionSampleFraction(1.0)
		, InitialVelocityType(EInitialVelocityTypeEnum::Chaos_Initial_Velocity_None)
		, InitialLinearVelocity(FVector(0))
		, InitialAngularVelocity(FVector(0))
		, CacheType(EGeometryCollectionCacheType::None)
		, CacheBeginTime(0.0f)
		, ReverseCacheBeginTime(0.0f)
		, bClearCache(false)
		, RemoveOnFractureEnabled(false)
		, SimulationFilterData()
		, QueryFilterData()
		, UserData(nullptr)
	{}

	FSimulationParameters(const FSimulationParameters& Other)
		: Name(Other.Name)
		, RestCollection(Other.RestCollection)
		, InitializationCommands(Other.InitializationCommands)
		, RecordedTrack(Other.RecordedTrack)
		, bOwnsTrack(false)
		, Simulating(Other.Simulating)
		, WorldTransform(Other.WorldTransform)
		, EnableClustering(Other.EnableClustering)
		, ClusterGroupIndex(Other.ClusterGroupIndex)
		, MaxClusterLevel(Other.MaxClusterLevel)
		, bUseSizeSpecificDamageThresholds(Other.bUseSizeSpecificDamageThresholds)
		, DamageThreshold(Other.DamageThreshold)
		, ClusterConnectionMethod(Other.ClusterConnectionMethod)
		, CollisionGroup(Other.CollisionGroup)
		, CollisionSampleFraction(Other.CollisionSampleFraction)
		, InitialVelocityType(Other.InitialVelocityType)
		, InitialLinearVelocity(Other.InitialLinearVelocity)
		, InitialAngularVelocity(Other.InitialAngularVelocity)
		, CacheType(Other.CacheType)
		, CacheBeginTime(Other.CacheBeginTime)
		, ReverseCacheBeginTime(Other.ReverseCacheBeginTime)
		, bClearCache(Other.bClearCache)
		, PhysicalMaterialHandle(Other.PhysicalMaterialHandle)
		, CollisionData(Other.CollisionData)
		, BreakingData(Other.BreakingData)
		, TrailingData(Other.TrailingData)
		, Shared(Other.Shared)
		, RemoveOnFractureEnabled(false)
		, SimulationFilterData(Other.SimulationFilterData)
		, QueryFilterData(Other.QueryFilterData)
		, UserData(Other.UserData)
	{
	}

	~FSimulationParameters()
	{
		if (bOwnsTrack)
		{
			delete const_cast<FRecordedTransformTrack*>(RecordedTrack);
		}
	}

	bool IsCacheRecording() { return CacheType == EGeometryCollectionCacheType::Record || CacheType == EGeometryCollectionCacheType::RecordAndPlay; }
	bool IsCachePlaying() { return CacheType == EGeometryCollectionCacheType::Play || CacheType == EGeometryCollectionCacheType::RecordAndPlay; }

	FString Name;
	const FGeometryCollection* RestCollection;
	TArray<FFieldSystemCommand> InitializationCommands;
	const FRecordedTransformTrack* RecordedTrack;
	bool bOwnsTrack;

	bool Simulating;

	FTransform WorldTransform;

	bool EnableClustering;
	int32 ClusterGroupIndex;
	int32 MaxClusterLevel;
	bool bUseSizeSpecificDamageThresholds;
	TArray<float> DamageThreshold;
	Chaos::FClusterCreationParameters<float>::EConnectionMethod ClusterConnectionMethod;

	int32 CollisionGroup;
	float CollisionSampleFraction;

	EInitialVelocityTypeEnum InitialVelocityType;
	FVector InitialLinearVelocity;
	FVector InitialAngularVelocity;

	EGeometryCollectionCacheType CacheType;
	float CacheBeginTime;
	float ReverseCacheBeginTime;
	bool bClearCache;

	EObjectStateTypeEnum ObjectType;

	Chaos::FMaterialHandle PhysicalMaterialHandle;

	FCollisionDataSimulationParameters CollisionData;
	FBreakingDataSimulationParameters BreakingData;
	FTrailingDataSimulationParameters TrailingData;

	FSharedSimulationParameters Shared;

	bool RemoveOnFractureEnabled;

	FCollisionFilterData SimulationFilterData;
	FCollisionFilterData QueryFilterData;
	void* UserData;
};
