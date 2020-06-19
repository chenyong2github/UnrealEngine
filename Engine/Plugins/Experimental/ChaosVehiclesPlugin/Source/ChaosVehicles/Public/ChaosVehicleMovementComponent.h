// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "AI/Navigation/NavigationAvoidanceTypes.h"
#include "AI/RVOAvoidanceInterface.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/PawnMovementComponent.h"
#include "ChaosVehicleWheel.h"
#include "AerofoilSystem.h"

#include "SimpleVehicle.h" // #todo: move

#include "ChaosVehicleMovementComponent.generated.h"

#define WANT_RVO 0
using namespace Chaos;

DECLARE_LOG_CATEGORY_EXTERN(LogVehicle, Log, All);

class UCanvas;

struct FVehicleDebugParams
{
	bool ShowCOM = false;
	bool ShowModelOrigin = false;
	bool ShowAerofoilForces = false;
	bool ShowAerofoilSurface = true;
	bool DisableAirControl = false;
	bool DisableGroundControl = false;
	bool DisableAerodynamics = false;
	bool BatchQueries = true;
};

// #todo: contact modification
struct FSolverSafeContactData
{
	/** This is read from off GT and needs to be completely thread safe. Modifying it in Update is safe because physx has not run yet. Constructor is also fine. Keep all data simple and thread safe like floats and ints */
	float ContactModificationOffset;
	float VehicleFloorFriction;
	float VehicleSideScrapeFriction;
};


struct FBodyInstance;

// #todo: are these too wheeled vehicle specific?
USTRUCT()
struct CHAOSVEHICLES_API FVehicleReplicatedState
{
	GENERATED_USTRUCT_BODY()

	// input replication: steering
	UPROPERTY()
	float SteeringInput;

	// input replication: throttle
	UPROPERTY()
	float ThrottleInput;

	// input replication: brake
	UPROPERTY()
	float BrakeInput;

	// input replication: body pitch
	UPROPERTY()
	float PitchInput;

	// input replication: body roll
	UPROPERTY()
	float RollInput;

	// input replication: body yaw
	UPROPERTY()
	float YawInput;

	// input replication: handbrake
	UPROPERTY()
	float HandbrakeInput;

	// state replication: target gear #todo: or current gear?
	UPROPERTY()
	int32 TargetGear;

	// input replication: increase throttle
	UPROPERTY()
	float ThrottleUp;

	// input replication: decrease throttle
	UPROPERTY()
	float ThrottleDown;

};

USTRUCT()
struct FVehicleAirControlConfig
{
	GENERATED_USTRUCT_BODY()

	/** Air Control Enabled */
	UPROPERTY(EditAnywhere, Category = Setup)
	bool Enabled;

	/** Yaw Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float YawTorqueScaling;

	/** Pitch Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float PitchTorqueScaling;

	/** Roll Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float RollTorqueScaling;

	/** Rotation damping */
	UPROPERTY(EditAnywhere, Category = Setup)
	float RotationDamping;
};


USTRUCT()
struct FVehicleThrustConfig
{
	GENERATED_USTRUCT_BODY()

	// Bone name on mesh where aerofoil is centered
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	FName BoneName;

	// Additional offset to give the aerofoil.
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	FVector Offset;

	UPROPERTY(EditAnywhere, Category = Setup)
	FRuntimeFloatCurve ThrustCurve;

	/** Roll Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float MaxSpeed;

	UPROPERTY(EditAnywhere, Category = Setup)
	float MaxThrustForce;
};


USTRUCT()
struct FVehicleGroundControlConfig
{
	GENERATED_USTRUCT_BODY()

	/** Ground Control Enabled */
	UPROPERTY(EditAnywhere, Category = Setup)
	bool Enabled;

	/** Yaw Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float YawTorqueScaling;

	/** Pitch/Wheelie Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float PitchTorqueScaling;

	/** Roll/Lean Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float RollTorqueScaling;

	/** pitch rotation damping */
	UPROPERTY(EditAnywhere, Category = Setup)
	float PitchDamping;

	/** roll rotation damping */
	UPROPERTY(EditAnywhere, Category = Setup)
	float RollDamping;

	/** yaw rotation damping */
	UPROPERTY(EditAnywhere, Category = Setup)
	float YawDamping;
};


/** Commonly used state - evaluated once used wherever required */
struct FVehicleState
{
	FVehicleState()
		: VehicleWorldTransform(FTransform::Identity)
		, VehicleWorldVelocity(FVector::ZeroVector)
		, VehicleWorldAngularVelocity(FVector::ZeroVector)
		, VehicleUpAxis(FVector(0.f,0.f,1.f))
		, VehicleForwardAxis(FVector(1.f,0.f,0.f))
		, VehicleRightAxis(FVector(0.f,1.f,0.f))
		, ForwardSpeed(0.f)
		, ForwardsAcceleration(0.f)
		, PrevForwardSpeed(0.f)
		, bVehicleInAir(false)
		, bSleeping(false)
	{

	}

	/** Cache some useful data */
	void CaptureState(FBodyInstance* TargetInstance, float DeltaTime);

	FTransform VehicleWorldTransform;
	FVector VehicleWorldVelocity;
	FVector VehicleWorldAngularVelocity;

	FVector VehicleUpAxis;
	FVector VehicleForwardAxis;
	FVector VehicleRightAxis;

	float ForwardSpeed;
	float ForwardsAcceleration;
	float PrevForwardSpeed;

	bool bVehicleInAir;
	bool bSleeping;
};

USTRUCT()
struct CHAOSVEHICLES_API FVehicleInputRateConfig
{
	GENERATED_USTRUCT_BODY()

	// Rate at which the input value rises
	UPROPERTY(EditAnywhere, Category=VehicleInputRate)
	float RiseRate;

	// Rate at which the input value falls
	UPROPERTY(EditAnywhere, Category=VehicleInputRate)
	float FallRate;

	FVehicleInputRateConfig() : RiseRate(5.0f), FallRate(5.0f) { }

	/** Change an output value using max rise and fall rates */
	float InterpInputValue( float DeltaTime, float CurrentValue, float NewValue ) const
	{
		const float DeltaValue = NewValue - CurrentValue;

		// We are "rising" when DeltaValue has the same sign as CurrentValue (i.e. delta causes an absolute magnitude gain)
		// OR we were at 0 before, and our delta is no longer 0.
		const bool bRising = (( DeltaValue > 0.0f ) == ( CurrentValue > 0.0f )) ||
								(( DeltaValue != 0.f ) && ( CurrentValue == 0.f ));

		const float MaxDeltaValue = DeltaTime * ( bRising ? RiseRate : FallRate );
		const float ClampedDeltaValue = FMath::Clamp( DeltaValue, -MaxDeltaValue, MaxDeltaValue );
		return CurrentValue + ClampedDeltaValue;
	}
};


UENUM()
enum class FVehicleAerofoilType : uint8
{
	Fixed = 0,
	Wing,
	Rudder,
	Elevator
};

USTRUCT()
struct CHAOSVEHICLES_API FVehicleAerofoilConfig
{
	GENERATED_USTRUCT_BODY()

	// Does this aerofoil represent a fixed spoiler, an aircraft wing, etc how is controlled.
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	FVehicleAerofoilType AerofoilType;

	// Bone name on mesh where aerofoil is centered
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	FName BoneName;

	// Additional offset to give the aerofoil.
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	FVector Offset;

	// Up Axis of aerofoil.
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	FVector UpAxis;

	// Area of aerofoil surface - larger value creates more lift but also more drag
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	float Area;

	// camber of wing - leave as zero for a rudder - can be used to trim/level elevator for level flight
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	float Camber;

	// The angle in degrees through which the control surface moves - leave at 0 if it is a fixed surface
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	float MaxControlAngle;

	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	float StallAngle;

	// cheat to control amount of lift independently from lift
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	float LiftMultiplier;

	// cheat to control amount of drag independently from lift, a value of zero will offer no drag
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	float DragMultiplier;

	const Chaos::FAerofoilConfig& GetPhysicsAerofoilConfig()
	{
		FillAerofoilSetup();
		return PAerofoilConfig;
	}

private:

	void FillAerofoilSetup()
	{
		// #todo: read position and axis from skeleton
		//PAerofoilConfig.BoneName = this->BoneName;
		PAerofoilConfig.Offset = this->Offset;
		PAerofoilConfig.UpAxis = this->UpAxis;
		PAerofoilConfig.Area = this->Area;
		PAerofoilConfig.Camber = this->Camber;
		PAerofoilConfig.MaxControlAngle = this->MaxControlAngle;
		PAerofoilConfig.StallAngle = this->StallAngle;
		PAerofoilConfig.Type = (Chaos::FAerofoilType)(this->AerofoilType);
		PAerofoilConfig.LiftMultiplier = this->LiftMultiplier;
		PAerofoilConfig.DragMultiplier = this->DragMultiplier;
	}

	Chaos::FAerofoilConfig PAerofoilConfig;
};

/**
 * Base component to handle the vehicle simulation for an actor.
 */
UCLASS(Abstract, hidecategories=(PlanarMovement, "Components|Movement|Planar", Activation, "Components|Activation"))
class CHAOSVEHICLES_API UChaosVehicleMovementComponent : public UPawnMovementComponent
#if WANT_RVO
	, public IRVOAvoidanceInterface
#endif
{
	GENERATED_UCLASS_BODY()

//#todo: these 2 oddities seem out of place

	/** If true, the brake and reverse controls will behave in a more arcade fashion where holding reverse also functions as brake. For a more realistic approach turn this off*/
	UPROPERTY(EditAnywhere, Category = VehicleSetup)
	uint8 bReverseAsBrake : 1;

#if WANT_RVO
	///** If set, component will use RVO avoidance */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadWrite)
	//uint8 bUseRVOAvoidance : 1;
#endif

public:
	/** Mass to set the vehicle chassis to. It's much easier to tweak vehicle settings when
	 * the mass doesn't change due to tweaks with the physics asset. [kg] */
	UPROPERTY(EditAnywhere, Category = VehicleSetup, meta = (ClampMin = "0.01", UIMin = "0.01"))
	float Mass;

	//#todo: entering area directly might be better for general shapes that are not boxes
	/** Chassis width used for drag force computation (cm)*/
	UPROPERTY(EditAnywhere, Category = VehicleSetup, meta = (ClampMin = "0.01", UIMin = "0.01"))
	float ChassisWidth;

	/** Chassis height used for drag force computation (cm)*/
	UPROPERTY(EditAnywhere, Category = VehicleSetup, meta = (ClampMin = "0.01", UIMin = "0.01"))
	float ChassisHeight;

	/** DragCoefficient of the vehicle chassis - force resisting forward motion at speed */
	UPROPERTY(EditAnywhere, Category = VehicleSetup)
	float DragCoefficient;

	/** DownforceCoefficient of the vehicle chassis - force pressing vehicle into ground at speed */
	UPROPERTY(EditAnywhere, Category = VehicleSetup)
	float DownforceCoefficient;

	// Drag area in cm^2
	UPROPERTY(transient)
	float DragArea;

	// Debug drag magnitude last applied
	UPROPERTY(transient)
	float DebugDragMagnitude;

	/** Scales the vehicle's inertia in each direction (forward, right, up) */
	UPROPERTY(EditAnywhere, Category=VehicleSetup, AdvancedDisplay)
	FVector InertiaTensorScale;

	/** Optional aerofoil setup - can be used for car spoilers or aircraft wings/elevator/rudder */
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	TArray<FVehicleAerofoilConfig> Aerofoils;

	/** Optional thruster setup, use one or more as your main engine or as supplementary booster */
	UPROPERTY(EditAnywhere, Category = ThrusterSetup)
	TArray<FVehicleThrustConfig> Thrusters;

	/** Arcade style in air control of your vehicle, permits some leveling before landing */
	UPROPERTY(EditAnywhere, Category = ArcadeControl)
	FVehicleAirControlConfig AirControl;

	// Used to recreate the physics if the blueprint changes.
	uint32 VehicleSetupTag;

protected:
	// True if the player is holding the handbrake
	UPROPERTY(Transient)
	uint8 bRawHandbrakeInput : 1;

	// True if the player is holding gear up
	UPROPERTY(Transient)
	uint8 bRawGearUpInput : 1;

	// True if the player is holding gear down
	UPROPERTY(Transient)
	uint8 bRawGearDownInput : 1;

	/** Was avoidance updated in this frame? */
	UPROPERTY(Transient)
	uint32 bWasAvoidanceUpdated : 1;

public:

	/** UObject interface */
	virtual void Serialize(FArchive& Ar) override;
	/** End UObject interface*/

#if WITH_EDITOR
	/** Respond to a property change in editor */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR

	/** Overridden to allow registration with components NOT owned by a Pawn. */
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	/** Allow the player controller of a different pawn to control this vehicle */
	virtual void SetOverrideController(AController* OverrideController);


	/** Return true if it's suitable to create a physics representation of the vehicle at this time */
	virtual bool ShouldCreatePhysicsState() const override;

	/** Returns true if the physics state exists */
	virtual bool HasValidPhysicsState() const override;

	/** Return true if we are ready to create a vehicle, false if the setup has missing references */
	virtual bool CanCreateVehicle() const;

	/** Are enough vehicle systems specified such that physics vehicle simulation is possible */
	virtual bool CanSimulate() const { return true; }


	/** Used to create any physics engine information for this component */
	virtual void OnCreatePhysicsState() override;

	/** Used to shut down and physics engine structure for this component */
	virtual void OnDestroyPhysicsState() override;

	/** Updates the vehicle tuning and other state such as user input. */
	virtual void PreTick(float DeltaTime);

	/** Tick this vehicle sim right before input is sent to the vehicle system */
	virtual void TickVehicle( float DeltaTime );

	/** Stops movement immediately (zeroes velocity, usually zeros acceleration for components with acceleration). */
	virtual void StopMovementImmediately() override;


	/** Set the user input for the vehicle throttle [range 0 to 1] */
	UFUNCTION(BlueprintCallable, Category="Game|Components|ChaosVehicleMovement")
	void SetThrottleInput(float Throttle);

	/** Increase the vehicle throttle position [throttle range normalized 0 to 1] */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	void IncreaseThrottleInput(float ThrottleDelta);

	/** Decrease the vehicle throttle position  [throttle range normalized 0 to 1] */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	void DecreaseThrottleInput(float ThrottleDelta);

	/** Set the user input for the vehicle Brake [range 0 to 1] */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	void SetBrakeInput(float Brake);
	
	/** Set the user input for the vehicle steering [range -1 to 1] */
	UFUNCTION(BlueprintCallable, Category="Game|Components|ChaosVehicleMovement")
	void SetSteeringInput(float Steering);

	/** Set the user input for the vehicle pitch [range -1 to 1] */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	void SetPitchInput(float Pitch);

	/** Set the user input for the vehicle roll [range -1 to 1] */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	void SetRollInput(float Roll);

	/** Set the user input for the vehicle yaw [range -1 to 1] */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	void SetYawInput(float Yaw);

	/** Set the user input for handbrake */
	UFUNCTION(BlueprintCallable, Category="Game|Components|ChaosVehicleMovement")
	void SetHandbrakeInput(bool bNewHandbrake);

	/** Set the user input for gear up */
	UFUNCTION(BlueprintCallable, Category="Game|Components|ChaosVehicleMovement")
	void SetChangeUpInput(bool bNewGearUp);

	/** Set the user input for gear down */
	UFUNCTION(BlueprintCallable, Category="Game|Components|ChaosVehicleMovement")
	void SetChangeDownInput(bool bNewGearDown);

	/** Set the user input for gear (-1 reverse, 0 neutral, 1+ forward)*/
	UFUNCTION(BlueprintCallable, Category="Game|Components|ChaosVehicleMovement")
	void SetTargetGear(int32 GearNum, bool bImmediate);

	/** Set the flag that will be used to select auto-gears */
	UFUNCTION(BlueprintCallable, Category="Game|Components|ChaosVehicleMovement")
	void SetUseAutomaticGears(bool bUseAuto);

	/** How fast the vehicle is moving forward */
	UFUNCTION(BlueprintCallable, Category="Game|Components|ChaosVehicleMovement")
	float GetForwardSpeed() const;

	/** How fast the vehicle is moving forward */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	float GetForwardSpeedMPH() const;

#if WANT_RVO
	// RVO Avoidance

	///** Vehicle Radius to use for RVO avoidance (usually half of vehicle width) */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadWrite)
	//float RVOAvoidanceRadius;
	//
	///** Vehicle Height to use for RVO avoidance (usually vehicle height) */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadWrite)
	//float RVOAvoidanceHeight;
	//
	///** Area Radius to consider for RVO avoidance */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadWrite)
	//float AvoidanceConsiderationRadius;

	///** Value by which to alter steering per frame based on calculated avoidance */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	//float RVOSteeringStep;

	///** Value by which to alter throttle per frame based on calculated avoidance */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	//float RVOThrottleStep;
	//
	///** calculate RVO avoidance and apply it to current velocity */
	//virtual void CalculateAvoidanceVelocity(float DeltaTime);

	///** No default value, for now it's assumed to be valid if GetAvoidanceManager() returns non-NULL. */
	//UPROPERTY(Category = "Avoidance", VisibleAnywhere, BlueprintReadOnly, AdvancedDisplay)
	//int32 AvoidanceUID;

	///** Moving actor's group mask */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadOnly, AdvancedDisplay)
	//FNavAvoidanceMask AvoidanceGroup;

	//UFUNCTION(BlueprintCallable, Category = "Pawn|Components|WheeledVehicleMovement", meta = (DeprecatedFunction, DeprecationMessage = "Please use SetAvoidanceGroupMask function instead."))
	//void SetAvoidanceGroup(int32 GroupFlags);

	//UFUNCTION(BlueprintCallable, Category = "Pawn|Components|WheeledVehicleMovement")
	//void SetAvoidanceGroupMask(const FNavAvoidanceMask& GroupMask);

	///** Will avoid other agents if they are in one of specified groups */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadOnly, AdvancedDisplay)
	//FNavAvoidanceMask GroupsToAvoid;

	//UFUNCTION(BlueprintCallable, Category = "Pawn|Components|WheeledVehicleMovement", meta = (DeprecatedFunction, DeprecationMessage = "Please use SetGroupsToAvoidMask function instead."))
	//void SetGroupsToAvoid(int32 GroupFlags);

	//UFUNCTION(BlueprintCallable, Category = "Pawn|Components|WheeledVehicleMovement")
	//void SetGroupsToAvoidMask(const FNavAvoidanceMask& GroupMask);

	///** Will NOT avoid other agents if they are in one of specified groups, higher priority than GroupsToAvoid */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadOnly, AdvancedDisplay)
	//FNavAvoidanceMask GroupsToIgnore;

	//UFUNCTION(BlueprintCallable, Category = "Pawn|Components|WheeledVehicleMovement", meta = (DeprecatedFunction, DeprecationMessage = "Please use SetGroupsToIgnoreMask function instead."))
	//void SetGroupsToIgnore(int32 GroupFlags);

	//UFUNCTION(BlueprintCallable, Category = "Pawn|Components|WheeledVehicleMovement")
	//void SetGroupsToIgnoreMask(const FNavAvoidanceMask& GroupMask);

	///** De facto default value 0.5 (due to that being the default in the avoidance registration function), indicates RVO behavior. */
	//UPROPERTY(Category = "Avoidance", EditAnywhere, BlueprintReadOnly)
	//float AvoidanceWeight;
	//
	/////** Temporarily holds launch velocity when pawn is to be launched so it happens at end of movement. */
	////UPROPERTY()
	////FVector PendingLaunchVelocity;
	//
	///** Change avoidance state and register with RVO manager if necessary */
	//UFUNCTION(BlueprintCallable, Category = "Pawn|Components|WheeledVehicleMovement")
	//void SetAvoidanceEnabled(bool bEnable);
#endif // WANT_RVO

	TUniquePtr<Chaos::FSimpleWheeledVehicle>& PhysicsVehicle()
	{
		check(PVehicle.IsValid());
		return PVehicle;
	}

	const FSolverSafeContactData& GetSolverSafeContactData() const
	{
		return SolverSafeContactData;
	}
protected:

	// replicated state of vehicle 
	UPROPERTY(Transient, Replicated)
	FVehicleReplicatedState ReplicatedState;

	// accumulator for RB replication errors 
	float AngErrorAccumulator;

	// What the player has the steering set to. Range -1...1
	UPROPERTY(Transient)
	float RawSteeringInput;

	// What the player has the accelerator set to. Range -1...1
	UPROPERTY(Transient)
	float RawThrottleInput;

	// What the player has the brake set to. Range -1...1
	UPROPERTY(Transient)
	float RawBrakeInput;

	// What the player has the pitch set to. Range -1...1
	UPROPERTY(Transient)
	float RawPitchInput;

	// What the player has the roll set to. Range -1...1
	UPROPERTY(Transient)
	float RawRollInput;

	// What the player has the yaw set to. Range -1...1
	UPROPERTY(Transient)
	float RawYawInput;

	// Steering output to physics system. Range -1...1
	UPROPERTY(Transient)
	float SteeringInput;

	// Accelerator output to physics system. Range 0...1
	UPROPERTY(Transient)
	float ThrottleInput;

	// Brake output to physics system. Range 0...1
	UPROPERTY(Transient)
	float BrakeInput;

	// Body Pitch output to physics system. Range -1...1
	UPROPERTY(Transient)
	float PitchInput;

	// Body Roll output to physics system. Range -1...1
	UPROPERTY(Transient)
	float RollInput;

	// Body Yaw output to physics system. Range -1...1
	UPROPERTY(Transient)
	float YawInput;

	// Handbrake output to physics system. Range 0...1
	UPROPERTY(Transient)
	float HandbrakeInput;

	// How much to press the brake when the player has release throttle
	UPROPERTY(EditAnywhere, Category=VehicleInput)
	float IdleBrakeInput;

	// Auto-brake when absolute vehicle forward speed is less than this (cm/s)
	UPROPERTY(EditAnywhere, Category=VehicleInput)
	float StopThreshold;

	// Auto-brake when vehicle forward speed is opposite of player input by at least this much (cm/s)
	UPROPERTY(EditAnywhere, Category = VehicleInput)
	float WrongDirectionThreshold;

	// Rate at which input throttle can rise and fall
	UPROPERTY(EditAnywhere, Category=VehicleInput, AdvancedDisplay)
	FVehicleInputRateConfig ThrottleInputRate;

	// Rate at which input brake can rise and fall
	UPROPERTY(EditAnywhere, Category=VehicleInput, AdvancedDisplay)
	FVehicleInputRateConfig BrakeInputRate;

	// Rate at which input steering can rise and fall
	UPROPERTY(EditAnywhere, Category=VehicleInput, AdvancedDisplay)
	FVehicleInputRateConfig SteeringInputRate;

	// Rate at which input handbrake can rise and fall
	UPROPERTY(EditAnywhere, Category=VehicleInput, AdvancedDisplay)
	FVehicleInputRateConfig HandbrakeInputRate;

	// Rate at which input pitch can rise and fall
	UPROPERTY(EditAnywhere, Category = VehicleInput, AdvancedDisplay)
	FVehicleInputRateConfig PitchInputRate;

	// Rate at which input roll can rise and fall
	UPROPERTY(EditAnywhere, Category = VehicleInput, AdvancedDisplay)
	FVehicleInputRateConfig RollInputRate;

	// Rate at which input yaw can rise and fall
	UPROPERTY(EditAnywhere, Category = VehicleInput, AdvancedDisplay)
	FVehicleInputRateConfig YawInputRate;

	// input related

	/** Compute steering input */
	float CalcSteeringInput();

	/** Compute brake input */
	float CalcBrakeInput();

	/** Compute handbrake input */
	float CalcHandbrakeInput();

	/** Compute pitch input */
	float CalcPitchInput();

	/** Compute roll input */
	float CalcRollInput();

	/** Compute yaw input */
	float CalcYawInput();

	/** Compute throttle input */
	virtual float CalcThrottleInput();

	/**
	* Clear all interpolated inputs to default values.
	* Raw input won't be cleared, the vehicle may resume input based movement next frame.
	*/
	virtual void ClearInput();
	
	/**
	* Clear all raw inputs to default values.
	* Interpolated input won't be cleared, the vehicle will begin interpolating to no input.
	*/
	virtual void ClearRawInput();

	void ClearAllInput()
	{
		ClearRawInput();
		ClearInput();
	}

	// Update

	/** Read current state for simulation */
	virtual void UpdateState(float DeltaTime);

	/** Advance the vehicle simulation */
	virtual void UpdateSimulation(float DeltaTime);

	/** Pass control Input to the vehicle systems */
	virtual void ApplyInput(float DeltaTime);

	/** Apply aerodynamic forces to vehicle body */
	virtual void ApplyAerodynamics(float DeltaTime);

	/** Apply Aerofoil forces to vehicle body */
	virtual void ApplyAerofoilForces(float DeltaTime);

	/** Apply in air control torque to vehicle body */
	virtual void ApplyAirControl(float DeltaTime);

	/** Apply on ground control torque to vehicle body */
	//virtual void ApplyGroundControl(float DeltaTime);

	// #todo: use this properly
	void CopyToSolverSafeContactStaticData();

	/** Pass current state to server */
	UFUNCTION(reliable, server, WithValidation)
	void ServerUpdateState(float InSteeringInput, float InThrottleInput, float InBrakeInput, float InHandbrakeInput, int32 TargetGear);

#if WANT_RVO

	/** Update RVO Avoidance for simulation */
	void UpdateAvoidance(float DeltaTime);
		
	/** called in Tick to update data in RVO avoidance manager */
	void UpdateDefaultAvoidance();
	
	/** lock avoidance velocity */
	void SetAvoidanceVelocityLock(class UAvoidanceManager* Avoidance, float Duration);
	
	/** Calculated avoidance velocity used to adjust steering and throttle */
	FVector AvoidanceVelocity;
	
	/** forced avoidance velocity, used when AvoidanceLockTimer is > 0 */
	FVector AvoidanceLockVelocity;
	
	/** remaining time of avoidance velocity lock */
	float AvoidanceLockTimer;
#endif
	
	// Setup

	/** Get our controller */
	AController* GetController() const;

	/** Get the mesh this vehicle is tied to */
	class USkinnedMeshComponent* GetMesh();

	/** Create and setup the Chaos vehicle */
	virtual void CreateVehicle();

	/** Allocate and setup the Chaos vehicle */
	virtual void SetupVehicle();

	/** Do some final setup after the Chaos vehicle gets created */
	virtual void PostSetupVehicle();

	/** Adjust the Chaos Physics mass */
	virtual void SetupVehicleMass();

	void UpdateMassProperties(FBodyInstance* BI);

	/** When vehicle is created we want to compute some helper data like drag area, etc.... Derived classes should use this to properly compute things like engine RPM */
	virtual void ComputeConstants();

	// Debug

	void ShowDebugInfo(class AHUD* HUD, class UCanvas* Canvas, const class FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);

	/** Draw debug text for the wheels and suspension */
	virtual void DrawDebug(UCanvas* Canvas, float& YL, float& YPos);

	/** Draw debug text for the wheels and suspension */
	virtual void DrawDebug3D();

	// draw 2D debug line to UI canvas
	void DrawLine2D(UCanvas* Canvas, const FVector2D& StartPos, const FVector2D& EndPos, FColor Color, float Thickness = 1.f);

	float GetForwardAcceleration()
	{
		return VehicleState.ForwardsAcceleration;
	}

	FBodyInstance* GetBodyInstance();

	FVehicleState VehicleState;

	// #todo: this isn't very configurable
	TUniquePtr<Chaos::FSimpleWheeledVehicle> PVehicle;

	/** Handle for delegate registered on mesh component */
	FDelegateHandle MeshOnPhysicsStateChangeHandle;

#if WANT_RVO
	/** BEGIN IRVOAvoidanceInterface */
	virtual void SetRVOAvoidanceUID(int32 UID) override;
	virtual int32 GetRVOAvoidanceUID() override;
	virtual void SetRVOAvoidanceWeight(float Weight) override;
	virtual float GetRVOAvoidanceWeight() override;
	virtual FVector GetRVOAvoidanceOrigin() override;
	virtual float GetRVOAvoidanceRadius() override;
	virtual float GetRVOAvoidanceHeight() override;
	virtual float GetRVOAvoidanceConsiderationRadius() override;
	virtual FVector GetVelocityForRVOConsideration() override;
	virtual void SetAvoidanceGroupMask(int32 GroupFlags) override;
	virtual int32 GetAvoidanceGroupMask() override;
	virtual void SetGroupsToAvoidMask(int32 GroupFlags) override;
	virtual int32 GetGroupsToAvoidMask() override;
	virtual void SetGroupsToIgnoreMask(int32 GroupFlags) override;
	virtual int32 GetGroupsToIgnoreMask() override;
	/** END IRVOAvoidanceInterface */
#endif

private:
	UPROPERTY(transient, Replicated)
	AController* OverrideController;
	FSolverSafeContactData SolverSafeContactData;

	const Chaos::FSimpleAerodynamicsConfig& GetAerodynamicsConfig()
	{
		FillAerodynamicsSetup();
		return PAerodynamicsSetup;
	}

	void FillAerodynamicsSetup()
	{
		// #todo
		PAerodynamicsSetup.DragCoefficient = this->DragCoefficient;
		PAerodynamicsSetup.DownforceCoefficient = this->DownforceCoefficient;
		PAerodynamicsSetup.AreaMetresSquared = Cm2ToM2(this->DragArea);
	}
	Chaos::FSimpleAerodynamicsConfig PAerodynamicsSetup;


};
