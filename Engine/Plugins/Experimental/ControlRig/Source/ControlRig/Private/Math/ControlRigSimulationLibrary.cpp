// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Math/ControlRigSimulationLibrary.h"

void FControlRigSimulationContainer::Reset()
{
	AccumulatedTime = TimeLeftForStep = 0.f;
}

void FControlRigSimulationContainer::ResetTime()
{
	AccumulatedTime = TimeLeftForStep = 0.f;
}

void FControlRigSimulationContainer::StepVerlet(float InDeltaTime, float InBlend)
{
	TimeLeftForStep -= InDeltaTime;

	if (TimeLeftForStep >= 0.f)
	{
		return;
	}

	while (TimeLeftForStep < 0.f)
	{
		TimeLeftForStep += TimeStep;
		AccumulatedTime += TimeStep;
		CachePreviousStep();
		IntegrateVerlet(InBlend);
	}
}

void FControlRigSimulationContainer::StepSemiExplicitEuler(float InDeltaTime)
{
	TimeLeftForStep -= InDeltaTime;

	if (TimeLeftForStep >= 0.f)
	{
		return;
	}

	while (TimeLeftForStep < 0.f)
	{
		TimeLeftForStep += TimeStep;
		AccumulatedTime += TimeStep;
		CachePreviousStep();
		IntegrateSemiExplicitEuler();
	}
}

void FControlRigSimulationPointContainer::Reset()
{
	FControlRigSimulationContainer::Reset();
	Points.Reset();
	Springs.Reset();
	Constraints.Reset();
	PreviousStep.Reset();
}

void FControlRigSimulationPointContainer::ResetTime()
{
	FControlRigSimulationContainer::ResetTime();
	PreviousStep.Reset();
	for (FControlRigSimulationPoint& Point : Points)
	{
		Point.LinearVelocity = FVector::ZeroVector;
	}
}

FControlRigSimulationPoint FControlRigSimulationPointContainer::GetPointInterpolated(int32 InIndex) const
{
	if (TimeLeftForStep <= SMALL_NUMBER || PreviousStep.Num() != Points.Num())
	{
		return Points[InIndex];
	}

	float T = 1.f - TimeLeftForStep / TimeStep;
	const FControlRigSimulationPoint& PrevPoint = PreviousStep[InIndex];
	FControlRigSimulationPoint Point = Points[InIndex];
	Point.Position = FMath::Lerp<FVector>(PrevPoint.Position, Point.Position, T);
	Point.LinearVelocity = FMath::Lerp<FVector>(PrevPoint.LinearVelocity, Point.LinearVelocity, T);
	return Point;
}

void FControlRigSimulationPointContainer::CachePreviousStep()
{
	PreviousStep = Points;
	for (FControlRigSimulationPoint& Point : Points)
	{
		Point.LinearVelocity = FVector::ZeroVector;
	}
}

void FControlRigSimulationPointContainer::IntegrateVerlet(float InBlend)
{
	IntegrateSprings();
	IntegrateVelocityVerlet(InBlend);
	ApplyConstraints();
}

void FControlRigSimulationPointContainer::IntegrateSemiExplicitEuler()
{
	IntegrateSprings();
	IntegrateVelocitySemiExplicitEuler();
	ApplyConstraints();
}

void FControlRigSimulationPointContainer::IntegrateSprings()
{
	for (int32 SpringIndex = 0; SpringIndex < Springs.Num(); SpringIndex++)
	{
		const FControlRigSimulationLinearSpring& Spring = Springs[SpringIndex];
		if (Spring.SubjectA == INDEX_NONE || Spring.SubjectB == INDEX_NONE)
		{
			continue;
		}
		const FControlRigSimulationPoint& PrevPointA = PreviousStep[Spring.SubjectA];
		const FControlRigSimulationPoint& PrevPointB = PreviousStep[Spring.SubjectB];

		FVector ForceA = FVector::ZeroVector;
		FVector ForceB = FVector::ZeroVector;
		FControlRigSimulationLibrary::ComputeLinearSpring(PrevPointA, PrevPointB, Spring, ForceA, ForceB);

		FControlRigSimulationPoint& PointA = Points[Spring.SubjectA];
		FControlRigSimulationPoint& PointB = Points[Spring.SubjectB];
		PointA.LinearVelocity += ForceA;
		PointB.LinearVelocity += ForceB;
	}
}

void FControlRigSimulationPointContainer::IntegrateVelocityVerlet(float InBlend)
{
	for (int32 PointIndex = 0; PointIndex < Points.Num(); PointIndex++)
	{
		Points[PointIndex] = FControlRigSimulationLibrary::IntegrateVerlet(PreviousStep[PointIndex], Points[PointIndex].LinearVelocity, InBlend, TimeStep);
	}
}

void FControlRigSimulationPointContainer::IntegrateVelocitySemiExplicitEuler()
{
	for (int32 PointIndex = 0; PointIndex < Points.Num(); PointIndex++)
	{
		Points[PointIndex] = FControlRigSimulationLibrary::IntegrateSemiExplicitEuler(PreviousStep[PointIndex], Points[PointIndex].LinearVelocity, TimeStep);
	}
}

void FControlRigSimulationPointContainer::ApplyConstraints()
{
	for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ConstraintIndex++)
	{
		const FControlRigSimulationPointConstraint& Constraint = Constraints[ConstraintIndex];
		if (Constraint.SubjectA == INDEX_NONE)
		{
			continue;
		}

		FControlRigSimulationPoint& PointA = Points[Constraint.SubjectA];
		FControlRigSimulationPoint& PointB = Points[Constraint.SubjectB == INDEX_NONE ? Constraint.SubjectA : Constraint.SubjectB];
		FControlRigSimulationLibrary::ApplyPointConstraint(Constraint, PointA, PointB);
	}
}

FControlRigSimulationPoint FControlRigSimulationLibrary::IntegrateVerlet(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InBlend, float InDeltaTime)
{
	FControlRigSimulationPoint Point = InPoint;
	if(Point.Mass > SMALL_NUMBER)
	{
		Point.LinearVelocity = FMath::Lerp<FVector>(Point.LinearVelocity, InForce, FMath::Clamp<float>(InBlend * InDeltaTime, 0.f, 1.f)) * FMath::Clamp<float>(1.f - InPoint.LinearDamping, 0.f, 1.f);
		Point.Position = Point.Position + Point.LinearVelocity * InDeltaTime;
	}
	return Point;
}

FControlRigSimulationPoint FControlRigSimulationLibrary::IntegrateSemiExplicitEuler(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InDeltaTime)
{
	FControlRigSimulationPoint Point = InPoint;
	if(Point.Mass > SMALL_NUMBER)
	{
		Point.LinearVelocity += InForce * InDeltaTime;
		Point.LinearVelocity -= InPoint.LinearVelocity * InPoint.LinearDamping;
		Point.Position += Point.LinearVelocity * InDeltaTime;
	}
	return Point;
}

void FControlRigSimulationLibrary::ComputeWeightsFromMass(float MassA, float MassB, float& OutWeightA, float& OutWeightB)
{
	const bool bIsDynamicA = MassA > SMALL_NUMBER;
	const bool bIsDynamicB = MassB > SMALL_NUMBER;

	OutWeightA = 0.f;
	OutWeightB = 0.f;

	if (bIsDynamicA && !bIsDynamicB)
	{
		OutWeightA = 1.f;
		OutWeightB = 0.f;
		return;
	}

	if (!bIsDynamicA && bIsDynamicB)
	{
		OutWeightA = 0.f;
		OutWeightB = 1.f;
		return;
	}

	float CombinedMass = MassA + MassB;
	if (CombinedMass > SMALL_NUMBER)
	{
		OutWeightA = FMath::Clamp<float>(MassB / CombinedMass, 0.f, 1.f);
		OutWeightB = FMath::Clamp<float>(MassA / CombinedMass, 0.f, 1.f);
	}
}

void FControlRigSimulationLibrary::ComputeLinearSpring(const FControlRigSimulationPoint& InPointA, const FControlRigSimulationPoint& InPointB, const FControlRigSimulationLinearSpring& InSpring, FVector& ForceA, FVector& ForceB)
{
	ForceA = ForceB = FVector::ZeroVector;

	float WeightA = 0.f, WeightB = 0.f;
	ComputeWeightsFromMass(InPointA.Mass, InPointB.Mass, WeightA, WeightB);
	if (WeightA + WeightB <= SMALL_NUMBER)
	{
		return;
	}

	const FVector Direction = InPointA.Position - InPointB.Position;
	const float Distance = Direction.Size();
	if (Distance < SMALL_NUMBER)
	{
		return;
	}

	const FVector Displacement = Direction * (InSpring.Equilibrium - Distance) / Distance;
	ForceA = Displacement * InSpring.Coefficient * WeightA;
	ForceB = -Displacement * InSpring.Coefficient * WeightB;
}

void FControlRigSimulationLibrary::ApplyPointConstraint(const FControlRigSimulationPointConstraint& InConstraint, FControlRigSimulationPoint& OutPointA, FControlRigSimulationPoint& OutPointB)
{
	switch (InConstraint.Type)
	{
		case EControlRigSimulationConstraintType::Distance:
		case EControlRigSimulationConstraintType::DistanceFromA:
		case EControlRigSimulationConstraintType::DistanceFromB:
		{
			float WeightA = 0.f, WeightB = 0.f;
			switch (InConstraint.Type)
			{
				case EControlRigSimulationConstraintType::Distance:
				{
					ComputeWeightsFromMass(OutPointA.Mass, OutPointB.Mass, WeightA, WeightB);
					break;
				}
				case EControlRigSimulationConstraintType::DistanceFromA:
				{
					WeightA = 0.f;
					WeightB = 1.f;
					break;
				}
				default:
				case EControlRigSimulationConstraintType::DistanceFromB:
				{
					WeightA = 1.f;
					WeightB = 0.f;
					break;
				}
			}
			if (WeightA + WeightB <= SMALL_NUMBER)
			{
				return;
			}

			const float Equilibrium = InConstraint.DataA.X;
			const FVector Direction = OutPointA.Position - OutPointB.Position;
			const float Distance = Direction.Size();
			if (Distance < SMALL_NUMBER)
			{
				break;
			}
			const FVector Displacement = Direction * (Equilibrium - Distance) / Distance;
			OutPointA.Position += Displacement * WeightA;
			OutPointB.Position -= Displacement * WeightB;
			break;
		}
		case EControlRigSimulationConstraintType::Plane:
		{
			// todo: Mark const
			FVector PlanePoint = InConstraint.DataA;
			FVector PlaneNormal = InConstraint.DataB;
			FVector Direction = OutPointA.Position - PlanePoint;
			OutPointA.Position -= FVector::DotProduct(PlaneNormal, Direction) * PlaneNormal;
			break;
		}
	}
}
