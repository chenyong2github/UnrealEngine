// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"

// for System Unit Tests
#include "AerodynamicsSystem.h"
#include "TransmissionSystem.h"
#include "EngineSystem.h"
#include "WheelSystem.h"
#include "TireSystem.h"
#include "SuspensionSystem.h"
#include "SuspensionUtility.h"

// for Simulation Tests
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

//////////////////////////////////////////////////////////////////////////
// These tests are mostly working in real word units rather than Unreal 
// units as it's easier to tell if the simulations are working close to 
// reality. i.e. Google stopping distance @ 30MPH ==> typically 15 metres
//////////////////////////////////////////////////////////////////////////
PRAGMA_DISABLE_OPTIMIZATION

namespace ChaosTest
{
	using namespace Chaos;

	TYPED_TEST(AllTraits, VehicleTest_SystemTemplate)
	{
		FSimpleTireConfig Setup;
		{
			Setup.Radius = 0.44f;
		}

		FSimpleTireSim Tire(&Setup);

		EXPECT_LT(Tire.AccessSetup().Radius - Setup.Radius, SMALL_NUMBER);
		EXPECT_LT(Tire.Setup().Radius - Setup.Radius, SMALL_NUMBER);

	}

	// Aerodynamics
	TYPED_TEST(AllTraits, VehicleTest_Aerodynamics)
	{
		FSimpleAerodynamicsConfig Setup;
		{
			Setup.AreaMetresSquared = 1.f * 2.f; // 1x2 m
			Setup.DragCoefficient = 0.5f;
			Setup.DownforceCoefficient = 0.1f;
		}

		FSimpleAerodynamicsSim Aerofoil(&Setup);
		Aerofoil.SetDensityOfMedium(RealWorldConsts::AirDensity());

		float Drag = 0;
		Drag = Aerofoil.GetDragForceFromVelocity(0.f);
		EXPECT_LT(Drag, SMALL_NUMBER);

		Drag = Aerofoil.GetDragForceFromVelocity(1.f); // 1m.s-1
		EXPECT_LT(Drag - (RealWorldConsts::AirDensity() * 0.5f), SMALL_NUMBER);

		Drag = Aerofoil.GetDragForceFromVelocity(5.f); // 5m.s-1
		EXPECT_LT(Drag - (RealWorldConsts::AirDensity() * 0.5f * 25.f), SMALL_NUMBER);

		Drag = Aerofoil.GetDragForceFromVelocity(10.f); // 10m.s-1
		EXPECT_LT(Drag - (RealWorldConsts::AirDensity() * 0.5f * 100.f), SMALL_NUMBER);

		float Lift = 0;
		Lift = Aerofoil.GetLiftForceFromVelocity(0.f);
		EXPECT_LT(Lift, SMALL_NUMBER);

		Lift = Aerofoil.GetLiftForceFromVelocity(1.f);
		EXPECT_LT(Lift - (RealWorldConsts::AirDensity() * 0.1f), SMALL_NUMBER);

		Lift = Aerofoil.GetLiftForceFromVelocity(5.f);
		EXPECT_LT(Lift - (RealWorldConsts::AirDensity() * 0.1f * 25.f), SMALL_NUMBER);

		Lift = Aerofoil.GetLiftForceFromVelocity(10.f);
		EXPECT_LT(Lift - (RealWorldConsts::AirDensity() * 0.1f * 100.f), SMALL_NUMBER);

		// investigating Unral Units vs real world units
		/*{
			FSimpleAerodynamicsConfig Setup;
			{
				Setup.AreaMetresSquared = 1.f * 2.f; // 1x2 m
				Setup.DragCoefficient = 0.5f;
				Setup.DownforceCoefficient = 0.1f;
			}

			///////////////////////////////////////////////////////////////
			FSimpleAerodynamicsSim Aerofoil(&Setup);
			Aerofoil.SetDensityOfMedium(RealWorldConsts::AirDensity());

			float Drag5 = 0;
			Drag5 = Aerofoil.GetDragForceFromVelocity(5.f);


			/////////////////////

			FSimpleAerodynamicsConfig SetupB;
			{
				Setup.AreaMetresSquared = 1.f * 2.f; // 1x2 m
				Setup.DragCoefficient = 0.5f;
				Setup.DownforceCoefficient = 0.1f;
			}

			FSimpleAerodynamicsSim AerofoilB(&Setup);
			Aerofoil.SetDensityOfMedium(RealWorldConsts::AirDensity());


			////////////////////////////////////////////////////////////////////
			// Trying to get head around standard vs unreal units.
			// If the mass is the same between the two simulations then the
			// the following is true.
			float PosA = 0, PosB = 0;
			float VelA = 100.0f;
			float VelB = 10000.0f;

			float DeltaTime = 1.f / 30.f;

			for (int i = 0; i < 200; i++)
			{
				float DragA = Aerofoil.GetDragForceFromVelocity(VelA);
				float DragB = (AerofoilB.GetDragForceFromVelocity(CmToM(VelB))); // no final MToCm conversion

				VelA += (DragA / 1000.0f) * DeltaTime;
				VelB += (DragB / 1000.0f) * DeltaTime;
				PosA += VelA * DeltaTime;
				PosB += VelB * DeltaTime;

				UE_LOG(LogChaos, Warning, TEXT("Drag %f %f"), DragA, DragB);
				UE_LOG(LogChaos, Warning, TEXT("Vel %f %f"), VelA, VelB);

				UE_LOG(LogChaos, Warning, TEXT("------"));
			}
		}*/
	}

	// Transmission
	TYPED_TEST(AllTraits, VehicleTest_TransmissionManualGearSelection)
	{
		FSimpleTransmissionConfig Setup;
		{
			Setup.ForwardRatios.Add(4.f);
			Setup.ForwardRatios.Add(3.f);
			Setup.ForwardRatios.Add(2.f);
			Setup.ForwardRatios.Add(1.f);
			Setup.ReverseRatios.Add(3.f);
			Setup.FinalDriveRatio = 4.f;
			Setup.ChangeUpRPM = 3000;
			Setup.ChangeDownRPM = 1200;
			Setup.GearChangeTime = 0.0f;
			Setup.TransmissionType = ETransmissionType::Manual;
			Setup.AutoReverse = true;
		}

		FSimpleTransmissionSim Transmission(&Setup);

		EXPECT_EQ(Transmission.GetCurrentGear(), 0);

		// Immediate Gear Change, since Setup.GearChangeTime = 0.0f
		Transmission.ChangeUp();

		EXPECT_EQ(Transmission.GetCurrentGear(), 1);
		Transmission.ChangeUp();
		Transmission.ChangeUp();
		Transmission.ChangeUp();
		EXPECT_EQ(Transmission.GetCurrentGear(), 4);

		Transmission.ChangeUp();
		EXPECT_EQ(Transmission.GetCurrentGear(), 4);

		Transmission.SetGear(1);
		EXPECT_EQ(Transmission.GetCurrentGear(), 1);

		Transmission.ChangeDown();
		EXPECT_EQ(Transmission.GetCurrentGear(), 0);

		Transmission.ChangeDown();
		EXPECT_EQ(Transmission.GetCurrentGear(), -1);

		Transmission.ChangeDown();
		EXPECT_EQ(Transmission.GetCurrentGear(), -1);

		Transmission.SetGear(1);

		// Now change settings so we have a delay in the gear changing
		Transmission.AccessSetup().GearChangeTime = 0.5f;

		Transmission.ChangeUp();
		EXPECT_EQ(Transmission.GetCurrentGear(), 0);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 0);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 2);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 2);

		Transmission.SetGear(4);
		EXPECT_EQ(Transmission.GetCurrentGear(), 0);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 0);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 4);


	}

	TYPED_TEST(AllTraits, VehicleTest_TransmissionAutoGearSelection)
	{
		FSimpleTransmissionConfig Setup;
		{
			Setup.ForwardRatios.Add(4.f);
			Setup.ForwardRatios.Add(3.f);
			Setup.ForwardRatios.Add(2.f);
			Setup.ForwardRatios.Add(1.f);
			Setup.ReverseRatios.Add(3.f);
			Setup.FinalDriveRatio = 4.f;
			Setup.ChangeUpRPM = 3000;
			Setup.ChangeDownRPM = 1200;
			Setup.GearChangeTime = 0.0f;
			Setup.TransmissionType = ETransmissionType::Automatic;
			Setup.AutoReverse = true;
		}

		FSimpleTransmissionSim Transmission(&Setup);

		Transmission.SetGear(1, true);

		Transmission.SetEngineRPM(1400);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 1);

		Transmission.SetEngineRPM(2000);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 1);

		Transmission.SetEngineRPM(3000);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 2);

		Transmission.SetEngineRPM(2000);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 2);

		Transmission.SetEngineRPM(1000);
		Transmission.Simulate(0.25f);
		EXPECT_EQ(Transmission.GetCurrentGear(), 1);

	}

	TYPED_TEST(AllTraits, VehicleTest_TransmissionGearRatios)
	{
		FSimpleTransmissionConfig Setup;
		{
			Setup.ForwardRatios.Add(4.f);
			Setup.ForwardRatios.Add(3.f);
			Setup.ForwardRatios.Add(2.f);
			Setup.ForwardRatios.Add(1.f);
			Setup.ReverseRatios.Add(3.f);
			Setup.FinalDriveRatio = 4.f;
			Setup.ChangeUpRPM = 3000;
			Setup.ChangeDownRPM = 1200;
			Setup.GearChangeTime = 0.0f;
			Setup.TransmissionType = ETransmissionType::Automatic;
			Setup.AutoReverse = true;
		}

		FSimpleTransmissionSim Transmission(&Setup);

		float Ratio = 0;
		Ratio = Transmission.GetGearRatio(-1);
		EXPECT_LT(-12.f - Ratio, SMALL_NUMBER); // -ve output for reverse gears

		Ratio = Transmission.GetGearRatio(0);
		EXPECT_LT(Ratio, SMALL_NUMBER);

		Ratio = Transmission.GetGearRatio(1);
		EXPECT_LT(16.f - Ratio, SMALL_NUMBER);

		Ratio = Transmission.GetGearRatio(2);
		EXPECT_LT(12.f - Ratio, SMALL_NUMBER);

		Ratio = Transmission.GetGearRatio(3);
		EXPECT_LT(8.f - Ratio, SMALL_NUMBER);

		Ratio = Transmission.GetGearRatio(4);
		EXPECT_LT(4.f - Ratio, SMALL_NUMBER);

	}

	// Engine
	TYPED_TEST(AllTraits, VehicleTest_EngineRPM)
	{
		// #todo: fix engine rev out of gear
		//FSimpleEngineConfig Setup;
		//{
		//	Setup.MaxRPM = 5000;
		//	Setup.EngineIdleRPM = 1000;
		//	Setup.MaxTorque = 100.f;
		//	//Setup.TorqueCurve;
		//}

		//FSimpleEngineSim Engine(&Setup);

		//Engine.SetThrottle(0.f);

		//float DeltaTime = 1.0f / 30.0f;
		//float TOLERANCE = 0.01f;

		//// engine idle - no throttle
		//for (int i = 0; i < 300; i++)
		//{
		//	Engine.Simulate(DeltaTime);
		//}

		//EXPECT_LT(Engine.GetEngineRPM() - Engine.Setup().EngineIdleRPM, TOLERANCE);

		//// apply half throttle
		//Engine.SetThrottle(0.5f);

		//for (int i = 0; i < 300; i++)
		//{
		//	Engine.Simulate(DeltaTime);

		//	//UE_LOG(LogChaos, Warning, TEXT("EngineSpeed %.2f rad/sec (%.1f RPM)"), Engine.GetEngineSpeed(), Engine.GetEngineRPM());
		//}

		//EXPECT_GT(Engine.GetEngineRPM(), Engine.Setup().EngineIdleRPM);

		////UE_LOG(LogChaos, Warning, TEXT(""));
		////UE_LOG(LogChaos, Warning, TEXT("No Throttle"));

		//Engine.SetThrottle(0.0f);

		//// engine idle - no throttle
		//for (int i = 0; i < 300; i++)
		//{
		//	Engine.Simulate(DeltaTime);

		//	//UE_LOG(LogChaos, Warning, TEXT("EngineSpeed %.2f rad/sec (%.1f RPM)"), Engine.GetEngineSpeed(), Engine.GetEngineRPM());
		//}

		//EXPECT_LT(Engine.GetEngineRPM() - Engine.Setup().EngineIdleRPM, TOLERANCE);

	}

	// Wheel
	void SimulateBraking(FSimpleWheelSim& Wheel
		, float VehicleSpeedMPH
		, float& StoppingDistanceOut
		, float DeltaTime)
	{
		StoppingDistanceOut = 0.f;

		const float Gravity = 9.8f;
		float MaxSimTime = 15.0f;
		float SimulatedTime = 0.f;
		float VehicleMass = 1300.f;
		float VehicleMassPerWheel = 1300.f / 4.f;

		Wheel.SetWheelLoadForce(VehicleMassPerWheel * Gravity);

		// Road speed
		FVector Velocity = FVector(MPHToMS(VehicleSpeedMPH), 0.f, 0.f);

		// wheel rolling speed matches road speed
		Wheel.SetMatchingSpeed(Velocity.X);

		while (SimulatedTime < MaxSimTime)
		{
			// rolling speed matches road speed
			Wheel.SetVehicleGroundSpeed(Velocity);

			Wheel.Simulate(DeltaTime);

			// deceleration from brake, F = m * a, a = F / m, v = dt * F / m
			Velocity += DeltaTime * Wheel.GetForceFromFriction() / VehicleMassPerWheel;
			StoppingDistanceOut += Velocity.X * DeltaTime;

			// #todo: make this better remove the 2.0f
			if (FMath::Abs(Velocity.X) < 2.0f)
			{
				Velocity.X = 0.f;
				break; // break out early if already stopped
			}

			SimulatedTime += DeltaTime;
		}
	}

	void SimulateAccelerating(FSimpleWheelSim& Wheel
		, const float Gravity
		, float VehicleSpeedMPH
		, float& DistanceTravelledOut
		, float DeltaTime)
	{
		DistanceTravelledOut = 0.f;

		float MaxSimTime = 15.0f;
		float SimulatedTime = 0.f;
		float VehicleMass = 1600.f;
		float VehicleMassPerWheel = VehicleMass / 4.f;

		Wheel.SetWheelLoadForce(VehicleMassPerWheel * Gravity);

		// Road speed
		FVector Velocity = FVector(MPHToMS(VehicleSpeedMPH), 0.f, 0.f);

		// wheel rolling speed matches road speed
		Wheel.SetMatchingSpeed(Velocity.X);

		while (SimulatedTime < MaxSimTime)
		{
			Wheel.SetVehicleGroundSpeed(Velocity);
			Wheel.Simulate(DeltaTime);

			Velocity += DeltaTime * Wheel.GetForceFromFriction() / VehicleMassPerWheel;
			DistanceTravelledOut += Velocity.X * DeltaTime;

			SimulatedTime += DeltaTime;

			if (SimulatedTime > 5.0f)
			{
				break; // break out early if already stopped
			}

		}
	}

	TYPED_TEST(AllTraits, DISABLED_VehicleTest_WheelBrakingLongitudinalSlip)
	{
		FSimpleWheelConfig Setup;
		FSimpleWheelSim Wheel(&Setup);

		// Google braking distance at 30mph says 14m (not interested in the thinking distance part)
		// So using a range 10-20 to ensure we are in the correct ballpark.
		// If specified more accurately in the test, then modifying the code would break the test all the time.

		float Tolerance = 0.5f;
		float DeltaTime = 1.f / 30.f;
		float StoppingDistanceA = 0.f;
		Wheel.SetSurfaceFriction(RealWorldConsts::DryRoadFriction());

		// reasonably ideal stopping distance - traveling forwards
		Wheel.SetBrakeTorque(450);
		float VehicleSpeedMPH = 30.f;
		SimulateBraking(Wheel, VehicleSpeedMPH, StoppingDistanceA, DeltaTime);
		//UE_LOG(LogChaos, Warning, TEXT("Braking Distance %3.2fm"), StoppingDistanceB);
		EXPECT_GT(StoppingDistanceA, 10.f);
		EXPECT_LT(StoppingDistanceA, 20.f);

		// traveling backwards stops just the same
		float StoppingDistanceReverseDir = 0.f;
		Wheel.SetBrakeTorque(450);
		VehicleSpeedMPH = -30.f;
		SimulateBraking(Wheel, VehicleSpeedMPH, StoppingDistanceReverseDir, DeltaTime);
		EXPECT_GT(StoppingDistanceReverseDir, -20.f);
		EXPECT_LT(StoppingDistanceReverseDir, -10.f);
		EXPECT_LT(StoppingDistanceA - FMath::Abs(StoppingDistanceReverseDir), Tolerance);

		// Similar results with different delta time
		float StoppingDistanceDiffDT = 0.f;
		VehicleSpeedMPH = 30.f;
		SimulateBraking(Wheel, VehicleSpeedMPH, StoppingDistanceDiffDT, DeltaTime * 0.5f);
		EXPECT_LT(StoppingDistanceA - StoppingDistanceDiffDT, Tolerance);

		// barely touching the brake - going to take longer to stop
		float StoppingDistanceLightBraking = 0.f;
		Wheel.SetBrakeTorque(150);
		VehicleSpeedMPH = 30.f;
		SimulateBraking(Wheel, VehicleSpeedMPH, StoppingDistanceLightBraking, DeltaTime);
		EXPECT_GT(StoppingDistanceLightBraking, StoppingDistanceA);

		// locking the wheels / too much brake torque -> dynamic friction rather than static friction -> going to take longer to stop
		float StoppingDistanceTooHeavyBreaking = 0.f;
		Wheel.SetBrakeTorque(5000);
		VehicleSpeedMPH = 30.f;
		SimulateBraking(Wheel, VehicleSpeedMPH, StoppingDistanceTooHeavyBreaking, DeltaTime);
		EXPECT_GT(StoppingDistanceTooHeavyBreaking, StoppingDistanceA);

		// lower initial speed - stops more quickly
		float StoppingDistanceLowerSpeed = 0.f;
		Wheel.SetBrakeTorque(450);
		VehicleSpeedMPH = 20.f;
		SimulateBraking(Wheel, VehicleSpeedMPH, StoppingDistanceLowerSpeed, DeltaTime);
		EXPECT_LT(StoppingDistanceLowerSpeed, StoppingDistanceA);

		// higher initial speed - stops more slowly
		float StoppingDistanceHigherSpeed = 0.f;
		Wheel.SetBrakeTorque(450);
		VehicleSpeedMPH = 60.f;
		SimulateBraking(Wheel, VehicleSpeedMPH, StoppingDistanceHigherSpeed, DeltaTime);
		EXPECT_GT(StoppingDistanceHigherSpeed, StoppingDistanceA);

		// slippy surface - stops more slowly
		float StoppingDistanceLowFriction = 0.f;
		Wheel.SetSurfaceFriction(0.3f);
		Wheel.SetBrakeTorque(450);
		VehicleSpeedMPH = 30.f;
		SimulateBraking(Wheel, VehicleSpeedMPH, StoppingDistanceLowFriction, DeltaTime);
		EXPECT_GT(StoppingDistanceLowFriction, StoppingDistanceA);
	}

	TYPED_TEST(AllTraits, DISABLED_VehicleTest_WheelAcceleratingLongitudinalSlip)
	{
		FSimpleWheelConfig Setup;
		FSimpleWheelSim Wheel(&Setup);

		/*
			Available Friction Force	= Mass * Gravity;
										= 1600Kg * 9.8 m.s-2 / 4 (wheels)
										= 3920 N

			Applied Wheel Torque		= AppliedEngineTorque * CombinedGearRatios / 2 (wheels)
										= 150Nm * 12 / 3;
										= 900

			Applied Wheel Force			= 900 / WheelRadius
										= 3000 N
		*/

		// units meters
		float Gravity = 9.8f;
		float DeltaTime = 1.f / 30.f;
		float DrivingDistanceA = 0.f;
		Wheel.SetDriveTorque(450);
		float VehicleSpeedMPH = 0.f;
		SimulateAccelerating(Wheel, Gravity, VehicleSpeedMPH, DrivingDistanceA, DeltaTime);
		EXPECT_GT(DrivingDistanceA, 70.f);
		EXPECT_LT(DrivingDistanceA, 90.f);

		// units cm
		float MtoCm = 100.0f;
		float DrivingDistanceCM = 0.f;
		Wheel.SetDriveTorque(450 * MtoCm);
		VehicleSpeedMPH = 0.f;
		SimulateAccelerating(Wheel, Gravity * MtoCm, VehicleSpeedMPH, DrivingDistanceCM, DeltaTime);
		EXPECT_GT(DrivingDistanceCM, 70.f * MtoCm);
		EXPECT_LT(DrivingDistanceCM, 90.f * MtoCm);


		float DrivingDistanceWheelspin = 0.f;
		Wheel.SetDriveTorque(5000); // definitely cause wheel spin
		VehicleSpeedMPH = 0.f;
		SimulateAccelerating(Wheel, Gravity, VehicleSpeedMPH, DrivingDistanceWheelspin, DeltaTime);
		EXPECT_LT(DrivingDistanceWheelspin, DrivingDistanceA);
	}

	TYPED_TEST(AllTraits, DISABLED_VehicleTest_WheelLateralSlip)
	{
		FSimpleWheelConfig Setup;
		FSimpleWheelSim Wheel(&Setup);
	}

	TYPED_TEST(AllTraits, VehicleTest_WheelRolling)
	{
		FSimpleWheelConfig Setup;
		FSimpleWheelSim Wheel(&Setup);

		float DeltaTime = 1.f / 30.f;
		float MaxSimTime = 10.0f;
		float Tolerance = 0.1f; // wheel friction losses slow wheel speed

		//------------------------------------------------------------------
		// Car is moving FORWARDS - with AMPLE friction we would expect an initially 
		// static rolling wheel to speed up and match the vehicle speed
		FVector VehicleGroundSpeed(10.0f, 0.f, 0.f); // X is forwards
		Wheel.SetVehicleGroundSpeed(VehicleGroundSpeed);
		Wheel.SetSurfaceFriction(1.0f);	// Some wheel/ground friction
		Wheel.SetWheelLoadForce(250.f); // wheel pressed into the ground, to give it grip
		Wheel.Omega = 0.f;

		// initially wheel is static
		EXPECT_LT(Wheel.GetAngularVelocity(), SMALL_NUMBER);

		// after some time, the wheel picks up speed to match the vehicle speed
		float SimulatedTime = 0.f;
		while (SimulatedTime < MaxSimTime)
		{
			Wheel.Simulate(DeltaTime);
			SimulatedTime += DeltaTime;
		}

		// there's enough grip to cause the wheel to spin and match the vehivle speed
		float WheelGroundSpeed = Wheel.GetAngularVelocity() * Wheel.GetEffectiveRadius();
		EXPECT_LT(VehicleGroundSpeed.X - WheelGroundSpeed, Tolerance);
		EXPECT_LT(VehicleGroundSpeed.X - WheelGroundSpeed, Tolerance);
		EXPECT_GT(Wheel.GetAngularVelocity(), 0.f); // +ve spin on it

		//------------------------------------------------------------------
		// Car is moving BACKWARDS - with AMPLE friction we would expect an initially 
		// static rolling wheel to speed up and match the vehicle speed
		VehicleGroundSpeed.Set(-10.0f, 0.f, 0.f); // X is -ve not travelling backwards
		Wheel.SetVehicleGroundSpeed(VehicleGroundSpeed);
		Wheel.SetSurfaceFriction(1.0f);	// Some wheel/ground friction
		Wheel.SetWheelLoadForce(250.f); // wheel pressed into the ground, to give it grip
		Wheel.Omega = 0.f;

		// initially wheel is static
		EXPECT_LT(Wheel.GetAngularVelocity(), SMALL_NUMBER);

		// after some time, the wheel picks up speed to match the vehicle speed
		SimulatedTime = 0.f;
		while (SimulatedTime < MaxSimTime)
		{
			Wheel.Simulate(DeltaTime);
			SimulatedTime += DeltaTime;
		}

		// there's enough grip to cause the wheel to spin and match the vehicle speed
		WheelGroundSpeed = Wheel.GetAngularVelocity() * Wheel.GetEffectiveRadius();
		EXPECT_LT(VehicleGroundSpeed.X - WheelGroundSpeed, Tolerance);
		EXPECT_LT(VehicleGroundSpeed.X - Wheel.GetWheelGroundSpeed(), Tolerance);
		EXPECT_LT(Wheel.GetAngularVelocity(), 0.f); // -ve spin on it

		//------------------------------------------------------------------
		// Car is moving FROWARDS - with NO friction we would expect an initially 
		// static wheel to NOT speed up to match the vehicle speed
		Wheel.SetVehicleGroundSpeed(VehicleGroundSpeed);
		Wheel.SetSurfaceFriction(0.0f);	// No wheel/ground friction
		Wheel.SetWheelLoadForce(250.f); // wheel pressed into the ground, to give it grip
		Wheel.Omega = 0.f;

		// initially wheel is static
		EXPECT_LT(Wheel.GetAngularVelocity(), SMALL_NUMBER);

		// after some time, the wheel picks up speed to match the vehicle speed
		SimulatedTime = 0.f;
		while (SimulatedTime < MaxSimTime)
		{
			Wheel.Simulate(DeltaTime);
			SimulatedTime += DeltaTime;
		}

		WheelGroundSpeed = Wheel.GetAngularVelocity() * Wheel.GetEffectiveRadius();

		// wheel is just sliding there's no friction to make it spin
		EXPECT_LT(WheelGroundSpeed, SMALL_NUMBER);

	}

	// Suspension

	float SumSprungMasses(TArray<float>& SprungMasses)
	{
		float Sum = 0.f;
		for (int I = 0; I < SprungMasses.Num(); I++)
		{
			Sum += SprungMasses[I];
		}
		return Sum;
	}

	TYPED_TEST(AllTraits, VehicleTest_SuspensionSprungMassesTwoWheels)
	{
		float TotalMass = 1000.f;
		float Tolerance = 0.01f;

		{
			// simple 1 Wheels unstable as offset from COM
			TArray<FVector> MassSpringPositions;
			TArray<float> OutSprungMasses;

			MassSpringPositions.Add(FVector(200, 0, 0));

			FSuspensionUtility::ComputeSprungMasses(MassSpringPositions, TotalMass, OutSprungMasses);

			EXPECT_EQ(MassSpringPositions.Num(), OutSprungMasses.Num());
			EXPECT_LT(FMath::Abs(OutSprungMasses[0] - 1000.f), Tolerance);
			EXPECT_LT(FMath::Abs(SumSprungMasses(OutSprungMasses) - TotalMass), Tolerance);
		}

		{
			// simple 2 Wheels equally spaced around COM
			TArray<FVector> MassSpringPositions;
			TArray<float> OutSprungMasses;

			MassSpringPositions.Add(FVector(200, 0, 0));
			MassSpringPositions.Add(FVector(-200, 0, 0));

			FSuspensionUtility::ComputeSprungMasses(MassSpringPositions, TotalMass, OutSprungMasses);

			EXPECT_EQ(MassSpringPositions.Num(), OutSprungMasses.Num());
			EXPECT_LT(FMath::Abs(OutSprungMasses[0] - 500.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[1] - 500.f), Tolerance);
			EXPECT_LT(FMath::Abs(SumSprungMasses(OutSprungMasses) - TotalMass), Tolerance);
		}

		{
			// simple 2 Wheels equally spaced around COM
			TArray<FVector> MassSpringPositions;
			TArray<float> OutSprungMasses;

			MassSpringPositions.Add(FVector(200, 0, 50));
			MassSpringPositions.Add(FVector(-200, 0, -50));

			FSuspensionUtility::ComputeSprungMasses(MassSpringPositions, TotalMass, OutSprungMasses);

			EXPECT_EQ(MassSpringPositions.Num(), OutSprungMasses.Num());
			EXPECT_LT(FMath::Abs(OutSprungMasses[0] - 500.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[1] - 500.f), Tolerance);
			EXPECT_LT(FMath::Abs(SumSprungMasses(OutSprungMasses) - TotalMass), Tolerance);
		}

		{
			// simple 2 Wheels equally spaced around COM
			TArray<FVector> MassSpringPositions;
			TArray<float> OutSprungMasses;

			MassSpringPositions.Add(FVector(200, 0, 0));
			MassSpringPositions.Add(FVector(0, 0, 0));

			FSuspensionUtility::ComputeSprungMasses(MassSpringPositions, TotalMass, OutSprungMasses);

			EXPECT_EQ(MassSpringPositions.Num(), OutSprungMasses.Num());
			EXPECT_LT(FMath::Abs(OutSprungMasses[0] - 1000.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[1] - 0.f), Tolerance);
			EXPECT_LT(FMath::Abs(SumSprungMasses(OutSprungMasses) - TotalMass), Tolerance);
		}

		{
			// simple 2 Wheels equally spaced around COM
			TArray<FVector> MassSpringPositions;
			TArray<float> OutSprungMasses;

			MassSpringPositions.Add(FVector(200, 0, 0));
			MassSpringPositions.Add(FVector(-100, 0, 0));

			FSuspensionUtility::ComputeSprungMasses(MassSpringPositions, TotalMass, OutSprungMasses);

			EXPECT_EQ(MassSpringPositions.Num(), OutSprungMasses.Num());
			EXPECT_LT(FMath::Abs(OutSprungMasses[0] - TotalMass * 2.f / 3.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[1] - TotalMass * 1.f / 3.f), Tolerance);
			EXPECT_LT(FMath::Abs(SumSprungMasses(OutSprungMasses) - TotalMass), Tolerance);
		}
	}

	TYPED_TEST(AllTraits, VehicleTest_SuspensionSprungMassesThreeWheels)
	{
		float TotalMass = 1000.f;
		float Tolerance = 0.01f;

		{
			// simple 3 Wheels equally spaced around COM
			TArray<FVector> MassSpringPositions;
			TArray<float> OutSprungMasses;

			MassSpringPositions.Add(FVector(200, 0, 0));
			MassSpringPositions.Add(FVector(-200, -100, 0));
			MassSpringPositions.Add(FVector(-200, 100, 0));

			FSuspensionUtility::ComputeSprungMasses(MassSpringPositions, TotalMass, OutSprungMasses);

			EXPECT_EQ(MassSpringPositions.Num(), OutSprungMasses.Num());
			EXPECT_LT(FMath::Abs(OutSprungMasses[0] - 500), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[1] - 250), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[2] - 250), Tolerance);
			EXPECT_LT(FMath::Abs(SumSprungMasses(OutSprungMasses) - TotalMass), Tolerance);
		}

	}

	TYPED_TEST(AllTraits, VehicleTest_SuspensionSprungMassesFourWheels)
	{
		float TotalMass = 1000.f;
		float Tolerance = 0.1f;

		{
			// simple 4 Wheels equally spaced around COM
			TArray<FVector> MassSpringPositions;
			TArray<float> OutSprungMasses;

			MassSpringPositions.Add(FVector(200, 0, 0));
			MassSpringPositions.Add(FVector(-200, 0, 0));
			MassSpringPositions.Add(FVector(200, -100, 0));
			MassSpringPositions.Add(FVector(-200, 100, 0));

			FSuspensionUtility::ComputeSprungMasses(MassSpringPositions, TotalMass, OutSprungMasses);

			EXPECT_EQ(MassSpringPositions.Num(), OutSprungMasses.Num());

			EXPECT_LT(FMath::Abs(OutSprungMasses[0] - 250.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[1] - 250.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[2] - 250.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[3] - 250.f), Tolerance);
			EXPECT_LT(FMath::Abs(SumSprungMasses(OutSprungMasses) - TotalMass), Tolerance);
		}

		{
			// simple 4 Wheels all weight on rear COM
			TArray<FVector> MassSpringPositions;
			TArray<float> OutSprungMasses;

			MassSpringPositions.Add(FVector(0, 0, 0));
			MassSpringPositions.Add(FVector(-200, 0, 0));
			MassSpringPositions.Add(FVector(0, -100, 0));
			MassSpringPositions.Add(FVector(-200, 100, 0));

			FSuspensionUtility::ComputeSprungMasses(MassSpringPositions, TotalMass, OutSprungMasses);

			EXPECT_EQ(MassSpringPositions.Num(), OutSprungMasses.Num());

			EXPECT_LT(FMath::Abs(OutSprungMasses[0] - 500.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[1] - 0.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[2] - 250.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[3] - 250.f), Tolerance);
			EXPECT_LT(FMath::Abs(SumSprungMasses(OutSprungMasses) - TotalMass), Tolerance);
		}

		{
			// 4 Wheels all weight on rear COM
			TArray<FVector> MassSpringPositions;
			TArray<float> OutSprungMasses;

			MassSpringPositions.Add(FVector(100, 0, 0));
			MassSpringPositions.Add(FVector(-200, 0, 0));
			MassSpringPositions.Add(FVector(100, -100, 0));
			MassSpringPositions.Add(FVector(-200, 100, 0));

			FSuspensionUtility::ComputeSprungMasses(MassSpringPositions, TotalMass, OutSprungMasses);

			EXPECT_EQ(MassSpringPositions.Num(), OutSprungMasses.Num());

			//for (int i = 0; i < 4; i++)
			//{
			//	UE_LOG(LogChaos, Warning, TEXT("SM = %f"), OutSprungMasses[i]);
			//}
			EXPECT_LT(FMath::Abs(OutSprungMasses[0] - TotalMass * 1.f / 3.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[1] - TotalMass * 1.f / 6.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[2] - TotalMass * 1.f / 4.f), Tolerance);
			EXPECT_LT(FMath::Abs(OutSprungMasses[3] - TotalMass * 1.f / 4.f), Tolerance);
			EXPECT_LT(FMath::Abs(SumSprungMasses(OutSprungMasses) - TotalMass), Tolerance);
		}
	}

	float PlaneZPos = 1.0f;
	bool RayCastPlane(FVec3& RayStart, FVec3& Direction, float Length, float& OutTime, FVec3& OutPosition, FVec3& OutNormal)
	{
		TPlane<float, 3> Plane(TVector<float, 3>(1), TVector<float, 3>(0, 0, PlaneZPos));
		int32 FaceIndex;

		return Plane.Raycast(RayStart, Direction, Length, 0, OutTime, OutPosition, OutNormal, FaceIndex);
	}

	void AddForceAtPosition(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FVector& InForce, const FVector& InPosition)
	{
		const Chaos::FVec3& CurrentForce = Rigid->F();
		const Chaos::FVec3& CurrentTorque = Rigid->Torque();
		const Chaos::FVec3 WorldCOM = FParticleUtilitiesGT::GetCoMWorldPosition(Rigid);

		Rigid->SetObjectState(EObjectStateType::Dynamic);

		const Chaos::FVec3 WorldTorque = Chaos::FVec3::CrossProduct(InPosition - WorldCOM, InForce);
		Rigid->SetF(CurrentForce + InForce);
		Rigid->SetTorque(CurrentTorque + WorldTorque);

	}

	FVector WorldVelocityAtPoint(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FVector& InPoint)
	{
		const Chaos::FVec3 COM = Rigid ? Chaos::FParticleUtilitiesGT::GetCoMWorldPosition(Rigid) : Chaos::FParticleUtilitiesGT::GetActorWorldTransform(Rigid).GetTranslation();
		const Chaos::FVec3 Diff = InPoint - COM;
		return Rigid->V() - Chaos::FVec3::CrossProduct(Diff, Rigid->W());

	}

	// #todo: break out vehicle simulation setup so it can be used across number of tests
	TYPED_TEST(AllEvolutions, VehicleTest_SuspensionSpringLoad)
	{
		using TEvolution = TypeParam;

		TPBDRigidsSOAs<FReal, 3> Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		TEvolution Evolution(Particles, PhysicalMaterials);

		float BodyMass = 1000.0f;
		float Gravity = FMath::Abs(Evolution.GetGravityForces().GetAcceleration().Z);

		FSimpleSuspensionConfig Setup;
		Setup.MaxLength = 20.0f;
		Setup.SpringRate = (2.0f * BodyMass * Gravity / 4.0f) / Setup.MaxLength;
		Setup.SpringPreload = 0.f;
		Setup.RaycastSafetyMargin = 0.f;
		Setup.SuspensionSmoothing = 0;
		Setup.ReboundDamping = 0.f;		// calculated later
		Setup.CompressionDamping = 0.f; // calculated later

		TArray<FSimpleSuspensionSim> Suspensions;
		for (int SpringIdx = 0; SpringIdx < 4; SpringIdx++)
		{
			FSimpleSuspensionSim Suspension(&Setup);
			Suspensions.Add(Suspension);
		}

		float HalfLength = 100.f;
		float HalfWidth = 50.f;
		TArray<FVector> LocalSpringPositions;
		LocalSpringPositions.Add(FVector( HalfLength, -HalfWidth, 0.f));
		LocalSpringPositions.Add(FVector( HalfLength,  HalfWidth, 0.f));
		LocalSpringPositions.Add(FVector(-HalfLength, -HalfWidth, 0.f));
		LocalSpringPositions.Add(FVector(-HalfLength,  HalfWidth, 0.f));

		for (int SpringIdx = 0; SpringIdx < 4; SpringIdx++)
		{
			Suspensions[SpringIdx].SetLocalRestingPosition(LocalSpringPositions[SpringIdx]);
		}

		//////////////////////////////////////////////////////////////////////////
		TArray<float> OutSprungMasses;
		FSuspensionUtility::ComputeSprungMasses(LocalSpringPositions, BodyMass, OutSprungMasses);

		TArray<FSuspensionTrace> Traces;
		Traces.SetNum(4);

		for (int SpringIdx = 0; SpringIdx < 4; SpringIdx++)
		{
			float NaturalFrequency = FSuspensionUtility::ComputeNaturalFrequency(Setup.SpringRate, OutSprungMasses[SpringIdx]);
			float Damping = FSuspensionUtility::ComputeCriticalDamping(Setup.SpringRate, OutSprungMasses[SpringIdx]);
			Setup.ReboundDamping = Damping;
			Setup.CompressionDamping = Damping;
		}

		float WheelRadius = 2.0f;
		//const float SuspendedDisplacement = SprungMass * Gravity / Setup.SpringRate;
		//UE_LOG(LogChaos, Warning, TEXT("SuspendedDisplacement %.1f cm"), SuspendedDisplacement);

		//////////////////////////////////////////////////////////////////////////

		auto Dynamic = Evolution.CreateDynamicParticles(1)[0];

		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->SleepCounterThreshold = 2;

		TUniquePtr<FImplicitObject> Box(new TSphere<FReal, 3>(FVec3(0, 0, 0), 50));
		Dynamic->SetGeometry(MakeSerializable(Box));

		Evolution.SetPhysicsMaterial(Dynamic, MakeSerializable(PhysicsMaterial));

		Dynamic->X() = FVec3(10, 10, 20);
		Dynamic->M() = BodyMass;
		Dynamic->InvM() = 1.0f / BodyMass;
		Dynamic->I() = FMatrix33(100000.0f, 100000.0f, 100000.0f);
		Dynamic->InvI() = FMatrix33(1.0f / 100000.0f, 1.0f / 100000.0f, 1.0f / 100000.0f);

		float AccumulatedTime = 0.f;
		const FReal Dt = 1 / 30.f;
		for (int i = 0; i < 500; ++i)
		{
			// latest body transform
			const FTransform BodyTM(Dynamic->R(), Dynamic->X());

			for (int SpringIdx = 0; SpringIdx < 4; SpringIdx++)
			{
				FSimpleSuspensionSim& Suspension = Suspensions[SpringIdx];
				FSuspensionTrace& Trace = Traces[SpringIdx];
				Suspension.UpdateWorldRaycastLocation(BodyTM, WheelRadius, Trace);

				// raycast
				FVec3 Start = Trace.Start;
				FVec3 Dir = Trace.TraceDir();
				float CurrentLength = Suspension.Setup().MaxLength;

				FVec3 Position(0,0,0);
				FVec3 Normal(0,0,0);
				bool bHit = RayCastPlane(Start, Dir, Trace.Length(), CurrentLength, Position, Normal);

				Suspension.SetSuspensionLength(CurrentLength, WheelRadius);
				Suspension.SetLocalVelocityFromWorld(BodyTM, WorldVelocityAtPoint(Dynamic, Trace.Start));
				Suspension.Simulate(Dt); // ComputeSuspensionForces

				if (bHit)
				{
					FVector SusForce = Suspension.GetSuspensionForceVector(BodyTM);
					AddForceAtPosition(Dynamic, SusForce, Start);
				}
			}

			Evolution.AdvanceOneTimeStep(Dt);
			Evolution.EndFrame(Dt);
			AccumulatedTime += Dt;
		}

		float Tolerance = 0.5f; // half cm
		float ExpectedRestingPosition = (10.f + PlaneZPos + WheelRadius);
		EXPECT_LT(Dynamic->X().Z - ExpectedRestingPosition, Tolerance);
	}

	//TYPED_TEST(AllTraits, VehicleTest_SuspensionNaturalFrequency)
	//{
	//	float SprungMass = 250.f;
	//	float SpringStiffness = 250.0f;
	//	float SpringDamping = 0.5f;



	//}

}

PRAGMA_ENABLE_OPTIMIZATION