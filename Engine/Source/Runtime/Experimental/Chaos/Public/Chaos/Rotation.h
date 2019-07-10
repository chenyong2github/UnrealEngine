// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Vector.h"

#include <cmath>
#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Math/Quat.h"
#else
#include <array>

struct FQuat
{
public:
	const float operator[](const int32 i) const
	{
		return angles[i];
	}
	float& operator[](const int32 i)
	{
		return angles[i];
	}
	std::array<float, 3> angles;
	static MakeFromEuler(const Vector<float, 3>& InAngles)
	{
		FQuat Quat;
		Quat.angles = InAngles;
		return Quat;
	}
};
#endif

namespace Chaos
{
	template<class T, int d>
	class TRotation
	{
	private:
		TRotation() {}
		~TRotation() {}
	};

	template<>
	class TRotation<float, 3> : public FQuat
	{
	public:
		TRotation()
		    : FQuat() {}
		TRotation(const FVector& Vec, const float Scalar)
		    : FQuat(Vec[0], Vec[1], Vec[2], Scalar) {}
		TRotation(const FQuat& Quat)
		    : FQuat(Quat) {}
		TRotation(const FMatrix& Matrix)
		    : FQuat(Matrix) {}

		static TRotation<float, 3> Conjugate(const ::Chaos::TRotation<float, 3>& InR)
		{
			TRotation<float, 3> R;
			R.X = -InR.X;
			R.Y = -InR.Y;
			R.Z = -InR.Z;
			R.W = InR.W;
			return R;
		}

		static TRotation<float, 3> FromVector(const ::Chaos::TVector<float, 3>& V)
		{
			TRotation<float, 3> Rot;
			float HalfSize = 0.5f * V.Size();
			float sinc = (FMath::Abs(HalfSize) > 1e-8) ? FMath::Sin(HalfSize) / HalfSize : 1;
			auto RotV = 0.5f * sinc * V;
			Rot.X = RotV.X;
			Rot.Y = RotV.Y;
			Rot.Z = RotV.Z;
			Rot.W = FMath::Cos(HalfSize);
			return Rot;
		}

		static TRotation<float, 3> FromRotatedVector(
		    const ::Chaos::TVector<float, 3>& InitialVector,
		    const ::Chaos::TVector<float, 3>& FinalVector)
		{
			typedef Chaos::TVector<float, 3> TV;
			typedef double T;
			checkSlow(FMath::Abs(InitialVector.Size() - 1.0) < KINDA_SMALL_NUMBER);
			checkSlow(FMath::Abs(FinalVector.Size() - 1.0) < KINDA_SMALL_NUMBER);

			const double CosTheta = FMath::Clamp(TV::DotProduct(InitialVector, FinalVector), -1.f, 1.f);

			TV V = TV::CrossProduct(InitialVector, FinalVector);
			const float VMagnitude = V.Size();
			if(VMagnitude == 0)
			{
				return TRotation<float, 3>(InitialVector, 0.f);
			}

			const T SSquared = .5 * (1.0 + CosTheta); // Uses the half angle formula
			const T VMagnitudeDesired = sqrt(1.0 - SSquared);
			V *= (VMagnitudeDesired / VMagnitude);

			return TRotation<float, 3>(V, sqrt(SSquared));
		}

		/**
		 * Calculate the angular velocity required to take an object with orientation R0 to orientation R1 in time Dt.
		 *
		 * Uses the relation: DQ/DT = (W * Q)/2
		 */
		static TVector<float, 3> CalculateAngularVelocity1(const FQuat& InR0, const FQuat& InR1, const float InDt)
		{
			check(InDt > SMALL_NUMBER);

			// Ensure both quaternions are in the same hemisphere to ensure shortest path rotation
			const float Dot = (InR0 | InR1);
			const FQuat& R0 = InR0;
			const FQuat R1 = InR1 * FMath::FloatSelect(Dot, 1.0f, -1.0f);

			// W = 2 * dQ/dT * Qinv
			const FQuat DRDt = (R1 - R0) / InDt;
			const FQuat RInv = Conjugate(R0);
			const FQuat W = (DRDt * RInv) * 2.0f;

			return TVector<float, 3>(W.X, W.Y, W.Z);
		}

		/**
		 * Calculate the angular velocity required to take an object with orientation R0 to orientation R1 in time Dt.
		 *
		 * Uses the Quaternion to Axis/Angle method.
		 */
		static TVector<float, 3> CalculateAngularVelocity2(const FQuat& InR0, const FQuat& InR1, const float InDt)
		{
			const FQuat DR = InR1 * Conjugate(InR0);
			FVector Axis;
			float Angle;
			DR.ToAxisAndAngle(Axis, Angle);
			return Axis * (Angle / InDt);
		}

		static TVector<float, 3> CalculateAngularVelocity(const FQuat& InR0, const FQuat& InR1, const float InDt)
		{
			return CalculateAngularVelocity1(InR0, InR1, InDt);
		}


		/**
		 * Return a new rotation equal to the input rotation with angular velocity W applied over time Dt.
		 *
		 * Uses the relation: DQ/DT = (W * Q)/2
		 */
		static TRotation<float, 3> IntegrateRotationWithAngularVelocity(const FQuat& InR0, const TVector<float, 3>& InW, const float InDt)
		{
			TRotation<float, 3> R1 = InR0 + (TRotation<float, 3>(InW, 0.f) * InR0) * (InDt * 0.5f);
			return R1.GetNormalized();
		}

	};
}
