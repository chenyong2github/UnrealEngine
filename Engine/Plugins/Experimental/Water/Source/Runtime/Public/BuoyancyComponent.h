// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "BuoyancyComponent.generated.h"

class AWaterBody;

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

	TMap<const AWaterBody*, float> SplineInputKeys;
	TMap<const AWaterBody*, float> SplineSegments;

	uint8 bIsInWater : 1;
	uint8 bEnabled : 1;
	uint8 bUseCenterSocket : 1;
	UPROPERTY(Transient, BlueprintReadOnly, Category = Buoyancy)
	AWaterBody* CurrentWaterBody;

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
		, bIsInWater(false)
		, bEnabled(true)
		, bUseCenterSocket(false)
		, CurrentWaterBody(nullptr)
	{
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPontoonEnteredWater, const FSphericalPontoon&, Pontoon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPontoonExitedWater, const FSphericalPontoon&, Pontoon);

UCLASS(Blueprintable, Config = Game, meta = (BlueprintSpawnableComponent))
class WATER_API UBuoyancyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuoyancyComponent(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void SetupWaterBodyOverlaps();

	bool HasPontoons() const { return Pontoons.Num() > 0; }
	void AddCustomPontoon(float Radius, FName CenterSocketName);
	void AddCustomPontoon(float Radius, const FVector& RelativeLocation);
	virtual int32 UpdatePontoons(float DeltaTime, float ForwardSpeed, float ForwardSpeedKmh, UPrimitiveComponent* PrimitiveComponent);
	void UpdatePontoonCoefficients();
	FVector ComputeWaterForce(const float DeltaTime, const FVector LinearVelocity) const;

	void EnteredWaterBody(AWaterBody* WaterBody);
	void ExitedWaterBody(AWaterBody* WaterBody);

	const TArray<AWaterBody*>& GetCurrentWaterBodies() const { return CurrentWaterBodies; }
	TArray<AWaterBody*>& GetCurrentWaterBodies() { return CurrentWaterBodies; }

	bool IsOverlappingWaterBody() const { return bIsOverlappingWaterBody; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Buoyancy)
	TArray<FSphericalPontoon> Pontoons;

	void GetWaterSplineKey(FVector Location, TMap<const AWaterBody*, float>& OutMap, TMap<const AWaterBody*, float>& OutSegmentMap) const;
	float GetWaterHeight(FVector Position, const TMap<const AWaterBody*, float>& SplineKeyMap, float DefaultHeight, AWaterBody*& OutWaterBody, float& OutWaterDepth, FVector& OutWaterPlaneLocation, FVector& OutWaterPlaneNormal, bool bShouldIncludeWaves = true);
	float GetWaterHeight(FVector Position, const TMap<const AWaterBody*, float>& SplineKeyMap, float DefaultHeight, bool bShouldIncludeWaves = true);

	UFUNCTION(BlueprintCallable, Category = Cosmetic)
	void OnPontoonEnteredWater(const FSphericalPontoon& Pontoon);

	UFUNCTION(BlueprintCallable, Category = Cosmetic)
	void OnPontoonExitedWater(const FSphericalPontoon& Pontoon);

	UPROPERTY(BlueprintAssignable, Category = Cosmetic)
	FOnPontoonEnteredWater OnEnteredWaterDelegate;

	UPROPERTY(BlueprintAssignable, Category = Cosmetic)
	FOnPontoonExitedWater OnExitedWaterDelegate;

protected:
	void ApplyBuoyancy(UPrimitiveComponent* PrimitiveComponent);
	void ComputeBuoyancy(FSphericalPontoon& Pontoon, float ForwardSpeedKmh);
	void ComputePontoonCoefficients();

	UPROPERTY(Transient)
	TArray<AWaterBody*> CurrentWaterBodies;

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

	uint32 PontoonConfiguration;
	TMap<uint32, TArray<float>> ConfiguredPontoonCoefficients;
	int32 VelocityPontoonIndex;
	int8 bIsOverlappingWaterBody : 1;
};