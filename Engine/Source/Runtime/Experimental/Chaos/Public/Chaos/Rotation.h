// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Vector.h"
#include "Chaos/Matrix.h"

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
		TRotation(const FQuat& Quat)
		    : FQuat(Quat) {}
		TRotation(const FMatrix& Matrix)
		    : FQuat(Matrix) {}

		PMatrix<float, 3, 3> ToMatrix() const
		{
			return FRotationMatrix::Make(*this);
		}

		/**
		 * Extract the axis and angle from the Quaternion.
		 * @param OutAxis The axis of rotation.
		 * @param OutAngle The angle of rotation about the axis (radians).
		 * @param DefaultAxis The axis to set when the angle is too small to accurately calculate the axis.
		 * @param EpsilonSq The squared tolerance used to check for small angles.
		 * @return Whether the axis was successfully calculated (true except for very small angles around or less than Epsilon).
		 * @warning The axis calculation cannot succeed for small angles due to numerical error. In this case, the function will return false, but set the axis to DefaultAxis.
		 * @note EpsilonSq is approximately the square of the angle below which we cannot calculate the axis. It needs to be "much greater" than square of the error in the
		 * quaternion values which is usually ~1e-4, so values around 1e-3^2 = 1e-6 or greater are about right.
		 */
		bool ToAxisAndAngleSafe(TVector<float, 3>& OutAxis, float& OutAngle, const TVector<float, 3>& DefaultAxis, float EpsilionSq = 1e-6f) const
		{
			OutAngle = GetAngle();
			return GetRotationAxisSafe(OutAxis, DefaultAxis, EpsilionSq);
		}

		/**
		 * Extract the axis from the Quaternion.
		 * @param OutAxis The axis of rotation.
		 * @param DefaultAxis The axis to set when the angle is too small to accurately calculate the axis.
		 * @param EpsilonSq The squared tolerance used to check for small angles.
		 * @return Whether the axis was successfully calculated (true except for very small angles around or less than Epsilon).
		 * @warning The axis calculation cannot succeed for small angles due to numerical error. In this case, the function will return false, but set the axis to DefaultAxis.
		 * @note EpsilonSq is approximately the square of the angle below which we cannot calculate the axis. It needs to be "much greater" than square of the error in the
		 * quaternion values which is usually ~1e-4, so values around 1e-3^2 = 1e-6 or greater are about right.
		 */
		bool GetRotationAxisSafe(TVector<float, 3>& OutAxis, const TVector<float, 3>& DefaultAxis, float EpsilionSq = 1e-6f) const
		{
			// Tolerance must be much larger than error in normalized vector (usually ~1e-4) for the 
			// axis calculation to succeed for small angles. For small angles, W ~= 1, and
			// X, Y, Z ~= 0. If the values of X, Y, Z are around 1e-4 we are just normalizing error.
			const float LenSq = X * X + Y * Y + Z * Z;
			if (LenSq > EpsilionSq)
			{
				float InvLen = FMath::InvSqrt(LenSq);
				OutAxis = FVector(X * InvLen, Y * InvLen, Z * InvLen);
				return true;
			}

			OutAxis = DefaultAxis;
			return false;
		}

		/**
		 * Return the complex conjugate of the rotation
		 */
		static TRotation<float, 3> Conjugate(const ::Chaos::TRotation<float, 3>& InR)
		{
			TRotation<float, 3> R;
			R.X = -InR.X;
			R.Y = -InR.Y;
			R.Z = -InR.Z;
			R.W = InR.W;
			return R;
		}

		/**
		 * Negate all values of the quaternion (note: not the inverse rotation. See Conjugate)
		 */
		static TRotation<float, 3> Negate(const ::Chaos::TRotation<float, 3>& InR)
		{
			TRotation<float, 3> R;
			R.X = -InR.X;
			R.Y = -InR.Y;
			R.Z = -InR.Z;
			R.W = -InR.W;
			return R;
		}

		/**
		 * Create an identity rotation
		 */
		static TRotation<float, 3> FromIdentity()
		{
			return FQuat(0, 0, 0, 1);
		}

		/**
		 * Create a rotation by explicitly specifying all elements
		 */
		static TRotation<float, 3> FromElements(const float X, const float Y, const float Z, const float W)
		{
			return FQuat(X, Y, Z, W);
		}

		/**
		 * Create a rotation by explicitly specifying all elements
		 */
		static TRotation<float, 3> FromElements(const ::Chaos::TVector<float, 3>& V, const float W)
		{
			return FromElements(V.X, V.Y, V.Z, W);
		}

		/**
		 * Create a rotation about an axis by an angle specified in radians
		 */
		static TRotation<float, 3> FromAxisAngle(const ::Chaos::TVector<float, 3>& Axis, const float AngleRad)
		{
			return FQuat(Axis, AngleRad);
		}

		/**
		 * Create a rotation about an axis V/|V| by angle |V| in radians
		 */
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

		/**
		 * Generate a Rotation that would rotate vector InitialVector to FinalVector
		 */
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
				return TRotation<float, 3>::FromElements(InitialVector, 0.f);
			}

			const T SSquared = .5 * (1.0 + CosTheta); // Uses the half angle formula
			const T VMagnitudeDesired = sqrt(1.0 - SSquared);
			V *= (VMagnitudeDesired / VMagnitude);

			return TRotation<float, 3>::FromElements(V, sqrt(SSquared));
		}

		/**
		 * Calculate the angular velocity required to take an object with orientation R0 to orientation R1 in time Dt.
		 *
		 * Uses the relation: DQ/DT = (W * Q)/2
		 */
		static TVector<float, 3> CalculateAngularVelocity1(const TRotation<float, 3>& InR0, const TRotation<float, 3>& InR1, const float InDt)
		{
			check(InDt > SMALL_NUMBER);

			const TRotation<float, 3>& R0 = InR0;
			TRotation<float, 3> R1 = InR1;
			R1.EnforceShortestArcWith(R0);

			// W = 2 * dQ/dT * Qinv
			const TRotation<float, 3> DRDt = (R1 - R0) / InDt;
			const TRotation<float, 3> RInv = Conjugate(R0);
			const TRotation<float, 3> W = (DRDt * RInv) * 2.0f;

			return TVector<float, 3>(W.X, W.Y, W.Z);
		}

		/**
		 * Calculate the angular velocity required to take an object with orientation R0 to orientation R1 in time Dt.
		 *
		 * Uses the Quaternion to Axis/Angle method.
		 */
		static TVector<float, 3> CalculateAngularVelocity2(const TRotation<float, 3>& InR0, const TRotation<float, 3>& InR1, const float InDt)
		{
			// @todo(ccaulfield): ToAxisAndAngle starts to return increasingly random, non-normalized axes for very small angles. This 
			// underestimates the angular velocity magnitude and randomizes direction.
			check(InDt > SMALL_NUMBER);

			const TRotation<float, 3>& R0 = InR0;
			TRotation<float, 3> R1 = InR1;
			R1.EnforceShortestArcWith(R0);

			const TRotation<float, 3> DR = R1 * Conjugate(R0);
			TVector<float, 3> Axis;
			float Angle;
			DR.ToAxisAndAngle(Axis, Angle);
			return Axis * (Angle / InDt);
		}

		/**
		 * Calculate the angular velocity required to take an object with orientation R0 to orientation R1 in time Dt.
		 *
		 * This should match the algorithm used in PerParticleUpdateFromDeltaPosition rule.
		 */
		static TVector<float, 3> CalculateAngularVelocity(const TRotation<float, 3>& InR0, const TRotation<float, 3>& InR1, const float InDt)
		{
			return CalculateAngularVelocity2(InR0, InR1, InDt);
		}

		/**
		 * Return a new rotation equal to the input rotation with angular velocity W applied over time Dt.
		 *
		 * Uses the relation: DQ/DT = (W * Q)/2
		 */
		static TRotation<float, 3> IntegrateRotationWithAngularVelocity(const TRotation<float, 3>& InR0, const TVector<float, 3>& InW, const float InDt)
		{
			TRotation<float, 3> R1 = InR0 + (TRotation<float, 3>::FromElements(InW.X, InW.Y, InW.Z, 0.f) * InR0) * (InDt * 0.5f);
			return R1.GetNormalized();
		}

	};
}
