// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/DenseMatrix.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Utilities.h"

//#pragma optimize("", off)

namespace Chaos
{

	void FPBDJointUtilities::DecomposeSwingTwistLocal(const FRotation3& R0, const FRotation3& R1, FRotation3& R01Swing, FRotation3& R01Twist)
	{
		const FRotation3 R01 = R0.Inverse() * R1;
		R01.ToSwingTwistX(R01Swing, R01Twist);
	}

	FReal FPBDJointUtilities::GetLinearStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FReal SolverStiffness = (SolverSettings.Stiffness > (FReal)0) ? SolverSettings.Stiffness : JointSettings.Motion.Stiffness;
		const FReal SoftSolverStiffness = (SolverSettings.SoftLinearStiffness > (FReal)0) ? SolverSettings.SoftLinearStiffness : JointSettings.Motion.SoftLinearStiffness;
		const bool bIsSoft = JointSettings.Motion.bSoftLinearLimitsEnabled && ((JointSettings.Motion.LinearMotionTypes[0] == EJointMotionType::Limited) || (JointSettings.Motion.LinearMotionTypes[1] == EJointMotionType::Limited) || (JointSettings.Motion.LinearMotionTypes[2] == EJointMotionType::Limited));
		const FReal Stiffness = bIsSoft ? SolverStiffness * SoftSolverStiffness : SolverStiffness;
		return Stiffness;
	}


	FReal FPBDJointUtilities::GetTwistStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FReal SolverStiffness = (SolverSettings.Stiffness > (FReal)0) ? SolverSettings.Stiffness : JointSettings.Motion.Stiffness;
		const FReal SoftSolverStiffness = (SolverSettings.SoftAngularStiffness > (FReal)0) ? SolverSettings.SoftAngularStiffness : JointSettings.Motion.SoftTwistStiffness;
		const bool bIsSoft = JointSettings.Motion.bSoftTwistLimitsEnabled && (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Limited);
		const FReal Stiffness = bIsSoft ? SolverStiffness * SoftSolverStiffness : SolverStiffness;
		return Stiffness;
	}


	FReal FPBDJointUtilities::GetSwingStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FReal SolverStiffness = (SolverSettings.Stiffness > (FReal)0) ? SolverSettings.Stiffness : JointSettings.Motion.Stiffness;
		const FReal SoftSolverStiffness = (SolverSettings.SoftAngularStiffness > (FReal)0) ? SolverSettings.SoftAngularStiffness : JointSettings.Motion.SoftSwingStiffness;
		const bool bIsSoft = JointSettings.Motion.bSoftSwingLimitsEnabled && ((JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1] == EJointMotionType::Limited) || (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2] == EJointMotionType::Limited));
		const FReal Stiffness = bIsSoft ? SolverStiffness * SoftSolverStiffness : SolverStiffness;
		return Stiffness;
	}

	FReal FPBDJointUtilities::GetAngularDriveStiffness(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.DriveStiffness > 0.0f) ? SolverSettings.DriveStiffness : JointSettings.Motion.AngularDriveStiffness;
	}


	FReal FPBDJointUtilities::GetAngularDriveDamping(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.DriveDamping > 0.0f) ? SolverSettings.DriveDamping: JointSettings.Motion.AngularDriveDamping;
	}

	FReal FPBDJointUtilities::GetLinearProjection(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.LinearProjection > 0.0f) ? SolverSettings.LinearProjection : JointSettings.Motion.LinearProjection;
	}

	FReal FPBDJointUtilities::GetAngularProjection(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return (SolverSettings.AngularProjection > 0.0f) ? SolverSettings.AngularProjection : JointSettings.Motion.AngularProjection;
	}

	FVec3 FPBDJointUtilities::ConditionInertia(const FVec3& InI, const FReal MaxRatio)
	{
		FReal IMin = InI.Min();
		FReal IMax = InI.Max();
		if ((MaxRatio > 0) && (IMin > 0))
		{
			FReal Ratio = IMax / IMin;
			if (Ratio > MaxRatio)
			{
				FReal MinIMin = IMax / MaxRatio;
				return FVec3(
					FMath::Lerp(MinIMin, IMax, (InI.X - IMin) / (IMax - IMin)),
					FMath::Lerp(MinIMin, IMax, (InI.Y - IMin) / (IMax - IMin)),
					FMath::Lerp(MinIMin, IMax, (InI.Z - IMin) / (IMax - IMin)));
			}
		}
		return InI;
	}

	
	FVec3 FPBDJointUtilities::ConditionParentInertia(const FVec3& IParent, const FVec3& IChild, const FReal MinRatio)
	{
		if (MinRatio > 0)
		{
			FReal IParentMax = IParent.Max();
			FReal IChildMax = IChild.Max();
			if ((IParentMax > 0) && (IChildMax > 0))
			{
				FReal Ratio = IParentMax / IChildMax;
				if (Ratio < MinRatio)
				{
					FReal Multiplier = MinRatio / Ratio;
					return IParent * Multiplier;
				}
			}
		}
		return IParent;
	}

	
	FReal FPBDJointUtilities::ConditionParentMass(const FReal MParent, const FReal MChild, const FReal MinRatio)
	{
		if ((MinRatio > 0) && (MParent > 0) && (MChild > 0))
		{
			FReal Ratio = MParent / MChild;
			if (Ratio < MinRatio)
			{
				FReal Multiplier = MinRatio / Ratio;
				return MParent * Multiplier;
			}
		}
		return MParent;
	}

	
	void FPBDJointUtilities::GetConditionedInverseMass(
		const FReal InMParent, 
		const FVec3 InIParent, 
		const FReal InMChild, 
		const FVec3 InIChild, 
		FReal& OutInvMParent, 
		FReal& OutInvMChild, 
		FMatrix33& OutInvIParent, 
		FMatrix33& OutInvIChild, 
		const FReal MinParentMassRatio, 
		const FReal MaxInertiaRatio)
	{
		FReal MParent = ConditionParentMass(InMParent, InMChild, MinParentMassRatio);
		FReal MChild = InMChild;

		FVec3 IParent = ConditionInertia(InIParent, MaxInertiaRatio);
		FVec3 IChild = ConditionInertia(InIChild, MaxInertiaRatio);
		IParent = ConditionParentInertia(IParent, IChild, MinParentMassRatio);

		OutInvMParent = 0;
		OutInvIParent = FMatrix33(0, 0, 0);
		if (MParent > 0)
		{
			OutInvMParent = (FReal)1 / MParent;
			OutInvIParent = FMatrix33((FReal)1 / IParent.X, (FReal)1 / IParent.Y, (FReal)1 / IParent.Z);
		}

		OutInvMChild = 0;
		OutInvIChild = FMatrix33(0, 0, 0);
		if (MChild > 0)
		{
			OutInvMChild = (FReal)1 / MChild;
			OutInvIChild = FMatrix33((FReal)1 / IChild.X, (FReal)1 / IChild.Y, (FReal)1 / IChild.Z);
		}
	}


	
	void FPBDJointUtilities::GetConditionedInverseMass(
		const FReal InM0,
		const FVec3 InI0,
		FReal& OutInvM0, 
		FMatrix33& OutInvI0, 
		const FReal MaxInertiaRatio)
	{
		OutInvM0 = 0;
		OutInvI0 = FMatrix33(0, 0, 0);
		if (InM0 > 0)
		{
			FVec3 I0 = ConditionInertia(InI0, MaxInertiaRatio);
			OutInvM0 = (FReal)1 / InM0;
			OutInvI0 = FMatrix33((FReal)1 / I0.X, (FReal)1 / I0.Y, (FReal)1 / I0.Z);
		}
	}


	
	FVec3 FPBDJointUtilities::GetSphereLimitedPositionError(const FVec3& CX, const FReal Radius)
	{
		FReal CXLen = CX.Size();
		if (CXLen < Radius)
		{
			return FVec3(0, 0, 0);
		}
		else if (CXLen > SMALL_NUMBER)
		{
			FVec3 Dir = CX / CXLen;
			return CX - Radius * Dir;
		}
		return CX;
	}

	
	FVec3 FPBDJointUtilities::GetSphereLimitedVelocityError(const FVec3& CX, const FReal Radius, const FVec3& CV)
	{
		FReal CXLen = CX.Size();
		if (CXLen < Radius)
		{
			return FVec3(0, 0, 0);
		}
		else if (CXLen > SMALL_NUMBER)
		{
			FVec3 Dir = CX / CXLen;
			FReal CVDir = FVec3::DotProduct(CV, Dir);
			return FMath::Max((FReal)0, CVDir) * Dir;
		}
		return CV;
	}

	
	FVec3 FPBDJointUtilities::GetCylinderLimitedPositionError(const FVec3& InCX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion)
	{
		FVec3 CXAxis = FVec3::DotProduct(InCX, Axis) * Axis;
		FVec3 CXPlane = InCX - CXAxis;
		FReal CXPlaneLen = CXPlane.Size();
		if (AxisMotion == EJointMotionType::Free)
		{
			CXAxis = FVec3(0, 0, 0);
		}
		if (CXPlaneLen < Limit)
		{
			CXPlane = FVec3(0, 0, 0);
		}
		else if (CXPlaneLen > KINDA_SMALL_NUMBER)
		{
			FVec3 Dir = CXPlane / CXPlaneLen;
			CXPlane = CXPlane - Limit * Dir;
		}
		return CXAxis + CXPlane;
	}

	
	FVec3 FPBDJointUtilities::GetCylinderLimitedVelocityError(const FVec3& InCX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion, const FVec3& CV)
	{
		FVec3 CXAxis = FVec3::DotProduct(InCX, Axis) * Axis;
		FVec3 CXPlane = InCX - CXAxis;
		FReal CXPlaneLen = CXPlane.Size();

		FVec3 CVAxis = FVec3::DotProduct(CV, Axis) * Axis;;
		FVec3 CVPlane = CV - CVAxis;

		if (AxisMotion == EJointMotionType::Free)
		{
			CVAxis = FVec3(0, 0, 0);
		}
		if (CXPlaneLen < Limit)
		{
			CVPlane = FVec3(0, 0, 0);
		}
		else if (CXPlaneLen > KINDA_SMALL_NUMBER)
		{
			FVec3 Dir = CXPlane / CXPlaneLen;
			FReal CVDir = FVec3::DotProduct(CV, Dir);
			CVPlane = FMath::Max((FReal)0, CVDir) * Dir;
		}
		return CVAxis + CVPlane;
	}

	
	FVec3 FPBDJointUtilities::GetLineLimitedPositionError(const FVec3& CX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion)
	{
		FReal CXDist = FVec3::DotProduct(CX, Axis);
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

	
	FVec3 FPBDJointUtilities::GetLineLimitedVelocityError(const FVec3& CX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion, const FVec3& CV)
	{
		FReal CXDist = FVec3::DotProduct(CX, Axis);
		FReal CVAxis = FVec3::DotProduct(CV, Axis);
		if ((AxisMotion == EJointMotionType::Free) || (FMath::Abs(CXDist) < Limit))
		{
			return CV - CVAxis * Axis;
		}
		else if (CXDist >= Limit)
		{
			return CV - FMath::Min((FReal)0, CVAxis) * Axis;
		}
		else
		{
			return CV - FMath::Max((FReal)0, CVAxis) * Axis;
		}
	}


	
	FVec3 FPBDJointUtilities::GetLimitedPositionError(const FPBDJointSettings& JointSettings, const FRotation3& R0, const FVec3& InCX)
	{
		const TVector<EJointMotionType, 3>& Motion = JointSettings.Motion.LinearMotionTypes;
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
			FVec3 Axis = R0 * FVec3(1, 0, 0);
			return GetCylinderLimitedPositionError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[0]);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (Y Axis)
			FVec3 Axis = R0 * FVec3(0, 1, 0);
			return GetCylinderLimitedPositionError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[1]);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited))
		{
			// Circular Limit (Z Axis)
			FVec3 Axis = R0 * FVec3(0, 0, 1);
			return GetCylinderLimitedPositionError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[2]);
		}
		else
		{
			// Line/Square/Cube Limits (no way to author square or cube limits, but would work if we wanted it)
			FVec3 CX = InCX;
			if (Motion[0] != EJointMotionType::Locked)
			{
				FVec3 Axis = R0 * FVec3(1, 0, 0);
				CX = GetLineLimitedPositionError(CX, Axis, JointSettings.Motion.LinearLimit, Motion[0]);
			}
			if (Motion[1] != EJointMotionType::Locked)
			{
				FVec3 Axis = R0 * FVec3(0, 1, 0);
				CX = GetLineLimitedPositionError(CX, Axis, JointSettings.Motion.LinearLimit, Motion[1]);
			}
			if (Motion[2] != EJointMotionType::Locked)
			{
				FVec3 Axis = R0 * FVec3(0, 0, 1);
				CX = GetLineLimitedPositionError(CX, Axis, JointSettings.Motion.LinearLimit, Motion[2]);
			}
			return CX;
		}
	}

	
	FVec3 FPBDJointUtilities::GetLimitedVelocityError(const FPBDJointSettings& JointSettings, const FRotation3& R0, const FVec3& InCX, const FVec3& InCV)
	{
		const TVector<EJointMotionType, 3>& Motion = JointSettings.Motion.LinearMotionTypes;
		if ((Motion[0] == EJointMotionType::Locked) && (Motion[1] == EJointMotionType::Locked) && (Motion[2] == EJointMotionType::Locked))
		{
			return InCV;
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Spherical distance constraints
			return GetSphereLimitedVelocityError(InCX, JointSettings.Motion.LinearLimit, InCV);
		}
		else if ((Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (X Axis)
			FVec3 Axis = R0 * FVec3(1, 0, 0);
			return GetCylinderLimitedVelocityError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[0], InCV);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (Y Axis)
			FVec3 Axis = R0 * FVec3(0, 1, 0);
			return GetCylinderLimitedVelocityError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[1], InCV);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited))
		{
			// Circular Limit (Z Axis)
			FVec3 Axis = R0 * FVec3(0, 0, 1);
			return GetCylinderLimitedVelocityError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[2], InCV);
		}
		else
		{
			// Line/Square/Cube Limits (no way to author square or cube limits, but would work if we wanted it)
			FVec3 CV = InCV;
			if (Motion[0] != EJointMotionType::Locked)
			{
				FVec3 Axis = R0 * FVec3(1, 0, 0);
				CV = GetLineLimitedVelocityError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[0], CV);
			}
			if (Motion[1] != EJointMotionType::Locked)
			{
				FVec3 Axis = R0 * FVec3(0, 1, 0);
				CV = GetLineLimitedVelocityError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[1], CV);
			}
			if (Motion[2] != EJointMotionType::Locked)
			{
				FVec3 Axis = R0 * FVec3(0, 0, 1);
				CV = GetLineLimitedVelocityError(InCX, Axis, JointSettings.Motion.LinearLimit, Motion[2], CV);
			}
			return CV;
		}
	}

	
	void ApplyPositionDelta(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		FVec3& P0,
		FVec3& V0,
		FVec3& P1,
		FVec3& V1,
		const FVec3& DP0,
		const FVec3& DP1)
	{
		P0 = P0 + Stiffness * DP0;
		P1 = P1 + Stiffness * DP1;
		if (Dt > SMALL_NUMBER)
		{
			V0 = V0 + Stiffness * DP0 / Dt;
			V1 = V1 + Stiffness * DP1 / Dt;
		}
	}

	
	void ApplyVelocityDelta(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		FVec3& P0,
		FVec3& V0,
		FVec3& P1,
		FVec3& V1,
		const FVec3& DV0,
		const FVec3& DV1)
	{
		V0 = V0 + Stiffness * DV0;
		V1 = V1 + Stiffness * DV1;
		P0 = P0 + Stiffness * DV0 * Dt;
		P1 = P1 + Stiffness * DV1 * Dt;
	}

	
	void ApplyRotationDelta(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		FRotation3& Q0,
		FVec3& W0,
		FRotation3& Q1,
		FVec3& W1,
		const FVec3& DR0,
		const FVec3& DR1)
	{
		const FRotation3 DQ0 = (FRotation3::FromElements(Stiffness * DR0, 0) * Q0) * (FReal)0.5;
		const FRotation3 DQ1 = (FRotation3::FromElements(Stiffness * DR1, 0) * Q1) * (FReal)0.5;
		Q0 = (Q0 + DQ0).GetNormalized();
		Q1 = (Q1 + DQ1).GetNormalized();
		Q1.EnforceShortestArcWith(Q0);
		
		if (Dt > SMALL_NUMBER)
		{
			W0 = W0 + Stiffness * DR0 / Dt;
			W1 = W1 + Stiffness * DR1 / Dt;
		}
	}


	
	void ApplyRotationVelocityDelta(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		FRotation3& Q0,
		FVec3& W0,
		FRotation3& Q1,
		FVec3& W1,
		const FVec3& DW0,
		const FVec3& DW1)
	{
		W0 = W0 + Stiffness * DW0;
		W1 = W1 + Stiffness * DW1;

		const FRotation3 DQ0 = (FRotation3::FromElements(Stiffness * DW0 * Dt, 0) * Q0) * (FReal)0.5;
		const FRotation3 DQ1 = (FRotation3::FromElements(Stiffness * DW1 * Dt, 0) * Q1) * (FReal)0.5;
		Q0 = (Q0 + DQ0).GetNormalized();
		Q1 = (Q1 + DQ1).GetNormalized();
		Q1.EnforceShortestArcWith(Q0);
	}


	
	void ApplyRotationVelocityDelta(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		FRotation3& Q0,
		FVec3& W0,
		FRotation3& Q1,
		FVec3& W1,
		const FReal InvM0,
		const FMatrix33& InvIL0,
		const FReal InvM1,
		const FMatrix33& InvIL1,
		const FVec3& Axis0,
		const FVec3& Axis1,
		const FReal WC)
	{
		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Q0, InvIL0);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Q1, InvIL1);
		//const FReal I0 = FVec3::DotProduct(Axis0, Utilities::Multiply(InvI0, Axis0));
		//const FReal I1 = FVec3::DotProduct(Axis1, Utilities::Multiply(InvI1, Axis1));
		//const FVec3 DW0 = Axis0 * WC * I0 / (I0 + I1);
		//const FVec3 DW1 = -Axis1 * WC * I1 / (I0 + I1);
		const FReal L = (FReal)1 / (FVec3::DotProduct(Axis0, Utilities::Multiply(InvI0, Axis0)) + FVec3::DotProduct(Axis1, Utilities::Multiply(InvI1, Axis1)));
		const FVec3 DW0 = Utilities::Multiply(InvI0, Axis0) * L * WC;
		const FVec3 DW1 = -Utilities::Multiply(InvI1, Axis1) * L * WC;

		ApplyRotationVelocityDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, DW0, DW1);
	}

	
	void ApplyRotationDelta(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		FRotation3& Q0,
		FVec3& W0,
		FRotation3& Q1,
		FVec3& W1,
		const FReal InvM0,
		const FMatrix33& InvIL0,
		const FReal InvM1,
		const FMatrix33& InvIL1,
		const FVec3& Axis0,
		const FReal Angle0,
		const FVec3& Axis1,
		const FReal Angle1)
	{
		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Q0, InvIL0);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Q1, InvIL1);

		//const FReal I0 = FVec3::DotProduct(Axis0, Utilities::Multiply(InvI0, Axis0));
		//const FReal I1 = FVec3::DotProduct(Axis1, Utilities::Multiply(InvI1, Axis1));
		//const FVec3 DR0 = Axis0 * Angle0 * I0 / (I0 + I1);
		//const FVec3 DR1 = Axis1 * Angle1 * I1 / (I0 + I1);
		const FReal L = (FReal)1 / (FVec3::DotProduct(Axis0, Utilities::Multiply(InvI0, Axis0)) + FVec3::DotProduct(Axis1, Utilities::Multiply(InvI1, Axis1)));
		const FVec3 DR0 = Utilities::Multiply(InvI0, Axis0) * L * Angle0;
		const FVec3 DR1 = Utilities::Multiply(InvI1, Axis1) * L * Angle1;

		ApplyRotationDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, DR0, DR1);
	}

	
	void FPBDJointUtilities::CalculateSwingConstraintSpace(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		const FVec3& P0,
		const FRotation3& Q0,
		const FVec3& P1,
		const FRotation3& Q1,
		FVec3& OutX0,
		FMatrix33& OutR0,
		FVec3& OutX1,
		FMatrix33& OutR1,
		FVec3& OutCR)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 TwistAxis01;
		FReal TwistAngle = (FReal)0;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAngle = -TwistAngle;
		}

		const FMatrix33 Axes0 = R0.ToMatrix();
		const FMatrix33 Axes1 = R1.ToMatrix();

		FReal Swing1Angle = (FReal)0;
		const FVec3 SwingCross1 = FVec3::CrossProduct(Axes0.GetAxis((int32)EJointAngularConstraintIndex::Swing1), Axes1.GetAxis((int32)EJointAngularConstraintIndex::Swing1));
		const FReal SwingCross1Len = SwingCross1.Size();
		if (SwingCross1Len > KINDA_SMALL_NUMBER)
		{
			Swing1Angle = FMath::Asin(FMath::Clamp(SwingCross1Len, (FReal)0, (FReal)1));
		}
		const FReal Swing1Dot = FVec3::DotProduct(Axes0.GetAxis((int32)EJointAngularConstraintIndex::Swing1), Axes1.GetAxis((int32)EJointAngularConstraintIndex::Swing1));
		if (Swing1Dot < (FReal)0)
		{
			Swing1Angle = (FReal)PI - Swing1Angle;
		}

		FReal Swing2Angle = (FReal)0;
		const FVec3 SwingCross2 = FVec3::CrossProduct(Axes0.GetAxis((int32)EJointAngularConstraintIndex::Swing2), Axes1.GetAxis((int32)EJointAngularConstraintIndex::Swing2));
		const FReal SwingCross2Len = SwingCross2.Size();
		if (SwingCross2Len > KINDA_SMALL_NUMBER)
		{
			Swing2Angle = FMath::Asin(FMath::Clamp(SwingCross2Len, (FReal)0, (FReal)1));
		}
		const FReal Swing2Dot = FVec3::DotProduct(Axes0.GetAxis((int32)EJointAngularConstraintIndex::Swing2), Axes1.GetAxis((int32)EJointAngularConstraintIndex::Swing2));
		if (Swing2Dot < (FReal)0)
		{
			Swing2Angle = (FReal)PI - Swing2Angle;
		}

		OutX0 = X0;
		OutX1 = X1;
		OutR0 = R0.ToMatrix();
		OutR1 = R1.ToMatrix();
		OutCR[(int32)EJointAngularAxisIndex::Twist] = TwistAngle;
		OutCR[(int32)EJointAngularAxisIndex::Swing1] = Swing1Angle;
		OutCR[(int32)EJointAngularAxisIndex::Swing2] = Swing2Angle;
	}

	
	void FPBDJointUtilities::CalculateConeConstraintSpace(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		const FVec3& P0,
		const FRotation3& Q0,
		const FVec3& P1,
		const FRotation3& Q1,
		FVec3& OutX0,
		FMatrix33& OutR0, 
		FVec3& OutX1, 
		FMatrix33& OutR1, 
		FVec3& OutCR)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}
		FVec3 TwistAxis0 = R0 * TwistAxis01;
		FVec3 TwistAxis1 = R1 * TwistAxis01;

		FVec3 SwingAxis01;
		FReal SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, FJointConstants::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (FReal)2 * PI;
		}
		FVec3 SwingAxis0 = R0 * SwingAxis01;
		FVec3 SwingAxis1 = SwingAxis0;

		OutX0 = X0;
		OutX1 = X1;
		OutR0.SetAxis((int32)EJointAngularAxisIndex::Twist, TwistAxis0);
		OutR0.SetAxis((int32)EJointAngularAxisIndex::Swing1, SwingAxis0);
		OutR0.SetAxis((int32)EJointAngularAxisIndex::Swing2, FVec3::CrossProduct(SwingAxis0, TwistAxis0));
		OutR1.SetAxis((int32)EJointAngularAxisIndex::Twist, TwistAxis1);
		OutR1.SetAxis((int32)EJointAngularAxisIndex::Swing1, SwingAxis1);
		OutR1.SetAxis((int32)EJointAngularAxisIndex::Swing2, FVec3::CrossProduct(SwingAxis1, TwistAxis1));
		OutCR[(int32)EJointAngularAxisIndex::Twist] = TwistAngle;
		OutCR[(int32)EJointAngularAxisIndex::Swing1] = SwingAngle;
		OutCR[(int32)EJointAngularAxisIndex::Swing2] = (FReal)0;
	}

	
	void FPBDJointUtilities::ApplyJointPositionConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{

		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Q0, InvIL0);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Q1, InvIL1);

		// Calculate constraint error
		const FVec3 CX = GetLimitedPositionError(JointSettings, R0, X1 - X0);

		// Calculate constraint correction
		FMatrix33 M0 = FMatrix33(0, 0, 0);
		FMatrix33 M1 = FMatrix33(0, 0, 0);
		if (InvM0 > 0)
		{
			M0 = Utilities::ComputeJointFactorMatrix(X0 - P0, InvI0, InvM0);
		}
		if (InvM1 > 0)
		{
			M1 = Utilities::ComputeJointFactorMatrix(X1 - P1, InvI1, InvM1);
		}
		const FMatrix33 MI = (M0 + M1).Inverse();
		const FVec3 DX = Utilities::Multiply(MI, CX);

		// Apply constraint correction
		const FVec3 DP0 = InvM0 * DX;
		const FVec3 DP1 = -InvM1 * DX;
		const FVec3 DR0 = Utilities::Multiply(InvI0, FVec3::CrossProduct(X0 - P0, DX));
		const FVec3 DR1 = Utilities::Multiply(InvI1, FVec3::CrossProduct(X1 - P1, -DX));

		ApplyPositionDelta(Dt, SolverSettings, JointSettings, Stiffness, P0, V0, P1, V1, DP0, DP1);
		ApplyRotationDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, DR0, DR1);
	}

	
	void FPBDJointUtilities::ApplyJointVelocityConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 XC0 = Q0 * XL0.GetTranslation();
		const FVec3 XC1 = Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Q0, InvIL0);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Q1, InvIL1);

		const FVec3 VC0 = V0 + FVec3::CrossProduct(W0, XC0);
		const FVec3 VC1 = V1 + FVec3::CrossProduct(W1, XC1);
		const FVec3 VC = GetLimitedVelocityError(JointSettings, R0, (P1 + XC1) - (P0 + XC0), VC1 - VC0);

		// Calculate constraint correction
		FMatrix33 M0 = FMatrix33(0, 0, 0);
		FMatrix33 M1 = FMatrix33(0, 0, 0);
		if (InvM0 > 0)
		{
			M0 = Utilities::ComputeJointFactorMatrix(XC0, InvI0, InvM0);
		}
		if (InvM1 > 0)
		{
			M1 = Utilities::ComputeJointFactorMatrix(XC1, InvI1, InvM1);
		}
		const FMatrix33 MI = (M0 + M1).Inverse();
		const FVec3 DL = Utilities::Multiply(MI, VC);

		// Apply constraint correction
		const FVec3 DV0 = InvM0 * DL;
		const FVec3 DV1 = -InvM1 * DL;
		const FVec3 DW0 = Utilities::Multiply(InvI0, FVec3::CrossProduct(XC0, DL));
		const FVec3 DW1 = -Utilities::Multiply(InvI1, FVec3::CrossProduct(XC1, DL));

		ApplyVelocityDelta(Dt, SolverSettings, JointSettings, Stiffness, P0, V0, P1, V1, DV0, DV1);
		ApplyRotationVelocityDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, DW0, DW1);
	}


	
	void FPBDJointUtilities::ApplyJointTwistConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		// Calculate the Twist Axis and Angle for each body
		const FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}

		const FVec3 TwistAxis0 = R0 * TwistAxis01;
		const FVec3 TwistAxis1 = R1 * TwistAxis01;
		FReal TwistAngleMax = FLT_MAX;
		if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Limited)
		{
			TwistAngleMax = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Twist];
		}
		else if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Locked)
		{
			TwistAngleMax = 0;
		}

		// Calculate the twist correction to apply to each body
		FReal DTwistAngle = 0;
		if (TwistAngle > TwistAngleMax)
		{
			DTwistAngle = TwistAngle - TwistAngleMax;
		}
		else if (TwistAngle < -TwistAngleMax)
		{
			DTwistAngle = TwistAngle + TwistAngleMax;
		}
		const FReal DTwistAngle0 = DTwistAngle;
		const FReal DTwistAngle1 = -DTwistAngle;

		// Apply twist correction
		ApplyRotationDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, InvM0, InvIL0, InvM1, InvIL1, TwistAxis0, DTwistAngle0, TwistAxis1, DTwistAngle1);
	}

	
	void FPBDJointUtilities::ApplyJointTwistVelocityConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		// Calculate the Twist Axis and Angle for each body
		const FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}

		const FVec3 TwistAxis0 = R0 * TwistAxis01;
		const FVec3 TwistAxis1 = R1 * TwistAxis01;

		FReal TwistAngleMax = FLT_MAX;
		if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Limited)
		{
			TwistAngleMax = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Twist];
		}
		else if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Locked)
		{
			TwistAngleMax = 0;
		}

		const FReal WC0 = FVec3::DotProduct(W0, TwistAxis0);
		const FReal WC1 = FVec3::DotProduct(W1, TwistAxis1);
		FReal DW = (FReal)0;
		if (TwistAngle > TwistAngleMax)
		{
			DW = FMath::Max((FReal)0, WC1 - WC0);
		}
		else if (TwistAngle < -TwistAngleMax)
		{
			DW = FMath::Min((FReal)0, WC1 - WC0);
		}

		ApplyRotationVelocityDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, InvM0, InvIL0, InvM1, InvIL1, TwistAxis0, TwistAxis1, DW);
	}


	
	void FPBDJointUtilities::ApplyJointConeConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		// Calculate Swing axis for each body
		const FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 SwingAxis01;
		FReal SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, FJointConstants::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (FReal)2 * PI;
		}

		const FVec3 SwingAxis0 = R0 * SwingAxis01;
		const FVec3 SwingAxis1 = SwingAxis0;

		// Calculate swing limit for the current swing axis
		FReal SwingAngleMax = FLT_MAX;
		const FReal Swing1Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];

		// Circular swing limit
		SwingAngleMax = Swing1Limit;

		// Elliptical swing limit
		if (!FMath::IsNearlyEqual(Swing1Limit, Swing2Limit, KINDA_SMALL_NUMBER))
		{
			// Map swing axis to ellipse and calculate limit for this swing axis
			const FReal DotSwing1 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing1Axis()));
			const FReal DotSwing2 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing2Axis()));
			SwingAngleMax = FMath::Sqrt(Swing1Limit * DotSwing2 * Swing1Limit * DotSwing2 + Swing2Limit * DotSwing1 * Swing2Limit * DotSwing1);
		}

		// Calculate swing error we need to correct
		FReal DSwingAngle = 0;
		if (SwingAngle > SwingAngleMax)
		{
			DSwingAngle = SwingAngle - SwingAngleMax;
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			DSwingAngle = SwingAngle + SwingAngleMax;
		}
		FReal DSwingAngle0 = DSwingAngle;
		FReal DSwingAngle1 = -DSwingAngle;

		// Apply swing correction
		ApplyRotationDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, InvM0, InvIL0, InvM1, InvIL1, SwingAxis0, DSwingAngle0, SwingAxis1, DSwingAngle1);
	}

	
	void FPBDJointUtilities::ApplyJointConeVelocityConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		// Calculate Swing axis for each body
		const FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 SwingAxis01;
		FReal SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, FJointConstants::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (FReal)2 * PI;
		}

		const FVec3 SwingAxis0 = R0 * SwingAxis01;
		const FVec3 SwingAxis1 = SwingAxis0;

		// Calculate swing limit for the current swing axis
		FReal SwingAngleMax = FLT_MAX;
		const FReal Swing1Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];

		// Circular swing limit
		SwingAngleMax = Swing1Limit;

		// Elliptical swing limit
		if (!FMath::IsNearlyEqual(Swing1Limit, Swing2Limit, KINDA_SMALL_NUMBER))
		{
			// Map swing axis to ellipse and calculate limit for this swing axis
			const FReal DotSwing1 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing1Axis()));
			const FReal DotSwing2 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing2Axis()));
			SwingAngleMax = FMath::Sqrt(Swing1Limit * DotSwing2 * Swing1Limit * DotSwing2 + Swing2Limit * DotSwing1 * Swing2Limit * DotSwing1);
		}

		// Only clamp veloicity if we are outside the limits and moving to increase the error
		const FReal WC0 = FVec3::DotProduct(W0, SwingAxis0);
		const FReal WC1 = FVec3::DotProduct(W1, SwingAxis1);
		FReal DW = 0;
		if (SwingAngle > SwingAngleMax)
		{
			DW = FMath::Max((FReal)0, WC1 - WC0);
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			DW = FMath::Min((FReal)0, WC1 - WC0);
		}

		ApplyRotationVelocityDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, InvM0, InvIL0, InvM1, InvIL1, SwingAxis0, SwingAxis1, DW);
	}


	
	void FPBDJointUtilities::ApplyJointSwingConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const EJointAngularAxisIndex SwingAxisIndex,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		// Calculate the swing axis for each body
		const FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}
		const FVec3 TwistAxis = R0 * TwistAxis01;

		const FRotation3 R1NoTwist = R1 * R01Twist.Inverse();
		const FMatrix33 Axes0 = R0.ToMatrix();
		const FMatrix33 Axes1 = R1NoTwist.ToMatrix();
		FVec3 SwingCross = FVec3::CrossProduct(Axes0.GetAxis((int32)SwingAxisIndex), Axes1.GetAxis((int32)SwingAxisIndex));
		SwingCross = SwingCross - FVec3::DotProduct(TwistAxis, SwingCross) * TwistAxis;
		const FReal SwingCrossLen = SwingCross.Size();
		if (SwingCrossLen > KINDA_SMALL_NUMBER)
		{
			const FVec3 SwingAxis = SwingCross / SwingCrossLen;
			FVec3 SwingAxis0 = SwingAxis;
			FVec3 SwingAxis1 = SwingAxis;

			FReal SwingAngle = FMath::Asin(FMath::Clamp(SwingCrossLen, (FReal)0, (FReal)1));
			const FReal SwingDot = FVec3::DotProduct(Axes0.GetAxis((int32)SwingAxisIndex), Axes1.GetAxis((int32)SwingAxisIndex));
			if (SwingDot < (FReal)0)
			{
				SwingAngle = (FReal)PI - SwingAngle;
			}

			FReal SwingAngleMax = FLT_MAX;
			if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraintIndex] == EJointMotionType::Limited)
			{
				FReal SwingLimit = JointSettings.Motion.AngularLimits[(int32)SwingConstraintIndex];
				SwingAngleMax = SwingLimit;
			}
			else if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraintIndex] == EJointMotionType::Locked)
			{
				SwingAngleMax = 0;
			}

			// Calculate swing error we need to correct
			FReal DSwingAngle = 0;
			if (SwingAngle > SwingAngleMax)
			{
				DSwingAngle = SwingAngle - SwingAngleMax;
			}
			else if (SwingAngle < -SwingAngleMax)
			{
				DSwingAngle = SwingAngle + SwingAngleMax;
			}
			FReal DSwingAngle0 = DSwingAngle;
			FReal DSwingAngle1 = -DSwingAngle;

			// Apply swing correction
			ApplyRotationDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, InvM0, InvIL0, InvM1, InvIL1, SwingAxis0, DSwingAngle0, SwingAxis1, DSwingAngle1);
		}
	}

	
	void FPBDJointUtilities::ApplyJointSwingVelocityConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const EJointAngularAxisIndex SwingAxisIndex,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		// Calculate the swing axis for each body
		const FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}
		const FVec3 TwistAxis = R0 * TwistAxis01;

		const FRotation3 R1NoTwist = R1 * R01Twist.Inverse();
		const FMatrix33 Axes0 = R0.ToMatrix();
		const FMatrix33 Axes1 = R1NoTwist.ToMatrix();
		FVec3 SwingCross = FVec3::CrossProduct(Axes0.GetAxis((int32)SwingAxisIndex), Axes1.GetAxis((int32)SwingAxisIndex));
		SwingCross = SwingCross - FVec3::DotProduct(TwistAxis, SwingCross) * TwistAxis;
		const FReal SwingCrossLen = SwingCross.Size();
		if (SwingCrossLen > KINDA_SMALL_NUMBER)
		{
			const FVec3 SwingAxis = SwingCross / SwingCrossLen;
			FVec3 SwingAxis0 = SwingAxis;
			FVec3 SwingAxis1 = SwingAxis;

			FReal SwingAngle = FMath::Asin(FMath::Clamp(SwingCrossLen, (FReal)0, (FReal)1));
			const FReal SwingDot = FVec3::DotProduct(Axes0.GetAxis((int32)SwingAxisIndex), Axes1.GetAxis((int32)SwingAxisIndex));
			if (SwingDot < (FReal)0)
			{
				SwingAngle = (FReal)PI - SwingAngle;
			}

			FReal SwingAngleMax = FLT_MAX;
			if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraintIndex] == EJointMotionType::Limited)
			{
				FReal Swing1Limit = JointSettings.Motion.AngularLimits[(int32)SwingConstraintIndex];
				SwingAngleMax = Swing1Limit;
			}
			else if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraintIndex] == EJointMotionType::Locked)
			{
				SwingAngleMax = 0;
			}

			// Only clamp veloicity if we are outside the limits and moving to increase the error
			const FReal WC0 = FVec3::DotProduct(W0, SwingAxis0);
			const FReal WC1 = FVec3::DotProduct(W1, SwingAxis1);
			FReal DW = 0;
			if (SwingAngle > SwingAngleMax)
			{
				DW = FMath::Max((FReal)0, WC1 - WC0);
			}
			else if (SwingAngle < -SwingAngleMax)
			{
				DW = FMath::Min((FReal)0, WC1 - WC0);
			}

			ApplyRotationVelocityDelta(Dt, SolverSettings, JointSettings, Stiffness, Q0, W0, Q1, W1, InvM0, InvIL0, InvM1, InvIL1, SwingAxis0, SwingAxis1, DW);
		}
	}

	
	void FPBDJointUtilities::ApplyJointTwistDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		const FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
			TwistAngle = -TwistAngle;
		}

		const FVec3 TwistAxis0 = R0 * TwistAxis01;
		const FVec3 TwistAxis1 = R1 * TwistAxis01;
		const FReal TwistAngleTarget = JointSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Twist];
		const FReal DriveStiffnessUnclamped = (SolverSettings.DriveStiffness > 0) ? SolverSettings.DriveStiffness : JointSettings.Motion.AngularDriveStiffness;
		const FReal DriveStiffness = FMath::Clamp(DriveStiffnessUnclamped, (FReal)0, (FReal)1);
		const FReal DTwistAngle = TwistAngle - TwistAngleTarget;
		const FReal DTwistAngle0 = DTwistAngle;
		const FReal DTwistAngle1 = -DTwistAngle;

		ApplyRotationDelta(Dt, SolverSettings, JointSettings, DriveStiffness, Q0, W0, Q1, W1, InvM0, InvIL0, InvM1, InvIL1, TwistAxis0, DTwistAngle0, TwistAxis1, DTwistAngle1);
	}

	
	void FPBDJointUtilities::ApplyJointConeDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		// Calculate Swing axis for each body
		const FRotation3 R01 = R0.Inverse() * R1;
		FRotation3 R01Twist, R01Swing;
		R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
		R01Swing = R01Swing.GetNormalized();
		R01Twist = R01Twist.GetNormalized();

		FVec3 SwingAxis01;
		FReal SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, FJointConstants::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (FReal)2 * PI;
		}

		const FVec3 SwingAxis0 = R0 * SwingAxis01;
		const FVec3 SwingAxis1 = SwingAxis0;

		// Circular swing target (max of Swing1, Swing2 targets)
		FReal Swing1Target = JointSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing1];
		FReal Swing2Target = JointSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing2];
		FReal SwingAngleTarget = FMath::Max(Swing1Target, Swing2Target);

		FReal DriveStiffnessUnclamped = (SolverSettings.DriveStiffness > 0) ? SolverSettings.DriveStiffness : JointSettings.Motion.AngularDriveStiffness;
		FReal DriveStiffness = FMath::Clamp(DriveStiffnessUnclamped, (FReal)0, (FReal)1);
		FReal DSwingAngle = SwingAngle - SwingAngleTarget;
		FReal DSwingAngle0 = DSwingAngle;
		FReal DSwingAngle1 = -DSwingAngle;

		// Apply swing correction
		ApplyRotationDelta(Dt, SolverSettings, JointSettings, DriveStiffness, Q0, W0, Q1, W1, InvM0, InvIL0, InvM1, InvIL1, SwingAxis0, DSwingAngle0, SwingAxis1, DSwingAngle1);
	}

	
	void FPBDJointUtilities::ApplyJointSLerpDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& V0,
		FVec3& W0,
		FVec3& P1,
		FRotation3& Q1,
		FVec3& V1,
		FVec3& W1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 DX0 = Q0 * XL0.GetTranslation();
		const FVec3 DX1 = Q1 * XL1.GetTranslation();
		const FVec3 X0 = P0 + DX0;
		const FVec3 X1 = P1 + DX1;
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FRotation3 R1 = Q1 * XL1.GetRotation();

		// Calculate the rotation we need to apply to resolve the rotation delta
		const FRotation3 TargetR1 = R0 * JointSettings.Motion.AngularDriveTarget;
		const FRotation3 DR1 = TargetR1 * R1.Inverse();
		const FRotation3 TargetQ0 = DR1.Inverse() * Q0;
		const FRotation3 TargetQ1 = DR1 * Q1;

		FReal DriveStiffnessUnclamped = (SolverSettings.DriveStiffness > 0) ? SolverSettings.DriveStiffness : JointSettings.Motion.AngularDriveStiffness;
		FReal DriveStiffness = FMath::Clamp(DriveStiffnessUnclamped, (FReal)0, (FReal)1);

		FVec3 SLerpAxis;
		FReal SLerpAngle;
		if (DR1.ToAxisAndAngleSafe(SLerpAxis, SLerpAngle, FVec3(1, 0, 0)))
		{
			const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Q0, InvIL0);
			const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Q1, InvIL1);
			const FReal I0 = FVec3::DotProduct(SLerpAxis, Utilities::Multiply(InvI0, SLerpAxis));
			const FReal I1 = FVec3::DotProduct(SLerpAxis, Utilities::Multiply(InvI1, SLerpAxis));
			const FReal F0 = DriveStiffness * I0 / (I0 + I1);
			const FReal F1 = DriveStiffness * I1 / (I0 + I1);

			// Apply the rotation delta about the connector
			Q0 = FRotation3::Slerp(Q0, TargetQ0, F0);
			Q1 = FRotation3::Slerp(Q1, TargetQ1, F1);
			Q1.EnforceShortestArcWith(Q0);

			// @todo(ccaulfield): this does not take into account the fact that some linear dofs may be inactive
			const FVec3 X0_2 = P0 + Q0 * XL0.GetTranslation();
			const FVec3 X1_2 = P1 + Q1 * XL1.GetTranslation();
			const FVec3 Delta = (X1_2 - X0_2) - (X1 - X0);
			P0 += (InvM0 / (InvM0 + InvM1)) * Delta;
			P1 -= (InvM1 / (InvM0 + InvM1)) * Delta;
		}
	}

	
	void FPBDJointUtilities::ApplyJointPositionProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& P1,
		FRotation3& Q1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		const FVec3 X0 = P0 + Q0 * XL0.GetTranslation();
		const FVec3 X1 = P1 + Q1 * XL1.GetTranslation();
		const FRotation3 R0 = Q0 * XL0.GetRotation();
		const FVec3 CX = GetLimitedPositionError(JointSettings, R0, X1 - X0);

		const FVec3 DP0 = CX * (Stiffness * InvM0 / (InvM0 + InvM1));
		const FVec3 DP1 = -CX * (Stiffness* InvM1 / (InvM0 + InvM1));
		P0 = P0 + DP0;
		P1 = P1 + DP1;
	}

	
	void FPBDJointUtilities::ApplyJointTwistProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& P1,
		FRotation3& Q1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		FVec3 V0(0, 0, 0);
		FVec3 W0(0, 0, 0);
		FVec3 V1(0, 0, 0);
		FVec3 W1(0, 0, 0);
		ApplyJointTwistConstraint(Dt, SolverSettings, JointSettings, Stiffness, XL0, XL1, P0, Q0, V0, W0, P1, Q1, V1, W1, InvM0, InvIL0, InvM1, InvIL1);
	}

	
	void FPBDJointUtilities::ApplyJointConeProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& P1,
		FRotation3& Q1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		FVec3 V0(0, 0, 0);
		FVec3 W0(0, 0, 0);
		FVec3 V1(0, 0, 0);
		FVec3 W1(0, 0, 0);
		ApplyJointConeConstraint(Dt, SolverSettings, JointSettings, Stiffness, XL0, XL1, P0, Q0, V0, W0, P1, Q1, V1, W1, InvM0, InvIL0, InvM1, InvIL1);
	}

	
	void FPBDJointUtilities::ApplyJointSwingProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const EJointAngularAxisIndex SwingAxisIndex,
		FVec3& P0,
		FRotation3& Q0,
		FVec3& P1,
		FRotation3& Q1,
		FReal InvM0,
		const FMatrix33& InvIL0,
		FReal InvM1,
		const FMatrix33& InvIL1)
	{
		FVec3 V0(0, 0, 0);
		FVec3 W0(0, 0, 0);
		FVec3 V1(0, 0, 0);
		FVec3 W1(0, 0, 0);
		ApplyJointSwingConstraint(Dt, SolverSettings, JointSettings, Stiffness, XL0, XL1, SwingConstraintIndex, SwingAxisIndex, P0, Q0, V0, W0, P1, Q1, V1, W1, InvM0, InvIL0, InvM1, InvIL1);
	}



}
