// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Core.h"
#include "Chaos/Matrix.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

#include "Math/NumericLimits.h"
#include "Templates/IsIntegral.h"

namespace Chaos
{
	namespace Utilities
	{
		//! Take the factorial of \p Num, which should be of integral type.
		template<class TINT = uint64>
		TINT Factorial(TINT Num)
		{
			static_assert(TIsIntegral<TINT>::Value, "Templated type must be integral.");
			TINT Result = Num;
			while (Num > 2)
			{
				Result *= --Num;
			}
			return Result;
		}

		//! Number of ways to choose of \p R elements from a set of size \p N, with no repetitions.
		template<class TINT = uint64>
		TINT NChooseR(const TINT N, const TINT R)
		{
			static_assert(TIsIntegral<TINT>::Value, "Templated type must be integral.");
			return Factorial(N) / (Factorial(R) * Factorial(N - R));
		}

		//! Number of ways to choose of \p R elements from a set of size \p N, with repetitions.
		template<class TINT = uint64>
		TINT NPermuteR(const TINT N, const TINT R)
		{
			static_assert(TIsIntegral<TINT>::Value, "Templated type must be integral.");
			return Factorial(N) / Factorial(N - R);
		}

		//! Compute the minimum, average, and maximum values of \p Values.
		template<class T, class TARRAY=TArray<T>>
		void GetMinAvgMax(const TARRAY& Values, T& MinV, double& AvgV, T& MaxV)
		{
			MinV = TNumericLimits<T>::Max();
			MaxV = TNumericLimits<T>::Lowest();
			AvgV = 0.0;
			for (const T& V : Values)
			{
				MinV = V < MinV ? V : MinV;
				MaxV = V > MaxV ? V : MaxV;
				AvgV += V;
			}
			if (Values.Num())
			{
				AvgV /= Values.Num();
			}
			else
			{
				MinV = MaxV = 0;
			}
		}

		//! Compute the average value.
		template<class T, class TARRAY=TArray<T>>
		T GetAverage(const TARRAY& Values)
		{
			double AvgV = 0.0;
			for (const T& V : Values)
			{
				AvgV += V;
			}
			if (Values.Num())
			{
				AvgV /= Values.Num();
			}
			return static_cast<T>(AvgV);
		}

		//! Compute the variance of \p Values, given the average value of \p Avg.
		template<class T, class TARRAY=TArray<T>>
		T GetVariance(const TARRAY& Values, const T Avg)
		{
			double Variance = 0.0;
			for (const T& V : Values)
			{
				const T Deviation = V - Avg;
				Variance += Deviation * Deviation;
			}
			if (Values.Num())
			{
				Variance /= Values.Num();
			}
			return Variance;
		}

		//! Compute the variance of \p Values (computes their average on the fly).
		template<class T, class TARRAY=TArray<T>>
		T GetVariance(const TARRAY& Values)
		{
			return GetVariance(Values, GetAverage(Values));
		}

		//! Compute the standard deviation of \p Values, given the average value of \p Avg.
		template<class T, class TARRAY=TArray<T>>
		T GetStandardDeviation(const TARRAY& Values, const T Avg)
		{
			const T Variance = GetVariance(Values, Avg);
			return FMath::Sqrt(Variance);
		}

		//! Compute the standard deviation of \p Values (computes their average on the fly).
		template<class T, class TARRAY=TArray<T>>
		T GetStandardDeviation(const TARRAY& Values)
		{
			const T Variance = GetVariance(Values);
			return FMath::Sqrt(Variance);
		}

		//! Compute the standard deviation from \p Variance.
		template<class T>
		T GetStandardDeviation(const T Variance)
		{
			return FMath::Sqrt(Variance);
		}

		inline static PMatrix<float, 3, 3> CrossProductMatrix(const TVector<float, 3>& V)
		{
			return PMatrix<float, 3, 3>(
			    0, -V.Z, V.Y,
			    V.Z, 0, -V.X,
			    -V.Y, V.X, 0);
		}

		/**
		 * Multiple two matrices: C = L.R
		 * @note This is the mathematically expected operator. FMatrix operator* calculates C = R.Transpose(L), so this is not equivalent to that.
		 */
		inline PMatrix<float, 3, 3> Multiply(const PMatrix<float, 3, 3>& L, const PMatrix<float, 3, 3>& R)
		{
			// @todo(ccaulfield): optimize: simd

			// We want L.R (FMatrix operator* actually calculates R.(L)T; i.e., Right is on the left, and the Left is transposed on the right.)
			// NOTE: PMatrix constructor takes values in column order
			return PMatrix<float, 3, 3>(
				L.M[0][0] * R.M[0][0] + L.M[1][0] * R.M[0][1] + L.M[2][0] * R.M[0][2],	// x00
				L.M[0][0] * R.M[1][0] + L.M[1][0] * R.M[1][1] + L.M[2][0] * R.M[1][2],	// x01
				L.M[0][0] * R.M[2][0] + L.M[1][0] * R.M[2][1] + L.M[2][0] * R.M[2][2],	// x02

				L.M[0][1] * R.M[0][0] + L.M[1][1] * R.M[0][1] + L.M[2][1] * R.M[0][2],	// x10
				L.M[0][1] * R.M[1][0] + L.M[1][1] * R.M[1][1] + L.M[2][1] * R.M[1][2],	// x11
				L.M[0][1] * R.M[2][0] + L.M[1][1] * R.M[2][1] + L.M[2][1] * R.M[2][2],	// x12

				L.M[0][2] * R.M[0][0] + L.M[1][2] * R.M[0][1] + L.M[2][2] * R.M[0][2],	// x20
				L.M[0][2] * R.M[1][0] + L.M[1][2] * R.M[1][1] + L.M[2][2] * R.M[1][2],	// x21
				L.M[0][2] * R.M[2][0] + L.M[1][2] * R.M[2][1] + L.M[2][2] * R.M[2][2]	// x22
				);
		}

		inline PMatrix<float, 3, 3> MultiplyAB(const PMatrix<float, 3, 3>& LIn, const PMatrix<float, 3, 3>& RIn)
		{
			return Multiply(LIn, RIn);
		}

		inline PMatrix<float, 3, 3> MultiplyABt(const PMatrix<float, 3, 3>& L, const PMatrix<float, 3, 3>& R)
		{
			return PMatrix<float, 3, 3>(
				L.M[0][0] * R.M[0][0] + L.M[1][0] * R.M[1][0] + L.M[2][0] * R.M[2][0],	// x00
				L.M[0][0] * R.M[0][1] + L.M[1][0] * R.M[1][1] + L.M[2][0] * R.M[2][1],	// x01
				L.M[0][0] * R.M[0][2] + L.M[1][0] * R.M[1][2] + L.M[2][0] * R.M[2][2],	// x02

				L.M[0][1] * R.M[0][0] + L.M[1][1] * R.M[1][0] + L.M[2][1] * R.M[2][0],	// x10
				L.M[0][1] * R.M[0][1] + L.M[1][1] * R.M[1][1] + L.M[2][1] * R.M[2][1],	// x11
				L.M[0][1] * R.M[0][2] + L.M[1][1] * R.M[1][2] + L.M[2][1] * R.M[2][2],	// x12

				L.M[0][2] * R.M[0][0] + L.M[1][2] * R.M[1][0] + L.M[2][2] * R.M[2][0],	// x20
				L.M[0][2] * R.M[0][1] + L.M[1][2] * R.M[1][1] + L.M[2][2] * R.M[2][1],	// x21
				L.M[0][2] * R.M[0][2] + L.M[1][2] * R.M[1][2] + L.M[2][2] * R.M[2][2]	// x22
				);
		}

		inline PMatrix<float, 3, 3> MultiplyAtB(const PMatrix<float, 3, 3>& L, const PMatrix<float, 3, 3>& R)
		{
			return PMatrix<float, 3, 3>(
				L.M[0][0] * R.M[0][0] + L.M[0][1] * R.M[0][1] + L.M[0][2] * R.M[0][2],	// x00
				L.M[0][0] * R.M[1][0] + L.M[0][1] * R.M[1][1] + L.M[0][2] * R.M[1][2],	// x01
				L.M[0][0] * R.M[2][0] + L.M[0][1] * R.M[2][1] + L.M[0][2] * R.M[2][2],	// x02

				L.M[1][0] * R.M[0][0] + L.M[1][1] * R.M[0][1] + L.M[1][2] * R.M[0][2],	// x10
				L.M[1][0] * R.M[1][0] + L.M[1][1] * R.M[1][1] + L.M[1][2] * R.M[1][2],	// x11
				L.M[1][0] * R.M[2][0] + L.M[1][1] * R.M[2][1] + L.M[1][2] * R.M[2][2],	// x12

				L.M[2][0] * R.M[0][0] + L.M[2][1] * R.M[0][1] + L.M[2][2] * R.M[0][2],	// x20
				L.M[2][0] * R.M[1][0] + L.M[2][1] * R.M[1][1] + L.M[2][2] * R.M[1][2],	// x21
				L.M[2][0] * R.M[2][0] + L.M[2][1] * R.M[2][1] + L.M[2][2] * R.M[2][2]	// x22
				);

		}

		/**
		 * Multiple a vector by a matrix: C = L.R
		 * If L is a rotation matrix, then this will return R rotated by that rotation.
		 */
		inline TVector<float, 3> Multiply(const PMatrix<float, 3, 3>& LIn, const TVector<float, 3>& R)
		{
			// @todo(ccaulfield): optimize: remove transposes and use simd etc
			PMatrix<float, 3, 3> L = LIn.GetTransposed();

			return TVector<float, 3>(
			    L.M[0][0] * R.X + L.M[0][1] * R.Y + L.M[0][2] * R.Z,
			    L.M[1][0] * R.X + L.M[1][1] * R.Y + L.M[1][2] * R.Z,
			    L.M[2][0] * R.X + L.M[2][1] * R.Y + L.M[2][2] * R.Z);
		}

		/**
		 * Concatenate two transforms. This returns a transform that logically applies R then L.
		 */
		template<class T, int d>
		TRigidTransform<T, d> Multiply(const TRigidTransform<T, d> L, const TRigidTransform<T, d>& R);

		template<>
		inline TRigidTransform<float, 3> Multiply(const TRigidTransform<float, 3> L, const TRigidTransform<float, 3>& R)
		{
			return TRigidTransform<float, 3>(L.GetTranslation() + L.GetRotation().RotateVector(R.GetTranslation()), L.GetRotation() * R.GetRotation());
		}

		/**
		 * Calculate the world-space inertia (or inverse inertia) for a body with center-of-mass rotation "CoMRotation" and local-space inertia/inverse-inertia "I".
		 */
		static FMatrix33 ComputeWorldSpaceInertia(const FRotation3& CoMRotation, const FMatrix33& I)
		{
			FMatrix33 QM = CoMRotation.ToMatrix();
			return MultiplyAB(QM, MultiplyABt(I, QM));
		}

		static FMatrix33 ComputeWorldSpaceInertia(const FRotation3& CoMRotation, const FVec3& I)
		{
			// @todo(ccaulfield): optimize ComputeWorldSpaceInertia
			return ComputeWorldSpaceInertia(CoMRotation, FMatrix33(I.X, I.Y, I.Z));
		}

		/**
		 * Calculate the matrix that maps a constraint position error to constraint position and rotation corrections.
		 */
		template<class T>
		PMatrix<T, 3, 3> ComputeJointFactorMatrix(const TVector<T, 3>& V, const PMatrix<T, 3, 3>& M, const T& Im)
		{
			// Rigid objects rotational contribution to the impulse.
			// Vx*M*VxT+Im
			check(Im > FLT_MIN);
			return PMatrix<T, 3, 3>(
			    -V[2] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]) + V[1] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]) + Im,
			    V[2] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) - V[0] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]),
			    -V[1] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) + V[0] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]),
			    V[2] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) - V[0] * (V[2] * M.M[2][0] - V[0] * M.M[2][2]) + Im,
			    -V[1] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) + V[0] * (V[2] * M.M[1][0] - V[0] * M.M[2][1]),
			    -V[1] * (-V[1] * M.M[0][0] + V[0] * M.M[1][0]) + V[0] * (-V[1] * M.M[1][0] + V[0] * M.M[1][1]) + Im);
		}

		/**
		 * Detects intersections between 2D line segments, returns intersection results as times along each line segment - these times can be
		 * used to calculate exact intersection locations
		 */
		template<typename T>
		bool IntersectLineSegments2D(const TVector<T, 2>& InStartA, const TVector<T, 2>& InEndA, const TVector<T, 2>& InStartB, const TVector<T, 2>& InEndB, T& OutTA, T& OutTB)
		{
			// Each line can be described as p0 + t(p1 - p0) = P. Set equal to each other and solve for t0 and t1
			OutTA = OutTB = 0;

			T Divisor = (InEndB[0] - InStartB[0]) * (InStartA[1] - InEndA[1]) - (InStartA[0] - InEndA[0]) * (InEndB[1] - InStartB[1]);

			if (FMath::IsNearlyZero(Divisor))
			{
				// The line segments are parallel and will never meet for any values of Ta/Tb
				return false;
			}

			OutTA = ((InStartB[1] - InEndB[1]) * (InStartA[0] - InStartB[0]) + (InEndB[0] - InStartB[0]) * (InStartA[1] - InStartB[1])) / Divisor;
			OutTB = ((InStartA[1] - InEndA[1]) * (InStartA[0] - InStartB[0]) + (InEndA[0] - InStartA[0]) * (InStartA[1] - InStartB[1])) / Divisor;

			return OutTA >= 0 && OutTA <= 1 && OutTB > 0 && OutTB < 1;
		}

		/**
		 * Clip a line segment to inside a plane (plane normal pointing outwards).
		 * @return false if the line is completely outside the plane, true otherwise.
		 */
		inline bool ClipLineSegmentToPlane(FVec3& V0, FVec3& V1, const FVec3& PlaneNormal, const FVec3& PlanePos)
		{
			FReal Dist0 = FVec3::DotProduct(V0 - PlanePos, PlaneNormal);
			FReal Dist1 = FVec3::DotProduct(V1 - PlanePos, PlaneNormal);
			if ((Dist0 > 0.0f) && (Dist1 > 0.0f))
			{
				// Whole line segment is outside of face - reject it
				return false;
			}
			
			if ((Dist0 > 0.0f) && (Dist1 < 0.0f))
			{
				// We must move vert 0 to the plane
				FReal ClippedT = -Dist1 / (Dist0 - Dist1);
				V0 = FMath::Lerp(V1, V0, ClippedT);
			}
			else if ((Dist1 > 0.0f) && (Dist0 < 0.0f))
			{
				// We must move vert 1 to the plane
				FReal ClippedT = -Dist0 / (Dist1 - Dist0);
				V1 = FMath::Lerp(V0, V1, ClippedT);
			}

			return true;
		}

		/**
		 * Clip a line segment to the inside of an axis aligned plane (normal pointing outwards).
		 */
		inline bool ClipLineSegmentToAxisAlignedPlane(FVec3& V0, FVec3& V1, const int32 AxisIndex, const FReal PlaneDir, const FReal PlanePos)
		{
			FReal Dist0 = (V0[AxisIndex] - PlanePos) * PlaneDir;
			FReal Dist1 = (V1[AxisIndex] - PlanePos) * PlaneDir;
			if ((Dist0 > 0.0f) && (Dist1 > 0.0f))
			{
				// Whole line segment is outside of face - reject it
				return false;
			}

			if ((Dist0 > 0.0f) && (Dist1 < 0.0f))
			{
				// We must move vert 0 to the plane
				FReal ClippedT = -Dist1 / (Dist0 - Dist1);
				V0 = FMath::Lerp(V1, V0, ClippedT);
			}
			else if ((Dist1 > 0.0f) && (Dist0 < 0.0f))
			{
				// We must move vert 1 to the plane
				FReal ClippedT = -Dist0 / (Dist1 - Dist0);
				V1 = FMath::Lerp(V0, V1, ClippedT);
			}

			return true;
		}

		/**
		 * Project a point V along direction Dir onto an axis aligned plane.
		 * /note Does not check for division by zero (Dir parallel to plane).
		 */
		inline void ProjectPointOntoAxisAlignedPlane(FVec3& V, const FVec3& Dir, int32 AxisIndex, FReal PlaneDir, FReal PlanePos)
		{
			// V -> V + ((PlanePos - V) | PlaneNormal) / (Dir | PlaneNormal)
			FReal Denominator = Dir[AxisIndex] * PlaneDir;
			FReal Numerator = (PlanePos - V[AxisIndex]) * PlaneDir;
			FReal F = Numerator / Denominator;
			V = V + F * Dir;
		}

		/**
		 * Project a point V along direction Dir onto an axis aligned plane.
		 * /return true if the point was successfully projected onto the plane; false if the direction is parallel to the plane.
		 */
		inline bool ProjectPointOntoAxisAlignedPlaneSafe(FVec3& V, const FVec3& Dir, int32 AxisIndex, FReal PlaneDir, FReal PlanePos, FReal Epsilon)
		{
			// V -> V + ((PlanePos - V) | PlaneNormal) / (Dir | PlaneNormal)
			FReal Denominator = Dir[AxisIndex] * PlaneDir;
			if (Denominator > Epsilon)
			{
				FReal Numerator = (PlanePos - V[AxisIndex]) * PlaneDir;
				FReal F = Numerator / Denominator;
				V = V + F * Dir;
				return true;
			}
			return false;
		}

		inline bool NormalizeSafe(FVec3& V, FReal EpsilonSq = SMALL_NUMBER)
		{
			FReal VLenSq = V.SizeSquared();
			if (VLenSq > EpsilonSq)
			{
				V = V * FMath::InvSqrt(VLenSq);
				return true;
			}
			return false;
		}

		/**
		 * Given the local-space inertia for an unscaled object, return an inertia as if generated from a non-uniformly scaled shape with the specified scale.
		 * If bScaleMass is true, it also takes into account the fact that the mass would have changed by the increase in volume.
		 */
		inline FVec3 ScaleInertia(const FVec3& Inertia, const FVec3& Scale, const bool bScaleMass)
		{
			FVec3 XYZSq = (FVec3(0.5f * (Inertia.X + Inertia.Y + Inertia.Z)) - Inertia) * Scale * Scale;
			FReal XX = XYZSq.Y + XYZSq.Z;
			FReal YY = XYZSq.X + XYZSq.Z;
			FReal ZZ = XYZSq.X + XYZSq.Y;
			FVec3 ScaledInertia = FVec3(XX, YY, ZZ);
			FReal MassScale = (bScaleMass) ? Scale.X * Scale.Y * Scale.Z : 1.0f;
			return MassScale * ScaledInertia;
		}

	} // namespace Utilities
} // namespace Chaos