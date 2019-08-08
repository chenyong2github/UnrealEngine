// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "VectorVM.h"
#include "NiagaraDataInterface.h"
#include "Chaos/ChaosSolver.h"
#include "EventsData.h"
#include "Chaos/ChaosSolverActor.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "NiagaraDataInterfaceChaosDestruction.generated.h"

#if INCLUDE_CHAOS
struct PhysicsProxyWrapper;
#endif

USTRUCT()
struct FChaosDestructionEvent
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "Chaos Destruction Event")
	FVector Position;

	UPROPERTY(EditAnywhere, Category = "Chaos Destruction Event")
	FVector Normal;

	UPROPERTY(EditAnywhere, Category = "Chaos Destruction Event")
	FVector Velocity;

	UPROPERTY(EditAnywhere, Category = "Chaos Destruction Event")
	FVector AngularVelocity;

	UPROPERTY(EditAnywhere, Category = "Chaos Destruction Event")
	float ExtentMin;

	UPROPERTY(EditAnywhere, Category = "Chaos Destruction Event")
	float ExtentMax;

	UPROPERTY(EditAnywhere, Category = "Chaos Destruction Event")
	int32 ParticleID;

	UPROPERTY(EditAnywhere, Category = "Chaos Destruction Event")
	float Time;

	UPROPERTY(EditAnywhere, Category = "Chaos Destruction Event")
	int32 Type;

	FChaosDestructionEvent()
		: Position(FVector::ZeroVector)
		, Normal(FVector::ZeroVector)
		, Velocity(FVector::ZeroVector)
		, AngularVelocity(FVector::ZeroVector)
		, ExtentMin(0.f)
		, ExtentMax(0.f)
		, ParticleID()
		, Time(0.f)
		, Type(-1)
	{
	}

	inline bool operator==(const FChaosDestructionEvent& Other) const
	{
		if ((Other.Position != Position) || (Other.Normal != Normal)
			|| (Other.Velocity != Velocity)
			|| (Other.AngularVelocity != AngularVelocity)
			|| (Other.ExtentMin != ExtentMin)
			|| (Other.ExtentMax != ExtentMax)
			|| (Other.ParticleID != ParticleID) || (Other.Time != Time)
			|| (Other.Type != Type))
			return false;

		return true;
	}

	inline bool operator!=(const FChaosDestructionEvent& Other) const
	{
		return !(*this == Other);
	}
};

#if INCLUDE_CHAOS
struct FSolverData
{
	FSolverData()
		: Solver(nullptr)
	{}

	TSharedPtr<FPhysScene_Chaos> PhysScene;
	Chaos::FPhysicsSolver* Solver;
};
#endif

struct FNDIChaosDestruction_InstanceData
{
	TArray<FVector> PositionArray;
	TArray<FVector> VelocityArray;
	TArray<float> ExtentMinArray;
	TArray<float> ExtentMaxArray;
	TArray<float> VolumeArray;
	TArray<int32> SolverIDArray;
	TArray<float> DensityArray;
	TArray<float> FrictionArray;
	TArray<float> RestitutionArray;
	TArray<int32> SurfaceTypeArray;
	TArray<FLinearColor> ColorArray;

	TArray<FVector> IncomingLocationArray;				// Collision, Breaking, Trailing
	TArray<FVector> IncomingAccumulatedImpulseArray;	// Collision
	TArray<FVector> IncomingNormalArray;				// Collision
	TArray<FVector> IncomingVelocity1Array;				// Collision, Breaking, Trailing
	TArray<FVector> IncomingVelocity2Array;				// Collision
	TArray<FVector> IncomingAngularVelocity1Array;		// Collision, Breaking, Trailing
	TArray<FVector> IncomingAngularVelocity2Array;		// Collision
	TArray<float> IncomingMass1Array;					// Collision, Breaking, Trailing
	TArray<float> IncomingMass2Array;					// Collision
	TArray<float> IncomingTimeArray;					// Collision, Breaking, Trailing

	TArray<FVector> TransformTranslationArray;
	TArray<FQuat> TransformRotationArray;
	TArray<FVector> TransformScaleArray;
	TArray<FVector> BoundsArray;
};

struct FNiagaraDIChaosDestruction_GPUData
{
	TArray<FVector> PositionArray;
	TArray<FVector> VelocityArray;
	TArray<float> ExtentMinArray;
	TArray<float> ExtentMaxArray;
	TArray<float> VolumeArray;
	TArray<int32> SolverIDArray;
	TArray<float> DensityArray;
	TArray<float> FrictionArray;
	TArray<float> RestitutionArray;
	TArray<int32> SurfaceTypeArray;
	TArray<FLinearColor> ColorArray;

	TArray<FVector> IncomingLocationArray;				// Collision, Breaking, Trailing
	TArray<FVector> IncomingAccumulatedImpulseArray;	// Collision
	TArray<FVector> IncomingNormalArray;				// Collision
	TArray<FVector> IncomingVelocity1Array;			// Collision, Breaking, Trailing
	TArray<FVector> IncomingVelocity2Array;			// Collision
	TArray<FVector> IncomingAngularVelocity1Array;		// Collision, Breaking, Trailing
	TArray<FVector> IncomingAngularVelocity2Array;		// Collision
	TArray<float> IncomingMass1Array;					// Collision, Breaking, Trailing
	TArray<float> IncomingMass2Array;					// Collision
	TArray<float> IncomingTimeArray;					// Collision, Breaking, Trailing

	TArray<FVector> TransformTranslationArray;
	TArray<FQuat> TransformRotationArray;
	TArray<FVector> TransformScaleArray;
	TArray<FVector> BoundsArray;

	void ResetAll()
	{
		PositionArray.Reset();
		VelocityArray.Reset();
		ExtentMinArray.Reset();
		ExtentMaxArray.Reset();
		VolumeArray.Reset();
		SolverIDArray.Reset();
		DensityArray.Reset();
		FrictionArray.Reset();
		RestitutionArray.Reset();
		SurfaceTypeArray.Reset();
		ColorArray.Reset();

		IncomingLocationArray.Reset();
		IncomingAccumulatedImpulseArray.Reset();
		IncomingNormalArray.Reset();
		IncomingVelocity1Array.Reset();
		IncomingVelocity2Array.Reset();
		IncomingAngularVelocity1Array.Reset();
		IncomingAngularVelocity2Array.Reset();
		IncomingMass1Array.Reset();
		IncomingMass2Array.Reset();
		IncomingTimeArray.Reset();
		
		TransformTranslationArray.Reset();
		TransformRotationArray.Reset();
		TransformScaleArray.Reset();
		BoundsArray.Reset();
	}

	FDynamicReadBuffer GPUPositionBuffer;
	FDynamicReadBuffer GPUVelocityBuffer;
	FDynamicReadBuffer GPUExtentMinBuffer;
	FDynamicReadBuffer GPUExtentMaxBuffer;
	FDynamicReadBuffer GPUVolumeBuffer;
	FDynamicReadBuffer GPUSolverIDBuffer;
	FDynamicReadBuffer GPUDensityBuffer;
	FDynamicReadBuffer GPUFrictionBuffer;
	FDynamicReadBuffer GPURestitutionBuffer;
	FDynamicReadBuffer GPUSurfaceTypeBuffer;
	FDynamicReadBuffer GPUColorBuffer;

	FDynamicReadBuffer GPUIncomingLocationBuffer;
	FDynamicReadBuffer GPUIncomingAccumulatedImpulseBuffer;
	FDynamicReadBuffer GPUIncomingNormalBuffer;
	FDynamicReadBuffer GPUIncomingVelocity1Buffer;
	FDynamicReadBuffer GPUIncomingVelocity2Buffer;
	FDynamicReadBuffer GPUIncomingAngularVelocity1Buffer;
	FDynamicReadBuffer GPUIncomingAngularVelocity2Buffer;
	FDynamicReadBuffer GPUIncomingMass1Buffer;
	FDynamicReadBuffer GPUIncomingMass2Buffer;
	FDynamicReadBuffer GPUIncomingTimeBuffer;

	FDynamicReadBuffer GPUTransformTranslationBuffer;
	FDynamicReadBuffer GPUTransformRotationBuffer;
	FDynamicReadBuffer GPUTransformScaleBuffer;
	FDynamicReadBuffer GPUBoundsBuffer;

	float SolverTime;
	int32 LastSpawnedPointID;
};

struct FNiagaraDIChaosDestruction_InstanceDataToPassToRT
{
	TArray<FVector>* PositionArray;
	TArray<FVector>* VelocityArray;
	TArray<float>* ExtentMinArray;
	TArray<float>* ExtentMaxArray;
	TArray<float>* VolumeArray;
	TArray<int32>* SolverIDArray;
	TArray<float>* DensityArray;
	TArray<float>* FrictionArray;
	TArray<float>* RestitutionArray;
	TArray<int32>* SurfaceTypeArray;
	TArray<FLinearColor>* ColorArray;

	TArray<FVector>* IncomingLocationArray;				// Collision, Breaking, Trailing
	TArray<FVector>* IncomingAccumulatedImpulseArray;	// Collision
	TArray<FVector>* IncomingNormalArray;				// Collision
	TArray<FVector>* IncomingVelocity1Array;			// Collision, Breaking, Trailing
	TArray<FVector>* IncomingVelocity2Array;			// Collision
	TArray<FVector>* IncomingAngularVelocity1Array;		// Collision, Breaking, Trailing
	TArray<FVector>* IncomingAngularVelocity2Array;		// Collision
	TArray<float>* IncomingMass1Array;					// Collision, Breaking, Trailing
	TArray<float>* IncomingMass2Array;					// Collision
	TArray<float>* IncomingTimeArray;					// Collision, Breaking, Trailing
	
	TArray<FVector>* TransformTranslationArray; // Breaking
	TArray<FQuat>* TransformRotationArray; // Breaking
	TArray<FVector>* TransformScaleArray; // Breaking
	TArray<FVector>* BoundsArray; // Breaking

	float SolverTime;
	int32 LastSpawnedPointID;

	FNiagaraDIChaosDestruction_InstanceDataToPassToRT()
	{
		FMemory::Memzero(this, sizeof(FNiagaraDIChaosDestruction_InstanceDataToPassToRT));
	}
};

UENUM(BlueprintType)
enum class EDataSortTypeEnum : uint8
{
	ChaosNiagara_DataSortType_NoSorting UMETA(DisplayName = "No Sorting"),
	ChaosNiagara_DataSortType_RandomShuffle UMETA(DisplayName = "Random Shuffle"),
	ChaosNiagara_DataSortType_SortByMassMaxToMin UMETA(DisplayName = "Sort by Mass - Max to Min"),
	ChaosNiagara_DataSortType_SortByMassMinToMax UMETA(DisplayName = "Sort by Mass - Min to Max"),
	//~~~
	//256th entry
	ChaosNiagara_Max                UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ERandomVelocityGenerationTypeEnum : uint8
{
	ChaosNiagara_RandomVelocityGenerationType_RandomDistribution UMETA(DisplayName = "Random Distribution"),
	ChaosNiagara_RandomVelocityGenerationType_RandomDistributionWithStreamers UMETA(DisplayName = "Random Distribution with Streamers"),
	ChaosNiagara_RandomVelocityGenerationType_CollisionNormalBased UMETA(DisplayName = "Collision Normal Based (Collision Data Only)"),
//	ChaosNiagara_RandomVelocityGenerationType_NRandomSpread UMETA(DisplayName = "N Random Spread"),
	//~~~
	//256th entry
	ChaosNiagara_Max                UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EDebugTypeEnum : uint8
{
	ChaosNiagara_DebugType_NoDebug UMETA(DisplayName = "No Debug"),
	ChaosNiagara_DebugType_ColorBySolver UMETA(DisplayName = "Color by Solver"),
	ChaosNiagara_DebugType_ColorByParticleIndex UMETA(DisplayName = "Color by ParticleIndex"),
	//~~~
	//256th entry
	ChaosNiagara_Max                UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EDataSourceTypeEnum : uint8
{
	ChaosNiagara_DataSourceType_Collision UMETA(DisplayName = "Collision Data"),
	ChaosNiagara_DataSourceType_Breaking UMETA(DisplayName = "Breaking Data"),
	ChaosNiagara_DataSourceType_Trailing UMETA(DisplayName = "Trailing Data"),
	//~~~
	//256th entry
	ChaosNiagara_Max                UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ELocationFilteringModeEnum : uint8
{
	ChaosNiagara_LocationFilteringMode_Inclusive UMETA(DisplayName = "Inclusive"),
	ChaosNiagara_LocationFilteringMode_Exclusive UMETA(DisplayName = "Exclusive"),
	//~~~
	//256th entry
	ChaosNiagara_Max                UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ELocationXToSpawnEnum : uint8
{
	ChaosNiagara_LocationXToSpawn_None UMETA(DisplayName = "None"),
	ChaosNiagara_LocationXToSpawn_Min UMETA(DisplayName = "Min <= LocationX"),
	ChaosNiagara_LocationXToSpawn_Max UMETA(DisplayName = "LocationX <= Max"),
	ChaosNiagara_LocationXToSpawn_MinMax UMETA(DisplayName = "Min <= LocationX <= Max"),
	//~~~
	//256th entry
	ChaosNiagara_Max                UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ELocationYToSpawnEnum : uint8
{
	ChaosNiagara_LocationYToSpawn_None UMETA(DisplayName = "None"),
	ChaosNiagara_LocationYToSpawn_Min UMETA(DisplayName = "Min <= LocationY"),
	ChaosNiagara_LocationYToSpawn_Max UMETA(DisplayName = "LocationY <= Max"),
	ChaosNiagara_LocationYToSpawn_MinMax UMETA(DisplayName = "Min <= LocationY <= Max"),
	//~~~
	//256th entry
	ChaosNiagara_Max                UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ELocationZToSpawnEnum : uint8
{
	ChaosNiagara_LocationZToSpawn_None UMETA(DisplayName = "None"),
	ChaosNiagara_LocationZToSpawn_Min UMETA(DisplayName = "Min <= LocationZ"),
	ChaosNiagara_LocationZToSpawn_Max UMETA(DisplayName = "LocationZ <= Max"),
	ChaosNiagara_LocationZToSpawn_MinMax UMETA(DisplayName = "Min <= LocationZ <= Max"),
	//~~~
	//256th entry
	ChaosNiagara_Max                UMETA(Hidden)
};

/** Data Interface allowing sampling of Chaos Destruction data. */
UCLASS(EditInlineNew, Category = "Chaos Niagara", meta = (DisplayName = "Chaos Destruction Data"))
class CHAOSNIAGARA_API UNiagaraDataInterfaceChaosDestruction : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	/* Chaos Solver */
	UPROPERTY(EditAnywhere, Category = "Solver", meta = (DisplayName = "Chaos Solver"))
	TSet<AChaosSolverActor*> ChaosSolverActorSet;

	/* */
	UPROPERTY(EditAnywhere, Category = "Solver Data", meta = (DisplayName = "Data Source"))
	EDataSourceTypeEnum DataSourceType;

	/* Number of times the RBD collision data gets processed every second */
	UPROPERTY(EditAnywhere, Category = "Solver Data", meta = (DisplayName = "Data Process Frequency", UIMin = 0))
	int32 DataProcessFrequency;

	/* Maximum number of collision/breaking/trailing entry used for spawning particles every time data from the physics solver gets processed */
	UPROPERTY(EditAnywhere, Category = "Solver Data", meta = (DisplayName = "Max Number of Data to Spawn Particles", UIMin = 0))
	int32 MaxNumberOfDataEntriesToSpawn;

	/* Turn on/off particle spawning */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings", meta = (DisplayName = "Spawn Particles"))
	bool DoSpawn;

	/* For every collision random number of particles will be spawned in the range of [SpawnMultiplierMin, SpawnMultiplierMax]  */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings", meta = (DisplayName = "Spawn Count Min/Max", UIMin = 0))
	FVector2D SpawnMultiplierMinMax;

	/* For every collision random number of particles will be spawned in the range of [SpawnMultiplierMin, SpawnMultiplierMax]  */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings", meta = (DisplayName = "Chance to Spawn", UIMin = 0.f, UIMax = 1.f))
	float SpawnChance;

	/* Min/Max collision accumulated impulse to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max Collision Impulse To Spawn Particles", UIMin = 0.0))
	FVector2D ImpulseToSpawnMinMax;

	/* Min/Max speed to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max Speed To Spawn Particles", UIMin = 0.0))
	FVector2D SpeedToSpawnMinMax;

	/* Min/Max mass to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max Mass To Spawn Particles", UIMin = 0.0))
	FVector2D MassToSpawnMinMax;

	/* Min/Max ExtentMin to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max ExtentMin To Spawn Particles", UIMin = 0.0))
	FVector2D ExtentMinToSpawnMinMax;

	/* Min/Max ExtentMax to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max ExtentMax To Spawn Particles", UIMin = 0.0))
	FVector2D ExtentMaxToSpawnMinMax;

	/* Min/Max volume to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max Volume To Spawn Particles", UIMin = 0.0))
	FVector2D VolumeToSpawnMinMax;

	/* Min/Max solver time mass to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max SolverTime To Spawn Particles", UIMin = 0.0))
	FVector2D SolverTimeToSpawnMinMax;

	/* SurfaceType to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "SurfaceType To Spawn Particles", UIMin = 0.0))
	int32 SurfaceTypeToSpawn;

	/* Location Filtering Mode */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Location Filtering Mode"))
	ELocationFilteringModeEnum LocationFilteringMode;

	/* How to use LocationX to filter */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "LocationX To Spawn Particles"))
	ELocationXToSpawnEnum LocationXToSpawn;

	/* Min/Max LocationX to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max LocationX To Spawn Particles", UIMin = 0.0))
	FVector2D LocationXToSpawnMinMax;

	/* How to use LocationY to filter */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "LocationY To Spawn Particles"))
	ELocationYToSpawnEnum LocationYToSpawn;

	/* Min/Max LocationY to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max LocationY To Spawn Particles", UIMin = 0.0))
	FVector2D LocationYToSpawnMinMax;

	/* How to use LocationZ to filter */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "LocationZ To Spawn Particles"))
	ELocationZToSpawnEnum LocationZToSpawn;

	/* Min/Max LocationX to spawn particles */
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Thresholds to Spawn", meta = (DisplayName = "Min/Max LocationZ To Spawn Particles", UIMin = 0.0))
	FVector2D LocationZToSpawnMinMax;

	/**
	* Sorting method to sort the collision data
	*/
	UPROPERTY(EditAnywhere, Category = "Spawn Settings - Sorting Solver Data", meta = (DisplayName = "Sorting Method"))
	EDataSortTypeEnum DataSortingType;

	/* TODO: Explanatory comment */
	UPROPERTY(VisibleAnywhere, Category = "Collision Data Settings", meta = (DisplayName = "Get External Collision Mesh and Physical Data"))
	bool bGetExternalCollisionData;

	/*  */
	UPROPERTY(EditAnywhere, Category = "Collision Data Settings", meta = (DisplayName = "Spatial Hash Collision/Breaking Data", UIMin = 0.0))
	bool DoSpatialHash;

	/* SpatialHash volume min */
	UPROPERTY(EditAnywhere, Category = "SpatialHash Settings", meta = (DisplayName = "SpatialHash Volume Min"))
	FVector SpatialHashVolumeMin;

	/* SpatialHash volume max */
	UPROPERTY(EditAnywhere, Category = "SpatialHash Settings", meta = (DisplayName = "SpatialHash Volume Max"))
	FVector SpatialHashVolumeMax;

	/* SpatialHash volume resolution */
	UPROPERTY(EditAnywhere, Category = "SpatialHash Settings", meta = (DisplayName = "SpatialHash Volume CellSize"))
	FVector SpatialHashVolumeCellSize;


	UPROPERTY(EditAnywhere, Category = "SpatialHash Settings", meta = (DisplayName = "Max Number of Collision/Breaking Per Cell", UIMin = 1))
	int32 MaxDataPerCell;

	/* Materials Filter */
	UPROPERTY(EditAnywhere, Category = "Breaking Data Settings", meta = (DisplayName = "Apply Materials Filter"))
	bool bApplyMaterialsFilter;

	/* TODO: Explanatory comment */
	UPROPERTY(EditAnywhere, Category = "Breaking Data Settings", meta = (DisplayName = "Breaking Filtered Materials", EditCondition = bApplyMaterialsFilter))
	TSet<UPhysicalMaterial*> ChaosBreakingMaterialSet;

	/* TODO: Explanatory comment */
	UPROPERTY(EditAnywhere, Category = "Breaking Data Settings", meta = (DisplayName = "Get External Breaking Mesh and Physical Data"))
	bool bGetExternalBreakingData;

	/* TODO: Explanatory comment */
	UPROPERTY(VisibleAnywhere, Category = "Trailing Data Settings", meta = (DisplayName = "Get External Trailing Mesh and Physical Data"))
	bool bGetExternalTrailingData;

	/* Random displacement value for the particle spawn position */
	UPROPERTY(EditAnywhere, Category = "Spawn Position Settings", meta = (DisplayName = "Position Random Spread Magnitude Min/Max", UIMin = 0.0))
	FVector2D RandomPositionMagnitudeMinMax;

	/* How much of the collision velocity gets inherited */
	UPROPERTY(EditAnywhere, Category = "Velocity Settings - Inherited Velocity", meta = (DisplayName = "Inherited Velocity Multiplier"))
	float InheritedVelocityMultiplier;

	/**
	* The method used to create the random velocities for the newly spawned particles
	*/
	UPROPERTY(EditAnywhere, Category = "Velocity Settings - Random Generation", meta = (DisplayName = "Velocity Model"))
	ERandomVelocityGenerationTypeEnum RandomVelocityGenerationType;

	/* Every particles will be spawned with random velocity with magnitude in the range of [RandomVelocityMagnitudeMin, RandomVelocityMagnitudeMax] */
	UPROPERTY(EditAnywhere, Category = "Velocity Settings", meta = (DisplayName = "Velocity Random Magnitude Min/Max", UIMin = 0.0))
	FVector2D RandomVelocityMagnitudeMinMax;

	/**/
	UPROPERTY(EditAnywhere, Category = "Velocity Settings - Collision Normal Based Velocity Model", meta = (DisplayName = "Spread Angle Max [Degrees]", UIMin = 0.0))
	float SpreadAngleMax;

	/* Min Offset value added to spawned particles velocity */
	UPROPERTY(EditAnywhere, Category = "Velocity Settings", meta = (DisplayName = "Velocity Offset Min"))
	FVector VelocityOffsetMin;

	/* Max Offset value added to spawned particles velocity */
	UPROPERTY(EditAnywhere, Category = "Velocity Settings", meta = (DisplayName = "Velocity Offset Max"))
	FVector VelocityOffsetMax;

	/* Clamp particles velocity */
	UPROPERTY(EditAnywhere, Category = "Velocity Settings", meta = (DisplayName = "Final Velocity Magnitude Maximum"))
	FVector2D FinalVelocityMagnitudeMinMax;

	UPROPERTY(EditAnywhere, Category = "Spawn Settings", meta = (DisplayName = "Max Latency"))
	float MaxLatency;

	/* Debug visualization method */
	UPROPERTY(EditAnywhere, Category = "Debug Settings", meta = (DisplayName = "Debug Visualization"))
	EDebugTypeEnum DebugType;

	/* ParticleIndex to process collisionData for */
//	UPROPERTY(EditAnywhere, Category = "Debug Settings", meta = (DisplayName = "ParticleIndex to Process"))
//	TGeometryParticleHandle<float, 3>* ParticleToProcess;

	//----------------------------------------------------------------------------
	// UObject Interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	//----------------------------------------------------------------------------
	// UNiagaraDataInterface Interface
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize() const override;

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true || (Target == ENiagaraSimTarget::CPUSim); }

	//----------------------------------------------------------------------------
	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override; 
	virtual bool GetFunctionHLSL(const FName&  DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;

	//----------------------------------------------------------------------------
	// EXPOSED FUNCTIONS
	template<typename ParticleIDParamType>
	void GetPosition(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetNormal(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetVelocity(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetAngularVelocity(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetExtentMin(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetExtentMax(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetVolume(FVectorVMContext& Context);

	template<typename TimeParamType>
	void GetParticleIdsToSpawnAtTime(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetPointType(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetColor(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetSolverTime(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetDensity(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetFriction(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetRestitution(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetSize(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetTransform(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetSurfaceType(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetCollisionData(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetBreakingData(FVectorVMContext& Context);

	template<typename ParticleIDParamType>
	void GetTrailingData(FVectorVMContext& Context);

	// Sort predicates to sort data
	inline static bool CollisionDataSortByMassPredicateMaxToMin(const Chaos::TCollisionDataExt<float, 3>& Lhs, const Chaos::TCollisionDataExt<float, 3>& Rhs)
	{
		return FMath::Max(Lhs.Mass1, Lhs.Mass2) > FMath::Max(Rhs.Mass1, Rhs.Mass2);
	}

	inline static bool CollisionDataSortByMassPredicateMinToMax(const Chaos::TCollisionDataExt<float, 3>& Lhs, const Chaos::TCollisionDataExt<float, 3>& Rhs)
	{
		return FMath::Max(Lhs.Mass1, Lhs.Mass2) < FMath::Max(Rhs.Mass1, Rhs.Mass2);
	}

	inline static bool CollisionDataRandomShuffleSortPredicate(const Chaos::TCollisionDataExt<float, 3>& Lhs, const Chaos::TCollisionDataExt<float, 3>& Rhs)
	{
		return FMath::FRand() < 0.5f;
	}

	inline static bool BreakingDataSortByMassPredicateMaxToMin(const Chaos::TBreakingDataExt<float, 3>& Lhs, const Chaos::TBreakingDataExt<float, 3>& Rhs)
	{
		return Lhs.Mass > Rhs.Mass;
	}

	inline static bool BreakingDataSortByMassPredicateMinToMax(const Chaos::TBreakingDataExt<float, 3>& Lhs, const Chaos::TBreakingDataExt<float, 3>& Rhs)
	{
		return Lhs.Mass < Rhs.Mass;
	}

	inline static bool BreakingDataRandomShuffleSortPredicate(const Chaos::TBreakingDataExt<float, 3>& Lhs, const Chaos::TBreakingDataExt<float, 3>& Rhs)
	{
		return FMath::FRand() < 0.5f;
	}

	inline static bool TrailingDataSortByMassPredicateMaxToMin(const Chaos::TTrailingDataExt<float, 3>& Lhs, const Chaos::TTrailingDataExt<float, 3>& Rhs)
	{
		return Lhs.Mass > Rhs.Mass;
	}

	inline static bool TrailingDataSortByMassPredicateMinToMax(const Chaos::TTrailingDataExt<float, 3>& Lhs, const Chaos::TTrailingDataExt<float, 3>& Rhs)
	{
		return Lhs.Mass < Rhs.Mass;
	}

	inline static bool TrailingDataRandomShuffleSortPredicate(const Chaos::TTrailingDataExt<float, 3>& Lhs, const Chaos::TTrailingDataExt<float, 3>& Rhs)
	{
		return FMath::FRand() < 0.5f;
	}

	inline int32 GetLastSpawnedPointID()
	{
		return LastSpawnedPointID;
	}
	inline float GetSolverTime()
	{
		return SolverTime;
	}

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FGuid& SystemInstance) override;
protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

#if INCLUDE_CHAOS
	void ResetInstData(FNDIChaosDestruction_InstanceData* InstData);

	void HandleCollisionEvents(const Chaos::FCollisionEventData& Event);
	void FilterAllCollisions(TArray<Chaos::TCollisionDataExt<float, 3>>& AllCollisionsArray);

	void SortCollisions(TArray<Chaos::TCollisionDataExt<float, 3>>& CollisionsArray);

	void GetCollisionsToSpawnFromCollisions(TArray<Chaos::TCollisionDataExt<float, 3>>& AllCollisionsArray,
							 			    TArray<Chaos::TCollisionDataExt<float, 3>>& CollisionsToSpawnArray);

	int32 SpawnParticlesFromCollision(FSolverData SolverData,
									  Chaos::TCollisionDataExt<float, 3>& Collision,
									  FNDIChaosDestruction_InstanceData* InstData,
									  float TimeData_MapsCreated,
									  int32 IdxSolver);
	
	void HandleBreakingEvents(const Chaos::FBreakingEventData& Event);
	void FilterAllBreakings(TArray<Chaos::TBreakingDataExt<float, 3>>& AllBreakingsArray);

	void SortBreakings(TArray<Chaos::TBreakingDataExt<float, 3>>& BreakingsArray);

	void GetBreakingsToSpawnFromBreakings(TArray<Chaos::TBreakingDataExt<float, 3>>& AllBreakingsArray,
										  TArray<Chaos::TBreakingDataExt<float, 3>>& BreakingsToSpawnArray);

	int32 SpawnParticlesFromBreaking(FSolverData SolverData,
									 Chaos::TBreakingDataExt<float, 3>& Breaking,
									 FNDIChaosDestruction_InstanceData* InstData,
									 float TimeData_MapsCreated,
									 int32 IdxSolver);
	   	  
	void HandleTrailingEvents(const Chaos::FTrailingEventData& Event);
	void FilterAllTrailings(TArray<Chaos::TTrailingDataExt<float, 3>>& AllTrailingsArray);

	void SortTrailings(TArray<Chaos::TTrailingDataExt<float, 3>>& TrailingsArray);

	void GetTrailingsToSpawnFromTrailings(TArray<Chaos::TTrailingDataExt<float, 3>>& AllTrailingsArray,
										  TArray<Chaos::TTrailingDataExt<float, 3>>& TrailingsToSpawnArray);

	int32 SpawnParticlesFromTrailing(FSolverData SolverData,
									 Chaos::TTrailingDataExt<float, 3>& Trailing,
									 FNDIChaosDestruction_InstanceData* InstData,
									 float TimeData_MapsCreated,
									 int32 IdxSolver);
	   	  
	bool CollisionCallback(FNDIChaosDestruction_InstanceData* InstData);
	bool BreakingCallback(FNDIChaosDestruction_InstanceData* InstData);
	bool TrailingCallback(FNDIChaosDestruction_InstanceData* InstData);
	   
#endif

	void PushToRenderThread();

	UPROPERTY()
	int32 LastSpawnedPointID;

	UPROPERTY()
	float LastSpawnTime;

	// Colors for debugging particles
	TArray<FVector> ColorArray;

	UPROPERTY()
	float SolverTime;

	UPROPERTY()
	float TimeStampOfLastProcessedData;

	bool ShouldSpawn;

#if INCLUDE_CHAOS
	TArray<FSolverData> Solvers;
	TArray<Chaos::TCollisionDataExt<float, 3>> CollisionEvents;
	TArray<Chaos::TBreakingDataExt<float, 3>> BreakingEvents;
	TArray<Chaos::TTrailingDataExt<float, 3>> TrailingEvents;

#endif

};

struct FNiagaraDataInterfaceProxyChaosDestruction : public FNiagaraDataInterfaceProxy
{
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FGuid& Instance) override;
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return sizeof(FNiagaraDIChaosDestruction_InstanceDataToPassToRT);
	}

	void CreatePerInstanceData(const FGuid& SystemInstance);

	void DestroyInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FGuid& SystemInstance);

	virtual void DeferredDestroy()
	{
		for (const FGuid& SystemInstance : InstancesToDestroy)
		{
			SystemsToGPUInstanceData.Remove(SystemInstance);
		}

		InstancesToDestroy.Empty();
	}

	float SolverTime;
	int32 LastSpawnedPointID;

	TMap<FGuid, FNiagaraDIChaosDestruction_GPUData> SystemsToGPUInstanceData;
	TSet<FGuid> InstancesToDestroy;
};