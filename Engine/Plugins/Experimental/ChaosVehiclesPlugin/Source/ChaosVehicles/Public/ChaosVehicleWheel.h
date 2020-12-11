// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * Component to handle the vehicle simulation for an actor
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/ScriptMacros.h"
#include "EngineDefines.h"
#include "SimpleVehicle.h"
#include "VehicleUtility.h"
#include "Engine/EngineTypes.h"

#include "ChaosVehicleWheel.generated.h"

class UPhysicalMaterial;
class FChaosVehicleManager;
class UChaosWheeledVehicleMovementComponent;

	UENUM()
	enum class ESweepShape : uint8
	{
		/** Use ray to determine suspension length to ground - fastest */
		Raycast		UMETA(DisplayName = "Raycast"),

		/** Use sphere to determine suspension length to ground */
		Spherecast	UMETA(DisplayName = "Spherecast"),

		/** Use wheel collision shape to determine suspension length to ground - Slowest */
		Shapecast	UMETA(DisplayName = "Shapecast")
	};

	UENUM()
	enum class ESweepType : uint8
	{
		/** Sweeps against simple geometry only */
		SimpleSweep				UMETA(DisplayName = "SimpleSweep"),
		/** Sweeps against complex geometry only */
		ComplexSweep			UMETA(DisplayName = "ComplexSweep")
	};

	UENUM()
	enum class EAxleType : uint8
	{
		Undefined = 0,
		Front,
		Rear
	};

	UCLASS(BlueprintType, Blueprintable)
		class CHAOSVEHICLES_API UChaosVehicleWheel : public UObject
	{
		GENERATED_UCLASS_BODY()

		/**
			* Static mesh with collision setup for wheel, will be used to create wheel shape
			* (if empty, sphere will be added as wheel shape, check bDontCreateShape flag)
			*/
		UPROPERTY(EditDefaultsOnly, Category = Shape)
		class UStaticMesh* CollisionMesh;

		/** If set, shape won't be created, but mapped from chassis mesh */
		//UPROPERTY(EditDefaultsOnly, Category = Shape, meta = (DisplayName = "UsePhysAssetShape"))
		//bool bDontCreateShape;

		/**
		 *	If true, ShapeRadius and ShapeWidth will be used to automatically scale collision taken from CollisionMesh to match wheel size.
		 *	If false, size of CollisionMesh won't be changed. Use if you want to scale wheels manually.
		 */
		//UPROPERTY(EditAnywhere, Category = Shape)
		//bool bAutoAdjustCollisionSize;

		/** If left undefined then the bAffectedByEngine value is used, if defined then bAffectedByEngine is ignored and the differential setup on the vehicle defines which wheels get power from the engine */
		UPROPERTY(EditAnywhere, Category = Wheel)
		EAxleType AxleType;

		/**
		 * If BoneName is specified, offset the wheel from the bone's location.
		 * Otherwise this offsets the wheel from the vehicle's origin.
		 */
		UPROPERTY(EditAnywhere, Category = Wheel)
		FVector Offset;

		/** Radius of the wheel */
		UPROPERTY(EditAnywhere, Category = Wheel, meta = (ClampMin = "0.01", UIMin = "0.01"))
		float WheelRadius;

		/** Width of the wheel */
		UPROPERTY(EditAnywhere, Category = Wheel, meta = (ClampMin = "0.01", UIMin = "0.01"))
		float WheelWidth;

		/** Mass of this wheel */
		//UPROPERTY(EditAnywhere, Category = Wheel, meta = (ClampMin = "0.01", UIMin = "0.01"))
		//float WheelMass;

		/** Longitudinal Friction Force Multiplier */
		UPROPERTY(EditAnywhere, Category = Wheel)
		float LongitudinalFrictionForceMultiplier;

		/** Lateral Friction Force Multiplier */
		UPROPERTY(EditAnywhere, Category = Wheel)
		float LateralFrictionForceMultiplier;

		/** Wheel Lateral Skid Grip Loss, lower number less grip on skid */
		UPROPERTY(EditAnywhere, Category = Wheel, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
		float SideSlipModifier;

		/** Wheel Longitudinal Slip Threshold */
		UPROPERTY(EditAnywhere, Category = Wheel, meta = (ClampMin = "0.0", UIMin = "0.0"))
		float SlipThreshold;

		/** Wheel Lateral Skid Threshold */
		UPROPERTY(EditAnywhere, Category = Wheel, meta = (ClampMin = "0.0", UIMin = "0.0"))
		float SkidThreshold;

		///** Damping rate for this wheel (Kgm^2/s) */
		//UPROPERTY(EditAnywhere, Category = Wheel, meta = (ClampMin = "0.01", UIMin = "0.01"))
		//float DampingRate;

		// steer angle in degrees for this wheel
		UPROPERTY(EditAnywhere, Category = WheelsSetup)
		float MaxSteerAngle;

		/** Whether steering should affect this wheel */
		UPROPERTY(EditAnywhere, Category = WheelsSetup)
		bool bAffectedBySteering;

		/** Whether brake should affect this wheel */
		UPROPERTY(EditAnywhere, Category = Wheel)
		bool bAffectedByBrake;

		/** Whether handbrake should affect this wheel */
		UPROPERTY(EditAnywhere, Category = Wheel)
		bool bAffectedByHandbrake;

		/** Whether engine should power this wheel */
		UPROPERTY(EditAnywhere, Category = Wheel)
		bool bAffectedByEngine;

		/** Advanced Braking System Enabled */
		UPROPERTY(EditAnywhere, Category = Wheel)
		bool bABSEnabled;

		/** Straight Line Traction Control Enabled */
		UPROPERTY(EditAnywhere, Category = Wheel)
		bool bTractionControlEnabled;

		///** Max normalized tire load at which the tire can deliver no more lateral stiffness no matter how much extra load is applied to the tire. */
		//UPROPERTY(EditAnywhere, Category = Tire, meta = (ClampMin = "0.01", UIMin = "0.01"))
		//float LatStiffMaxLoad;

		///** How much lateral stiffness to have given lateral slip */
		//UPROPERTY(EditAnywhere, Category = Tire, meta = (ClampMin = "0.01", UIMin = "0.01"))
		//float LatStiffValue;

		///** How much longitudinal stiffness to have given longitudinal slip */
		//UPROPERTY(EditAnywhere, Category = Tire)
		//float LongStiffValue;

		/** Local body direction in which where suspension forces are applied (typically along -Z-axis) */
		UPROPERTY(EditAnywhere, Category = Suspension)
		FVector SuspensionAxis;

		/** Vertical offset from where suspension forces are applied (along Z-axis) */
		UPROPERTY(EditAnywhere, Category = Suspension)
		FVector SuspensionForceOffset;

		/** How far the wheel can go above the resting position */
		UPROPERTY(EditAnywhere, Category = Suspension)
		float SuspensionMaxRaise;

		/** How far the wheel can drop below the resting position */
		UPROPERTY(EditAnywhere, Category = Suspension)
		float SuspensionMaxDrop;

		///** Oscillation frequency of suspension. Standard cars have values between 5 and 10 */
		//UPROPERTY(EditAnywhere, Category = Suspension)
		//float SuspensionNaturalFrequency;

		UPROPERTY(EditAnywhere, Category = Suspension)
		float SuspensionDampingRatio;

		/**
		 *	When 0 no weight load is transferred, 1 is Normal weight shift. Lower value cures lift off oversteer.
		 */
		UPROPERTY(EditAnywhere, Category = Suspension, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
		float WheelLoadRatio;

		/** Spring Force (N/m) */
		UPROPERTY(EditAnywhere, Category = Suspension)
		float SpringRate;

		/** Spring Preload (N/m) */
		UPROPERTY(EditAnywhere, Category = Suspension)
		float SpringPreload;

		/** Smooth suspension [0-off, 10-max] - Warning might cause momentary visual inter-penetration of the wheel against objects/terrain */
		UPROPERTY(EditAnywhere, Category = Suspension, meta = (UIMin = "0", UIMax = "10"))
		int SuspensionSmoothing;

		/** Anti-roll effect */
		UPROPERTY(EditAnywhere, Category = Suspension, meta = (UIMin = "0", UIMax = "1"))
		float RollbarScaling;

		// Now calculating damping from Suspension Damping Ratio - normalized value is slightly more meaningful
		///** Dampen rate of change of spring compression */
		//UPROPERTY(EditAnywhere, Category = Suspension)
		//float CompressionDamping;

		///** Dampen rate of change of spring extension */
		//UPROPERTY(EditAnywhere, Category = Suspension)
		//float ReboundDamping;

		/** Whether wheel suspension considers simple, complex, or both */
		UPROPERTY(EditAnywhere, Category = Suspension)
		ESweepShape SweepShape;

		/** Whether wheel suspension considers simple, complex, or both */
		UPROPERTY(EditAnywhere, Category = Suspension)
		ESweepType SweepType;

		/** max brake torque for this wheel (Nm) */
		UPROPERTY(EditAnywhere, Category = Brakes)
		float MaxBrakeTorque;

		/**
		 *	Max handbrake brake torque for this wheel (Nm). A handbrake should have a stronger brake torque
		 *	than the brake. This will be ignored for wheels that are not affected by the handbrake.
		 */
		UPROPERTY(EditAnywhere, Category = Brakes)
		float MaxHandBrakeTorque;

		/** The vehicle that owns us */
		UPROPERTY(transient)
		class UChaosWheeledVehicleMovementComponent* VehicleSim;

		// Our index in the vehicle's (and setup's) wheels array
		UPROPERTY(transient)
		int32 WheelIndex;

		// Longitudinal slip experienced by the wheel
		UPROPERTY(transient)
		float DebugLongSlip;

		// Lateral slip experienced by the wheel
		UPROPERTY(transient)
		float DebugLatSlip;

		// How much force the tire experiences at rest divided by how much force it is experiencing now
		UPROPERTY(transient)
		float DebugNormalizedTireLoad;

		//How much force the tire is experiencing now
		float DebugTireLoad;

		// Wheel torque
		UPROPERTY(transient)
		float DebugWheelTorque;

		// Longitudinal force the wheel is applying to the chassis
		UPROPERTY(transient)
		float DebugLongForce;

		// Lateral force the wheel is applying to the chassis
		UPROPERTY(transient)
		float DebugLatForce;

		// Worldspace location of this wheel
		UPROPERTY(transient)
		FVector Location;

		// Worldspace location of this wheel last frame
		UPROPERTY(transient)
		FVector OldLocation;

		// Current velocity of the wheel center (change in location over time)
		UPROPERTY(transient)
		FVector Velocity;

		UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
		float GetSteerAngle() const;

		UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
		float GetRotationAngle() const;

		UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
		float GetSuspensionOffset() const;

		UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
		bool IsInAir() const;

		UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
		EAxleType GetAxleType() { return AxleType; }


		/**
		 * Initialize this wheel instance
		 */
		virtual void Init(class UChaosWheeledVehicleMovementComponent* InVehicleSim, int32 InWheelIndex);

		/**
		 * Notify this wheel it will be removed from the scene
		 */
		virtual void Shutdown();

		/**
		 * Get the Axle setup we were created from
		 */
		struct FChaosWheelSetup& GetWheelSetup();

		/**
		 * Tick this class when the vehicle ticks
		 */
		virtual void Tick(float DeltaTime);

#if WITH_EDITOR

		/**
		 * Respond to a property change in editor
		 */
		virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

#endif //WITH_EDITOR

	protected:
	
		/**
		 * Get the wheel's location in physics land
		 */
		FVector GetPhysicsLocation();

	private:

		FChaosVehicleManager* GetVehicleManager() const;

		void FillWheelSetup()
		{
			// Perform any unit conversions here; between editable property and simulation system
			PWheelConfig.Offset = this->Offset;
			//PWheelConfig.WheelMass = this->WheelMass;
			PWheelConfig.WheelRadius = this->WheelRadius;
			PWheelConfig.WheelWidth = this->WheelWidth;
			PWheelConfig.MaxSteeringAngle = this->MaxSteerAngle;
			PWheelConfig.MaxBrakeTorque = this->MaxBrakeTorque;
			PWheelConfig.HandbrakeTorque = this->MaxHandBrakeTorque;

			PWheelConfig.SteeringEnabled = this->bAffectedBySteering;
			PWheelConfig.BrakeEnabled = this->bAffectedByBrake;
			PWheelConfig.HandbrakeEnabled = this->bAffectedByHandbrake;
			PWheelConfig.EngineEnabled = this->bAffectedByEngine;
			PWheelConfig.ABSEnabled = this->bABSEnabled;
			PWheelConfig.TractionControlEnabled = this->bTractionControlEnabled;
			PWheelConfig.LongitudinalFrictionMultiplier = this->LongitudinalFrictionForceMultiplier;
			PWheelConfig.LateralFrictionMultiplier = this->LateralFrictionForceMultiplier;
			PWheelConfig.SideSlipModifier = this->SideSlipModifier;
			PWheelConfig.SlipThreshold = this->SlipThreshold;
			PWheelConfig.SkidThreshold = this->SkidThreshold;
		}

		void FillSuspensionSetup()
		{
			PSuspensionConfig.SuspensionAxis = this->SuspensionAxis;

			PSuspensionConfig.SuspensionForceOffset = this->SuspensionForceOffset;
			PSuspensionConfig.SuspensionMaxRaise = this->SuspensionMaxRaise;
			PSuspensionConfig.SuspensionMaxDrop = this->SuspensionMaxDrop;
			PSuspensionConfig.SpringRate = Chaos::MToCm(this->SpringRate);
			PSuspensionConfig.SpringPreload = Chaos::MToCm(this->SpringPreload);

			PSuspensionConfig.DampingRatio = this->SuspensionDampingRatio;
			PSuspensionConfig.WheelLoadRatio = this->WheelLoadRatio;

			PSuspensionConfig.SuspensionSmoothing = this->SuspensionSmoothing;

			// These are calculated later from the PSuspensionConfig.DampingRatio
			//		PSuspensionConfig.ReboundDamping
			//		PSuspensionConfig.CompressionDamping
		}

		Chaos::FSimpleWheelConfig PWheelConfig;
		Chaos::FSimpleSuspensionConfig PSuspensionConfig;

	public:

		const Chaos::FSimpleWheelConfig& GetPhysicsWheelConfig()
		{
			FillWheelSetup();
			return PWheelConfig;
		}

		const Chaos::FSimpleSuspensionConfig& GetPhysicsSuspensionConfig()
		{
			FillSuspensionSetup();
			return PSuspensionConfig;
		}

		/** Get contact surface material */
		UPhysicalMaterial* GetContactSurfaceMaterial();

		/** suspension raycast results */
		FHitResult HitResult;

	};

