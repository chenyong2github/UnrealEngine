// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VehicleUtility.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

/**
 * #todo: 
 * -proper suspension setup for resting position - decide on parameters i.e. use SuspensionMaxRaise/SuspensionMaxDrop??
 * -natural frequency stuff
 * -defaults
 */

namespace Chaos
{

	#define NUM_SUS_AVERAGING 10

	struct FSimpleSuspensionConfig
	{
		FSimpleSuspensionConfig()
			: SuspensionForceOffset(FVector::ZeroVector)
			, SuspensionMaxRaise(0.f)
			, SuspensionMaxDrop(0.f)
			, MaxLength(0.f)
			, SpringRate(1.f)
			, SpringPreload(0.5f)
			, CompressionDamping(0.9f)
			, ReboundDamping(0.9f)
			, Swaybar(0.5f)
			, DampingRatio(0.3f)
			, RaycastSafetyMargin(10.0f)
			, SuspensionSmoothing(6)
		{
			MaxLength = FMath::Abs(SuspensionMaxRaise) + FMath::Abs(SuspensionMaxDrop);
			SuspensionSmoothing = FMath::Clamp(SuspensionSmoothing, 0, NUM_SUS_AVERAGING);
		}


		FVector SuspensionForceOffset; // #todo: as yet unused
		float SuspensionMaxRaise;	// distance [cm]
		float SuspensionMaxDrop;	// distance [cm]
		float MaxLength;			// distance [cm]

		float SpringRate;			// spring constant
		float SpringPreload;		// Amount of Spring force (independent spring movement)
		float CompressionDamping;	// limit compression speed
		float ReboundDamping;		// limit rebound speed

		float Swaybar;				// Anti-roll bar

		float DampingRatio;			// value between (0-no damping) and (1-critical damping)
		float RaycastSafetyMargin;	// raise start of raycast [cm]

		int SuspensionSmoothing;	// [0-off , 10-max] smoothing visual appearance of wheel movement
	};

	/** Suspension world ray/shape trace start and end positions */
	struct FSuspensionTrace
	{
		FVector Start;
		FVector End;

		FVector TraceDir()
		{
			FVector Dir(End - Start);
			return Dir.FVector::GetSafeNormal();
		}

		float Length()
		{
			FVector Dir(End - Start);
			return Dir.Size();
		}
	};

	class FSimpleSuspensionSim : public TVehicleSystem<FSimpleSuspensionConfig>
	{
	public:
		FSimpleSuspensionSim(const FSimpleSuspensionConfig* SetupIn)
			: TVehicleSystem<FSimpleSuspensionConfig>(SetupIn)
			, DisplacementInput(0.f)
			, LastDisplacement(0.f)
			, LocalVelocity(FVector::ZeroVector)
			, SuspensionForce(0.f)
			, LocalOffset(FVector::ZeroVector)
			, WheelRadius(0.3f)
			, SpringDisplacement(0.f)
			, SpringIndex(0)
			, LastSpringLength(0.f)
			, AveragingLength()
			, AveragingCount(0)
			, AveragingNum(0)
		{
		}

// Inputs

		/** Set suspension length after determined from raycast */
		void SetSuspensionLength(float InLength)
		{
			DisplacementInput = InLength - Setup().RaycastSafetyMargin - WheelRadius;
			//if (SpringIndex == 2)
			//{
			//	UE_LOG(LogChaos, Warning, TEXT("DisplacementInput %f = InLength %f - Offset %f"), DisplacementInput, InLength, Setup().RaycastSafetyMargin + WheelRadius);
			//}
		}

		/** set local velocity at suspension position */
		void SetLocalVelocity(const FVector& InVelocity)
		{
			LocalVelocity = InVelocity;
		}

		void SetLocalVelocityFromWorld(const FTransform& InWorldTransform, const FVector& InWorldVelocity)
		{
			LocalVelocity = InWorldTransform.InverseTransformVector(InWorldVelocity);
		}

		void SetLocalRestingPosition(const FVector& InOffset)
		{
			LocalOffset = InOffset;
		}

		void SetSpringIndex(uint32 InIndex)
		{
			SpringIndex = InIndex;
		}

		void UpdateWorldRaycastLocation(const FTransform& InTransform, float InWheelRadius)
		{
			FVector LocalDirection(0.f, 0.f, -1.f);
			FVector WorldLocation = InTransform.TransformPosition(LocalOffset);
			FVector WorldDirection = InTransform.TransformVector(LocalDirection);

			Trace.Start = WorldLocation - WorldDirection * Setup().RaycastSafetyMargin;
			Trace.End = WorldLocation + WorldDirection * (Setup().MaxLength + InWheelRadius);
			WheelRadius = InWheelRadius;
		}

// Outputs

		float GetSpringLength()
		{
			if (Setup().SuspensionSmoothing)
			{
				// Trying smoothing the suspension movement out - looks Sooo much better when wheel traveling over pile of bricks
				// The digital up and down of the wheels is slowed/smoothed out
				float NewValue = SpringDisplacement - Setup().MaxLength;

				if (AveragingNum < Setup().SuspensionSmoothing)
				{
					AveragingNum++;
				}

				AveragingLength[AveragingCount++] = NewValue;

				if (AveragingCount >= Setup().SuspensionSmoothing)
				{
					AveragingCount = 0;
				}

				float Total = 0.0f;
				for (int i = 0; i < AveragingNum; i++)
				{
					Total += AveragingLength[i];
				}
				float Average = Total / AveragingNum;

				return Average;
			}
			else
			{
				return  (SpringDisplacement - Setup().MaxLength);
			}
		}

		float GetSuspensionForce() const
		{
			return SuspensionForce;
		}

		FVector GetSuspensionForceVector(const FTransform& InTransform)
		{
			FVector LocalDirection(0.f, 0.f, 1.f);
			return InTransform.TransformVector(LocalDirection) * SuspensionForce;
		}

		const FSuspensionTrace& GetTrace() const
		{
			return Trace;
		}

		FSuspensionTrace& GetTrace() 		
		{
			return Trace;
		}

		const FVector& GetLocalRestingPosition() const
		{
			return LocalOffset;
		}

// Simulation

		void Simulate(float DeltaTime)
		{
			float Damping = (DisplacementInput < LastDisplacement) ? Setup().CompressionDamping : Setup().ReboundDamping;

			SpringDisplacement = Setup().MaxLength - DisplacementInput;
			const float StiffnessForce = SpringDisplacement * Setup().SpringRate;
			const float DampingForce = LocalVelocity.Z * Damping;
			SuspensionForce = StiffnessForce - DampingForce;
			LastDisplacement = DisplacementInput;
			//if (SpringIndex == 2)
			//{
			//	UE_LOG(LogChaos, Warning, TEXT("MaxLength %f   DisplacementInput %f => SpringDisplacement %f")
			//		, Setup().MaxLength, DisplacementInput, SpringDisplacement);
			//}
		}

	protected:

		float DisplacementInput;	
		float LastDisplacement;	
		FVector LocalVelocity;
		float SuspensionForce;

		FVector LocalOffset;
		FSuspensionTrace Trace;
		float WheelRadius;
		float SpringDisplacement;
		uint32 SpringIndex;

		float LastSpringLength; // blend rather than jump to new location
		float AveragingLength[NUM_SUS_AVERAGING];
		int AveragingCount;
		int AveragingNum;
	};

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
