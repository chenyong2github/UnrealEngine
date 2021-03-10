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

		inline static FMatrix33 CrossProductMatrix(const FVec3& V)
		{
			return FMatrix33(
			    0, -V.Z, V.Y,
			    V.Z, 0, -V.X,
			    -V.Y, V.X, 0);
		}

		/**
		 * Multiple two matrices: C = L.R
		 * @note This is the mathematically expected operator. FMatrix operator* calculates C = R.Transpose(L), so this is not equivalent to that.
		 */
		inline FMatrix33 Multiply(const FMatrix33& L, const FMatrix33& R)
		{
			// @todo(ccaulfield): optimize: simd

			// We want L.R (FMatrix operator* actually calculates R.(L)T; i.e., Right is on the left, and the Left is transposed on the right.)
			// NOTE: PMatrix constructor takes values in column order
			return FMatrix33(
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

		inline FMatrix44 Multiply(const FMatrix44& L, const FMatrix44& R)
		{
			// @todo(ccaulfield): optimize: simd

			// We want L.R (FMatrix operator* actually calculates R.(L)T; i.e., Right is on the left, and the Left is transposed on the right.)
			// NOTE: PMatrix constructor takes values in column order
			return FMatrix44(
				L.M[0][0] * R.M[0][0] + L.M[1][0] * R.M[0][1] + L.M[2][0] * R.M[0][2] + L.M[3][0] * R.M[0][3],	// x00
				L.M[0][0] * R.M[1][0] + L.M[1][0] * R.M[1][1] + L.M[2][0] * R.M[1][2] + L.M[3][0] * R.M[1][3],	// x01
				L.M[0][0] * R.M[2][0] + L.M[1][0] * R.M[2][1] + L.M[2][0] * R.M[2][2] + L.M[3][0] * R.M[2][3],	// x02
				L.M[0][0] * R.M[3][0] + L.M[1][0] * R.M[3][1] + L.M[2][0] * R.M[3][2] + L.M[3][0] * R.M[3][3],	// x03

				L.M[0][1] * R.M[0][0] + L.M[1][1] * R.M[0][1] + L.M[2][1] * R.M[0][2] + L.M[3][1] * R.M[0][3],	// x10
				L.M[0][1] * R.M[1][0] + L.M[1][1] * R.M[1][1] + L.M[2][1] * R.M[1][2] + L.M[3][1] * R.M[1][3],	// x11
				L.M[0][1] * R.M[2][0] + L.M[1][1] * R.M[2][1] + L.M[2][1] * R.M[2][2] + L.M[3][1] * R.M[2][3],	// x12
				L.M[0][1] * R.M[3][0] + L.M[1][1] * R.M[3][1] + L.M[2][1] * R.M[3][2] + L.M[3][1] * R.M[3][3],	// x13

				L.M[0][2] * R.M[0][0] + L.M[1][2] * R.M[0][1] + L.M[2][2] * R.M[0][2] + L.M[3][2] * R.M[0][3],	// x20
				L.M[0][2] * R.M[1][0] + L.M[1][2] * R.M[1][1] + L.M[2][2] * R.M[1][2] + L.M[3][2] * R.M[1][3],	// x21
				L.M[0][2] * R.M[2][0] + L.M[1][2] * R.M[2][1] + L.M[2][2] * R.M[2][2] + L.M[3][2] * R.M[2][3],	// x22
				L.M[0][2] * R.M[3][0] + L.M[1][2] * R.M[3][1] + L.M[2][2] * R.M[3][2] + L.M[3][2] * R.M[3][3],	// x23

				L.M[0][3] * R.M[0][0] + L.M[1][3] * R.M[0][1] + L.M[2][3] * R.M[0][2] + L.M[3][3] * R.M[0][3],	// x30
				L.M[0][3] * R.M[1][0] + L.M[1][3] * R.M[1][1] + L.M[2][3] * R.M[1][2] + L.M[3][3] * R.M[1][3],	// x31
				L.M[0][3] * R.M[2][0] + L.M[1][3] * R.M[2][1] + L.M[2][3] * R.M[2][2] + L.M[3][3] * R.M[2][3],	// x32
				L.M[0][3] * R.M[3][0] + L.M[1][3] * R.M[3][1] + L.M[2][3] * R.M[3][2] + L.M[3][3] * R.M[3][3]	// x33
				);
		}

		inline FMatrix33 MultiplyAB(const FMatrix33& LIn, const FMatrix33& RIn)
		{
			return Multiply(LIn, RIn);
		}

		inline FMatrix33 MultiplyABt(const FMatrix33& L, const FMatrix33& R)
		{
			return FMatrix33(
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

		inline FMatrix33 MultiplyAtB(const FMatrix33& L, const FMatrix33& R)
		{
			return FMatrix33(
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
		inline FVec3 Multiply(const FMatrix33& LIn, const FVec3& R)
		{
			// @todo(ccaulfield): optimize: remove transposes and use simd etc
			FMatrix33 L = LIn.GetTransposed();

			return FVec3(
			    L.M[0][0] * R.X + L.M[0][1] * R.Y + L.M[0][2] * R.Z,
			    L.M[1][0] * R.X + L.M[1][1] * R.Y + L.M[1][2] * R.Z,
			    L.M[2][0] * R.X + L.M[2][1] * R.Y + L.M[2][2] * R.Z);
		}

		inline FVec4 Multiply(const FMatrix44& LIn, const FVec4& R)
		{
			// @todo(ccaulfield): optimize: remove transposes and use simd etc
			FMatrix44 L = LIn.GetTransposed();

			return FVec4(
				L.M[0][0] * R.X + L.M[0][1] * R.Y + L.M[0][2] * R.Z + L.M[0][3] * R.W,
				L.M[1][0] * R.X + L.M[1][1] * R.Y + L.M[1][2] * R.Z + L.M[1][3] * R.W,
				L.M[2][0] * R.X + L.M[2][1] * R.Y + L.M[2][2] * R.Z + L.M[2][3] * R.W,
				L.M[3][0] * R.X + L.M[3][1] * R.Y + L.M[3][2] * R.Z + L.M[3][3] * R.W
				);
		}

		/**
		 * Concatenate two transforms. This returns a transform that logically applies R then L.
		 */
		inline FRigidTransform3 Multiply(const FRigidTransform3 L, const FRigidTransform3& R)
		{
			return FRigidTransform3(L.GetTranslation() + L.GetRotation().RotateVector(R.GetTranslation()), L.GetRotation() * R.GetRotation());
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
		PMatrix<T, 3, 3> ComputeJointFactorMatrix(const TVec3<T>& V, const PMatrix<T, 3, 3>& M, const T& Im)
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
		bool IntersectLineSegments2D(const TVec2<T>& InStartA, const TVec2<T>& InEndA, const TVec2<T>& InStartB, const TVec2<T>& InEndB, T& OutTA, T& OutTB)
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
		 * Given the local-space diagonal inertia for an unscaled object, return an inertia as if generated from a non-uniformly scaled shape with the specified scale.
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

		/**
		 * Given the local-space inertia for an unscaled object, return an inertia as if generated from a non-uniformly scaled shape with the specified scale.
		 * If bScaleMass is true, it also takes into account the fact that the mass would have changed by the increase in volume.
		 */
		inline FMatrix33 ScaleInertia(const FMatrix33& Inertia, const FVec3& Scale, const bool bScaleMass)
		{
			// @todo(chaos): do we need to support a rotation of the scale axes?
			FVec3 D = Inertia.GetDiagonal();
			FVec3 XYZSq = (FVec3(0.5f * (D.X + D.Y + D.Z)) - D) * Scale * Scale;
			FReal XX = XYZSq.Y + XYZSq.Z;
			FReal YY = XYZSq.X + XYZSq.Z;
			FReal ZZ = XYZSq.X + XYZSq.Y;
			FReal XY = Inertia.M[0][1] * Scale.X * Scale.Y;
			FReal XZ = Inertia.M[0][2] * Scale.X * Scale.Z;
			FReal YZ = Inertia.M[1][2] * Scale.Y * Scale.Z;
			FReal MassScale = (bScaleMass) ? Scale.X * Scale.Y * Scale.Z : 1.0f;
			FMatrix33 ScaledInertia = FMatrix33(
				MassScale * XX, MassScale * XY, MassScale * XZ,
				MassScale * XY, MassScale * YY, MassScale * YZ,
				MassScale * XZ, MassScale * YZ, MassScale * ZZ);
			return ScaledInertia;
		}

		// Replacement for FMath::Wrap that works for integers and returns a value in [Begin, End).
		// Note: this implementation uses a loop to bring the value into range - it should not be used if the value is much larger than the range.
		inline int32 WrapIndex(int32 V, int32 Begin, int32 End)
		{
			int32 Range = End - Begin;
			while (V < Begin)
			{
				V += Range;
			}
			while (V >= End)
			{
				V -= Range;
			}
			return V;
		}

		// For implementation notes, see "Realtime Collision Detection", Christer Ericson, 2005
		inline void NearestPointsOnLineSegments(
			const FVec3& P1, const FVec3& Q1,
			const FVec3& P2, const FVec3& Q2,
			FReal& S, FReal& T,
			FVec3& C1, FVec3& C2,
			const FReal Epsilon = 1.e-4f)
		{
			const FReal EpsilonSq = Epsilon * Epsilon;
			const FVec3 D1 = Q1 - P1;
			const FVec3 D2 = Q2 - P2;
			const FVec3 R = P1 - P2;
			const FReal A = FVec3::DotProduct(D1, D1);
			const FReal B = FVec3::DotProduct(D1, D2);
			const FReal C = FVec3::DotProduct(D1, R);
			const FReal E = FVec3::DotProduct(D2, D2);
			const FReal F = FVec3::DotProduct(D2, R);

			S = 0.0f;
			T = 0.0f;

			if ((A <= EpsilonSq) && (B <= EpsilonSq))
			{
				// Both segments are points
			}
			else if (A <= Epsilon)
			{
				// First segment (only) is a point
				T = FMath::Clamp(F / E, 0.0f, 1.0f);
			}
			else if (E <= Epsilon)
			{
				// Second segment (only) is a point
				S = FMath::Clamp(-C / A, 0.0f, 1.0f);
			}
			else
			{
				// Non-degenrate case - we have two lines
				const FReal Denom = A * E - B * B;
				if (Denom != 0.0f)
				{
					S = FMath::Clamp((B * F - C * E) / Denom, 0.0f, 1.0f);
				}
				T = (B * S + F) / E;

				if (T < 0.0f)
				{
					S = FMath::Clamp(-C / A, 0.0f, 1.0f);
					T = 0.0f;
				}
				else if (T > 1.0f)
				{
					S = FMath::Clamp((B - C) / A, 0.0f, 1.0f);
					T = 1.0f;
				}
			}

			C1 = P1 + S * D1;
			C2 = P2 + T * D2;
		}


	} // namespace Utilities
} // namespace Chaos