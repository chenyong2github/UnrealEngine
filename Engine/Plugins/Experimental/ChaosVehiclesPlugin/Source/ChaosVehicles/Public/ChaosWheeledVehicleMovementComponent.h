// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ChaosVehicleMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "VehicleUtility.h"
#include "ChaosWheeledVehicleMovementComponent.generated.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

/**
 * There is too much information for one screen full of debug data, so sub-pages of information are available 
 * Advance through pages using p.Vehicles.NextDebugPage | p.Vehicles.PrevDebugPage which can be hooked
 * up to the keyboard or a controller in blueprint using execCommand
 */
enum EDebugPages : uint8
{
	BasicPage = 0,
	FrictionPage,
	SuspensionPage,
	TransmissionPage,

	MaxDebugPages	// keep as last value
};

// #todo: none of these are implemented yet - probably closest to Open configuration right now
UENUM()
enum class EVehicleDifferential : uint8
{
	LimitedSlip_4W,
	LimitedSlip_FrontDrive,
	LimitedSlip_RearDrive,
	Open_4W,
	Open_FrontDrive,
	Open_RearDrive,
};

USTRUCT()
struct FVehicleDifferentialConfig
{
	GENERATED_USTRUCT_BODY()

	/** Type of differential */
	UPROPERTY(EditAnywhere, Category=Setup)
	EVehicleDifferential DifferentialType;
	
	/** Ratio of torque split between front and rear (>0.5 means more to front, <0.5 means more to rear, works only with 4W type) */
	UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	float FrontRearSplit;

	///** Ratio of torque split between front-left and front-right (>0.5 means more to front-left, <0.5 means more to front-right, works only with 4W and LimitedSlip_FrontDrive) */
	//UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	//float FrontLeftRightSplit;

	///** Ratio of torque split between rear-left and rear-right (>0.5 means more to rear-left, <0.5 means more to rear-right, works only with 4W and LimitedSlip_RearDrive) */
	//UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	//float RearLeftRightSplit;
	//
	///** Maximum allowed ratio of average front wheel rotation speed and rear wheel rotation speeds (range: 1..inf, works only with LimitedSlip_4W) */
	//UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "1.0", UIMin = "1.0"))
	//float CentreBias;

	///** Maximum allowed ratio of front-left and front-right wheel rotation speeds (range: 1..inf, works only with LimitedSlip_4W, LimitedSlip_FrontDrive) */
	//UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "1.0", UIMin = "1.0"))
	//float FrontBias;

	///** Maximum allowed ratio of rear-left and rear-right wheel rotation speeds (range: 1..inf, works only with LimitedSlip_4W, LimitedSlip_FrontDrive) */
	//UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "1.0", UIMin = "1.0"))
	//float RearBias;
};

USTRUCT()
struct FVehicleEngineConfig
{
	GENERATED_USTRUCT_BODY()

	/** Torque [Normalized 0..1] for a given RPM */
	UPROPERTY(EditAnywhere, Category = Setup)
	FRuntimeFloatCurve TorqueCurve;

	/** Max Engine Torque (Nm) is multiplied by TorqueCurve */
	UPROPERTY(EditAnywhere, Category = Setup)
	float MaxTorque;

	/** Maximum revolutions per minute of the engine */
	UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "0.01", UIMin = "0.01"))
	float MaxRPM;

	/** Idle RMP of engine then in neutral/stationary */
	UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "0.01", UIMin = "0.01"))
	float EngineIdleRPM;

	/** Maximum revolutions per minute of the engine */
	UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "0.001", UIMin = "0.001"))
	float EngineBrakeEffect;

	///** Moment of inertia of the engine around the axis of rotation (Kgm^2). */
	//UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "0.01", UIMin = "0.01"))
	//float MOI;

	///** Damping rate of engine when full throttle is applied (Kgm^2/s) */
	//UPROPERTY(EditAnywhere, Category = Setup, AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0"))
	//float DampingRateFullThrottle;

	///** Damping rate of engine in at zero throttle when the clutch is engaged (Kgm^2/s)*/
	//UPROPERTY(EditAnywhere, Category = Setup, AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0"))
	//float DampingRateZeroThrottleClutchEngaged;

	///** Damping rate of engine in at zero throttle when the clutch is disengaged (in neutral gear) (Kgm^2/s)*/
	//UPROPERTY(EditAnywhere, Category = Setup, AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0"))
	//float DampingRateZeroThrottleClutchDisengaged;

	/** Find the peak torque produced by the TorqueCurve */
	float FindPeakTorque() const;

	const Chaos::FSimpleEngineConfig& GetPhysicsEngineConfig()
	{
		FillEngineSetup();
		return PEngineConfig;
	}

private:

	void FillEngineSetup()
	{
		// #todo: better Chaos torque curve representation
		PEngineConfig.TorqueCurve.Empty();
		for (float X=0; X<1.1f; X+=0.1f)
		{ 
			PEngineConfig.TorqueCurve.Add(this->TorqueCurve.GetRichCurveConst()->Eval(X));
		}
		PEngineConfig.MaxTorque = this->MaxTorque;
		PEngineConfig.MaxRPM = this->MaxRPM;
		PEngineConfig.EngineIdleRPM = this->EngineIdleRPM;
		PEngineConfig.EngineBrakeEffect = this->EngineBrakeEffect;
	}

	Chaos::FSimpleEngineConfig PEngineConfig;

};

USTRUCT()
struct FVehicleTransmissionConfig
{
	GENERATED_USTRUCT_BODY()

	/** Whether to use automatic transmission */
	UPROPERTY(EditAnywhere, Category = VehicleSetup, meta=(DisplayName = "Automatic Transmission"))
	bool bUseAutomaticGears;

	UPROPERTY(EditAnywhere, Category = VehicleSetup, meta = (DisplayName = "Automatic Reverse"))
	bool bUseAutoReverse;

	/** Time it takes to switch gears (seconds) */
	UPROPERTY(EditAnywhere, Category = Setup, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GearChangeTime;

	/** The final gear ratio multiplies the transmission gear ratios.*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Setup)
	float FinalRatio;

	/** Forward gear ratios */
	UPROPERTY(EditAnywhere, Category = Setup, AdvancedDisplay)
	TArray<float> ForwardGearRatios;

	/** Reverse gear ratio(s) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Setup)
	TArray<float> ReverseGearRatios;

	/** Engine Revs at which gear up change ocurrs */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"), Category = Setup)
	float ChangeUpRPM;

	/** Engine Revs at which gear down change ocurrs */
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"), Category = Setup)
	float ChangeDownRPM;

	/** Value of engineRevs/maxEngineRevs that is high enough to increment gear*/
	//UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Setup, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	//float NeutralGearUpRatio;

	///** Strength of clutch (Kgm^2/s)*/
	//UPROPERTY(EditAnywhere, Category = Setup, AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0"))
	//float ClutchStrength;

	///** Loss of Power from mechanical friction in the transmission system (N.m) */
	//UPROPERTY(EditAnywhere, Category = Setup, AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0"))
	//float MechanicalFrictionLosses;

	const Chaos::FSimpleTransmissionConfig& GetPhysicsTransmissionConfig()
	{
		FillTransmissionSetup();
		return PTransmissionConfig;
	}

private:

	void FillTransmissionSetup()
	{
		PTransmissionConfig.TransmissionType = this->bUseAutomaticGears ? Chaos::ETransmissionType::Automatic : Chaos::ETransmissionType::Manual;
		PTransmissionConfig.AutoReverse = this->bUseAutoReverse;
		PTransmissionConfig.ChangeDownRPM = this->ChangeUpRPM;
		PTransmissionConfig.ChangeUpRPM = this->ChangeDownRPM;
		PTransmissionConfig.GearChangeTime = this->GearChangeTime;
		PTransmissionConfig.FinalDriveRatio = this->FinalRatio;
		PTransmissionConfig.ForwardRatios.Reset();
		for (float Ratio : this->ForwardGearRatios)
		{
			PTransmissionConfig.ForwardRatios.Add(Ratio);
		}

		PTransmissionConfig.ReverseRatios.Reset();
		for (float Ratio : this->ReverseGearRatios)
		{
			PTransmissionConfig.ReverseRatios.Add(Ratio);
		}
	}

	Chaos::FSimpleTransmissionConfig PTransmissionConfig;

};

USTRUCT()
struct CHAOSVEHICLES_API FChaosWheelSetup
{
	GENERATED_USTRUCT_BODY()

	// The wheel class to use
	UPROPERTY(EditAnywhere, Category = WheelSetup)
	TSubclassOf<UChaosVehicleWheel> WheelClass;

	// Bone name on mesh to create wheel at
	UPROPERTY(EditAnywhere, Category = WheelSetup)
	FName BoneName;

	// Additional offset to give the wheels for this axle.
	UPROPERTY(EditAnywhere, Category = WheelSetup)
	FVector AdditionalOffset;

	FChaosWheelSetup();
};


UCLASS(ClassGroup = (Physics), meta = (BlueprintSpawnableComponent), hidecategories = (PlanarMovement, "Components|Movement|Planar", Activation, "Components|Activation"))
class CHAOSVEHICLES_API UChaosWheeledVehicleMovementComponent : public UChaosVehicleMovementComponent
{
	GENERATED_UCLASS_BODY()

	/** Wheels to create */
	UPROPERTY(EditAnywhere, Category = VehicleSetup)
	TArray<FChaosWheelSetup> WheelSetups;

	/** Engine */
	UPROPERTY(EditAnywhere, Category = MechanicalSetup)
	FVehicleEngineConfig EngineSetup;

	/** Differential */
	//UPROPERTY(EditAnywhere, Category = MechanicalSetup)
	//FVehicleDifferentialConfig DifferentialSetup;

	/** Accuracy of Ackermann steer calculation (range: 0..1) */
	//UPROPERTY(EditAnywhere, Category = SteeringSetup, AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "1.0", UIMax = "1.0"))
	//float AckermannAccuracy;

	/** Transmission data */
	UPROPERTY(EditAnywhere, Category = MechanicalSetup)
	FVehicleTransmissionConfig TransmissionSetup;

	/** Maximum steering versus forward speed (km/h) */
	UPROPERTY(EditAnywhere, Category = SteeringSetup)
	FRuntimeFloatCurve SteeringCurve;

	// Our instanced wheels
	UPROPERTY(transient, duplicatetransient, BlueprintReadOnly, Category = Vehicle)
	TArray<class UChaosVehicleWheel*> Wheels;

	////////////////////////////////////////////////////////////////////////////
		/** Set the user input for the vehicle throttle */
	//UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
	//	void SetThrottleInput(float Throttle);

	///** Set the user input for the vehicle Brake */
	//UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
	//	void SetBrakeInput(float Brake);

	///** Set the user input for the vehicle steering */
	//UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
	//	void SetSteeringInput(float Steering);

	///** Set the user input for handbrake */
	//UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
	//	void SetHandbrakeInput(bool bNewHandbrake);

	///** Set the user input for gear up */
	//UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
	//	void SetGearUp(bool bNewGearUp);

	///** Set the user input for gear down */
	//UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
	//	void SetGearDown(bool bNewGearDown);

	///** Set the user input for gear (-1 reverse, 0 neutral, 1+ forward)*/
	//UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
	//	void SetTargetGear(int32 GearNum, bool bImmediate);

	///** Set the flag that will be used to select auto-gears */
	//UFUNCTION(BlueprintCallable, Category = "Game|Components|WheeledVehicleMovement")
	//	void SetUseAutoGears(bool bUseAuto);

	/** Get current engine's rotation speed */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosWheeledVehicleMovement")
		float GetEngineRotationSpeed() const;

	/** Get current engine's max rotation speed */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosWheeledVehicleMovement")
		float GetEngineMaxRotationSpeed() const;

	/** Get current gear */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosWheeledVehicleMovement")
		int32 GetCurrentGear() const;

	/** Get target gear */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosWheeledVehicleMovement")
		int32 GetTargetGear() const;

	/** Are gears being changed automatically? */
	UFUNCTION(BlueprintCallable, Category = "Game|Components|ChaosWheeledVehicleMovement")
		bool GetUseAutoGears() const;

	float GetMaxSpringForce() const; //??

	virtual void Serialize(FArchive & Ar) override;
	virtual void ComputeConstants() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Tick this vehicle sim right before input is sent to the vehicle system  */
	virtual void TickVehicle(float DeltaTime);

	/** Advance the vehicle simulation */
	virtual void UpdateSimulation(float DeltaTime);

	/** Used to create any physics engine information for this component */
	virtual void OnCreatePhysicsState() override;

	/** Used to shut down and pysics engine structure for this component */
	virtual void OnDestroyPhysicsState() override;

	virtual bool CanCreateVehicle() const override;

	/** Set up the chassis and wheel shapes */
	virtual void SetupVehicleShapes();

	/** Setup calculated suspension parameters */
	void SetupSuspension();

	/** Are enough vehicle systems specified such that physics vehicle simulation is possible */
	virtual bool CanSimulate() const override;

	/** display next debug page */
	static void NextDebugPage();

	/** display previous debug page */
	static void PrevDebugPage();

	/** Get the local position of the wheel at rest */
	virtual FVector GetWheelRestingPosition(const FChaosWheelSetup& WheelSetup);

	/** Perform suspension ray/shape traces */
	virtual void PerformSuspensionTraces(TArray<FSimpleSuspensionSim>& Suspension);

protected:
	/** Allocate and setup the Chaos vehicle */
	virtual void SetupVehicle() override;

	void DrawDebug3D();


	///** update simulation data: engine */
	//void UpdateEngineSetup(const FVehicleEngineConfig& NewEngineSetup);

	///** update simulation data: differential */
	//void UpdateDifferentialSetup(const FVehicleDifferentialConfig& NewDifferentialSetup);

	///** update simulation data: transmission */
	//void UpdateTransmissionSetup(const FVehicleTransmissionConfig& NewGearSetup);


	/** Instantiate and setup our wheel objects */
	virtual void CreateWheels();

	/** Release our wheel objects */
	virtual void DestroyWheels();

	/** Draw 2D debug text graphs on UI for the wheels, suspension and other systems */
	virtual void DrawDebug(UCanvas* Canvas, float& YL, float& YPos);

	private:
	uint32 NumDrivenWheels;

	/** Get distances between wheels - primarily a debug display helper */
	FVector2D GetWheelLayoutDimensions();
	static EDebugPages DebugPage;

};

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
