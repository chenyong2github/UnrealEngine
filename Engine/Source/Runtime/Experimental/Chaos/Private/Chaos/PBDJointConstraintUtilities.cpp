// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
	template<typename T, int d>
	TVector<T, d> TPBDJointUtilities<T, d>::ConditionInertia(const TVector<T, d>& InI, const T MaxRatio)
	{
		// @todo(ccaulfield): simd
		if (MaxRatio > 0)
		{
			T IMin = InI.Min();
			T IMax = InI.Max();
			T Ratio = IMax / IMin;
			if (Ratio > MaxRatio)
			{
				T MinIMin = IMax / MaxRatio;
				return TVector<T, d>(
					FMath::Lerp(MinIMin, IMax, (InI.X - IMin) / (IMax - IMin)),
					FMath::Lerp(MinIMin, IMax, (InI.Y - IMin) / (IMax - IMin)),
					FMath::Lerp(MinIMin, IMax, (InI.Z - IMin) / (IMax - IMin)));
			}
		}
		return InI;
	}

	template<typename T, int d>
	TVector<T, d> TPBDJointUtilities<T, d>::ConditionParentInertia(const TVector<T, d>& IParent, const TVector<T, d>& IChild, const T MinRatio)
	{
		// @todo(ccaulfield): simd
		if (MinRatio > 0)
		{
			T IParentMax = IParent.Max();
			T IChildMax = IChild.Max();
			T Ratio = IParentMax / IChildMax;
			if (Ratio < MinRatio)
			{
				T Multiplier = MinRatio / Ratio;
				return IParent * Multiplier;
			}
		}
		return IParent;
	}

	template<typename T, int d>
	T TPBDJointUtilities<T, d>::ConditionParentMass(const T MParent, const T MChild, const T MinRatio)
	{
		if (MinRatio > 0)
		{
			T Ratio = MParent / MChild;
			if (Ratio < MinRatio)
			{
				T Multiplier = MinRatio / Ratio;
				return MParent * Multiplier;
			}
		}
		return MParent;
	}

	template<typename T, int d>
	void TPBDJointUtilities<T, d>::GetConditionedInverseMass(const TPBDRigidParticleHandle<T, d>* PParent, const TPBDRigidParticleHandle<T, d>* PChild, T& OutInvMParent, T& OutInvMChild, PMatrix<T, d, d>& OutInvIParent, PMatrix<T, d, d>& OutInvIChild, const T MinParentMassRatio, const T MaxInertiaRatio)
	{
		T MParent = PParent->M();
		T MChild = PChild->M();
		MParent = ConditionParentMass(MParent, MChild, MinParentMassRatio);

		TVector<T, d> IParent = ConditionInertia(PParent->I().GetDiagonal(), MaxInertiaRatio);
		TVector<T, d> IChild = ConditionInertia(PChild->I().GetDiagonal(), MaxInertiaRatio);
		IParent = ConditionParentInertia(IParent, IChild, MinParentMassRatio);

		OutInvMParent = (T)1 / MParent;
		OutInvMChild = (T)1 / MChild;
		OutInvIParent = PMatrix<T, d, d>((T)1 / IParent.X, (T)1 / IParent.Y, (T)1 / IParent.Z);
		OutInvIChild = PMatrix<T, d, d>((T)1 / IChild.X, (T)1 / IChild.Y, (T)1 / IChild.Z);
	}


	template<typename T, int d>
	void TPBDJointUtilities<T, d>::GetConditionedInverseMass(const TPBDRigidParticleHandle<T, d>* P0, T& OutInvM0, PMatrix<T, d, d>& OutInvI0, const T MaxInertiaRatio)
	{
		TVector<T, d> I0 = ConditionInertia(P0->I().GetDiagonal(), MaxInertiaRatio);

		OutInvM0 = P0->InvM();
		OutInvI0 = PMatrix<T, d, d>((T)1 / I0.X, (T)1 / I0.Y, (T)1 / I0.Z);
	}


	template<typename T, int d>
	TVector<T, d> GetSphereLimitedPositionError(const TVector<T, d>& CX, const T Radius)
	{
		T CXLen = CX.Size();
		if (CXLen < Radius)
		{
			return TVector<T, d>(0, 0, 0);
		}
		else if (CXLen > SMALL_NUMBER)
		{
			TVector<T, d> Dir = CX / CXLen;
			return CX - Radius * Dir;
		}
		return CX;
	}

	template<typename T, int d>
	TVector<T, d> GetCylinderLimitedPositionError(const TVector<T, d>& InCX, const TVector<T, d>& Axis, const T Limit, const EJointMotionType AxisMotion)
	{
		TVector<T, d> CXAxis = TVector<T, d>::DotProduct(InCX, Axis) * Axis;
		TVector<T, d> CXPlane = InCX - CXAxis;
		T CXPlaneLen = CXPlane.Size();
		if (AxisMotion == EJointMotionType::Free)
		{
			CXAxis = TVector<T, d>(0, 0, 0);
		}
		if (CXPlaneLen < Limit)
		{
			CXPlane = TVector<T, d>(0, 0, 0);
		}
		else if (CXPlaneLen > KINDA_SMALL_NUMBER)
		{
			TVector<T, d> Dir = CXPlane / CXPlaneLen;
			CXPlane = CXPlane - Limit * Dir;
		}
		return CXAxis + CXPlane;
	}

	template<typename T, int d>
	TVector<T, d> GetLineLimitedPositionError(const TVector<T, d>& CX, const TVector<T, d>& Axis, const T Limit, const EJointMotionType AxisMotion)
	{
		T CXDist = TVector<T, d>::DotProduct(CX, Axis);
		if ((AxisMotion == EJointMotionType::Free) || (FMath::Abs(CXDist) < Limit))
		{
			return CX - CXDist * Axis;
		}
		else if (CXDist >= Limit)
		{
			return CX - Limit * Axis;
		}
		else
		{
			return CX + Limit * Axis;
		}
	}

	template<typename T, int d>
	TVector<T, d> GetLimitedPositionError(const TPBDJointSettings<T, d>& JointSettings, const TRotation<T, d>& R0, const TVector<T, d>& InCX)
	{
		const TVector<EJointMotionType, d>& Motion = JointSettings.Motion.LinearMotionTypes;
		if ((Motion[0] == EJointMotionType::Locked) && (Motion[1] == EJointMotionType::Locked) && (Motion[2] == EJointMotionType::Locked))
		{
			return InCX;
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Spherical distance constraints
			return GetSphereLimitedPositionError(InCX, JointSettings.Motion.LinearLimit);
		}
		else if ((Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (X Axis)
			TVector<T, d> Axis = R0 * TVector<T, d>(1, 0, 0);
			return GetCylinderLimitedPositionError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[0]);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (Y Axis)
			TVector<T, d> Axis = R0 * TVector<T, d>(0, 1, 0);
			return GetCylinderLimitedPositionError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[1]);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited))
		{
			// Circular Limit (Z Axis)
			TVector<T, d> Axis = R0 * TVector<T, d>(0, 0, 1);
			return GetCylinderLimitedPositionError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[2]);
		}
		else
		{
			// Line/Square/Cube Limits (no way to author square or cube limits, but would work if we wanted it)
			TVector<T, d> CX = InCX;
			if (Motion[0] != EJointMotionType::Locked)
			{
				TVector<T, d> Axis = R0 * TVector<T, d>(1, 0, 0);
				CX = GetLineLimitedPositionError(CX, Axis, JointSettings.Motion.LinearLimit, Motion[0]);
			}
			if (Motion[1] != EJointMotionType::Locked)
			{
				TVector<T, d> Axis = R0 * TVector<T, d>(0, 1, 0);
				CX = GetLineLimitedPositionError(CX, Axis, JointSettings.Motion.LinearLimit, Motion[1]);
			}
			if (Motion[2] != EJointMotionType::Locked)
			{
				TVector<T, d> Axis = R0 * TVector<T, d>(0, 0, 1);
				CX = GetLineLimitedPositionError(CX, Axis, JointSettings.Motion.LinearLimit, Motion[2]);
			}
			return CX;
		}
	}


	template<class T, int d>
	void TPBDJointUtilities<T, d>::CalculateSwingConstraintSpace(
		const TPBDJointSolverSettings<T, d>& SolverSettings,
		const TPBDJointSettings<T, d>& JointSettings,
		const int32 Index0,
		const int32 Index1,
		TVector<T, d>& P0,
		TRotation<T, d>& Q0,
		TVector<T, d>& P1,
		TRotation<T, d>& Q1,
		TVector<T, d>& OutX0,
		PMatrix<T, d, d>& OutR0,
		TVector<T, d>& OutX1,
		PMatrix<T, d, d>& OutR1,
		TVector<T, d>& OutCR)
	{
		const TRigidTransform<T, d>& XL0 = JointSettings.ConstraintFrames[Index0];
		const TRigidTransform<T, d>& XL1 = JointSettings.ConstraintFrames[Index1];
		const TVector<T, d> X0 = P0 + Q0 * XL0.GetTranslation();
		const TVector<T, d> X1 = P1 + Q1 * XL1.GetTranslation();
		const TRotation<T, d> R0 = Q0 * XL0.GetRotation();
		const TRotation<T, d> R1 = Q1 * XL1.GetRotation();

		TRotation<T, d> R01 = R0.Inverse() * R1;
		TRotation<T, d> R01Twist, R01Swing;
		R01.ToSwingTwist(TJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		TVector<T, d> TwistAxis01;
		T TwistAngle = (T)0;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, TJointConstants<T, d>::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (T)2 * PI;
		}
		if (TVector<T, d>::DotProduct(TwistAxis01, TJointConstants<T, d>::TwistAxis()) < 0)
		{
			TwistAngle = -TwistAngle;
		}

		const PMatrix<T, d, d> Axes0 = R0.ToMatrix();
		const PMatrix<T, d, d> Axes1 = R1.ToMatrix();

		T Swing1Angle = (T)0;
		const TVector<T, d> SwingCross1 = TVector<T, d>::CrossProduct(Axes0.GetAxis((int32)EJointAngularConstraintIndex::Swing1), Axes1.GetAxis((int32)EJointAngularConstraintIndex::Swing1));
		const T SwingCross1Len = SwingCross1.Size();
		if (SwingCross1Len > KINDA_SMALL_NUMBER)
		{
			Swing1Angle = FMath::Asin(FMath::Clamp(SwingCross1Len, (T)0, (T)1));
		}
		const T Swing1Dot = TVector<T, d>::DotProduct(Axes0.GetAxis((int32)EJointAngularConstraintIndex::Swing1), Axes1.GetAxis((int32)EJointAngularConstraintIndex::Swing1));
		if (Swing1Dot < (T)0)
		{
			Swing1Angle = (T)PI - Swing1Angle;
		}

		T Swing2Angle = (T)0;
		const TVector<T, d> SwingCross2 = TVector<T, d>::CrossProduct(Axes0.GetAxis((int32)EJointAngularConstraintIndex::Swing2), Axes1.GetAxis((int32)EJointAngularConstraintIndex::Swing2));
		const T SwingCross2Len = SwingCross2.Size();
		if (SwingCross2Len > KINDA_SMALL_NUMBER)
		{
			Swing2Angle = FMath::Asin(FMath::Clamp(SwingCross2Len, (T)0, (T)1));
		}
		const T Swing2Dot = TVector<T, d>::DotProduct(Axes0.GetAxis((int32)EJointAngularConstraintIndex::Swing2), Axes1.GetAxis((int32)EJointAngularConstraintIndex::Swing2));
		if (Swing2Dot < (T)0)
		{
			Swing2Angle = (T)PI - Swing2Angle;
		}

		OutX0 = X0;
		OutX1 = X1;
		OutR0 = R0.ToMatrix();
		OutR1 = R1.ToMatrix();
		OutCR[(int32)EJointAngularAxisIndex::Twist] = TwistAngle;
		OutCR[(int32)EJointAngularAxisIndex::Swing1] = Swing1Angle;
		OutCR[(int32)EJointAngularAxisIndex::Swing2] = Swing2Angle;
	}

	template<class T, int d>
	void TPBDJointUtilities<T, d>::CalculateConeConstraintSpace(
		const TPBDJointSolverSettings<T, d>& SolverSettings,
		const TPBDJointSettings<T, d>& JointSettings,
		const int32 Index0,
		const int32 Index1,
		TVector<T, d>& P0,
		TRotation<T, d>& Q0,
		TVector<T, d>& P1,
		TRotation<T, d>& Q1,
		TVector<T, d>& OutX0,
		PMatrix<T, d, d>& OutR0, 
		TVector<T, d>& OutX1, 
		PMatrix<T, d, d>& OutR1, 
		TVector<T, d>& OutCR)
	{
		const TRigidTransform<T, d>& XL0 = JointSettings.ConstraintFrames[Index0];
		const TRigidTransform<T, d>& XL1 = JointSettings.ConstraintFrames[Index1];
		const TVector<T, d> X0 = P0 + Q0 * XL0.GetTranslation();
		const TVector<T, d> X1 = P1 + Q1 * XL1.GetTranslation();
		const TRotation<T, d> R0 = Q0 * XL0.GetRotation();
		const TRotation<T, d> R1 = Q1 * XL1.GetRotation();

		TRotation<T, d> R01 = R0.Inverse() * R1;
		TRotation<T, d> R01Twist, R01Swing;
		R01.ToSwingTwist(TJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		TVector<T, d> TwistAxis01;
		T TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, TJointConstants<T, d>::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (T)2 * PI;
		}
		if (TVector<T, d>::DotProduct(TwistAxis01, TJointConstants<T, d>::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}
		TVector<T, d> TwistAxis0 = R0 * TwistAxis01;
		TVector<T, d> TwistAxis1 = R1 * TwistAxis01;

		TVector<T, d> SwingAxis01;
		T SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, TJointConstants<T, d>::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (T)2 * PI;
		}
		TVector<T, d> SwingAxis0 = R0 * SwingAxis01;
		TVector<T, d> SwingAxis1 = SwingAxis0;

		OutX0 = X0;
		OutX1 = X1;
		OutR0.SetAxis((int32)EJointAngularAxisIndex::Twist, TwistAxis0);
		OutR0.SetAxis((int32)EJointAngularAxisIndex::Swing1, SwingAxis0);
		OutR0.SetAxis((int32)EJointAngularAxisIndex::Swing2, TVector<T, d>::CrossProduct(SwingAxis0, TwistAxis0));
		OutR1.SetAxis((int32)EJointAngularAxisIndex::Twist, TwistAxis1);
		OutR1.SetAxis((int32)EJointAngularAxisIndex::Swing1, SwingAxis1);
		OutR1.SetAxis((int32)EJointAngularAxisIndex::Swing2, TVector<T, d>::CrossProduct(SwingAxis1, TwistAxis1));
		OutCR[(int32)EJointAngularAxisIndex::Twist] = TwistAngle;
		OutCR[(int32)EJointAngularAxisIndex::Swing1] = SwingAngle;
		OutCR[(int32)EJointAngularAxisIndex::Swing2] = (T)0;
	}


	template<typename T, int d>
	void TPBDJointUtilities<T, d>::ApplyJointPositionConstraint(
		const T Dt,
		const TPBDJointSolverSettings<T, d>& SolverSettings,
		const TPBDJointSettings<T, d>& JointSettings,
		const int32 Index0,
		const int32 Index1,
		TVector<T, d>& P0,
		TRotation<T, d>& Q0,
		TVector<T, d>& P1,
		TRotation<T, d>& Q1,
		float InvM0,
		const PMatrix<T, d, d>& InvIL0,
		float InvM1,
		const PMatrix<T, d, d>& InvIL1)
	{
		const TRigidTransform<T, d>& XL0 = JointSettings.ConstraintFrames[Index0];
		const TRigidTransform<T, d>& XL1 = JointSettings.ConstraintFrames[Index1];
		const TVector<T, d> X0 = P0 + Q0 * XL0.GetTranslation();
		const TVector<T, d> X1 = P1 + Q1 * XL1.GetTranslation();
		const TRotation<T, d> R0 = Q0 * XL0.GetRotation();
		PMatrix<T, d, d> InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
		PMatrix<T, d, d> InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));

		// Calculate constraint error
		TVector<T, d> CX = GetLimitedPositionError(JointSettings, R0, X1 - X0);

		// Calculate constraint correction
		PMatrix<T, d, d> M0 = PMatrix<T, d, d>(0, 0, 0);
		PMatrix<T, d, d> M1 = PMatrix<T, d, d>(0, 0, 0);
		if (InvM0 > 0)
		{
			M0 = Utilities::ComputeJointFactorMatrix(X0 - P0, InvI0, InvM0);
		}
		if (InvM1 > 0)
		{
			M1 = Utilities::ComputeJointFactorMatrix(X1 - P1, InvI1, InvM1);
		}
		PMatrix<T, d, d> MI = (M0 + M1).Inverse();
		TVector<T, d> DX = Utilities::Multiply(MI, CX);

		// Apply constraint correction
		TVector<T, d> DP0 = InvM0 * DX;
		TVector<T, d> DP1 = -InvM1 * DX;
		TVector<T, d> DR0 = Utilities::Multiply(InvI0, TVector<T, d>::CrossProduct(X0 - P0, DX));
		TVector<T, d> DR1 = Utilities::Multiply(InvI1, TVector<T, d>::CrossProduct(X1 - P1, -DX));
		TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(DR0, 0) * Q0) * (T)0.5;
		TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(DR1, 0) * Q1) * (T)0.5;
		P0 = P0 + DP0;
		P1 = P1 + DP1;
		Q0 = (Q0 + DQ0).GetNormalized();
		Q1 = (Q1 + DQ1).GetNormalized();
		Q1.EnforceShortestArcWith(Q0);
	}

	template<typename T, int d>
	void TPBDJointUtilities<T, d>::ApplyJointTwistConstraint(
		const T Dt,
		const TPBDJointSolverSettings<T, d>& SolverSettings,
		const TPBDJointSettings<T, d>& JointSettings,
		const int32 Index0,
		const int32 Index1,
		TVector<T, d>& P0,
		TRotation<T, d>& Q0,
		TVector<T, d>& P1,
		TRotation<T, d>& Q1,
		float InvM0,
		const PMatrix<T, d, d>& InvIL0,
		float InvM1,
		const PMatrix<T, d, d>& InvIL1)
	{
		const TRigidTransform<T, d>& XL0 = JointSettings.ConstraintFrames[Index0];
		const TRigidTransform<T, d>& XL1 = JointSettings.ConstraintFrames[Index1];
		const TRotation<T, d> R0 = Q0 * XL0.GetRotation();
		const TRotation<T, d> R1 = Q1 * XL1.GetRotation();
		PMatrix<T, d, d> InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
		PMatrix<T, d, d> InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));

		TRotation<T, d> R01 = R0.Inverse() * R1;
		TRotation<T, d> R01Twist, R01Swing;
		R01.ToSwingTwist(TJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		TVector<T, d> TwistAxis01;
		T TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, TJointConstants<T, d>::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (T)2 * PI;
		}
		if (TVector<T, d>::DotProduct(TwistAxis01, TJointConstants<T, d>::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}

		TVector<T, d> TwistAxis0 = R0 * TwistAxis01;
		TVector<T, d> TwistAxis1 = R1 * TwistAxis01;
		T TwistAngleMax = FLT_MAX;
		if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Limited)
		{
			TwistAngleMax = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Twist];
		}
		else if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Locked)
		{
			TwistAngleMax = 0;
		}

		T DTwistAngle = 0;
		if (TwistAngle > TwistAngleMax)
		{
			DTwistAngle = TwistAngle - TwistAngleMax;
		}
		else if (TwistAngle < -TwistAngleMax)
		{
			DTwistAngle = TwistAngle + TwistAngleMax;
		}
		T DTwistAngle0 = DTwistAngle;
		T DTwistAngle1 = -DTwistAngle;

		T L = (T)1 / (TVector<T, d>::DotProduct(TwistAxis0, Utilities::Multiply(InvI0, TwistAxis0)) + TVector<T, d>::DotProduct(TwistAxis1, Utilities::Multiply(InvI1, TwistAxis1)));
		TVector<T, d> W0 = Utilities::Multiply(InvI0, TwistAxis0) * L * DTwistAngle0;
		TVector<T, d> W1 = Utilities::Multiply(InvI1, TwistAxis1) * L * DTwistAngle1;
		TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
		TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
		Q0 = (Q0 + DQ0).GetNormalized();
		Q1 = (Q1 + DQ1).GetNormalized();
		Q1.EnforceShortestArcWith(Q0);
	}

	template<typename T, int d>
	void TPBDJointUtilities<T, d>::ApplyJointConeConstraint(
		const T Dt,
		const TPBDJointSolverSettings<T, d>& SolverSettings,
		const TPBDJointSettings<T, d>& JointSettings,
		const int32 Index0,
		const int32 Index1,
		TVector<T, d>& P0,
		TRotation<T, d>& Q0,
		TVector<T, d>& P1,
		TRotation<T, d>& Q1,
		float InvM0,
		const PMatrix<T, d, d>& InvIL0,
		float InvM1,
		const PMatrix<T, d, d>& InvIL1)
	{
		const TRigidTransform<T, d>& XL0 = JointSettings.ConstraintFrames[Index0];
		const TRigidTransform<T, d>& XL1 = JointSettings.ConstraintFrames[Index1];
		const TRotation<T, d> R0 = Q0 * XL0.GetRotation();
		const TRotation<T, d> R1 = Q1 * XL1.GetRotation();
		PMatrix<T, d, d> InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
		PMatrix<T, d, d> InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));

		// Calculate Swing axis for each body
		TRotation<T, d> R01 = R0.Inverse() * R1;
		TRotation<T, d> R01Twist, R01Swing;
		R01.ToSwingTwist(TJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		TVector<T, d> SwingAxis01;
		T SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, TJointConstants<T, d>::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (T)2 * PI;
		}

		TVector<T, d> SwingAxis0 = R0 * SwingAxis01;
		TVector<T, d> SwingAxis1 = SwingAxis0;

		// Calculate swing limit for the current swing axis
		T SwingAngleMax = FLT_MAX;
		T Swing1Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		T Swing2Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];

		// Circular swing limit
		SwingAngleMax = Swing1Limit;

		// Elliptical swing limit
		if (!FMath::IsNearlyEqual(Swing1Limit, Swing2Limit, KINDA_SMALL_NUMBER))
		{
			// Map swing axis to ellipse and calculate limit for this swing axis
			T DotSwing1 = FMath::Abs(TVector<T, d>::DotProduct(SwingAxis01, TJointConstants<T, d>::Swing1Axis()));
			T DotSwing2 = FMath::Abs(TVector<T, d>::DotProduct(SwingAxis01, TJointConstants<T, d>::Swing2Axis()));
			SwingAngleMax = FMath::Sqrt(Swing1Limit * DotSwing2 * Swing1Limit * DotSwing2 + Swing2Limit * DotSwing1 * Swing2Limit * DotSwing1);
		}

		// Calculate swing error we need to correct
		T DSwingAngle = 0;
		if (SwingAngle > SwingAngleMax)
		{
			DSwingAngle = SwingAngle - SwingAngleMax;
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			DSwingAngle = SwingAngle + SwingAngleMax;
		}
		T DSwingAngle0 = DSwingAngle;
		T DSwingAngle1 = -DSwingAngle;

		// Apply swing correction
		T L = (T)1 / (TVector<T, d>::DotProduct(SwingAxis0, Utilities::Multiply(InvI0, SwingAxis0)) + TVector<T, d>::DotProduct(SwingAxis1, Utilities::Multiply(InvI1, SwingAxis1)));
		TVector<T, d> W0 = Utilities::Multiply(InvI0, SwingAxis0) * L * DSwingAngle0;
		TVector<T, d> W1 = Utilities::Multiply(InvI1, SwingAxis1) * L * DSwingAngle1;
		TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
		TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
		Q0 = (Q0 + DQ0).GetNormalized();
		Q1 = (Q1 + DQ1).GetNormalized();
		Q1.EnforceShortestArcWith(Q0);
	}

	template<typename T, int d>
	void TPBDJointUtilities<T, d>::ApplyJointSwingConstraint(
		const T Dt,
		const TPBDJointSolverSettings<T, d>& SolverSettings,
		const TPBDJointSettings<T, d>& JointSettings,
		const int32 Index0,
		const int32 Index1,
		const EJointAngularConstraintIndex SwingConstraint,
		TVector<T, d>& P0,
		TRotation<T, d>& Q0,
		TVector<T, d>& P1,
		TRotation<T, d>& Q1,
		float InvM0,
		const PMatrix<T, d, d>& InvIL0,
		float InvM1,
		const PMatrix<T, d, d>& InvIL1)
	{
		const TRigidTransform<T, d>& XL0 = JointSettings.ConstraintFrames[Index0];
		const TRigidTransform<T, d>& XL1 = JointSettings.ConstraintFrames[Index1];
		const TRotation<T, d> R0 = Q0 * XL0.GetRotation();
		const TRotation<T, d> R1 = Q1 * XL1.GetRotation();
		PMatrix<T, d, d> InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
		PMatrix<T, d, d> InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));

		const PMatrix<T, d, d> Axes0 = R0.ToMatrix();
		const PMatrix<T, d, d> Axes1 = R1.ToMatrix();
		const TVector<T, d> SwingCross = TVector<T, d>::CrossProduct(Axes0.GetAxis((int32)SwingConstraint), Axes1.GetAxis((int32)SwingConstraint));
		const T SwingCrossLen = SwingCross.Size();
		if (SwingCrossLen > KINDA_SMALL_NUMBER)
		{
			const TVector<T, d> SwingAxis = SwingCross / SwingCrossLen;
			TVector<T, d> SwingAxis0 = SwingAxis;
			TVector<T, d> SwingAxis1 = SwingAxis;

			T SwingAngle = FMath::Asin(FMath::Clamp(SwingCrossLen, (T)0, (T)1));
			const T SwingDot = TVector<T, d>::DotProduct(Axes0.GetAxis((int32)SwingConstraint), Axes1.GetAxis((int32)SwingConstraint));
			if (SwingDot < (T)0)
			{
				SwingAngle = (T)PI - SwingAngle;
			}

			T SwingAngleMax = FLT_MAX;
			if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraint] == EJointMotionType::Limited)
			{
				T Swing1Limit = JointSettings.Motion.AngularLimits[(int32)SwingConstraint];
				SwingAngleMax = Swing1Limit;
			}
			else if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraint] == EJointMotionType::Locked)
			{
				SwingAngleMax = 0;
			}

			// Calculate swing error we need to correct
			T DSwingAngle = 0;
			if (SwingAngle > SwingAngleMax)
			{
				DSwingAngle = SwingAngle - SwingAngleMax;
			}
			else if (SwingAngle < -SwingAngleMax)
			{
				DSwingAngle = SwingAngle + SwingAngleMax;
			}
			T DSwingAngle0 = DSwingAngle;
			T DSwingAngle1 = -DSwingAngle;

			// Apply swing correction
			T L = (T)1 / (TVector<T, d>::DotProduct(SwingAxis0, Utilities::Multiply(InvI0, SwingAxis0)) + TVector<T, d>::DotProduct(SwingAxis1, Utilities::Multiply(InvI1, SwingAxis1)));
			TVector<T, d> W0 = Utilities::Multiply(InvI0, SwingAxis0) * L * DSwingAngle0;
			TVector<T, d> W1 = Utilities::Multiply(InvI1, SwingAxis1) * L * DSwingAngle1;
			TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
			TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
			Q0 = (Q0 + DQ0).GetNormalized();
			Q1 = (Q1 + DQ1).GetNormalized();
			Q1.EnforceShortestArcWith(Q0);
		}
	}

	template<typename T, int d>
	void TPBDJointUtilities<T, d>::ApplyJointTwistDrive(
		const T Dt,
		const TPBDJointSolverSettings<T, d>& SolverSettings,
		const TPBDJointSettings<T, d>& JointSettings,
		const int32 Index0,
		const int32 Index1,
		TVector<T, d>& P0,
		TRotation<T, d>& Q0,
		TVector<T, d>& P1,
		TRotation<T, d>& Q1,
		float InvM0,
		const PMatrix<T, d, d>& InvIL0,
		float InvM1,
		const PMatrix<T, d, d>& InvIL1)
	{
		const TRigidTransform<T, d>& XL0 = JointSettings.ConstraintFrames[Index0];
		const TRigidTransform<T, d>& XL1 = JointSettings.ConstraintFrames[Index1];
		const TRotation<T, d> R0 = Q0 * XL0.GetRotation();
		const TRotation<T, d> R1 = Q1 * XL1.GetRotation();
		PMatrix<T, d, d> InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
		PMatrix<T, d, d> InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));

		TRotation<T, d> R01 = R0.Inverse() * R1;
		TRotation<T, d> R01Twist, R01Swing;
		R01.ToSwingTwist(TJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		TVector<T, d> TwistAxis01;
		T TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, TJointConstants<T, d>::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (T)2 * PI;
		}
		if (TVector<T, d>::DotProduct(TwistAxis01, TJointConstants<T, d>::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}

		TVector<T, d> TwistAxis0 = R0 * TwistAxis01;
		TVector<T, d> TwistAxis1 = R1 * TwistAxis01;
		T TwistAngleTarget = JointSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Twist];
		T Stiffness = (SolverSettings.PBDDriveStiffness > 0) ? SolverSettings.PBDDriveStiffness : JointSettings.Motion.AngularDriveStiffness;
		T DriveStiffness = FMath::Clamp(Stiffness, (T)0, (T)1);
		T DTwistAngle = TwistAngle - TwistAngleTarget;
		T DTwistAngle0 = DriveStiffness * DTwistAngle;
		T DTwistAngle1 = -DriveStiffness * DTwistAngle;

		T L = (T)1 / (TVector<T, d>::DotProduct(TwistAxis0, Utilities::Multiply(InvI0, TwistAxis0)) + TVector<T, d>::DotProduct(TwistAxis1, Utilities::Multiply(InvI1, TwistAxis1)));
		TVector<T, d> W0 = Utilities::Multiply(InvI0, TwistAxis0) * L * DTwistAngle0;
		TVector<T, d> W1 = Utilities::Multiply(InvI1, TwistAxis1) * L * DTwistAngle1;
		TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
		TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
		Q0 = (Q0 + DQ0).GetNormalized();
		Q1 = (Q1 + DQ1).GetNormalized();
		Q1.EnforceShortestArcWith(Q0);
	}

	template<typename T, int d>
	void TPBDJointUtilities<T, d>::ApplyJointConeDrive(
		const T Dt,
		const TPBDJointSolverSettings<T, d>& SolverSettings,
		const TPBDJointSettings<T, d>& JointSettings,
		const int32 Index0,
		const int32 Index1,
		TVector<T, d>& P0,
		TRotation<T, d>& Q0,
		TVector<T, d>& P1,
		TRotation<T, d>& Q1,
		float InvM0,
		const PMatrix<T, d, d>& InvIL0,
		float InvM1,
		const PMatrix<T, d, d>& InvIL1)
	{
		const TRigidTransform<T, d>& XL0 = JointSettings.ConstraintFrames[Index0];
		const TRigidTransform<T, d>& XL1 = JointSettings.ConstraintFrames[Index1];
		const TRotation<T, d> R0 = Q0 * XL0.GetRotation();
		const TRotation<T, d> R1 = Q1 * XL1.GetRotation();
		PMatrix<T, d, d> InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
		PMatrix<T, d, d> InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));

		// Calculate Swing axis for each body
		TRotation<T, d> R01 = R0.Inverse() * R1;
		TRotation<T, d> R01Twist, R01Swing;
		R01.ToSwingTwist(TJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		TVector<T, d> SwingAxis01;
		T SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, TJointConstants<T, d>::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (T)2 * PI;
		}

		TVector<T, d> SwingAxis0 = R0 * SwingAxis01;
		TVector<T, d> SwingAxis1 = SwingAxis0;

		// Circular swing target (max of Swing1, Swing2 targets)
		T Swing1Target = JointSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing1];
		T Swing2Target = JointSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing2];
		T SwingAngleTarget = FMath::Max(Swing1Target, Swing2Target);

		T Stiffness = (SolverSettings.PBDDriveStiffness > 0) ? SolverSettings.PBDDriveStiffness : JointSettings.Motion.AngularDriveStiffness;
		T DriveStiffness = FMath::Clamp(Stiffness, (T)0, (T)1);
		T DSwingAngle = SwingAngle - SwingAngleTarget;
		T DSwingAngle0 = DriveStiffness * DSwingAngle;
		T DSwingAngle1 = -DriveStiffness * DSwingAngle;

		// Apply swing correction
		T L = (T)1 / (TVector<T, d>::DotProduct(SwingAxis0, Utilities::Multiply(InvI0, SwingAxis0)) + TVector<T, d>::DotProduct(SwingAxis1, Utilities::Multiply(InvI1, SwingAxis1)));
		TVector<T, d> W0 = Utilities::Multiply(InvI0, SwingAxis0) * L * DSwingAngle0;
		TVector<T, d> W1 = Utilities::Multiply(InvI1, SwingAxis1) * L * DSwingAngle1;
		TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
		TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
		Q0 = (Q0 + DQ0).GetNormalized();
		Q1 = (Q1 + DQ1).GetNormalized();
		Q1.EnforceShortestArcWith(Q0);
	}

	template<typename T, int d>
	void TPBDJointUtilities<T, d>::ApplyJointSLerpDrive(
		const T Dt,
		const TPBDJointSolverSettings<T, d>& SolverSettings,
		const TPBDJointSettings<T, d>& JointSettings,
		const int32 Index0,
		const int32 Index1,
		TVector<T, d>& P0,
		TRotation<T, d>& Q0,
		TVector<T, d>& P1,
		TRotation<T, d>& Q1,
		float InvM0,
		const PMatrix<T, d, d>& InvIL0,
		float InvM1,
		const PMatrix<T, d, d>& InvIL1)
	{
		const TRigidTransform<T, d>& XL0 = JointSettings.ConstraintFrames[Index0];
		const TRigidTransform<T, d>& XL1 = JointSettings.ConstraintFrames[Index1];
		const TRotation<T, d> R0 = Q0 * XL0.GetRotation();
		const TRotation<T, d> R1 = Q1 * XL1.GetRotation();
		PMatrix<T, d, d> InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
		PMatrix<T, d, d> InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));

		const TRotation<T, d> TargetR1 = R0 * JointSettings.Motion.AngularDriveTarget;
		const TRotation<T, d> DR1 = TargetR1 * R1.Inverse();
		const TRotation<T, d> TargetQ0 = DR1.Inverse() * Q0;
		const TRotation<T, d> TargetQ1 = DR1 * Q1;

		T Stiffness = (SolverSettings.PBDDriveStiffness > 0) ? SolverSettings.PBDDriveStiffness : JointSettings.Motion.AngularDriveStiffness;
		T DriveStiffness = FMath::Clamp(Stiffness, (T)0, (T)1);

		// @todo(ccaulfield): use ang mom in slerp drive
		T L0 = InvM0;// TVector<T, d>::DotProduct(Axis0, Utilities::Multiply(InvI0, Axis0));
		T L1 = InvM1;//TVector<T, d>::DotProduct(Axis1, Utilities::Multiply(InvI1, Axis1));
		const T F0 = DriveStiffness * L0 / (L0 + L1);
		const T F1 = DriveStiffness * L1 / (L0 + L1);

		Q0 = TRotation<T, d>::Slerp(Q0, TargetQ0, F0);
		Q1 = TRotation<T, d>::Slerp(Q1, TargetQ1, F1);
		Q1.EnforceShortestArcWith(Q0);
	}
}

namespace Chaos
{
	template class TPBDJointUtilities<float, 3>;
}