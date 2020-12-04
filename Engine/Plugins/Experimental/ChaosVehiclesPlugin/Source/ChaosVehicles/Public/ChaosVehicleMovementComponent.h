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
#include "ThrustSystem.h"

#include "SimpleVehicle.h" // #todo: move

#include "ChaosVehicleMovementComponent.generated.h"

class CHAOSVEHICLES_API UChaosVehicleMovementComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogVehicle, Log, All);

class UCanvas;

struct FVehicleDebugParams
{
	bool ShowCOM = false;
	bool ShowModelOrigin = false;
	bool ShowAllForces = false;
	bool ShowAerofoilForces = false;
	bool ShowAerofoilSurface = false;
	bool DisableTorqueControl = false;
	bool DisableStabilizeControl = false;
	bool DisableAerodynamics = false;
	bool DisableAerofoils = false;
	bool DisableThrusters = false;
	bool BatchQueries = false;	// Turned off due to Issue with Overlap Queries on scaled terrain
	float ForceDebugScaling = 0.0006f;
	float SleepCounterThreshold = 15;
	bool DisableVehicleSleep = true;
};

struct FBodyInstance;

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

	// state replication: gear
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
struct FVehicleTorqueControlConfig
{
	GENERATED_USTRUCT_BODY()

	/** Torque Control Enabled */
	UPROPERTY(EditAnywhere, Category = Setup)
	bool Enabled;

	/** Yaw Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float YawTorqueScaling;

	UPROPERTY(EditAnywhere, Category = Setup)
	float YawFromSteering;
	
	UPROPERTY(EditAnywhere, Category = Setup)
	float YawFromRollTorqueScaling;

	/** Pitch Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float PitchTorqueScaling;

	/** Roll Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float RollTorqueScaling;

	UPROPERTY(EditAnywhere, Category = Setup)
	float RollFromSteering;

	/** Rotation damping */
	UPROPERTY(EditAnywhere, Category = Setup)
	float RotationDamping;

	void InitDefaults()
	{
		Enabled = false;
		YawTorqueScaling = 0.0f;
		YawFromSteering = 0.0f;
		YawFromRollTorqueScaling = 0.0f;
		PitchTorqueScaling = 0.0f;
		RollTorqueScaling = 0.0f;
		RollFromSteering = 0.0f;
		RotationDamping = 0.02f;
	}
};

USTRUCT()
struct FVehicleTargetRotationControlConfig
{
	GENERATED_USTRUCT_BODY()

	/** Rotation Control Enabled */
	UPROPERTY(EditAnywhere, Category = Setup)
	bool Enabled;

	UPROPERTY(EditAnywhere, Category = Setup)
	bool bRollVsSpeedEnabled;

	UPROPERTY(EditAnywhere, Category = Setup)
	float RollControlScaling;

	UPROPERTY(EditAnywhere, Category = Setup)
	float RollMaxAngle;

	UPROPERTY(EditAnywhere, Category = Setup)
	float PitchControlScaling;

	UPROPERTY(EditAnywhere, Category = Setup)
	float PitchMaxAngle;

	/** Rotation stiffness */
	UPROPERTY(EditAnywhere, Category = Setup)
	float RotationStiffness;

	/** Rotation damping */
	UPROPERTY(EditAnywhere, Category = Setup)
	float RotationDamping;

	/** Rotation mac accel */
	UPROPERTY(EditAnywhere, Category = Setup)
	float MaxAccel;

	UPROPERTY(EditAnywhere, Category = Setup)
	float AutoCentreRollStrength;

	UPROPERTY(EditAnywhere, Category = Setup)
	float AutoCentrePitchStrength;

	UPROPERTY(EditAnywhere, Category = Setup)
	float AutoCentreYawStrength;


	void InitDefaults()
	{
		Enabled = false;

		bRollVsSpeedEnabled = false;

		RollControlScaling = 0.f;
		RollMaxAngle = 0.f;
		PitchControlScaling = 0.f;
		PitchMaxAngle = 0.f;

		RotationStiffness = 0.f;
		RotationDamping = 0.2;
		MaxAccel = 0.f;

		AutoCentreRollStrength = 0.f;
		AutoCentrePitchStrength = 0.f;
		AutoCentreYawStrength = 0.f;
	}
};

USTRUCT()
struct FVehicleStabilizeControlConfig
{
	GENERATED_USTRUCT_BODY()

	/** Torque Control Enabled */
	UPROPERTY(EditAnywhere, Category = Setup)
	bool Enabled;

	/** Yaw Torque Scaling */
	UPROPERTY(EditAnywhere, Category = Setup)
	float AltitudeHoldZ;

	UPROPERTY(EditAnywhere, Category = Setup)
	float PositionHoldXY;

	void InitDefaults()
	{
		Enabled = false;
		AltitudeHoldZ = 4.0f;
		PositionHoldXY = 8.0f;
	}
};

/** Commonly used state - evaluated once used wherever required */
struct FVehicleState
{
	FVehicleState()
		: VehicleWorldTransform(FTransform::Identity)
		, VehicleWorldVelocity(FVector::ZeroVector)
		, VehicleLocalVelocity(FVector::ZeroVector)
		, VehicleWorldAngularVelocity(FVector::ZeroVector)
		, VehicleWorldCOM(FVector::ZeroVector)
		, WorldVelocityNormal(FVector::ZeroVector)
		, VehicleUpAxis(FVector(0.f,0.f,1.f))
		, VehicleForwardAxis(FVector(1.f,0.f,0.f))
		, VehicleRightAxis(FVector(0.f,1.f,0.f))
		, LocalAcceleration(FVector::ZeroVector)
		, LocalGForce(FVector::ZeroVector)
		, LastFrameVehicleLocalVelocity(FVector::ZeroVector)
		, ForwardSpeed(0.f)
		, ForwardsAcceleration(0.f)
		, NumWheelsOnGround(0)
		, bAllWheelsOnGround(false)
		, bVehicleInAir(true)
		, bSleeping(false)
		, SleepCounter(0)
	{

	}

	/** Cache some useful data at the start of the frame */
	void CaptureState(FBodyInstance* TargetInstance, float GravityZ, float DeltaTime);

	FTransform VehicleWorldTransform;
	FVector VehicleWorldVelocity;
	FVector VehicleLocalVelocity;
	FVector VehicleWorldAngularVelocity;
	FVector VehicleWorldCOM;
	FVector WorldVelocityNormal;

	FVector VehicleUpAxis;
	FVector VehicleForwardAxis;
	FVector VehicleRightAxis;
	FVector LocalAcceleration;
	FVector LocalGForce;
	FVector LastFrameVehicleLocalVelocity;

	float ForwardSpeed;
	float ForwardsAcceleration;

	int NumWheelsOnGround;
	bool bAllWheelsOnGround;
	bool bVehicleInAir;
	bool bSleeping;
	int SleepCounter;
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
enum class EVehicleAerofoilType : uint8
{
	Fixed = 0,
	Wing,			// affected by Roll input
	Rudder,			// affected by steering/yaw input
	Elevator		// affected by Pitch input
};


UENUM()
enum class EVehicleThrustType : uint8
{
	Fixed = 0,
	Wing,				// affected by Roll input
	Rudder,				// affected by steering/yaw input
	Elevator,			// affected by Pitch input
//	HelicopterRotor,	// affected by pitch/roll inputs
};


USTRUCT()
struct CHAOSVEHICLES_API FVehicleAerofoilConfig
{
	GENERATED_USTRUCT_BODY()

	// Does this aerofoil represent a fixed spoiler, an aircraft wing, etc how is controlled.
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	EVehicleAerofoilType AerofoilType;

	// Bone name on mesh where aerofoil is centered
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	FName BoneName;

	// Additional offset to give the aerofoil.
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	FVector Offset;

	// Up Axis of aerofoil.
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	FVector UpAxis;

	// Area of aerofoil surface [Meters Squared] - larger value creates more lift but also more drag
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

	const Chaos::FAerofoilConfig& GetPhysicsAerofoilConfig(const UChaosVehicleMovementComponent& MovementComponent)
	{
		FillAerofoilSetup(MovementComponent);
		return PAerofoilConfig;
	}

	void InitDefaults()
	{
		AerofoilType = EVehicleAerofoilType::Fixed;
		BoneName = NAME_None;
		Offset = FVector::ZeroVector;
		UpAxis = FVector(0.f, 0.f, -1.f);
		Area = 1.f;
		Camber = 3.f;
		MaxControlAngle = 0.f;
		StallAngle = 16.f;
		LiftMultiplier = 1.0f;
		DragMultiplier = 1.0f;
	}

private:

	void FillAerofoilSetup(const UChaosVehicleMovementComponent& MovementComponent);

	Chaos::FAerofoilConfig PAerofoilConfig;
};

USTRUCT()
struct FVehicleThrustConfig
{
	GENERATED_USTRUCT_BODY()

	// Does this aerofoil represent a fixed spoiler, an aircraft wing, etc how is controlled.
	UPROPERTY(EditAnywhere, Category = ThrustSetup)
	EVehicleThrustType ThrustType;

	/** Bone name on mesh where thrust is located */
	UPROPERTY(EditAnywhere, Category = ThrustSetup)
	FName BoneName;

	/** Additional offset to give the location, or use in preference to the bone */
	UPROPERTY(EditAnywhere, Category = ThrustSetup)
	FVector Offset;

	/** Up Axis of thrust. */
	UPROPERTY(EditAnywhere, Category = ThrustSetup)
	FVector ThrustAxis;

	///** How the thrust is applied as the speed increases */
	//UPROPERTY(EditAnywhere, Category = ThrustSetup)
	//FRuntimeFloatCurve ThrustCurve;

	///** Maximum speed after which the thrust will cut off */
	//UPROPERTY(EditAnywhere, Category = ThrustSetup)
	//float MaxSpeed;

	/** Maximum thrust force */
	UPROPERTY(EditAnywhere, Category = ThrustSetup)
	float MaxThrustForce;

	/** The angle in degrees through which the control surface moves - leave at 0 if it is a fixed surface */
	UPROPERTY(EditAnywhere, Category = ThrustSetup)
	float MaxControlAngle;

	// #todo:ControlAxes - X, Y, Z, or X & Y, etc
	const Chaos::FSimpleThrustConfig& GetPhysicsThrusterConfig(const UChaosVehicleMovementComponent& MovementComponent)
	{
		FillThrusterSetup(MovementComponent);
		return PThrusterConfig;
	}

	void InitDefaults()
	{
		ThrustType = EVehicleThrustType::Fixed;
		BoneName = NAME_None;
		Offset = FVector::ZeroVector;
		ThrustAxis = FVector(1,0,0);
		//ThrustCurve.GetRichCurve()->AddKey(0.f, 1.f);
		//ThrustCurve.GetRichCurve()->AddKey(1.f, 1.f);
		MaxThrustForce = 1000.0f;
		MaxControlAngle = 0.f;
	}

private:
	void FillThrusterSetup(const UChaosVehicleMovementComponent &MovementComponent);

	Chaos::FSimpleThrustConfig PThrusterConfig;

};


/**
 * Base component to handle the vehicle simulation for an actor.
 */
UCLASS(Abstract, hidecategories=(PlanarMovement, "Components|Movement|Planar", Activation, "Components|Activation"))
class CHAOSVEHICLES_API UChaosVehicleMovementComponent : public UPawnMovementComponent
{
	GENERATED_UCLASS_BODY()

//#todo: these 2 oddities seem out of place

	/** If true, the brake and reverse controls will behave in a more arcade fashion where holding reverse also functions as brake. For a more realistic approach turn this off*/
	UPROPERTY(EditAnywhere, Category = VehicleSetup)
	uint8 bReverseAsBrake : 1;

public:
	/** Mass to set the vehicle chassis to. It's much easier to tweak vehicle settings when
	 * the mass doesn't change due to tweaks with the physics asset. [kg] */
	UPROPERTY(EditAnywhere, Category = VehicleSetup, meta = (ClampMin = "0.01", UIMin = "0.01"))
	float Mass;

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

	// Drag area in Meters^2
	UPROPERTY(transient)
	float DragArea;

	// Debug drag magnitude last applied
	UPROPERTY(transient)
	float DebugDragMagnitude;

	/** Scales the vehicle's inertia in each direction (forward, right, up) */
	UPROPERTY(EditAnywhere, Category=VehicleSetup, AdvancedDisplay)
	FVector InertiaTensorScale;

	/** Option to apply some aggressive sleep logic, larger number is more agressive, 0 disables */
	UPROPERTY(EditAnywhere, Category = VehicleSetup)
	float SleepThreshold;
	
	/** Option to apply some aggressive sleep logic if slopes up Z is less than this value, i.e value = Cos(SlopeAngle) so 0.866 will sleep up to 30 degree slopes */
	UPROPERTY(EditAnywhere, Category = VehicleSetup, meta = (ClampMin = "0.01", UIMin = "0.01", ClampMax = "1.0", UIMax = "1.0"))
	float SleepSlopeLimit;

	/** Optional aerofoil setup - can be used for car spoilers or aircraft wings/elevator/rudder */
	UPROPERTY(EditAnywhere, Category = AerofoilSetup)
	TArray<FVehicleAerofoilConfig> Aerofoils;

	/** Optional thruster setup, use one or more as your main engine or as supplementary booster */
	UPROPERTY(EditAnywhere, Category = ThrusterSetup)
	TArray<FVehicleThrustConfig> Thrusters;

	/** Arcade style direct control of vehicle rotation via torque force */
	UPROPERTY(EditAnywhere, Category = ArcadeControl)
	FVehicleTorqueControlConfig TorqueControl;
	
	/** Arcade style direct control of vehicle rotation via torque force */
	UPROPERTY(EditAnywhere, Category = ArcadeControl)
	FVehicleTargetRotationControlConfig TargetRotationControl;

	/** Arcade style control of vehicle */
	UPROPERTY(EditAnywhere, Category = ArcadeControl)
	FVehicleStabilizeControlConfig StabilizeControl;

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

	/** Get current gear */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	int32 GetCurrentGear() const;

	/** Get target gear */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	int32 GetTargetGear() const;

	/** Are gears being changed automatically? */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	bool GetUseAutoGears() const;

	/** How fast the vehicle is moving forward */
	UFUNCTION(BlueprintCallable, Category="Game|Components|ChaosVehicleMovement")
	float GetForwardSpeed() const;

	/** How fast the vehicle is moving forward */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	float GetForwardSpeedMPH() const;

	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosVehicleMovement")
	void EnableSelfRighting(bool InState)
	{
		TargetRotationControl.Enabled = InState;
		TorqueControl.Enabled = InState;
		StabilizeControl.Enabled = InState;
	}

	/** location local coordinates of named bone in skeleton, apply additional offset or just use offset if no bone located */
	FVector LocateBoneOffset(const FName InBoneName, const FVector& InExtraOffset) const;

	TUniquePtr<Chaos::FSimpleWheeledVehicle>& PhysicsVehicle()
	{
		return PVehicle;
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

	/** Compute throttle inputs */
	virtual float CalcThrottleInput();
	float CalcThrottleUpInput();
	float CalcThrottleDownInput();

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

	/** Apply Thruster forces to vehicle body */
	virtual void ApplyThrustForces(float DeltaTime);

	/** Apply direct control over vehicle body rotation */
	virtual void ApplyTorqueControl(float DeltaTime);

	/** Option to aggressively sleep the vehicle */
	virtual void ProcessSleeping();

	/** Pass current state to server */
	UFUNCTION(reliable, server, WithValidation)
	void ServerUpdateState(float InSteeringInput, float InThrottleInput, float InBrakeInput
			, float InHandbrakeInput, int32 InCurrentGear, float InRollInput, float InPitchInput, float InYawInput);


	// Setup

	/** Get our controller */
	AController* GetController() const;

	/** Get the mesh this vehicle is tied to */
	class UMeshComponent* GetMesh() const;

	/** Get Mesh cast as USkeletalMeshComponent, may return null if cast fails */
	USkeletalMeshComponent* GetSkeletalMesh();

	/** Get Mesh cast as UStaticMeshComponent, may return null if cast fails */
	UStaticMeshComponent* GetStaticMesh();

	/** Create and setup the Chaos vehicle */
	virtual void CreateVehicle();

	virtual void CreatePhysicsVehicle();

	/** Skeletal mesh needs some special handling in the vehicle case */
	virtual void FixupSkeletalMesh() {}

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

protected:
	/** Add a force to this vehicle */
	void AddForce(const FVector& Force, bool bAllowSubstepping = true, bool bAccelChange = false);
	/** Add a force at a particular position (world space when bIsLocalForce = false, body space otherwise) */
	void AddForceAtPosition(const FVector& Force, const FVector& Position, bool bAllowSubstepping = true, bool bIsLocalForce = false);
	/** Add an impulse to this vehicle */
	void AddImpulse(const FVector& Impulse, bool bVelChange);
	/** Add an impulse to this vehicle and a particular world position */
	void AddImpulseAtPosition(const FVector& Impulse, const FVector& Position);
	/** Add a torque to this vehicle */
	void AddTorqueInRadians(const FVector& Torque, bool bAllowSubstepping = true, bool bAccelChange = false);

private:
	UPROPERTY(transient, Replicated)
	AController* OverrideController;

	const Chaos::FSimpleAerodynamicsConfig& GetAerodynamicsConfig()
	{
		FillAerodynamicsSetup();
		return PAerodynamicsSetup;
	}

	void FillAerodynamicsSetup()
	{
		PAerodynamicsSetup.DragCoefficient = this->DragCoefficient;
		PAerodynamicsSetup.DownforceCoefficient = this->DownforceCoefficient;
		PAerodynamicsSetup.AreaMetresSquared = Chaos::Cm2ToM2(this->DragArea);
	}
	Chaos::FSimpleAerodynamicsConfig PAerodynamicsSetup;

};
