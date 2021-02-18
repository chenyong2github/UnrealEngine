// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WaterBodyTypes.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "BuoyancyTypes.generated.h"

class UBuoyancyComponent;

USTRUCT(Blueprintable)
struct FSphericalPontoon
{
	GENERATED_BODY()

	/** The socket to center this pontoon on */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Buoyancy)
	FName CenterSocket;

	/** Relative Location of pontoon WRT parent actor. Overridden by Center Socket. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Buoyancy)
	FVector RelativeLocation;

	/** The radius of the pontoon */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Buoyancy)
	float Radius;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	FVector LocalForce;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	FVector CenterLocation;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	FQuat SocketRotation;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	FVector Offset;

	float PontoonCoefficient;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	float WaterHeight;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	float WaterDepth;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	float ImmersionDepth;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	FVector WaterPlaneLocation;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	FVector WaterPlaneNormal;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	FVector WaterSurfacePosition;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	FVector WaterVelocity;

	UPROPERTY(BlueprintReadOnly, Category = Buoyancy)
	int32 WaterBodyIndex;

	FTransform SocketTransform;

	TMap<const AWaterBody*, float> SplineInputKeys;
	TMap<const AWaterBody*, float> SplineSegments;

	TMap<const FSolverSafeWaterBodyData*, float> SolverSplineInputKeys;
	TMap<const FSolverSafeWaterBodyData*, float> SolverSplineSegments;

	uint8 bIsInWater : 1;
	uint8 bEnabled : 1;
	uint8 bUseCenterSocket : 1;
	UPROPERTY(Transient, BlueprintReadOnly, Category = Buoyancy)
	AWaterBody* CurrentWaterBody;

	FSolverSafeWaterBodyData* SolverWaterBody;

	FSphericalPontoon()
		: RelativeLocation(FVector::ZeroVector)
		, Radius(100.f)
		, LocalForce(FVector::ZeroVector)
		, CenterLocation(FVector::ZeroVector)
		, SocketRotation(FQuat::Identity)
		, Offset(FVector::ZeroVector)
		, PontoonCoefficient(1.f)
		, WaterHeight(-10000.f)
		, WaterDepth(0.f)
		, ImmersionDepth(0.f)
		, WaterPlaneLocation(FVector::ZeroVector)
		, WaterPlaneNormal(FVector::UpVector)
		, WaterSurfacePosition(FVector::ZeroVector)
		, WaterVelocity(FVector::ZeroVector)
		, WaterBodyIndex(0)
		, SocketTransform(FTransform::Identity)
		, bIsInWater(false)
		, bEnabled(true)
		, bUseCenterSocket(false)
		, CurrentWaterBody(nullptr)
		, SolverWaterBody(nullptr)
	{
	}

	void CopyDataFromPT(const FSphericalPontoon& PTPontoon)
	{
		LocalForce = PTPontoon.LocalForce;
		CenterLocation = PTPontoon.CenterLocation;
		SocketRotation = PTPontoon.SocketRotation;
		WaterHeight = PTPontoon.WaterHeight;
		bIsInWater = PTPontoon.bIsInWater;
		ImmersionDepth = PTPontoon.ImmersionDepth;
		WaterDepth = PTPontoon.WaterDepth;
		WaterPlaneLocation = PTPontoon.WaterPlaneLocation;
		WaterPlaneNormal = PTPontoon.WaterPlaneNormal;
		WaterSurfacePosition = PTPontoon.WaterSurfacePosition;
		WaterVelocity = PTPontoon.WaterVelocity;
		WaterBodyIndex = PTPontoon.WaterBodyIndex;
		CurrentWaterBody = PTPontoon.CurrentWaterBody;
	}

	//void CopyDataToPT(const FSphericalPontoon& GTPontoon)
	//{
	//	CenterSocket = GTPontoon.CenterSocket;
	//	RelativeLocation = GTPontoon.RelativeLocation;
	//	Radius = GTPontoon.Radius;
	//	Offset = GTPontoon.Offset;
	//	PontoonCoefficient = GTPontoon.PontoonCoefficient;
	//	SocketTransform = GTPontoon.SocketTransform;
	//	bEnabled = GTPontoon.bEnabled;
	//	bUseCenterSocket = GTPontoon.bUseCenterSocket;
	//}

	void Serialize(FArchive& Ar)
	{
		Ar << CenterSocket;
		Ar << RelativeLocation;
		Ar << Radius;
		Ar << LocalForce;
		Ar << CenterLocation;
		Ar << SocketRotation;
		Ar << Offset;
		Ar << PontoonCoefficient;
		Ar << WaterHeight;
		Ar << WaterDepth;
		Ar << ImmersionDepth;
		Ar << WaterPlaneLocation;
		Ar << WaterPlaneNormal;
		Ar << WaterSurfacePosition;
		Ar << WaterVelocity;
		Ar << WaterBodyIndex;
		Ar << SocketTransform;
		uint8 IsInWater = bIsInWater;
		uint8 Enabled = bEnabled;
		uint8 UseCenterSocket = bUseCenterSocket;
		Ar << IsInWater;
		Ar << Enabled;
		Ar << UseCenterSocket;
		bIsInWater = IsInWater;
		bEnabled = Enabled;
		bUseCenterSocket = UseCenterSocket;
	}
};

USTRUCT(Blueprintable)
struct FBuoyancyData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Buoyancy)
	TArray<FSphericalPontoon> Pontoons;

	/** Increases buoyant force applied on each pontoon. */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float BuoyancyCoefficient;

	/** Damping factor to scale damping based on Z velocity. */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float BuoyancyDamp;

	/**Second Order Damping factor to scale damping based on Z velocity. */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float BuoyancyDamp2;

	/** Minimum velocity to start applying a ramp to buoyancy. */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float BuoyancyRampMinVelocity;

	/** Maximum velocity until which the buoyancy can ramp up. */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float BuoyancyRampMaxVelocity;

	/** Maximum value that buoyancy can ramp to (at or beyond max velocity). */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float BuoyancyRampMax;

	/** Maximum buoyant force in the Up direction. */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float MaxBuoyantForce;

	/** Coefficient for nudging objects to shore (for perf reasons). */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float WaterShorePushFactor;

	/** Coefficient for applying push force in rivers. */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float WaterVelocityStrength;

	/** Maximum push force that can be applied by rivers. */
	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	float MaxWaterForce;

	UPROPERTY(EditDefaultsOnly, Category = Buoyancy, Meta = (EditCondition = "bApplyDragForcesInWater"))
	float DragCoefficient = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = Buoyancy, Meta = (EditCondition = "bApplyDragForcesInWater"))
	float DragCoefficient2 = 0.01f;

	UPROPERTY(EditDefaultsOnly, Category = Buoyancy, Meta = (EditCondition = "bApplyDragForcesInWater"))
	float AngularDragCoefficient = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = Buoyancy, Meta = (EditCondition = "bApplyDragForcesInWater"))
	float MaxDragSpeed = 15.f;

	UPROPERTY(EditDefaultsOnly, Category = Buoyancy)
	bool bApplyDragForcesInWater = false;

	FBuoyancyData()
		: BuoyancyCoefficient(0.1f)
		, BuoyancyDamp(1000.f)
		, BuoyancyDamp2(1.f)
		, BuoyancyRampMinVelocity(20.f)
		, BuoyancyRampMaxVelocity(50.f)
		, BuoyancyRampMax(1.f)
		, MaxBuoyantForce(5000000.f)
		, WaterShorePushFactor(0.3f)
		, WaterVelocityStrength(0.01f)
		, MaxWaterForce(10000.f)
	{
	}

	void Serialize(FArchive& Ar)
	{
		int32 NumPontoons = Pontoons.Num();
		Ar << NumPontoons;
		if (Ar.IsLoading())
		{
			Pontoons.SetNum(NumPontoons);
		}
		for (int32 Index = 0; Index < NumPontoons; ++Index)
		{
			Pontoons[Index].Serialize(Ar);
		}
		Ar << BuoyancyCoefficient;
		Ar << BuoyancyDamp;
		Ar << BuoyancyDamp2;
		Ar << BuoyancyRampMinVelocity;
		Ar << BuoyancyRampMaxVelocity;
		Ar << BuoyancyRampMax;
		Ar << MaxBuoyantForce;
		Ar << WaterShorePushFactor;
		Ar << WaterVelocityStrength;
		Ar << MaxWaterForce;
		Ar << DragCoefficient;
		Ar << DragCoefficient2;
		Ar << AngularDragCoefficient;
		Ar << MaxDragSpeed;
		Ar << bApplyDragForcesInWater;
	}
};

/* async structs */

enum EAsyncBuoyancyComponentDataType : int8
{
	AsyncBuoyancyInvalid,
	AsyncBuoyancyBase,
	AsyncBuoyancyVehicle,
	AsyncBuoyancyBoat
};

UENUM()
enum class EBuoyancyEvent : uint8
{
	EnteredWaterBody,
	ExitedWaterBody
};

struct FBuoyancyAuxData
{
	FBuoyancyAuxData()
		: SmoothedWorldTimeSeconds(0.f)
	{ }

	TArray<FSphericalPontoon> Pontoons;
	TArray<AWaterBody*> WaterBodies;
	float SmoothedWorldTimeSeconds;
};

// Auxiliary, persistent data which the update can use
struct FBuoyancyComponentAsyncAux
{
	FBuoyancyData BuoyancyData;

	FBuoyancyComponentAsyncAux() = default;
	virtual ~FBuoyancyComponentAsyncAux() = default;
};

struct FBuoyancyComponentAsyncInput
{
	const EAsyncBuoyancyComponentDataType Type;
	const UBuoyancyComponent* BuoyancyComponent;

	FSingleParticlePhysicsProxy* Proxy;

	virtual TUniquePtr<struct FBuoyancyComponentAsyncOutput> PreSimulate(UWorld* World, const float DeltaSeconds, const float TotalSeconds, FBuoyancyComponentAsyncAux* Aux, const TMap<AWaterBody*, TUniquePtr<FSolverSafeWaterBodyData>>& WaterBodyData) const = 0;

	FBuoyancyComponentAsyncInput(EAsyncBuoyancyComponentDataType InType = EAsyncBuoyancyComponentDataType::AsyncBuoyancyInvalid)
		: Type(InType)
		, BuoyancyComponent(nullptr)
	{
		Proxy = nullptr;	//indicates async/sync task not needed. This can happen due to various logic when update is not needed
	}

	virtual ~FBuoyancyComponentAsyncInput() = default;
};

struct FBuoyancyManagerAsyncInput : public Chaos::FSimCallbackInput
{
	TArray<TUniquePtr<FBuoyancyComponentAsyncInput>> Inputs;
	TMap<AWaterBody*, TUniquePtr<FSolverSafeWaterBodyData>> WaterBodyToSolverData;
	TWeakObjectPtr<UWorld> World;
	int32 Timestamp = INDEX_NONE;

	void Reset()
	{
		Inputs.Reset();
		World.Reset();
		WaterBodyToSolverData.Reset();
	}
};

struct FBuoyancyComponentAsyncOutput
{
	const EAsyncBuoyancyComponentDataType Type;
	bool bValid;	//indicates no work was actually done. This is here because it can early out due to a lot of internal logic and we still want to go wide

	FBuoyancyComponentAsyncOutput(EAsyncBuoyancyComponentDataType InType = EAsyncBuoyancyComponentDataType::AsyncBuoyancyInvalid)
		: Type(InType)
		, bValid(false)
	{ }

	virtual ~FBuoyancyComponentAsyncOutput() = default;
};

struct FBuoyancyManagerAsyncOutput : public Chaos::FSimCallbackOutput
{
	TArray<TUniquePtr<FBuoyancyComponentAsyncOutput>> Outputs;
	int32 Timestamp = INDEX_NONE;

	void Reset()
	{
		Outputs.Reset();
	}
};
/* async structs end here */