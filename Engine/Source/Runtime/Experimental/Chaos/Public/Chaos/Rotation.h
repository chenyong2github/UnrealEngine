// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Real.h"
#include "Chaos/Vector.h"
#include "Chaos/Matrix.h"

#include <cmath>
#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Math/Quat.h"
#include "Math/RotationMatrix.h"
#else
#include <array>

struct FQuat
{
public:
	const Chaos::FReal operator[](const int32 i) const
	{
		return angles[i];
	}
	Chaos::FReal& operator[](const int32 i)
	{
		return angles[i];
	}
	std::array<Chaos::FReal, 3> angles;
	static MakeFromEuler(const Vector<Chaos::FReal, 3>& InAngles)
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
	class TRotation<FReal, 3> : public FQuat
	{
	public:
		TRotation()
		    : FQuat() {}
		TRotation(const FQuat& Quat)
		    : FQuat(Quat) {}
		TRotation(const FMatrix& Matrix)
		    : FQuat(Matrix) {}

		PMatrix<FReal, 3, 3> ToMatrix() const
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
		bool ToAxisAndAngleSafe(TVector<FReal, 3>& OutAxis, FReal& OutAngle, const TVector<FReal, 3>& DefaultAxis, FReal EpsilionSq = 1e-6f) const
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
		bool GetRotationAxisSafe(TVector<FReal, 3>& OutAxis, const TVector<FReal, 3>& DefaultAxis, FReal EpsilionSq = 1e-6f) const
		{
			// Tolerance must be much larger than error in normalized vector (usually ~1e-4) for the 
			// axis calculation to succeed for small angles. For small angles, W ~= 1, and
			// X, Y, Z ~= 0. If the values of X, Y, Z are around 1e-4 we are just normalizing error.
			const FReal LenSq = X * X + Y * Y + Z * Z;
			if (LenSq > EpsilionSq)
			{
				FReal InvLen = FMath::InvSqrt(LenSq);
				OutAxis = FVector(X * InvLen, Y * InvLen, Z * InvLen);
				return true;
			}

			OutAxis = DefaultAxis;
			return false;
		}

		/**
		 * Extract the Swing and Twist rotations, assuming that the Twist Axis is (1,0,0).
		 * /see ToSwingTwist
		 */
		void ToSwingTwistX(FQuat& OutSwing, FQuat& OutTwist) const
		{
			OutTwist = (X != 0.0f)? FQuat(X, 0, 0, W).GetNormalized() : FQuat::Identity;
			OutSwing = *this * OutTwist.Inverse();
		}

		/**
		 * Return the complex conjugate of the rotation
		 */
		static TRotation<FReal, 3> Conjugate(const ::Chaos::TRotation<FReal, 3>& InR)
		{
			TRotation<FReal, 3> R;
			R.X = -InR.X;
			R.Y = -InR.Y;
			R.Z = -InR.Z;
			R.W = InR.W;
			return R;
		}

		/**
		 * Negate all values of the quaternion (note: not the inverse rotation. See Conjugate)
		 */
		static TRotation<FReal, 3> Negate(const ::Chaos::TRotation<FReal, 3>& InR)
		{
			TRotation<FReal, 3> R;
			R.X = -InR.X;
			R.Y = -InR.Y;
			R.Z = -InR.Z;
			R.W = -InR.W;
			return R;
		}

		/**
		 * Create an identity rotation
		 */
		static TRotation<FReal, 3> FromIdentity()
		{
			return FQuat(0, 0, 0, 1);
		}

		/**
		 * Create a rotation by explicitly specifying all elements
		 */
		static TRotation<FReal, 3> FromElements(const FReal X, const FReal Y, const FReal Z, const FReal W)
		{
			return FQuat(X, Y, Z, W);
		}

		/**
		 * Create a rotation by explicitly specifying all elements
		 */
		static TRotation<FReal, 3> FromElements(const ::Chaos::TVector<FReal, 3>& V, const FReal W)
		{
			return FromElements(V.X, V.Y, V.Z, W);
		}

		/**
		 * Create a rotation about an axis by an angle specified in radians
		 */
		static TRotation<FReal, 3> FromAxisAngle(const ::Chaos::TVector<FReal, 3>& Axis, const FReal AngleRad)
		{
			return FQuat(Axis, AngleRad);
		}

		/**
		 * Create a rotation about an axis V/|V| by angle |V| in radians
		 */
		static TRotation<FReal, 3> FromVector(const ::Chaos::TVector<FReal, 3>& V)
		{
			TRotation<FReal, 3> Rot;
			FReal HalfSize = 0.5f * V.Size();
			FReal sinc = (FMath::Abs(HalfSize) > 1e-8) ? FMath::Sin(HalfSize) / HalfSize : 1;
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
		static TRotation<FReal, 3> FromRotatedVector(
		    const ::Chaos::TVector<FReal, 3>& InitialVector,
		    const ::Chaos::TVector<FReal, 3>& FinalVector)
		{
			typedef Chaos::TVector<FReal, 3> TV;
			typedef double T;
			checkSlow(FMath::Abs(InitialVector.Size() - 1.0) < KINDA_SMALL_NUMBER);
			checkSlow(FMath::Abs(FinalVector.Size() - 1.0) < KINDA_SMALL_NUMBER);

			const double CosTheta = FMath::Clamp(TV::DotProduct(InitialVector, FinalVector), -1.f, 1.f);

			TV V = TV::CrossProduct(InitialVector, FinalVector);
			const FReal VMagnitude = V.Size();
			if(VMagnitude == 0)
			{
				return TRotation<FReal, 3>::FromElements(InitialVector, 0.f);
			}

			const T SSquared = .5 * (1.0 + CosTheta); // Uses the half angle formula
			const T VMagnitudeDesired = sqrt(1.0 - SSquared);
			V *= (VMagnitudeDesired / VMagnitude);

			return TRotation<FReal, 3>::FromElements(V, sqrt(SSquared));
		}

		/**
		 * Calculate the angular velocity required to take an object with orientation R0 to orientation R1 in time Dt.
		 *
		 * Uses the relation: DQ/DT = (W * Q)/2
		 */
		static TVector<FReal, 3> CalculateAngularVelocity1(const TRotation<FReal, 3>& InR0, const TRotation<FReal, 3>& InR1, const FReal InDt)
		{
			check(InDt > SMALL_NUMBER);

			const TRotation<FReal, 3>& R0 = InR0;
			TRotation<FReal, 3> R1 = InR1;
			R1.EnforceShortestArcWith(R0);

			// W = 2 * dQ/dT * Qinv
			const TRotation<FReal, 3> DRDt = (R1 - R0) / InDt;
			const TRotation<FReal, 3> RInv = Conjugate(R0);
			const TRotation<FReal, 3> W = (DRDt * RInv) * 2.0f;

			return TVector<FReal, 3>(W.X, W.Y, W.Z);
		}

		/**
		 * Calculate the angular velocity required to take an object with orientation R0 to orientation R1 in time Dt.
		 *
		 * Uses the Quaternion to Axis/Angle method.
		 */
		static TVector<FReal, 3> CalculateAngularVelocity2(const TRotation<FReal, 3>& InR0, const TRotation<FReal, 3>& InR1, const FReal InDt)
		{
			// @todo(ccaulfield): ToAxisAndAngle starts to return increasingly random, non-normalized axes for very small angles. This 
			// underestimates the angular velocity magnitude and randomizes direction.
			check(InDt > SMALL_NUMBER);

			const TRotation<FReal, 3>& R0 = InR0;
			TRotation<FReal, 3> R1 = InR1;
			R1.EnforceShortestArcWith(R0);

			const TRotation<FReal, 3> DR = R1 * Conjugate(R0);
			TVector<FReal, 3> Axis;
			FReal Angle;
			DR.ToAxisAndAngle(Axis, Angle);
			return Axis * (Angle / InDt);
		}

		/**
		 * Calculate the angular velocity required to take an object with orientation R0 to orientation R1 in time Dt.
		 *
		 * This should match the algorithm used in PerParticleUpdateFromDeltaPosition rule.
		 */
		static TVector<FReal, 3> CalculateAngularVelocity(const TRotation<FReal, 3>& InR0, const TRotation<FReal, 3>& InR1, const FReal InDt)
		{
			return CalculateAngularVelocity1(InR0, InR1, InDt);
		}

		/**
		 * Calculate the axis-angle delta (angular velocity * dt) required to take an object with orientation R0 to orientation R1.
		 *
		 * This should match the algorithm used in PerParticleUpdateFromDeltaPosition rule.
		 */
		static TVector<FReal, 3> CalculateAngularDelta(const TRotation<FReal, 3>& InR0, const TRotation<FReal, 3>& InR1)
		{
			return CalculateAngularVelocity(InR0, InR1, 1.0f);
		}

		/**
		 * Return a new rotation equal to the input rotation with angular velocity W applied over time Dt.
		 *
		 * Uses the relation: DQ/DT = (W * Q)/2
		 */
		static TRotation<FReal, 3> IntegrateRotationWithAngularVelocity(const TRotation<FReal, 3>& InR0, const TVector<FReal, 3>& InW, const FReal InDt)
		{
			TRotation<FReal, 3> R1 = InR0 + (TRotation<FReal, 3>::FromElements(InW.X, InW.Y, InW.Z, 0.f) * InR0) * (InDt * 0.5f);
			return R1.GetNormalized();
		}

		/**
		 * Check that two rotations are approximately equal. Assumes the quaternions are normalized and in the same hemisphere.
		 * For small values of Epsilon, this is approximately equivalent to checking that the rotations are within 2*Epsilon
		 * radians of each other.
		 */
		static bool IsNearlyEqual(const TRotation<FReal, 3>& A, const TRotation<FReal, 3>& B, const FReal Epsilon)
		{
			// Only check imaginary part. This is comparing Epsilon to 2*AngleDelta for small angle deltas
			return FMath::IsNearlyEqual(A.X, B.X, Epsilon) 
				&& FMath::IsNearlyEqual(A.Y, B.Y, Epsilon) 
				&& FMath::IsNearlyEqual(A.Z, B.Z, Epsilon);
		}
	};
}
