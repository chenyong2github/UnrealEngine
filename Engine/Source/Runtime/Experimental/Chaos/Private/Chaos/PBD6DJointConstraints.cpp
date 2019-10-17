// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBD6DJointConstraints.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Utilities.h"
#include "ChaosLog.h"

#include "HAL/IConsoleManager.h"

//#pragma optimize("", off)

using namespace Chaos;

//
// Constraint Handle
//

template<typename T, int d>
TPBD6DJointConstraintHandle<T, d>::TPBD6DJointConstraintHandle()
{
}

template<typename T, int d>
TPBD6DJointConstraintHandle<T, d>::TPBD6DJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : TContainerConstraintHandle<TPBD6DJointConstraints<T, d>>(InConstraintContainer, InConstraintIndex)
{
}

template<typename T, int d>
void TPBD6DJointConstraintHandle<T, d>::CalculateConstraintSpace(TVector<T, d>& OutXa, PMatrix<T, d, d>& OutRa, TVector<T, d>& OutXb, PMatrix<T, d, d>& OutRb, TVector<T, d>& OutCR) const
{
	ConstraintContainer->CalculateConstraintSpace(ConstraintIndex, OutXa, OutRa, OutXb, OutRb, OutCR);
}

template<typename T, int d>
void TPBD6DJointConstraintHandle<T, d>::SetParticleLevels(const TVector<int32, 2>& ParticleLevels)
{
	ConstraintContainer->SetParticleLevels(ConstraintIndex, ParticleLevels);
}

template<typename T, int d>
int32 TPBD6DJointConstraintHandle<T, d>::GetConstraintLevel() const
{
	return ConstraintContainer->GetConstraintLevel(ConstraintIndex);
}

//
// Constraint Math
//

/**
	* Invert a 2x2 block of square matrices using block-wise inversion
	* https://en.wikipedia.org/wiki/Invertible_matrix#Blockwise_inversion
	*
	*	| AI		BI	|	=	| A		B |^-1
	*	| CI		DI	|		| C		D |
	*
	*/
template<typename T, int d>
void TPBD6DJointConstraintUtilities<T, d>::BlockwiseInverse(const PMatrix<T, d, d>& A, const PMatrix<T, d, d>& B, const PMatrix<T, d, d>& C, const PMatrix<T, d, d>& D, PMatrix<T, d, d>& AI, PMatrix<T, d, d>& BI, PMatrix<T, d, d>& CI, PMatrix<T, d, d>& DI)
{
	PMatrix<T, d, d> AInv = A.Inverse();
	PMatrix<T, d, d> ZInv = (D - Utilities::Multiply(C, Utilities::Multiply(AInv, B))).Inverse();
	AI = AInv + Utilities::Multiply(AInv, Utilities::Multiply(B, Utilities::Multiply(ZInv, Utilities::Multiply(C, AInv))));
	BI = -Utilities::Multiply(AInv, Utilities::Multiply(B, ZInv));
	CI = -Utilities::Multiply(ZInv, Utilities::Multiply(C, AInv));
	DI = ZInv;
}

template<typename T, int d>
void TPBD6DJointConstraintUtilities<T, d>::BlockwiseInverse2(const PMatrix<T, d, d>& A, const PMatrix<T, d, d>& B, const PMatrix<T, d, d>& C, const PMatrix<T, d, d>& D, PMatrix<T, d, d>& AI, PMatrix<T, d, d>& BI, PMatrix<T, d, d>& CI, PMatrix<T, d, d>& DI)
{
	PMatrix<T, d, d> DInv = D.Inverse();
	PMatrix<T, d, d> ZInv = (A - Utilities::Multiply(B, Utilities::Multiply(DInv, C))).Inverse();
	AI = ZInv;
	BI = -Utilities::Multiply(ZInv, Utilities::Multiply(B, DInv));
	CI = -Utilities::Multiply(DInv, Utilities::Multiply(C, ZInv));
	DI = DInv + Utilities::Multiply(DInv, Utilities::Multiply(C, Utilities::Multiply(ZInv, Utilities::Multiply(B, DInv))));
}

/**
 * This function returns:
 *
 *	F(X,R)	=	[J . M^-1 . J^t]
 *
 * for a single body, which is the (6x6) matrix that is the reciprocal component of the lambda matrix:
 *
 *	L(X,R)	=	(1 / [J . M^-1 . J^t]) . C	=	(1 / F) . C
 *
 * (although the reciprocal part is the sum of F for both bodies). Lambda is part of the constraint correction calculation:
 *
 *	D(X,R)	=	M^-1 . J . L
 *
 * F is returned as 4 3x3 matrices F00...F11 to avoid the need for a 6x6 matrix (plus, the problem splits nicely into
 * 3x3 matrices for position and rotation degrees of freedom, although it is wasteful if we are not using all 3 rotation constraints...)
 *
 * F	=	| F00	F01	|
 * 			| F10	F11	|
 *
 */
template<typename T, int d>
void TPBD6DJointConstraintUtilities<T, d>::ComputeJointFactorMatrix(const PMatrix<T, d, d>& XR, const PMatrix<T, d, d>& RR, float MInv, const PMatrix<T, d, d>& IInv, PMatrix<T, d, d>& M00, PMatrix<T, d, d>& M01, PMatrix<T, d, d>& M10, PMatrix<T, d, d>& M11)
{
	// XR(3x3)	= dCX/DR: derivative of position constraint error with respect to rotation. 
	//			= Cross-product matrix of X (the world-space offset of the constraint)
	// RR(3x3)	= dCR/DR: derivative of world-space rotation constraint error with respect to rotation.
	// 			=	|	(Swing1a x Swing2b)	|
	//				|	(Swing1a x Twistb)	|
	//				|	(Swing2a x Twistb)	|
	//
	// A^t = Transpose(A)
	//
	//	MInv	=	|	1(3x3).MInv		0(3x3)		|
	//	(6x6)		|	0(3x3)			IInv		|
	//
	// Jacobian:
	//
	//	J(C(X,R)) =	|	dCX/dX			dCX/dR		|
	//	(6x6)		|	dCR/dX			dCR/dR		|
	//
	//			=	|	1(3x3)			-XR			|
	//				|	0(3x3)			RR			|
	//
	// FactorMatrix:
	//
	//	F		=	J.MInv.J^t
	//	(6x6)
	//			=	|	1/Ma(3x3) - XR.IInv.XR		-XR.IInv.RR^t	|
	//				|	(-XR.IInv.RR^t)^t			RR.IInv.RR^t	|
	//
	// @todo(ccaulfield): optimize
	PMatrix<T, d, d> IXR = Utilities::Multiply(IInv, XR);
	PMatrix<T, d, d> IRR = Utilities::Multiply(IInv, RR.GetTransposed());
	M00 = PMatrix<T, d, d>(MInv, MInv, MInv) - Utilities::Multiply(XR, IXR);
	M01 = -Utilities::Multiply(XR, IRR);
	M10 = M01.GetTransposed();
	M11 = Utilities::Multiply(RR, IRR);
}

template<typename T, int d>
TVector<T, d> TPBD6DJointConstraintUtilities<T, d>::Calculate6dConstraintAngles(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TRotation<T, d>& Ra,
	const TRotation<T, d>& Rb, 
	const TPBD6DJointMotionSettings<T, d>& RSettings)
{
	// @todo(ccaulfield): optimize
	TVector<T, d> CR;
	PMatrix<T, d, d> RRa, RRb;
	Calculate6dConstraintRotation(SolverSettings, Ra, Rb, RSettings, CR, RRa, RRb);
	return CR;
}

template<typename T, int d>
bool TPBD6DJointConstraintUtilities<T, d>::Calculate6dConstraintRotation_SwingFixed(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TRotation<T, d>& Ra,
	const TRotation<T, d>& Rb, 
	const TPBD6DJointMotionSettings<T, d>& MotionSettings,
	TVector<T, d>& CR, 
	PMatrix<T, d, d>& RRa, 
	PMatrix<T, d, d>& RRb)
{
	// @todo(ccaulfield): optimize

	// Get the transform from A to B, and use it to generate twist angles.
	TRotation<T, d> Rab = Ra.Inverse() * Rb;
	TRotation<T, d> RTwist, RSwing;
	Rab.ToSwingTwist(T6DJointConstants<T, d>::TwistAxis(), RSwing, RTwist);
	RSwing = RSwing.GetNormalized();
	RTwist = RTwist.GetNormalized();

	TVector<T, d> TwistAxisAB;
	T TwistAngleAB;
	RTwist.ToAxisAndAngleSafe(TwistAxisAB, TwistAngleAB, T6DJointConstants<T, d>::TwistAxis());
	if (TwistAngleAB > PI)
	{
		TwistAngleAB = TwistAngleAB - (T)2 * PI;
		RTwist = TRotation<T, d>::FromAxisAngle(T6DJointConstants<T, d>::TwistAxis(), TwistAngleAB);
	}

	PMatrix<T, d, d> Axesa = Ra.ToMatrix();
	PMatrix<T, d, d> Axesb = Rb.ToMatrix();

	// Constraint-space in body A is just the constraint transform
	TVector<T, d> Twista = Axesa.GetAxis((int32)E6DJointAngularAxisIndex::Twist);
	TVector<T, d> Swing1a = Axesa.GetAxis((int32)E6DJointAngularAxisIndex::Swing1);
	TVector<T, d> Swing2a = Axesa.GetAxis((int32)E6DJointAngularAxisIndex::Swing2);

	// Remove Twist from body B's swing axes
	TVector<T, d> Twistb = Axesb.GetAxis((int32)E6DJointAngularAxisIndex::Twist);
	TVector<T, d> Swing1b = Rb * RTwist.Inverse() * T6DJointConstants<T, d>::Swing1Axis();
	TVector<T, d> Swing2b = TVector<T, d>::CrossProduct(Swing1b, Twistb);

	RRa.SetRow((int32)E6DJointAngularConstraintIndex::Twist, Twista);
	RRa.SetRow((int32)E6DJointAngularConstraintIndex::Swing1, Swing1a);
	RRa.SetRow((int32)E6DJointAngularConstraintIndex::Swing2, Swing2a);
	RRb.SetRow((int32)E6DJointAngularConstraintIndex::Twist, Twistb);
	RRb.SetRow((int32)E6DJointAngularConstraintIndex::Swing1, Swing1b);
	RRb.SetRow((int32)E6DJointAngularConstraintIndex::Swing2, Swing2b);

	T TwistAngle = TwistAngleAB * TVector<T, d>::DotProduct(TwistAxisAB, T6DJointConstants<T, d>::TwistAxis());
	T Swing1Angle = FMath::Asin(FMath::Clamp(TVector<T, d>::DotProduct(TVector<T, d>::CrossProduct(Swing2a, Swing2b), Swing1a), (T)-1, (T)1));
	T Swing2Angle = FMath::Asin(FMath::Clamp(TVector<T, d>::DotProduct(TVector<T, d>::CrossProduct(Swing1a, Swing1b), Swing2a), (T)-1, (T)1));

	CR[(int32)E6DJointAngularConstraintIndex::Twist] = TwistAngle;
	CR[(int32)E6DJointAngularConstraintIndex::Swing1] = Swing1Angle;
	CR[(int32)E6DJointAngularConstraintIndex::Swing2] = Swing2Angle;

	// If we're flipped 180 degrees about swing, just pretend the error is zero
	T DotTT = TVector<T, d>::DotProduct(Twista, Twistb);
	const T MinDotTT = (T)-1 + SolverSettings.InvertedAxisTolerance;
	if (DotTT < MinDotTT)
	{
		return false;
	}

	return true;
}

template<typename T, int d>
void TPBD6DJointConstraintUtilities<T, d>::Calculate6dConstraintRotationLimits_SwingFixed(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TRotation<T, d>& Ra,
	const TRotation<T, d>& Rb, 
	const TPBD6DJointMotionSettings<T, d>& MotionSettings, 
	TVector<T, d>& SR,
	TVector<T, d>& CR,
	PMatrix<T, d, d>& RRa, 
	PMatrix<T, d, d>& RRb, 
	TVector<T, d>& LRMin, 
	TVector<T, d>& LRMax)
{
	// Convert the target rotation into target angles
	// @todo(ccaulfield): optimize (cache these values, or store them directly rather than the target rotation)
	TRotation<T, d> DriveTwist, DriveSwing;
	MotionSettings.AngularDriveTarget.ToSwingTwist(T6DJointConstants<T, d>::TwistAxis(), DriveSwing, DriveTwist);
	
	T DriveTwistAngle, DriveSwingAngle;
	TVector<T, d> DriveTwistAxis, DriveSwingAxis;
	DriveTwist.ToAxisAndAngleSafe(DriveTwistAxis, DriveTwistAngle, T6DJointConstants<T, d>::TwistAxis());
	DriveSwing.ToAxisAndAngleSafe(DriveSwingAxis, DriveSwingAngle, T6DJointConstants<T, d>::Swing1Axis());
	if (TVector<T, d>::DotProduct(DriveTwistAxis, T6DJointConstants<T, d>::TwistAxis()) < 0)
	{
		DriveTwistAngle = -DriveTwistAngle;
	}
	if ((TVector<T, d>::DotProduct(DriveSwingAxis, T6DJointConstants<T, d>::Swing1Axis()) < -(T)0.9) || (TVector<T, d>::DotProduct(DriveSwingAxis, T6DJointConstants<T, d>::Swing2Axis()) < -(T)0.9))
	{
		DriveSwingAngle = -DriveSwingAngle;
	}

	bool bDriveEnabled[3] = { false, false, false };
	if (SolverSettings.bEnableDrives)
	{
		bDriveEnabled[(int32)E6DJointAngularConstraintIndex::Twist] = MotionSettings.bAngularTwistDriveEnabled;
		bDriveEnabled[(int32)E6DJointAngularConstraintIndex::Swing1] = MotionSettings.bAngularSwingDriveEnabled;
		bDriveEnabled[(int32)E6DJointAngularConstraintIndex::Swing2] = MotionSettings.bAngularSwingDriveEnabled;
	}

	T DriveAngles[3] = { 0,0,0 };
	DriveAngles[(int32)E6DJointAngularConstraintIndex::Twist] = DriveTwistAngle;
	DriveAngles[(int32)E6DJointAngularConstraintIndex::Swing1] = DriveSwingAngle;
	DriveAngles[(int32)E6DJointAngularConstraintIndex::Swing2] = DriveSwingAngle;

	T DriveStiffness = (SolverSettings.PBDDriveStiffness > 0) ? SolverSettings.PBDDriveStiffness : MotionSettings.AngularDriveStiffness;

	// Use constraint limits settings to specify valid range for constraint-space rotation corrections
	for (int32 Axis = 0; Axis < d; ++Axis)
	{
		if (MotionSettings.AngularMotionTypes[Axis] == E6DJointMotionType::Free)
		{
			if (bDriveEnabled[Axis] && DriveStiffness > KINDA_SMALL_NUMBER)
			{
				CR[Axis] = CR[Axis] + DriveAngles[Axis];
				SR[Axis] = DriveStiffness;
			}
			else
			{
				CR[Axis] = 0;
				SR[Axis] = 0;
				LRMin[Axis] = 0;
				LRMax[Axis] = 0;
			}
		}
		else if (MotionSettings.AngularMotionTypes[Axis] == E6DJointMotionType::Limited)
		{
			if (CR[Axis] >= MotionSettings.AngularLimits[Axis])
			{
				CR[Axis] -= MotionSettings.AngularLimits[Axis];
				LRMin[Axis] = 0;
			}
			else if (CR[Axis] <= -MotionSettings.AngularLimits[Axis])
			{
				CR[Axis] -= -MotionSettings.AngularLimits[Axis];
				LRMax[Axis] = 0;
			}
			if (bDriveEnabled[Axis] && DriveStiffness > KINDA_SMALL_NUMBER)
			{
				CR[Axis] = CR[Axis] + DriveAngles[Axis];
				SR[Axis] = DriveStiffness;
			}
			else
			{
				CR[Axis] = 0;
				SR[Axis] = 0;
				LRMin[Axis] = 0;
				LRMax[Axis] = 0;
			}
		}
	}
}

template<typename T, int d>
bool TPBD6DJointConstraintUtilities<T, d>::Calculate6dConstraintRotation_SwingCone(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TRotation<T, d>& Ra, 
	const TRotation<T, d>& Rb, 
	const TPBD6DJointMotionSettings<T, d>& MotionSettings,
	TVector<T, d>& CR, 
	PMatrix<T, d, d>& RRa, 
	PMatrix<T, d, d>& RRb)
{
	// @todo(ccaulfield): optimize

	// Get the transform from A to B, and use it to generate twist angles.
	TRotation<T, d> Rab = Ra.Inverse() * Rb;
	TRotation<T, d> RTwist, RSwing;
	Rab.ToSwingTwist(T6DJointConstants<T, d>::TwistAxis(), RSwing, RTwist);
	RSwing = RSwing.GetNormalized();
	RTwist = RTwist.GetNormalized();

	TVector<T, d> TwistAxisAB, SwingAxisAB;
	T TwistAngleAB, SwingAngleAB;
	RTwist.ToAxisAndAngleSafe(TwistAxisAB, TwistAngleAB, T6DJointConstants<T, d>::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
	RSwing.ToAxisAndAngleSafe(SwingAxisAB, SwingAngleAB, T6DJointConstants<T, d>::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
	if (TwistAngleAB > PI)
	{
		TwistAngleAB = TwistAngleAB - (T)2 * PI;
	}
	if (SwingAngleAB > PI)
	{
		SwingAngleAB = SwingAngleAB - (T)2 * PI;
	}
	if (TVector<T, d>::DotProduct(TwistAxisAB, T6DJointConstants<T, d>::TwistAxis()) < 0)
	{
		TwistAxisAB = -TwistAxisAB;
		TwistAngleAB = -TwistAngleAB;
	}

	PMatrix<T, d, d> Axesa = Ra.ToMatrix();
	PMatrix<T, d, d> Axesb = Rb.ToMatrix();

	// Calculate constraint space axes for each body. Swing axes are generated as if twist rotation was removed from body B
	TVector<T, d> Twista = Axesa.GetAxis((int32)E6DJointAngularAxisIndex::Twist);
	TVector<T, d> Swing1a = Ra * SwingAxisAB;
	TVector<T, d> Swing2a = TVector<T, d>::CrossProduct(Twista, Swing1a);
	TVector<T, d> Twistb = Axesb.GetAxis((int32)E6DJointAngularAxisIndex::Twist);
	TVector<T, d> Swing1b = Rb * RTwist.Inverse() * SwingAxisAB;
	TVector<T, d> Swing2b = TVector<T, d>::CrossProduct(Twistb, Swing1b);

	RRa.SetRow((int32)E6DJointAngularConstraintIndex::Twist, Twista);
	RRa.SetRow((int32)E6DJointAngularConstraintIndex::Swing1, Swing1a);
	RRa.SetRow((int32)E6DJointAngularConstraintIndex::Swing2, Swing2a);

	RRb.SetRow((int32)E6DJointAngularConstraintIndex::Twist, Twistb);
	RRb.SetRow((int32)E6DJointAngularConstraintIndex::Swing1, Swing1b);
	RRb.SetRow((int32)E6DJointAngularConstraintIndex::Swing2, Swing2b);

	CR[(int32)E6DJointAngularConstraintIndex::Twist] = TwistAngleAB;
	CR[(int32)E6DJointAngularConstraintIndex::Swing1] = SwingAngleAB;
	CR[(int32)E6DJointAngularConstraintIndex::Swing2] = 0;

	// If we're flipped 180 degrees about swing, just pretend the error is zero
	T DotTT = TVector<T, d>::DotProduct(Twista, Twistb);
	const T MinDotTT = (T)-1 + SolverSettings.InvertedAxisTolerance;
	if (DotTT < MinDotTT)
	{
		return false;
	}

	return true;
}

template<typename T, int d>
void TPBD6DJointConstraintUtilities<T, d>::Calculate6dConstraintRotationLimits_SwingCone(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TRotation<T, d>& Ra,
	const TRotation<T, d>& Rb, 
	const TPBD6DJointMotionSettings<T, d>& MotionSettings,
	TVector<T, d>& SR,
	TVector<T, d>& CR,
	PMatrix<T, d, d>& RRa, 
	PMatrix<T, d, d>& RRb, 
	TVector<T, d>& LRMin, 
	TVector<T, d>& LRMax)
{
	// @todo(ccaulfield): Fix low angle problems and use fixed axes when joint drives are being used

	// Get the transform from A to B, and use it to generate twist angles.
	TRotation<T, d> Rab = Ra.Inverse() * Rb;
	TRotation<T, d> RTwist, RSwing;
	Rab.ToSwingTwist(T6DJointConstants<T, d>::TwistAxis(), RSwing, RTwist);
	RSwing = RSwing.GetNormalized();
	RTwist = RTwist.GetNormalized();

	TVector<T, d> TwistAxisAB, SwingAxisAB;
	T TwistAngleAB, SwingAngleAB;
	RTwist.ToAxisAndAngleSafe(TwistAxisAB, TwistAngleAB, T6DJointConstants<T, d>::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
	RSwing.ToAxisAndAngleSafe(SwingAxisAB, SwingAngleAB, T6DJointConstants<T, d>::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
	if (TwistAngleAB > PI)
	{
		TwistAngleAB = TwistAngleAB - (T)2 * PI;
	}
	if (SwingAngleAB > PI)
	{
		SwingAngleAB = SwingAngleAB - (T)2 * PI;
	}
	if (TVector<T, d>::DotProduct(TwistAxisAB, T6DJointConstants<T, d>::TwistAxis()) < 0)
	{
		TwistAxisAB = -TwistAxisAB;
		TwistAngleAB = -TwistAngleAB;
	}

	// Calculate angular limits in new constraint space (our cone constraint axes do not map directly onto settings' constraint axes)
	T TwistLimit = MotionSettings.AngularLimits[(int32)E6DJointAngularConstraintIndex::Twist];
	T DotSwing1 = FMath::Abs(TVector<T, d>::DotProduct(SwingAxisAB, T6DJointConstants<T, d>::Swing1Axis()));
	T DotSwing2 = FMath::Abs(TVector<T, d>::DotProduct(SwingAxisAB, T6DJointConstants<T, d>::Swing2Axis()));
	T Swing1Limit = MotionSettings.AngularLimits[(int32)E6DJointAngularConstraintIndex::Swing1];
	T Swing2Limit = MotionSettings.AngularLimits[(int32)E6DJointAngularConstraintIndex::Swing2];
	T SwingLimit = FMath::Sqrt(Swing1Limit * DotSwing1 * Swing1Limit * DotSwing1 + Swing2Limit * DotSwing2 * Swing2Limit * DotSwing2 );

	TVector<T, d> AngularLimits;
	AngularLimits[(int32)E6DJointAngularConstraintIndex::Twist] = TwistLimit;
	AngularLimits[(int32)E6DJointAngularConstraintIndex::Swing1] = SwingLimit;
	AngularLimits[(int32)E6DJointAngularConstraintIndex::Swing2] = FLT_MAX;

	// Convert the target rotation into target angles
	// @todo(ccaulfield): optimize (cache these values, or store them directly rather than the target rotation)
	TRotation<T, d> DriveTwist, DriveSwing;
	MotionSettings.AngularDriveTarget.ToSwingTwist(T6DJointConstants<T, d>::TwistAxis(), DriveSwing, DriveTwist);
	T DriveTwistAngle, DriveSwingAngle;
	TVector<T, d> DriveTwistAxis, DriveSwingAxis;
	DriveTwist.ToAxisAndAngleSafe(DriveTwistAxis, DriveTwistAngle, T6DJointConstants<T, d>::TwistAxis());
	DriveSwing.ToAxisAndAngleSafe(DriveSwingAxis, DriveSwingAngle, T6DJointConstants<T, d>::Swing1Axis());
	if (TVector<T, d>::DotProduct(DriveTwistAxis, TwistAxisAB) < 0)
	{
		DriveTwistAngle = -DriveTwistAngle;
	}

	TVector<T, d> SwingAxis2AB = TVector<T, d>::CrossProduct(TwistAxisAB, SwingAxisAB);
	T DriveDotSwing1 = TVector<T, d>::DotProduct(DriveSwingAxis, SwingAxisAB);
	T DriveDotSwing2 = TVector<T, d>::DotProduct(DriveSwingAxis, SwingAxis2AB);
	T DriveSwing1Angle = DriveDotSwing1 * DriveSwingAngle;
	T DriveSwing2Angle = DriveDotSwing2 * DriveSwingAngle;


	bool bDriveEnabled[3] = { false, false, false };
	if (SolverSettings.bEnableDrives)
	{
		bDriveEnabled[(int32)E6DJointAngularConstraintIndex::Twist] = MotionSettings.bAngularTwistDriveEnabled;
		bDriveEnabled[(int32)E6DJointAngularConstraintIndex::Swing1] = MotionSettings.bAngularSwingDriveEnabled;
		bDriveEnabled[(int32)E6DJointAngularConstraintIndex::Swing2] = MotionSettings.bAngularSwingDriveEnabled;
	}

	T DriveAngles[3] = { 0, 0, 0 };
	DriveAngles[(int32)E6DJointAngularConstraintIndex::Twist] = DriveTwistAngle;
	DriveAngles[(int32)E6DJointAngularConstraintIndex::Swing1] = DriveSwing1Angle;
	DriveAngles[(int32)E6DJointAngularConstraintIndex::Swing2] = DriveSwing2Angle;

	T DriveStiffness = (SolverSettings.PBDDriveStiffness > 0)? SolverSettings.PBDDriveStiffness : MotionSettings.AngularDriveStiffness;

	// Use constraint limits settings to specify valid range for constraint-space rotation corrections
	for (int32 Axis = 0; Axis < d; ++Axis)
	{
		if (MotionSettings.AngularMotionTypes[Axis] == E6DJointMotionType::Free)
		{
			if (bDriveEnabled[Axis] && DriveStiffness > KINDA_SMALL_NUMBER)
			{
				CR[Axis] = CR[Axis] + DriveAngles[Axis];
				SR[Axis] = DriveStiffness;
			}
			else
			{
				CR[Axis] = 0;
				SR[Axis] = 0;
				LRMin[Axis] = 0;
				LRMax[Axis] = 0;
			}
		}
		else if (MotionSettings.AngularMotionTypes[Axis] == E6DJointMotionType::Limited)
		{
			if (CR[Axis] >= AngularLimits[Axis])
			{
				CR[Axis] -= AngularLimits[Axis];
				LRMin[Axis] = 0;
			}
			else if (CR[Axis] <= -AngularLimits[Axis])
			{
				CR[Axis] -= -AngularLimits[Axis];
				LRMax[Axis] = 0;
			}
			else if (bDriveEnabled[Axis] && DriveStiffness > KINDA_SMALL_NUMBER)
			{
				CR[Axis] = CR[Axis] + DriveAngles[Axis];
				SR[Axis] = DriveStiffness;
			}
			else
			{
				CR[Axis] = 0;
				SR[Axis] = 0;
				LRMin[Axis] = 0;
				LRMax[Axis] = 0;
			}
		}
	}
}

template<typename T, int d>
bool TPBD6DJointConstraintUtilities<T, d>::Calculate6dConstraintRotation(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TRotation<T, d>& Ra, 
	const TRotation<T, d>& Rb, 
	const TPBD6DJointMotionSettings<T, d>& MotionSettings,
	TVector<T, d>& CR, 
	PMatrix<T, d, d>& RRa, 
	PMatrix<T, d, d>& RRb)
{
	RRa = PMatrix<T, d, d>(0, 0, 0);
	RRb = PMatrix<T, d, d>(0, 0, 0);
	CR = TVector<T, d>(0, 0, 0);

	bool bUseSwingCone = true
		&& (MotionSettings.AngularMotionTypes[(int32)E6DJointAngularConstraintIndex::Swing1] != E6DJointMotionType::Locked)
		&& (MotionSettings.AngularMotionTypes[(int32)E6DJointAngularConstraintIndex::Swing2] != E6DJointMotionType::Locked);
	if (bUseSwingCone)
	{
		return Calculate6dConstraintRotation_SwingCone(SolverSettings, Ra, Rb, MotionSettings, CR, RRa, RRb);
	}
	else
	{
		return Calculate6dConstraintRotation_SwingFixed(SolverSettings, Ra, Rb, MotionSettings, CR, RRa, RRb);
	}
}

template<typename T, int d>
void TPBD6DJointConstraintUtilities<T, d>::Calculate6dConstraintRotationLimits(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TRotation<T, d>& Ra, 
	const TRotation<T, d>& Rb, 
	const TPBD6DJointMotionSettings<T, d>& MotionSettings,
	TVector<T, d>& SR,
	TVector<T, d>& CR,
	PMatrix<T, d, d>& RRa, 
	PMatrix<T, d, d>& RRb, 
	TVector<T, d>& LRMin, 
	TVector<T, d>& LRMax)
{
	LRMin = TVector<T, d>(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	LRMax = TVector<T, d>(FLT_MAX, FLT_MAX, FLT_MAX);
	SR = TVector<T, d>(1, 1, 1);

	bool bUseSwingCone = true
		&& (MotionSettings.AngularMotionTypes[(int32)E6DJointAngularConstraintIndex::Swing1] != E6DJointMotionType::Locked)
		&& (MotionSettings.AngularMotionTypes[(int32)E6DJointAngularConstraintIndex::Swing2] != E6DJointMotionType::Locked);
	if (bUseSwingCone)
	{
		Calculate6dConstraintRotationLimits_SwingCone(SolverSettings, Ra, Rb, MotionSettings, SR, CR, RRa, RRb, LRMin, LRMax);
	}
	else
	{
		Calculate6dConstraintRotationLimits_SwingFixed(SolverSettings, Ra, Rb, MotionSettings, SR, CR, RRa, RRb, LRMin, LRMax);
	}
}

template<typename T, int d>
bool TPBD6DJointConstraintUtilities<T, d>::Calculate6dDelta(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const T Dt,
	const TVector<T, d>& Pa,
	const TRotation<T, d>& Qa,
	float MaInv,
	const PMatrix<T, d, d>& IaInv,
	const TVector<T, d>& Pb,
	const TRotation<T, d>& Qb,
	float MbInv,
	const PMatrix<T, d, d>& IbInv,
	const TVector<T, d>& Xa,
	const TRotation<T, d>& Ra,
	const TVector<T, d>& Xb,
	const TRotation<T, d>& Rb,
	const TPBD6DJointMotionSettings<T, d>& MotionSettings,
	TPBD6DJointState<T, d>& State,
	TVector<T, d>& DPa,
	TRotation<T, d>& DQa,
	TVector<T, d>& DPb,
	TRotation<T, d>& DQb)
{
	// @todo(ccaulfield): optimize: should add the constraints 1 by one

	DPa = TVector<T, d>(0, 0, 0);
	DQa = TRotation<T, d>::FromElements(0, 0, 0, 0);
	DPb = TVector<T, d>(0, 0, 0);
	DQb = TRotation<T, d>::FromElements(0, 0, 0, 0);

	PMatrix<T, d, d> Axesa = Ra.ToMatrix();
	PMatrix<T, d, d> Axesb = Rb.ToMatrix();

	// Constraint-space errors (B - A), each row represents a constraint.
	// CX are the (x,y,z) position constraint error.
	// CR are the (twist, swing1, swing2) rotation constraint error.
	//
	//	C	=	| CX |
	//			| CR |
	//
	// Derivative of constraint error with respect to constraint parameters (Jacobian).
	// Each row represents a constraint, and contains the derivative of the constraint error wrt each constraint variable.
	// There is a J for each body: Ja and Jb.
	//
	//	J(C(X,R)) =	| dCX/dX^t	dCX/dR^t |
	//	(6x6)		| dCR/dX^t	dCR/dR^t |
	//
	//	Ja	=	| 1(3x3)	-XRa(3x3) |
	//			| 0(3x3)	RRa(3x3)  |
	//
	// Where XR is the cross-product matrix of the world-space constraint position relative to the body, and
	// RR rows are the twist, swing1 and swing2 axes (about which we are calculating the required rotation to correct the error).
	//
	// J should be negated for body B, but it cancels out in lambda calculation
	// and we reintroduce the sign in the final lambda multiple (where J gets used again)
	//
	TVector<T, d> CX = (Xb - Xa);
	TVector<T, d> CR;
	PMatrix<T, d, d> RRa, RRb;
	bool bRotationValid = Calculate6dConstraintRotation(SolverSettings, Ra, Rb, MotionSettings, CR, RRa, RRb);

	// Set limits, apply drives
	TVector<T, d> SX = TVector<T, d>(1, 1, 1);
	TVector<T, d> LRMin, LRMax, SR;
	Calculate6dConstraintRotationLimits(SolverSettings, Ra, Rb, MotionSettings, SR, CR, RRa, RRb, LRMin, LRMax);

	CX = SX * CX;
	CR = SR * CR;

	if (!SolverSettings.bEnableTwistLimits || !bRotationValid)
	{
		CR[(int32)E6DJointAngularConstraintIndex::Twist] = 0;
		LRMin[(int32)E6DJointAngularConstraintIndex::Twist] = 0;
		LRMax[(int32)E6DJointAngularConstraintIndex::Twist] = 0;
		RRa.SetRow((int32)E6DJointAngularConstraintIndex::Twist, TVector<T, d>(0, 0, 0));
		RRb.SetRow((int32)E6DJointAngularConstraintIndex::Twist, TVector<T, d>(0, 0, 0));
	}
	if (!SolverSettings.bEnableSwingLimits || !bRotationValid)
	{
		CR[(int32)E6DJointAngularConstraintIndex::Swing1] = 0;
		CR[(int32)E6DJointAngularConstraintIndex::Swing2] = 0;
		LRMin[(int32)E6DJointAngularConstraintIndex::Swing1] = 0;
		LRMax[(int32)E6DJointAngularConstraintIndex::Swing1] = 0;
		LRMin[(int32)E6DJointAngularConstraintIndex::Swing2] = 0;
		LRMax[(int32)E6DJointAngularConstraintIndex::Swing2] = 0;
		RRa.SetRow((int32)E6DJointAngularConstraintIndex::Swing1, TVector<T, d>(0, 0, 0));
		RRa.SetRow((int32)E6DJointAngularConstraintIndex::Swing2, TVector<T, d>(0, 0, 0));
		RRb.SetRow((int32)E6DJointAngularConstraintIndex::Swing1, TVector<T, d>(0, 0, 0));
		RRb.SetRow((int32)E6DJointAngularConstraintIndex::Swing2, TVector<T, d>(0, 0, 0));
	}

	PMatrix<T, d, d> XRa = Utilities::CrossProductMatrix(Xa - Pa);
	PMatrix<T, d, d> XRb = Utilities::CrossProductMatrix(Xb - Pb);

	// Lambda values for position and rotation (stored in 2x2 block of 3x3 matrices).
	// Lambda holds the constraint-space corrections required to fix the constraint-space error C.
	//
	//	L   =   (1 / ([Ja . Ma^-1 . Ja^t] + [Jb . Mb^-1 . Jb^t])) . C	=	(1 / F) . C
	//
	//		=   | F00       F01	|^-1 . |CX|
	//          | F10       F11	|      |CR|
	//

	// Fa = [Ja . Ma^-1 . Ja^t] = | Fa00 Fa01 |
	//                            | Fa10 Da11 |
	// Fb = [Jb . Mb^-1 . Jb^t]
	// F = Fa + Fb
	PMatrix<T, d, d> Fa00, Fa01, Fa10, Fa11;
	PMatrix<T, d, d> Fb00, Fb01, Fb10, Fb11;
	TPBD6DJointConstraintUtilities<T, d>::ComputeJointFactorMatrix(XRa, RRa, MaInv, IaInv, Fa00, Fa01, Fa10, Fa11);
	TPBD6DJointConstraintUtilities<T, d>::ComputeJointFactorMatrix(XRb, RRb, MbInv, IbInv, Fb00, Fb01, Fb10, Fb11);

	PMatrix<T, d, d> F00 = Fa00 + Fb00;
	PMatrix<T, d, d> F01 = Fa01 + Fb01;
	PMatrix<T, d, d> F10 = Fa10 + Fb10;
	PMatrix<T, d, d> F11 = Fa11 + Fb11;
	PMatrix<T, d, d> FI00, FI01, FI10, FI11;

	// Stiffness and damping Pt1 (XPBD denominator)
	// (Also support PBD stiffness if XPBD stiffness is 0)
	// Alpha = Inverse Stiffness, Beta = Damping (not inverse)
	T Stiffness = (SolverSettings.PBDStiffness > 0) ? SolverSettings.PBDStiffness : MotionSettings.Stiffness;
	T AlphaX = SolverSettings.XPBDAlphaX / (Dt * Dt);
	T AlphaR = SolverSettings.XPBDAlphaR / (Dt * Dt);
	T GammaX = SolverSettings.XPBDAlphaX * SolverSettings.XPBDBetaX / Dt;
	T GammaR = SolverSettings.XPBDAlphaR * SolverSettings.XPBDBetaR / Dt;
	if ((SolverSettings.XPBDAlphaX > 0) && (SolverSettings.XPBDAlphaR > 0))
	{
		F00.M[0][0] = ((T)1 + GammaX) * F00.M[0][0] + AlphaX;
		F00.M[1][1] = ((T)1 + GammaX) * F00.M[1][1] + AlphaX;
		F00.M[2][2] = ((T)1 + GammaX) * F00.M[2][2] + AlphaX;
		F11.M[0][0] = ((T)1 + GammaR) * F11.M[0][0] + AlphaR;
		F11.M[1][1] = ((T)1 + GammaR) * F11.M[1][1] + AlphaR;
		F11.M[2][2] = ((T)1 + GammaR) * F11.M[2][2] + AlphaR;
		SX = TVector<T, d>(1, 1, 1);
		SR = TVector<T, d>(1, 1, 1);
	}

	// If we have no error for a constraint we remove its entry from F
	// @todo(ccaulfield): this will not be necessary when constraints are built up correctly as opposed to always added.
	for (int32 Axis = 0; Axis < d; ++Axis)
	{
		if (SR[Axis] == 0)
		{
			F01.SetColumn(Axis, { 0,0,0 });
			F11.SetColumn(Axis, { 0,0,0 });
			F10.SetRow(Axis, { 0,0,0 });
			F11.SetRow(Axis, { 0,0,0 });
			F11.M[Axis][Axis] = 1;
		}
	}

	TVector<T, d> VX = CX - State.PrevTickCX;
	TVector<T, d> VR = CR - State.PrevTickCR;
	TVector<T, d> CXa = CX - AlphaX * State.LambdaXa - GammaX * (VX - Utilities::Multiply(XRa, VR));
	TVector<T, d> CRa = CR - AlphaR * State.LambdaRa - GammaR * Utilities::Multiply(RRa, VR);
	TVector<T, d> CXb = -CX - AlphaX * State.LambdaXb - GammaX * (VX - Utilities::Multiply(XRb, VR));
	TVector<T, d> CRb = -CR - AlphaR * State.LambdaRb - GammaR * Utilities::Multiply(RRb, VR);

	// FI = 1 / F = | FI00 FI01 |
	//              | FI10 FI11 |
	//
	TPBD6DJointConstraintUtilities<T, d>::BlockwiseInverse(F00, F01, F10, F11, FI00, FI01, FI10, FI11);

	// L = FI . C = | LX |
	//              | LR |
	//
	TVector<T, d> LXXa = Utilities::Multiply(FI00, CXa);
	TVector<T, d> LXRa = Utilities::Multiply(FI01, CRa);
	TVector<T, d> LRXa = Utilities::Multiply(FI10, CXa);
	TVector<T, d> LRRa = Utilities::Multiply(FI11, CRa);
	TVector<T, d> LXa = (LXXa + LXRa);
	TVector<T, d> LRa = (LRXa + LRRa);

	TVector<T, d> LXXb = Utilities::Multiply(FI00, CXb);
	TVector<T, d> LXRb = Utilities::Multiply(FI01, CRb);
	TVector<T, d> LRXb = Utilities::Multiply(FI10, CXb);
	TVector<T, d> LRRb = Utilities::Multiply(FI11, CRb);
	TVector<T, d> LXb = (LXXb + LXRb);
	TVector<T, d> LRb = (LRXb + LRRb);

	// Apply joint limits (which are either 0 or -/+infinity)
	for (int32 Axis = 0; Axis < d; ++Axis)
	{
		LRa[Axis] = FMath::Clamp(LRa[Axis], LRMin[Axis], LRMax[Axis]);
		LRb[Axis] = FMath::Clamp(LRb[Axis], -LRMax[Axis], -LRMin[Axis]);
	}


	// Final corrections
	//
	//	Da	=	Ma^-1 . Ja^t . L
	//
	//	Da	= | DPa | =	 Ma^-1 . |         LX           |   =    Ma^-1 . |      LX           |
	//	      | DRa |            | -XRa^t.LX + RRa^t.LR |		         | XRa.LX + RRa^t.LR |
	//
	TVector<T, d> DRaX, DRaR, DRa;
	TVector<T, d> DRbX, DRbR, DRb;
	TRotation<T, d> DQaq, DQbq;

	// Reduce solver stiffness until the rotation correction falls below some threshold
	for (int32 StiffnesIt = 0; StiffnesIt < 2; ++StiffnesIt)
	{
		DRaX = Utilities::Multiply(XRa, LXa);
		DRaR = Utilities::Multiply(RRa.GetTransposed(), LRa);
		DRbX = Utilities::Multiply(XRb, LXb);
		DRbR = Utilities::Multiply(RRb.GetTransposed(), LRb);

		DPa = Stiffness * MaInv * LXa;
		DPb = Stiffness * MbInv * LXb;
		DRa = Stiffness * Utilities::Multiply(IaInv, (DRaX + DRaR));
		DRb = Stiffness * Utilities::Multiply(IbInv, (DRbX + DRbR));

		DQaq = TRotation<T, d>::FromElements(DRa, 0);
		DQbq = TRotation<T, d>::FromElements(DRb, 0);

		T MaxAbsDQaq = FMath::Max(FMath::Max(FMath::Abs(DQaq.X), FMath::Abs(DQaq.Y)), FMath::Max(FMath::Abs(DQaq.Z), FMath::Abs(DQaq.W)));
		T MaxAbsDQbq = FMath::Max(FMath::Max(FMath::Abs(DQbq.X), FMath::Abs(DQbq.Y)), FMath::Max(FMath::Abs(DQbq.Z), FMath::Abs(DQbq.W)));
		T MaxDQq = FMath::Max(MaxAbsDQaq, MaxAbsDQbq);
		if ((MaxDQq < SolverSettings.MaxRotComponent) || !SolverSettings.bEnableAutoStiffness || (SolverSettings.MaxRotComponent == 0))
		{
			break;
		}
		Stiffness = Stiffness * (SolverSettings.MaxRotComponent / MaxDQq);
	}

	DQa = (DQaq * Qa) * T(0.5);
	DQb = (DQbq * Qb) * T(0.5);

	// Keep track of current constraint-space corrections for XPBD timestep dependence fix
	State.LambdaXa = State.LambdaXa + LXa;
	State.LambdaRa = State.LambdaRa + LRa;
	State.LambdaXb = State.LambdaXb + LXb;
	State.LambdaRb = State.LambdaRb + LRb;
	State.PrevItCX = CX;
	State.PrevItCR = CR;

	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  C(X, R)   = (%f, %f, %f) (%f, %f, %f)"), CX.X, CX.Y, CX.Z, CR.X, CR.Y, CR.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  X(A, B)   = (%f, %f, %f) (%f, %f, %f)"), (Xa - Pa).X, (Xa - Pa).Y, (Xa - Pa).Z, (Xb - Pb).X, (Xb - Pb).Y, (Xb - Pb).Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  RRX(A, B) = (%f, %f, %f) [%f] (%f, %f, %f) [%f]"), RRa.GetRow(0).X, RRa.GetRow(0).Y, RRa.GetRow(0).Z, RRa.GetRow(0).Size(), RRb.GetRow(0).X, RRb.GetRow(0).Y, RRb.GetRow(0).Z, RRa.GetRow(0).Size());
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  RRY(A, B) = (%f, %f, %f) [%f] (%f, %f, %f) [%f]"), RRa.GetRow(1).X, RRa.GetRow(1).Y, RRa.GetRow(1).Z, RRa.GetRow(1).Size(), RRb.GetRow(1).X, RRb.GetRow(1).Y, RRb.GetRow(1).Z, RRa.GetRow(1).Size());
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  RRZ(A, B) = (%f, %f, %f) [%f] (%f, %f, %f) [%f]"), RRa.GetRow(2).X, RRa.GetRow(2).Y, RRa.GetRow(2).Z, RRa.GetRow(2).Size(), RRb.GetRow(2).X, RRb.GetRow(2).Y, RRb.GetRow(2).Z, RRa.GetRow(2).Size());
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  LXA(X, R) = (%f, %f, %f) (%f, %f, %f)"), LXXa.X, LXXa.Y, LXXa.Z, LXRa.X, LXRa.Y, LXRa.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  LRA(X, R) = (%f, %f, %f) (%f, %f, %f)"), LRXa.X, LRXa.Y, LRXa.Z, LRRa.X, LRRa.Y, LRRa.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  LA(X, R)  = (%f, %f, %f) (%f, %f, %f)"), LXa.X, LXa.Y, LXa.Z, LRa.X, LRa.Y, LRa.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  LXB(X, R) = (%f, %f, %f) (%f, %f, %f)"), LXXb.X, LXXb.Y, LXXb.Z, LXRb.X, LXRb.Y, LXRb.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  LRB(X, R) = (%f, %f, %f) (%f, %f, %f)"), LRXb.X, LRXb.Y, LRXb.Z, LRRb.X, LRRb.Y, LRRb.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  LB(X, R)  = (%f, %f, %f) (%f, %f, %f)"), LXb.X, LXb.Y, LXb.Z, LRb.X, LRb.Y, LRb.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  DP(A, B)  = (%f, %f, %f) (%f, %f, %f)"), DPa.X, DPa.Y, DPa.Z, DPb.X, DPb.Y, DPb.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  DRA(X, R) = (%f, %f, %f) (%f, %f, %f)"), DRaX.X, DRaX.Y, DRaX.Z, DRaR.X, DRaR.Y, DRaR.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  DRB(X, R) = (%f, %f, %f) (%f, %f, %f)"), DRbX.X, DRbX.Y, DRbX.Z, DRbR.X, DRbR.Y, DRbR.Z);
	UE_LOG(LogChaos6DJoint, VeryVerbose, TEXT("  DQ(A, B)  = (%f, %f, %f) (%f, %f, %f)"), DQaq.X, DQaq.Y, DQaq.Z, DQbq.X, DQbq.Y, DQbq.Z);

	return true;
}

template<typename T, int d>
int TPBD6DJointConstraintUtilities<T, d>::Solve6dConstraint(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const T Dt,
	TVector<T, d>& PaInOut,
	TRotation<T, d>& QaInOut, 
	T MaInv, 
	const PMatrix<T, d, d>& ILaInv, 
	const TVector<T, d>& XLa,
	const TRotation<T, d>& RLa,
	TVector<T, d>& PbInOut,
	TRotation<T, d>& QbInOut, 
	T MbInv, 
	const PMatrix<T, d, d>& ILbInv, 
	const TVector<T, d>& XLb,
	const TRotation<T, d>& RLb,
	const TPBD6DJointMotionSettings<T, d>& MotionSettings,
	TPBD6DJointState<T, d>& State)
{
	TVector<T, d>& Pa = PaInOut;
	TRotation<T, d>& Qa = QaInOut;
	TVector<T, d>& Pb = PbInOut;
	TRotation<T, d>& Qb = QbInOut;

	// World-space mass state
	PMatrix<T, d, d> IaInv = Utilities::Multiply(Qa.ToMatrix(), Utilities::Multiply(ILaInv, Qa.ToMatrix().GetTransposed()));
	PMatrix<T, d, d> IbInv = Utilities::Multiply(Qb.ToMatrix(), Utilities::Multiply(ILbInv, Qb.ToMatrix().GetTransposed()));

	bool bFlipQ = ((Qa | Qb) < 0);
	if (bFlipQ)
	{
		Qa = TRotation<T, d>::Negate(Qa);
	}

	T CurrentError = FLT_MAX;

	int NumLoops = 0;
	for (int LoopIndex = 0; (LoopIndex < SolverSettings.MaxIterations) && (CurrentError > SolverSettings.SolveTolerance); ++LoopIndex)
	{
		// World-space constraint state
		TVector<T, d> Xa = Pa + Qa * XLa;
		TVector<T, d> Xb = Pb + Qb * XLb;
		TRotation<T, d> Ra = Qa * RLa;
		TRotation<T, d> Rb = Qb * RLb;

#if !NO_LOGGING
		if (!LogChaos6DJoint.IsSuppressed(ELogVerbosity::Verbose))
		{
			TVector<T, d> ConstraintAngles = Calculate6dConstraintAngles(SolverSettings, Ra, Rb, MotionSettings);
			ConstraintAngles = TVector<T, d>(FMath::RadiansToDegrees(ConstraintAngles.X), FMath::RadiansToDegrees(ConstraintAngles.Y), FMath::RadiansToDegrees(ConstraintAngles.Z));
			UE_LOG(LogChaos6DJoint, Verbose, TEXT("Pre Loop %d: Pos = %f (%f, %f, %f) Angle = (%f, %f, %f)"),
				LoopIndex, (Xb - Xa).Size(), (Xb - Xa).X, (Xb - Xa).Y, (Xb - Xa).Z,
				ConstraintAngles.X, ConstraintAngles.Y, ConstraintAngles.Z);
		}
#endif

		// Get deltas to apply to position and rotation to correct constraint error
		TVector<T, d> DPa, DPb;
		TRotation<T, d> DQa, DQb;
		bool bSolveOk = Calculate6dDelta(SolverSettings, Dt, Pa, Qa, MaInv, IaInv, Pb, Qb, MbInv, IbInv, Xa, Ra, Xb, Rb, MotionSettings, State, DPa, DQa, DPb, DQb);
		if (!bSolveOk)
		{
			break;
		}

		// New world-space body state
		TVector<T, d> Pa2 = Pa + DPa;
		TVector<T, d> Pb2 = Pb + DPb;
		TRotation<T, d> Qa2 = (Qa + DQa).GetNormalized();
		TRotation<T, d> Qb2 = (Qb + DQb).GetNormalized();
		Qb2.EnforceShortestArcWith(Qb);
		Qa2.EnforceShortestArcWith(Qb2);

		// Update body state
		Pa = Pa2;
		Pb = Pb2;
		Qa = Qa2;
		Qb = Qb2;
		IaInv = Qa.ToMatrix() * ILaInv * Qa.ToMatrix().GetTransposed();
		IbInv = Qb.ToMatrix() * ILbInv * Qb.ToMatrix().GetTransposed();

		// New world-space constraint state
		TVector<T, d> Xa2 = Pa2 + Qa2 * XLa;
		TVector<T, d> Xb2 = Pb2 + Qb2 * XLb;
		TRotation<T, d> Ra2 = Qa2 * RLa;
		TRotation<T, d> Rb2 = Qb2 * RLb;

#if !NO_LOGGING
		if (!LogChaos6DJoint.IsSuppressed(ELogVerbosity::Verbose))
		{
			TVector<T, d> ConstraintAngles2 = Calculate6dConstraintAngles(SolverSettings, Ra2, Rb2, MotionSettings);
			ConstraintAngles2 = TVector<T, d>(FMath::RadiansToDegrees(ConstraintAngles2.X), FMath::RadiansToDegrees(ConstraintAngles2.Y), FMath::RadiansToDegrees(ConstraintAngles2.Z));
			UE_LOG(LogChaos6DJoint, Verbose, TEXT("Post Loop %d: Pos = %f (%f, %f, %f) Angle = (%f, %f, %f)"),
				LoopIndex, (Xb2 - Xa2).Size(), (Xb2 - Xa2).X, (Xb2 - Xa2).Y, (Xb2 - Xa2).Z,
				ConstraintAngles2.X, ConstraintAngles2.Y, ConstraintAngles2.Z);
		}
#endif

		// @todo(ccaulfield): this isn't really a good error calculation - the magnitudes of positions and rotations are too different and its very expensive. It'll do for now though.
		TVector<T, d> CX = Xb2 - Xa2;
		TVector<T, d> CR, SR;
		PMatrix<T, d, d> RRa, RRb;
		TVector<T, d> LRMin, LRMax;
		Calculate6dConstraintRotation(SolverSettings, Ra2, Rb2, MotionSettings, CR, RRa, RRb);
		Calculate6dConstraintRotationLimits(SolverSettings, Ra2, Rb2, MotionSettings, SR, CR, RRa, RRb, LRMin, LRMax);
		CurrentError = FMath::Sqrt(CX.SizeSquared() + CR.SizeSquared());
		++NumLoops;
	}

	if (bFlipQ)
	{
		Qa = TRotation<T, d>::Negate(Qa);
	}

	return NumLoops;
}

template<typename T, int d>
void TPBD6DJointConstraintUtilities<T, d>::Calculate3dDelta(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TVector<T, d>& Pa,
	const TRotation<T, d>& Qa,
	float MaInv,
	const PMatrix<T, d, d>& IaInv,
	const TVector<T, d>& Pb,
	const TRotation<T, d>& Qb,
	float MbInv,
	const PMatrix<T, d, d>& IbInv,
	const TVector<T, d>& Xa,
	const TVector<T, d>& Xb,
	const TPBD6DJointMotionSettings<T, d>& XSettings,
	TVector<T, d>& DPa,
	TRotation<T, d>& DQa,
	TVector<T, d>& DPb,
	TRotation<T, d>& DQb)
{
	TVector<T, d> CX = Xb - Xa;

	T Stiffness = (SolverSettings.PBDStiffness > 0) ? SolverSettings.PBDStiffness : XSettings.Stiffness;
	CX = Stiffness * CX;

	PMatrix<T, d, d> Ma00 = PMatrix<T, d, d>(0, 0, 0);
	PMatrix<T, d, d> Mb00 = PMatrix<T, d, d>(0, 0, 0);
	if (MaInv > 0)
	{
		Ma00 = Utilities::ComputeJointFactorMatrix(Xa - Pa, IaInv, MaInv);
	}
	if (MbInv > 0)
	{
		Mb00 = Utilities::ComputeJointFactorMatrix(Xb - Pb, IbInv, MbInv);
	}
	PMatrix<T, d, d> MI00 = (Ma00 + Mb00).Inverse();
	TVector<T, d> DX = Utilities::Multiply(MI00, CX);

	// Divide position and rotation error between bodies based on mass distribution
	DPa = MaInv * DX;
	DPb = -(MbInv * DX);
	TVector<T, d> DQav = Utilities::Multiply(IaInv, TVector<T, d>::CrossProduct(Xa - Pa, DX));
	TVector<T, d> DQbv = Utilities::Multiply(IbInv, TVector<T, d>::CrossProduct(Xb - Pb, -DX));
	TRotation<T, d> DQaq = TRotation<T, d>::FromElements(DQav, 0);
	TRotation<T, d> DQbq = TRotation<T, d>::FromElements(DQbv, 0);
	DQa = (DQaq * Qa) * 0.5f;
	DQb = (DQbq * Qb) * 0.5f;
}

template<typename T, int d>
void TPBD6DJointConstraintUtilities<T, d>::Solve3dConstraint(
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	TVector<T, d>& P0,
	TRotation<T, d>& Q0,
	T InvM0,
	const PMatrix<T, d, d>& InvIL0,
	const TVector<T, d>& XL0,
	const TRotation<T, d>& RL0,
	TVector<T, d>& P1,
	TRotation<T, d>& Q1,
	T InvM1,
	const PMatrix<T, d, d>& InvIL1,
	const TVector<T, d>& XL1,
	const TRotation<T, d>& RL1,
	const TPBD6DJointMotionSettings<T, d>& MotionSettings)
{
	TVector<T, d> X0 = P0 + Q0.RotateVector(XL0);
	TVector<T, d> X1 = P1 + Q1.RotateVector(XL1);
	PMatrix<T, d, d> InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
	PMatrix<T, d, d> InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));
	TVector<T, d> DP0, DP1;
	TRotation<T, d> DQ0, DQ1;

	T CurrentError = FLT_MAX;
	int NumLoops = 0;
	for (int LoopIndex = 0; (LoopIndex < SolverSettings.MaxPreIterations) && (CurrentError > SolverSettings.SolveTolerance); ++LoopIndex)
	{
		// Calculate position and rotation corrections
		Calculate3dDelta(SolverSettings, P0, Q0, InvM0, InvI0, P1, Q1, InvM1, InvI1, X0, X1, MotionSettings, DP0, DQ0, DP1, DQ1);

		// Apply corrections to body state
		P0 = P0 + DP0;
		Q0 = (Q0 + DQ0).GetNormalized();
		P1 = P1 + DP1;
		Q1 = (Q1 + DQ1).GetNormalized();

		// Recalculate constraint state
		X0 = P0 + Q0 * XL0;
		X1 = P1 + Q1 * XL1;
		InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
		InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));
		
		// Measure error
		TVector<T, d> CX = X1 - X0;
		CurrentError = CX.Size();
		++NumLoops;
	}
}

//
// Constraint JointSettings
//

template<class T, int d>
TPBD6DJointMotionSettings<T, d>::TPBD6DJointMotionSettings()
	: Stiffness((T)1) 
	, LinearMotionTypes({ E6DJointMotionType::Locked, E6DJointMotionType::Locked, E6DJointMotionType::Locked })
	, LinearLimits(TVector<T, d>(FLT_MAX, FLT_MAX, FLT_MAX))
	, AngularMotionTypes({ E6DJointMotionType::Free, E6DJointMotionType::Free, E6DJointMotionType::Free })
	, AngularLimits(TVector<T, d>(FLT_MAX, FLT_MAX, FLT_MAX))
	, AngularDriveTarget(TRotation<T, d>::FromIdentity())
	, AngularDriveTargetAngles(TVector<T, d>(0, 0, 0))
	, bAngularSLerpDriveEnabled(false)
	, bAngularTwistDriveEnabled(false)
	, bAngularSwingDriveEnabled(false)
	, AngularDriveStiffness(0)
	, AngularDriveDamping(0)
{
}

template<class T, int d>
TPBD6DJointMotionSettings<T, d>::TPBD6DJointMotionSettings(const TVector<E6DJointMotionType, d>& InLinearMotionTypes, const TVector<E6DJointMotionType, d>& InAngularMotionTypes)
	: Stiffness((T)1)
	, LinearMotionTypes(InLinearMotionTypes)
	, LinearLimits(TVector<T, d>(FLT_MAX, FLT_MAX, FLT_MAX))
	, AngularMotionTypes({ E6DJointMotionType::Free, E6DJointMotionType::Free, E6DJointMotionType::Free })
	, AngularLimits(TVector<T, d>(FLT_MAX, FLT_MAX, FLT_MAX))
	, AngularDriveTarget(TRotation<T, d>::FromIdentity())
	, AngularDriveTargetAngles(TVector<T, d>(0, 0, 0))
	, bAngularSLerpDriveEnabled(false)
	, bAngularTwistDriveEnabled(false)
	, bAngularSwingDriveEnabled(false)
	, AngularDriveStiffness(0)
	, AngularDriveDamping(0)
{
}

template<class T, int d>
TPBD6DJointSettings<T, d>::TPBD6DJointSettings()
	: ConstraintFrames({ FTransform::Identity, FTransform::Identity })
{
}

template<class T, int d>
TPBD6DJointState<T, d>::TPBD6DJointState()
	: LambdaXa(TVector<T, d>(0, 0, 0))
	, LambdaRa(TVector<T, d>(0, 0, 0))
	, LambdaXb(TVector<T, d>(0, 0, 0))
	, LambdaRb(TVector<T, d>(0, 0, 0))
	, PrevTickCX(TVector<T, d>(0, 0, 0))
	, PrevTickCR(TVector<T, d>(0, 0, 0))
	, PrevItCX(TVector<T, d>(0, 0, 0))
	, PrevItCR(TVector<T, d>(0, 0, 0))
	, Level(INDEX_NONE)
	, ParticleLevels({ INDEX_NONE, INDEX_NONE })
{
}

//
// Container JointSettings
//

template<class T, int d>
TPBD6DJointSolverSettings<T, d>::TPBD6DJointSolverSettings()
	: SolveTolerance(KINDA_SMALL_NUMBER)
	, InvertedAxisTolerance( 0.001f)	// 1 - Cos(97.5deg)
	, SwingTwistAngleTolerance(1.0e-6f)
	, bApplyProjection(false)
	, MaxIterations(10)
	, MaxPreIterations(0)
	, MaxDriveIterations(1)
	, MaxRotComponent(0.0f)
	, PBDMinParentMassRatio(0.5f)
	, PBDMaxInertiaRatio(5.0f)
	, FreezeIterations(0)
	, FrozenIterations(0)
	, bEnableAutoStiffness(true)
	, bEnableTwistLimits(1)
	, bEnableSwingLimits(1)
	, bEnableDrives(1)
	, XPBDAlphaX(0)
	, XPBDAlphaR(0)
	, XPBDBetaX(0)
	, XPBDBetaR(0)
	, PBDStiffness(0.0f)
	, PBDDriveStiffness(0.0f)
	, bFastSolve(false)
{
}

//
// Constraint Container
//

template<class T, int d>
TPBD6DJointConstraints<T, d>::TPBD6DJointConstraints(const TPBD6DJointSolverSettings<T, d>& InSettings)
	: Settings(InSettings)
	, PreApplyCallback(nullptr)
	, PostApplyCallback(nullptr)
{
}

template<class T, int d>
TPBD6DJointConstraints<T, d>::~TPBD6DJointConstraints() {}


template<class T, int d>
const TPBD6DJointSolverSettings<T, d>& TPBD6DJointConstraints<T, d>::GetSettings() const
{
	return Settings;
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::SetSettings(const TPBD6DJointSolverSettings<T, d>& InSettings)
{
	Settings = InSettings;
}

template<class T, int d>
int32 TPBD6DJointConstraints<T, d>::NumConstraints() const
{
	return ConstraintParticles.Num();
}

template<class T, int d>
typename TPBD6DJointConstraints<T, d>::FConstraintHandle* TPBD6DJointConstraints<T, d>::AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames)
{
	int ConstraintIndex = Handles.Num();
	Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
	ConstraintParticles.Add(InConstrainedParticles);
	ConstraintSettings.Add(FJointSettings());
	ConstraintSettings[ConstraintIndex].ConstraintFrames = ConstraintFrames;
	ConstraintStates.Add(FJointState());
	return Handles.Last();
}

template<class T, int d>
typename TPBD6DJointConstraints<T, d>::FConstraintHandle* TPBD6DJointConstraints<T, d>::AddConstraint(const FParticlePair& InConstrainedParticles, const TPBD6DJointSettings<T, d>& InConstraintSettings)
{
	int ConstraintIndex = Handles.Num();
	Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
	ConstraintParticles.Add(InConstrainedParticles);
	ConstraintSettings.Add(InConstraintSettings);
	ConstraintStates.Add(FJointState());
	return Handles.Last();
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::RemoveConstraint(int ConstraintIndex)
{
	FConstraintHandle* ConstraintHandle = Handles[ConstraintIndex];
	if (ConstraintHandle != nullptr)
	{
		// Release the handle for the freed constraint
		HandleAllocator.FreeHandle(ConstraintHandle);
		Handles[ConstraintIndex] = nullptr;
	}

	// Swap the last constraint into the gap to keep the array packed
	ConstraintParticles.RemoveAtSwap(ConstraintIndex);
	ConstraintSettings.RemoveAtSwap(ConstraintIndex);
	ConstraintStates.RemoveAtSwap(ConstraintIndex);
	Handles.RemoveAtSwap(ConstraintIndex);

	// Update the handle for the constraint that was moved
	if (ConstraintIndex < Handles.Num())
	{
		SetConstraintIndex(Handles[ConstraintIndex], ConstraintIndex);
	}
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
{
}


template<class T, int d>
void TPBD6DJointConstraints<T, d>::SetPreApplyCallback(const TD6JointPreApplyCallback<T, d>& Callback)
{
	PreApplyCallback = Callback;
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::ClearPreApplyCallback()
{
	PreApplyCallback = nullptr;
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::SetPostApplyCallback(const TD6JointPostApplyCallback<T, d>& Callback)
{
	PostApplyCallback = Callback;
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::ClearPostApplyCallback()
{
	PostApplyCallback = nullptr;
}

template<class T, int d>
const typename TPBD6DJointConstraints<T, d>::FConstraintHandle* TPBD6DJointConstraints<T, d>::GetConstraintHandle(int32 ConstraintIndex) const
{
	return Handles[ConstraintIndex];
}

template<class T, int d>
typename TPBD6DJointConstraints<T, d>::FConstraintHandle* TPBD6DJointConstraints<T, d>::GetConstraintHandle(int32 ConstraintIndex)
{
	return Handles[ConstraintIndex];
}

template<class T, int d>
const typename TPBD6DJointConstraints<T, d>::FParticlePair& TPBD6DJointConstraints<T, d>::GetConstrainedParticles(int32 ConstraintIndex) const
{
	return ConstraintParticles[ConstraintIndex];
}

template<class T, int d>
int32 TPBD6DJointConstraints<T, d>::GetConstraintLevel(int32 ConstraintIndex) const
{
	return ConstraintStates[ConstraintIndex].Level;
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::SetParticleLevels(int32 ConstraintIndex, const TVector<int32, 2>& ParticleLevels)
{
	ConstraintStates[ConstraintIndex].Level = FMath::Min(ParticleLevels[0], ParticleLevels[1]);
	ConstraintStates[ConstraintIndex].ParticleLevels = ParticleLevels;
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::UpdatePositionBasedState(const T Dt)
{
	// @todo(ccaulfield): repurposing this since it is called before Apply, but maybe we need to rename the callback
	for (FJointState& State : ConstraintStates)
	{
		// @todo(ccaulfield): we should reinitialize PrevCX and PrevCR when initialized and teleported, etc
		State.LambdaXa = TVector<T, d>(0, 0, 0);
		State.LambdaRa = TVector<T, d>(0, 0, 0);
		State.LambdaXb = TVector<T, d>(0, 0, 0);
		State.LambdaRb = TVector<T, d>(0, 0, 0);
		State.PrevTickCX = State.PrevItCX;
		State.PrevTickCR = State.PrevItCR;
	}
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::CalculateConstraintSpace(int32 ConstraintIndex, TVector<T, d>& OutXa, PMatrix<T, d, d>& OutRa, TVector<T, d>& OutXb, PMatrix<T, d, d>& OutRb, TVector<T, d>& OutCR) const
{
	const TVector<TGeometryParticleHandle<T, d>*, 2>& ConstrainedParticles = ConstraintParticles[ConstraintIndex];
	const TPBD6DJointSettings<T, d>& JointSettings = ConstraintSettings[ConstraintIndex];

	const TVector<T, d>& P0 = ConstrainedParticles[0]->AsDynamic() ? ConstrainedParticles[0]->AsDynamic()->P() : ConstrainedParticles[0]->X();
	const TVector<T, d>& P1 = ConstrainedParticles[1]->AsDynamic() ? ConstrainedParticles[1]->AsDynamic()->P() : ConstrainedParticles[1]->X();
	const TRotation<T, d>& Q0 = ConstrainedParticles[0]->AsDynamic() ? ConstrainedParticles[0]->AsDynamic()->Q() : ConstrainedParticles[0]->R();
	const TRotation<T, d>& Q1 = ConstrainedParticles[1]->AsDynamic() ? ConstrainedParticles[1]->AsDynamic()->Q() : ConstrainedParticles[1]->R();
	const TVector<T, d>& XL0 = JointSettings.ConstraintFrames[0].GetTranslation();
	const TRotation<T, d>& RL0 = JointSettings.ConstraintFrames[0].GetRotation();
	const TVector<T, d>& XL1 = JointSettings.ConstraintFrames[1].GetTranslation();
	const TRotation<T, d>& RL1 = JointSettings.ConstraintFrames[1].GetRotation();
	const TVector<T, d> X0 = P0 + Q0 * XL0;
	const TVector<T, d> X1 = P1 + Q1 * XL1;
	const TRotation<T, d> R0 = Q0 * RL0;
	const TRotation<T, d> R1 = Q1 * RL1;

	TPBD6DJointConstraintUtilities<T, d>::Calculate6dConstraintRotation(Settings, R0, R1, JointSettings.Motion, OutCR, OutRa, OutRb);
	OutXa = X0;
	OutXb = X1;
	OutRa = OutRa.GetTransposed();
	OutRb = OutRb.GetTransposed();
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
{
	// @todo(ccaulfield): handles should be sorted by level by the constraint rule/graph
	// @todo(ccaulfield): the best sort order depends on whether we are freezing.
	// If we are freezing, we want the root-most bodies solved first, otherwise we want them last
	TArray<FConstraintHandle*> SortedConstraintHandles = InConstraintHandles;
	SortedConstraintHandles.Sort([](const FConstraintHandle& L, const FConstraintHandle& R)
		{
			return L.GetConstraintLevel() > R.GetConstraintLevel();
		});

	if (PreApplyCallback != nullptr)
	{
		PreApplyCallback(Dt, SortedConstraintHandles);
	}

	if (Settings.bFastSolve)
	{
		for (FConstraintHandle* ConstraintHandle : SortedConstraintHandles)
		{
			ApplySingleFast(Dt, ConstraintHandle->GetConstraintIndex(), It, NumIts);
		}
	}
	else
	{
		T FreezeScale = (T)1;
		if ((Settings.FreezeIterations + Settings.FrozenIterations) > 0)
		{
			const int32 BeginFreezingAt = NumIts - (Settings.FreezeIterations + Settings.FrozenIterations);
			const int32 BeginFrozenAt = NumIts - Settings.FrozenIterations;
			if (It >= BeginFrozenAt)
			{
				FreezeScale = (T)0;
			}
			else if (It >= BeginFreezingAt)
			{
				FreezeScale = (T)1 - (T)(It - BeginFreezingAt + 1) / (T)(BeginFrozenAt - BeginFreezingAt);
			}
		}

		for (FConstraintHandle* ConstraintHandle : SortedConstraintHandles)
		{
			ApplySingle(Dt, ConstraintHandle->GetConstraintIndex(), FreezeScale);
		}
	}

	if (PostApplyCallback != nullptr)
	{
		PostApplyCallback(Dt, SortedConstraintHandles);
	}
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::ApplyPushOut(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles)
{
	for (FConstraintHandle* ConstraintHandle : InConstraintHandles)
	{
		ApplyPushOutSingle(Dt, ConstraintHandle->GetConstraintIndex());
	}
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::ApplySingle(const T Dt, const int32 ConstraintIndex, const T FreezeScale)
{
	const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
	UE_LOG(LogChaos6DJoint, Verbose, TEXT("6DoF Solve Constraint %d %s %s (dt = %f; freeze = %f)"), ConstraintIndex, *Constraint[0]->ToString(), *Constraint[1]->ToString(), Dt, FreezeScale);

	if (Constraint[0]->AsDynamic() && Constraint[1]->AsDynamic())
	{
		ApplyDynamicDynamic(Dt, ConstraintIndex, 0, 1, FreezeScale);
	}
	else if (Constraint[0]->AsDynamic())
	{
		ApplyDynamicStatic(Dt, ConstraintIndex, 0, 1);
	}
	else
	{
		ApplyDynamicStatic(Dt, ConstraintIndex, 1, 0);
	}
}

/**
 * Increase the lower inertia components to ensure that the maximum ratio between any pair of elements is MaxRatio.
 *
 * @param InI The input inertia.
 * @return An altered inertia so that the minimum element is at least MaxElement/MaxRatio.
 */
template<class T, int d>
TVector<T, d> ConditionInertia(const TVector<T, d>& InI, const T MaxRatio)
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


/**
 * Increase the IParent inertia so that its largest component is at least MinRatio times the largest IChild component.
 * This is used to condition joint chains for more robust solving with low iteration counts or larger time steps.
 *
 * @param IParent The input inertia.
 * @param IChild The input inertia.
 * @param OutIParent The output inertia.
 * @param MinRatio Parent inertia will be at least this multiple of child inertia
 * @return The max/min ratio of InI elements.
 */
template<class T, int d>
TVector<T, d> ConditionParentInertia(const TVector<T, d>& IParent, const TVector<T, d>& IChild, const T MinRatio)
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

template<class T, int d>
T ConditionParentMass(const T MParent, const T MChild, const T MinRatio)
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

template<class T, int d>
void GetConditionedInverseMass(const TPBDRigidParticleHandle<T, d>* PParent, const TPBDRigidParticleHandle<T, d>* PChild, T& OutInvMParent, T& OutInvMChild, PMatrix<T, d, d>& OutInvIParent, PMatrix<T, d, d>& OutInvIChild, const T MinParentMassRatio, const T MaxInertiaRatio)
{
	T MParent = PParent->M();
	T MChild = PChild->M();
	MParent = ConditionParentMass<T, d>(MParent, MChild, MinParentMassRatio);

	TVector<T, d> IParent = ConditionInertia<T, d>(PParent->I().GetDiagonal(), MaxInertiaRatio);
	TVector<T, d> IChild = ConditionInertia<T, d>(PChild->I().GetDiagonal(), MaxInertiaRatio);
	IParent = ConditionParentInertia(IParent, IChild, MinParentMassRatio);

	OutInvMParent = (T)1 / MParent;
	OutInvMChild = (T)1 / MChild;
	OutInvIParent = PMatrix<T, d, d>((T)1 / IParent.X, (T)1 / IParent.Y, (T)1 / IParent.Z);
	OutInvIChild = PMatrix<T, d, d>((T)1 / IChild.X, (T)1 / IChild.Y, (T)1 / IChild.Z);
}


template<class T, int d>
void GetConditionedInverseMass(const TPBDRigidParticleHandle<T, d>* P0, T& OutInvM0, PMatrix<T, d, d>& OutInvI0, const T MaxInertiaRatio)
{
	TVector<T, d> I0 = ConditionInertia<T, d>(P0->I().GetDiagonal(), MaxInertiaRatio);

	OutInvM0 = P0->InvM();
	OutInvI0 = PMatrix<T, d, d>((T)1 / I0.X, (T)1 / I0.Y, (T)1 / I0.Z);
}


template<class T, int d>
void TPBD6DJointConstraints<T, d>::ApplyDynamicDynamic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 PBDRigid1Index, const T FreezeScale)
{
	check((PBDRigid0Index == 0) || (PBDRigid0Index == 1));
	check((PBDRigid1Index == 0) || (PBDRigid1Index == 1));
	check(PBDRigid0Index != PBDRigid1Index);

	TPBDRigidParticleHandle<T, d>* PBDRigid0 = ConstraintParticles[ConstraintIndex][PBDRigid0Index]->AsDynamic();
	TPBDRigidParticleHandle<T, d>* PBDRigid1 = ConstraintParticles[ConstraintIndex][PBDRigid1Index]->AsDynamic();
	check(PBDRigid0 && PBDRigid1 && (PBDRigid0->Island() == PBDRigid1->Island()));

	TRotation<T, d> Q0 = PBDRigid0->Q();
	TVector<T, d> P0 = PBDRigid0->P();
	const TVector<T, d>& XL0 = ConstraintSettings[ConstraintIndex].ConstraintFrames[PBDRigid0Index].GetTranslation();
	const TRotation<T, d>& RL0 = ConstraintSettings[ConstraintIndex].ConstraintFrames[PBDRigid0Index].GetRotation();
	TRotation<T, d> Q1 = PBDRigid1->Q();
	TVector<T, d> P1 = PBDRigid1->P();
	const TVector<T, d>& XL1 = ConstraintSettings[ConstraintIndex].ConstraintFrames[PBDRigid1Index].GetTranslation();
	const TRotation<T, d>& RL1 = ConstraintSettings[ConstraintIndex].ConstraintFrames[PBDRigid1Index].GetRotation();
	float InvM0, InvM1, InvM0F, InvM1F;
	PMatrix<T, d, d> InvIL0, InvIL1, InvIL0F, InvIL1F;

	// Adjust mass and freeze particle up the chain (nearest to a connection to a non-dynamic particle).
	if (ConstraintStates[ConstraintIndex].ParticleLevels[0] < ConstraintStates[ConstraintIndex].ParticleLevels[1])
	{
		GetConditionedInverseMass(PBDRigid0, PBDRigid1, InvM0, InvM1, InvIL0, InvIL1, Settings.PBDMinParentMassRatio, Settings.PBDMaxInertiaRatio);
		InvM0F = InvM0 * FreezeScale;
		InvM1F = InvM1;
		InvIL0F = InvIL0 * FreezeScale;
		InvIL1F = InvIL1;
	}
	else if (ConstraintStates[ConstraintIndex].ParticleLevels[0] > ConstraintStates[ConstraintIndex].ParticleLevels[1])
	{
		GetConditionedInverseMass(PBDRigid1, PBDRigid0, InvM1, InvM0, InvIL1, InvIL0, Settings.PBDMinParentMassRatio, Settings.PBDMaxInertiaRatio);
		InvM0F = InvM0;
		InvM1F = InvM1 * FreezeScale;
		InvIL0F = InvIL0;
		InvIL1F = InvIL1 * FreezeScale;
	}
	else
	{
		GetConditionedInverseMass(PBDRigid1, PBDRigid0, InvM1, InvM0, InvIL1, InvIL0, (T)0, Settings.PBDMaxInertiaRatio);
		InvM0F = InvM0;
		InvM1F = InvM1;
		InvIL0F = InvIL0;
		InvIL1F = InvIL1;
	}

	TVector<E6DJointMotionType, d> AngularMotionTypes = ConstraintSettings[ConstraintIndex].Motion.AngularMotionTypes;
	bool bTwistDriveEnabled = ConstraintSettings[ConstraintIndex].Motion.bAngularTwistDriveEnabled;
	bool bSwingDriveEnabled = ConstraintSettings[ConstraintIndex].Motion.bAngularSwingDriveEnabled;
	bool bAutoStiffness = Settings.bEnableAutoStiffness;

	if (Settings.MaxPreIterations > 0)
	{
		TPBD6DJointConstraintUtilities<T, d>::Solve3dConstraint(Settings, P0, Q0, InvM0F, InvIL0F, XL0, RL0, P1, Q1, InvM1F, InvIL1F, XL1, RL1, ConstraintSettings[ConstraintIndex].Motion);
	}
	if (Settings.MaxIterations > 0)
	{
		ConstraintSettings[ConstraintIndex].Motion.bAngularTwistDriveEnabled = false;
		ConstraintSettings[ConstraintIndex].Motion.bAngularSwingDriveEnabled = false;

		//TPBD6DJointConstraintUtilities<T, d>::Solve6dConstraint(Settings, Dt, P0, Q0, InvM0F, InvIL0F, XL0, RL0, P1, Q1, InvM1F, InvIL1F, XL1, RL1, ConstraintSettings[ConstraintIndex].Motion, ConstraintStates[ConstraintIndex]);
		TPBD6DJointConstraintUtilities<T, d>::Solve6dConstraint(Settings, Dt, P1, Q1, InvM1F, InvIL1F, XL1, RL1, P0, Q0, InvM0F, InvIL0F, XL0, RL0, ConstraintSettings[ConstraintIndex].Motion, ConstraintStates[ConstraintIndex]);

		ConstraintSettings[ConstraintIndex].Motion.bAngularTwistDriveEnabled = bTwistDriveEnabled;
		ConstraintSettings[ConstraintIndex].Motion.bAngularSwingDriveEnabled = bSwingDriveEnabled;
	}
	if ((bTwistDriveEnabled || bSwingDriveEnabled) && (Settings.MaxDriveIterations > 0))
	{
		Settings.bEnableAutoStiffness = false;
		ConstraintSettings[ConstraintIndex].Motion.AngularMotionTypes = { E6DJointMotionType::Free, E6DJointMotionType::Free , E6DJointMotionType::Free };

		TPBD6DJointConstraintUtilities<T, d>::Solve6dConstraint(Settings, Dt, P1, Q1, InvM1, InvIL1, XL1, RL1, P0, Q0, InvM0, InvIL0, XL0, RL0, ConstraintSettings[ConstraintIndex].Motion, ConstraintStates[ConstraintIndex]);

		Settings.bEnableAutoStiffness = bAutoStiffness;
		ConstraintSettings[ConstraintIndex].Motion.AngularMotionTypes = AngularMotionTypes;
	}

	PBDRigid0->SetQ(Q0);
	PBDRigid0->SetP(P0);
	PBDRigid1->SetQ(Q1);
	PBDRigid1->SetP(P1);
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::ApplyDynamicStatic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 Static1Index)
{
	check((PBDRigid0Index == 0) || (PBDRigid0Index == 1));
	check((Static1Index == 0) || (Static1Index == 1));
	check(PBDRigid0Index != Static1Index);

	TPBDRigidParticleHandle<T, d>* PBDRigid0 = ConstraintParticles[ConstraintIndex][PBDRigid0Index]->AsDynamic();
	TGeometryParticleHandle<T, d>* Static1 = ConstraintParticles[ConstraintIndex][Static1Index];
	check(PBDRigid0 && Static1 && !Static1->AsDynamic());

	TRotation<T, d> Q0 = PBDRigid0->Q();
	TVector<T, d> P0 = PBDRigid0->P();
	const TVector<T, d>& XL0 = ConstraintSettings[ConstraintIndex].ConstraintFrames[PBDRigid0Index].GetTranslation();
	const TRotation<T, d>& RL0 = ConstraintSettings[ConstraintIndex].ConstraintFrames[PBDRigid0Index].GetRotation();
	TRotation<T, d> Q1 = Static1->R();
	TVector<T, d> P1 = Static1->X();
	const TVector<T, d>& XL1 = ConstraintSettings[ConstraintIndex].ConstraintFrames[Static1Index].GetTranslation();
	const TRotation<T, d>& RL1 = ConstraintSettings[ConstraintIndex].ConstraintFrames[Static1Index].GetRotation();
	float InvM0;
	PMatrix<T, d, d> InvIL0;
	GetConditionedInverseMass(PBDRigid0, InvM0, InvIL0, Settings.PBDMaxInertiaRatio);
	const float InvM1 = 0;
	const PMatrix<T, d, d> InvIL1 = PMatrix<T, d, d>(0, 0, 0);

	TVector<E6DJointMotionType, d> AngularMotionTypes = ConstraintSettings[ConstraintIndex].Motion.AngularMotionTypes;
	bool bTwistDriveEnabled = ConstraintSettings[ConstraintIndex].Motion.bAngularTwistDriveEnabled;
	bool bSwingDriveEnabled = ConstraintSettings[ConstraintIndex].Motion.bAngularSwingDriveEnabled;
	bool bAutoStiffness = Settings.bEnableAutoStiffness;

	// NOTE: We put the static body first in the solver - swing axes are calculated relative to this
	if (Settings.MaxPreIterations > 0)
	{
		TPBD6DJointConstraintUtilities<T, d>::Solve3dConstraint(Settings, P1, Q1, InvM1, InvIL1, XL1, RL1, P0, Q0, InvM0, InvIL0, XL0, RL0, ConstraintSettings[ConstraintIndex].Motion);
	}
	if (Settings.MaxIterations > 0)
	{
		ConstraintSettings[ConstraintIndex].Motion.bAngularTwistDriveEnabled = false;
		ConstraintSettings[ConstraintIndex].Motion.bAngularSwingDriveEnabled = false;

		TPBD6DJointConstraintUtilities<T, d>::Solve6dConstraint(Settings, Dt, P1, Q1, InvM1, InvIL1, XL1, RL1, P0, Q0, InvM0, InvIL0, XL0, RL0, ConstraintSettings[ConstraintIndex].Motion, ConstraintStates[ConstraintIndex]);

		ConstraintSettings[ConstraintIndex].Motion.bAngularTwistDriveEnabled = bTwistDriveEnabled;
		ConstraintSettings[ConstraintIndex].Motion.bAngularSwingDriveEnabled = bSwingDriveEnabled;
	}
	if ((bTwistDriveEnabled || bSwingDriveEnabled) && (Settings.MaxDriveIterations > 0))
	{
		Settings.bEnableAutoStiffness = false;
		ConstraintSettings[ConstraintIndex].Motion.AngularMotionTypes = { E6DJointMotionType::Free, E6DJointMotionType::Free , E6DJointMotionType::Free };

		TPBD6DJointConstraintUtilities<T, d>::Solve6dConstraint(Settings, Dt, P1, Q1, InvM1, InvIL1, XL1, RL1, P0, Q0, InvM0, InvIL0, XL0, RL0, ConstraintSettings[ConstraintIndex].Motion, ConstraintStates[ConstraintIndex]);

		Settings.bEnableAutoStiffness = bAutoStiffness;
		ConstraintSettings[ConstraintIndex].Motion.AngularMotionTypes = AngularMotionTypes;
	}

	PBDRigid0->SetQ(Q0);
	PBDRigid0->SetP(P0);
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::ApplyPushOutSingle(const T Dt, const int32 ConstraintIndex)
{
	// Correct any remaining error by translating
	if (Settings.bApplyProjection)
	{
		const TVector<TGeometryParticleHandle<T, d>*, 2>& ConstrainedParticles = ConstraintParticles[ConstraintIndex];
		const TPBD6DJointSettings<T, d>& JointSettings = ConstraintSettings[ConstraintIndex];

		TVector<T, d> P0 = ConstrainedParticles[0]->AsDynamic() ? ConstrainedParticles[0]->AsDynamic()->P() : ConstrainedParticles[0]->X();
		TVector<T, d> P1 = ConstrainedParticles[1]->AsDynamic() ? ConstrainedParticles[1]->AsDynamic()->P() : ConstrainedParticles[1]->X();
		TRotation<T, d> Q0 = ConstrainedParticles[0]->AsDynamic() ? ConstrainedParticles[0]->AsDynamic()->Q() : ConstrainedParticles[0]->R();
		TRotation<T, d> Q1 = ConstrainedParticles[1]->AsDynamic() ? ConstrainedParticles[1]->AsDynamic()->Q() : ConstrainedParticles[1]->R();
		const TVector<T, d>& XL0 = JointSettings.ConstraintFrames[0].GetTranslation();
		const TRotation<T, d>& RL0 = JointSettings.ConstraintFrames[0].GetRotation();
		const TVector<T, d>& XL1 = JointSettings.ConstraintFrames[1].GetTranslation();
		const TRotation<T, d>& RL1 = JointSettings.ConstraintFrames[1].GetRotation();
		TVector<T, d> X0 = P0 + Q0 * XL0;
		TVector<T, d> X1 = P1 + Q1 * XL1;
		const T InvM0 = ConstrainedParticles[0]->AsDynamic()? ConstrainedParticles[0]->AsDynamic()->InvM() : (T)0;
		const T InvM1 = ConstrainedParticles[1]->AsDynamic() ? ConstrainedParticles[1]->AsDynamic()->InvM() : (T)0;


		const TVector<T, d> DeltaProj = (X1 - X0) / (InvM0 + InvM1);
		P0 += InvM0 * DeltaProj;
		P1 -= InvM1 * DeltaProj;

		if (ConstrainedParticles[0]->AsDynamic())
		{
			ConstrainedParticles[0]->AsDynamic()->P() = P0;
		}
		if (ConstrainedParticles[1]->AsDynamic())
		{
			ConstrainedParticles[1]->AsDynamic()->P() = P1;
		}
	}
}

template<class T, int d>
void ApplyJointPositionConstraint(
	const T Dt,
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TPBD6DJointSettings<T, d>& JointSettings,
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
	PMatrix<T, d, d> InvI0 = Utilities::Multiply(Q0.ToMatrix(), Utilities::Multiply(InvIL0, Q0.ToMatrix().GetTransposed()));
	PMatrix<T, d, d> InvI1 = Utilities::Multiply(Q1.ToMatrix(), Utilities::Multiply(InvIL1, Q1.ToMatrix().GetTransposed()));

	TVector<T, d> DP0, DP1;
	TRotation<T, d> DQ0, DQ1;
	TPBD6DJointConstraintUtilities<T, d>::Calculate3dDelta(SolverSettings, P0, Q0, InvM0, InvI0, P1, Q1, InvM1, InvI1, X0, X1, JointSettings.Motion, DP0, DQ0, DP1, DQ1);

	P0 = P0 + DP0;
	P1 = P1 + DP1;
	Q0 = (Q0 + DQ0).GetNormalized();
	Q1 = (Q1 + DQ1).GetNormalized();
	Q1.EnforceShortestArcWith(Q0);
}

template<class T, int d>
void ApplyJointTwistConstraint(
	const T Dt,
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TPBD6DJointSettings<T, d>& JointSettings,
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

	TRotation<T, d> R01 = R0.Inverse() * R1;
	TRotation<T, d> R01Twist, R01Swing;
	R01.ToSwingTwist(T6DJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
	R01Swing = R01Swing.GetNormalized();
	R01Twist = R01Twist.GetNormalized();

	TVector<T, d> TwistAxis01;
	T TwistAngle;
	R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, T6DJointConstants<T, d>::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
	if (TwistAngle > PI)
	{
		TwistAngle = TwistAngle - (T)2 * PI;
	}
	if (TVector<T, d>::DotProduct(TwistAxis01, T6DJointConstants<T, d>::TwistAxis()) < 0)
	{
		TwistAxis01 = -TwistAxis01;
		TwistAngle = -TwistAngle;
	}

	TVector<T, d> TwistAxis0 = R0 * TwistAxis01;
	TVector<T, d> TwistAxis1 = R1 * TwistAxis01;
	T TwistAngleMax = FLT_MAX;
	if (JointSettings.Motion.AngularMotionTypes[(int32)E6DJointAngularConstraintIndex::Twist] == E6DJointMotionType::Limited)
	{
		TwistAngleMax = JointSettings.Motion.AngularLimits[(int32)E6DJointAngularConstraintIndex::Twist];
	}
	else if (JointSettings.Motion.AngularMotionTypes[(int32)E6DJointAngularConstraintIndex::Twist] == E6DJointMotionType::Locked)
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
	T DTwistAngle0 = DTwistAngle * InvM0 / (InvM0 + InvM1);
	T DTwistAngle1 = -DTwistAngle * InvM1 / (InvM0 + InvM1);

	TVector<T, d> W0 = TwistAxis0 * DTwistAngle0;
	TVector<T, d> W1 = TwistAxis1 * DTwistAngle1;
	TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
	TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
	Q0 = (Q0 + DQ0).GetNormalized();
	Q1 = (Q1 + DQ1).GetNormalized();
	Q1.EnforceShortestArcWith(Q0);
}

template<class T, int d>
void ApplyJointConeConstraint(
	const T Dt,
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TPBD6DJointSettings<T, d>& JointSettings,
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

	// Calculate Swing axis for each body
	TRotation<T, d> R01 = R0.Inverse() * R1;
	TRotation<T, d> R01Twist, R01Swing;
	R01.ToSwingTwist(T6DJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
	R01Swing = R01Swing.GetNormalized();
	R01Twist = R01Twist.GetNormalized();

	TVector<T, d> SwingAxis01;
	T SwingAngle;
	R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, T6DJointConstants<T, d>::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
	if (SwingAngle > PI)
	{
		SwingAngle = SwingAngle - (T)2 * PI;
	}

	TVector<T, d> SwingAxis0 = R0 * SwingAxis01;
	TVector<T, d> SwingAxis1 = SwingAxis0;

	// Calculate swing limit for the current swing axis
	T SwingAngleMax = FLT_MAX;
	T Swing1Limit = JointSettings.Motion.AngularLimits[(int32)E6DJointAngularConstraintIndex::Swing1];
	T Swing2Limit = JointSettings.Motion.AngularLimits[(int32)E6DJointAngularConstraintIndex::Swing2];

	// Circular swing limit
	SwingAngleMax = Swing1Limit;

	// Elliptical swing limit
	if (!FMath::IsNearlyEqual(Swing1Limit, Swing2Limit, KINDA_SMALL_NUMBER))
	{
		SwingAngleMax = (T)0.5 * (Swing1Limit + Swing2Limit);

		// Map swing axis to ellipse and calculate limit for this swing axis
		//T DotSwing1 = FMath::Abs(TVector<T, d>::DotProduct(SwingAxis01, T6DJointConstants<T, d>::Swing1Axis()));
		//T DotSwing2 = FMath::Abs(TVector<T, d>::DotProduct(SwingAxis01, T6DJointConstants<T, d>::Swing2Axis()));
		//SwingAngleMax = FMath::Sqrt(Swing1Limit * DotSwing1 * Swing1Limit * DotSwing1 + Swing2Limit * DotSwing2 * Swing2Limit * DotSwing2);
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
	T DSwingAngle0 = DSwingAngle * InvM0 / (InvM0 + InvM1);
	T DSwingAngle1 = -DSwingAngle * InvM1 / (InvM0 + InvM1);

	// Apply swing correction
	TVector<T, d> W0 = SwingAxis0 * DSwingAngle0;
	TVector<T, d> W1 = SwingAxis1 * DSwingAngle1;
	TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
	TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
	Q0 = (Q0 + DQ0).GetNormalized();
	Q1 = (Q1 + DQ1).GetNormalized();
	Q1.EnforceShortestArcWith(Q0);
}

template<class T, int d>
void ApplyJointSwingConstraint(
	const T Dt,
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TPBD6DJointSettings<T, d>& JointSettings,
	const int32 Index0,
	const int32 Index1,
	const E6DJointAngularConstraintIndex SwingConstraint,
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
		if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraint] == E6DJointMotionType::Limited)
		{
			T Swing1Limit = JointSettings.Motion.AngularLimits[(int32)SwingConstraint];
			SwingAngleMax = Swing1Limit;
		}
		else if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraint] == E6DJointMotionType::Locked)
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
		T DSwingAngle0 = DSwingAngle * InvM0 / (InvM0 + InvM1);
		T DSwingAngle1 = -DSwingAngle * InvM1 / (InvM0 + InvM1);

		// Apply swing correction
		TVector<T, d> W0 = SwingAxis0 * DSwingAngle0;
		TVector<T, d> W1 = SwingAxis1 * DSwingAngle1;
		TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
		TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
		Q0 = (Q0 + DQ0).GetNormalized();
		Q1 = (Q1 + DQ1).GetNormalized();
		Q1.EnforceShortestArcWith(Q0);
	}
}

template<class T, int d>
void ApplyJointTwistDrive(
	const T Dt,
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TPBD6DJointSettings<T, d>& JointSettings,
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

	TRotation<T, d> R01 = R0.Inverse() * R1;
	TRotation<T, d> R01Twist, R01Swing;
	R01.ToSwingTwist(T6DJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
	R01Swing = R01Swing.GetNormalized();
	R01Twist = R01Twist.GetNormalized();

	TVector<T, d> TwistAxis01;
	T TwistAngle;
	R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, T6DJointConstants<T, d>::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
	if (TwistAngle > PI)
	{
		TwistAngle = TwistAngle - (T)2 * PI;
	}
	if (TVector<T, d>::DotProduct(TwistAxis01, T6DJointConstants<T, d>::TwistAxis()) < 0)
	{
		TwistAxis01 = -TwistAxis01;
		TwistAngle = -TwistAngle;
	}

	TVector<T, d> TwistAxis0 = R0 * TwistAxis01;
	TVector<T, d> TwistAxis1 = R1 * TwistAxis01;
	T TwistAngleTarget = JointSettings.Motion.AngularDriveTargetAngles[(int32)E6DJointAngularConstraintIndex::Twist];
	T Stiffness = (SolverSettings.PBDDriveStiffness > 0) ? SolverSettings.PBDDriveStiffness : JointSettings.Motion.AngularDriveStiffness;
	T DriveStiffness = FMath::Clamp(Stiffness, (T)0, (T)1);
	T DTwistAngle = TwistAngle - TwistAngleTarget;
	T DTwistAngle0 = DriveStiffness * DTwistAngle * InvM0 / (InvM0 + InvM1);
	T DTwistAngle1 = -DriveStiffness * DTwistAngle * InvM1 / (InvM0 + InvM1);

	TVector<T, d> W0 = TwistAxis0 * DTwistAngle0;
	TVector<T, d> W1 = TwistAxis1 * DTwistAngle1;
	TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
	TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
	Q0 = (Q0 + DQ0).GetNormalized();
	Q1 = (Q1 + DQ1).GetNormalized();
	Q1.EnforceShortestArcWith(Q0);
}

template<class T, int d>
void ApplyJointConeDrive(
	const T Dt,
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TPBD6DJointSettings<T, d>& JointSettings,
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

	// Calculate Swing axis for each body
	TRotation<T, d> R01 = R0.Inverse() * R1;
	TRotation<T, d> R01Twist, R01Swing;
	R01.ToSwingTwist(T6DJointConstants<T, d>::TwistAxis(), R01Swing, R01Twist);
	R01Swing = R01Swing.GetNormalized();
	R01Twist = R01Twist.GetNormalized();

	TVector<T, d> SwingAxis01;
	T SwingAngle;
	R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, T6DJointConstants<T, d>::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
	if (SwingAngle > PI)
	{
		SwingAngle = SwingAngle - (T)2 * PI;
	}

	TVector<T, d> SwingAxis0 = R0 * SwingAxis01;
	TVector<T, d> SwingAxis1 = SwingAxis0;

	// Circular swing target (max of Swing1, Swing2 targets)
	T Swing1Target = JointSettings.Motion.AngularDriveTargetAngles[(int32)E6DJointAngularConstraintIndex::Swing1];
	T Swing2Target = JointSettings.Motion.AngularDriveTargetAngles[(int32)E6DJointAngularConstraintIndex::Swing2];
	T SwingAngleTarget = FMath::Max(Swing1Target, Swing2Target);

	T Stiffness = (SolverSettings.PBDDriveStiffness > 0) ? SolverSettings.PBDDriveStiffness : JointSettings.Motion.AngularDriveStiffness;
	T DriveStiffness = FMath::Clamp(Stiffness, (T)0, (T)1);
	T DSwingAngle = SwingAngle - SwingAngleTarget;
	T DSwingAngle0 = DriveStiffness * DSwingAngle * InvM0 / (InvM0 + InvM1);
	T DSwingAngle1 = -DriveStiffness * DSwingAngle * InvM1 / (InvM0 + InvM1);

	// Apply swing correction
	TVector<T, d> W0 = SwingAxis0 * DSwingAngle0;
	TVector<T, d> W1 = SwingAxis1 * DSwingAngle1;
	TRotation<T, d> DQ0 = (TRotation<T, d>::FromElements(W0, (T)0.0) * Q0) * (T)0.5;
	TRotation<T, d> DQ1 = (TRotation<T, d>::FromElements(W1, (T)0.0) * Q1) * (T)0.5;
	Q0 = (Q0 + DQ0).GetNormalized();
	Q1 = (Q1 + DQ1).GetNormalized();
	Q1.EnforceShortestArcWith(Q0);
}

template<class T, int d>
void ApplyJointSLerpDrive(
	const T Dt,
	const TPBD6DJointSolverSettings<T, d>& SolverSettings,
	const TPBD6DJointSettings<T, d>& JointSettings,
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

	const TRotation<T, d> TargetR1 = R0 * JointSettings.Motion.AngularDriveTarget;
	const TRotation<T, d> DR1 = TargetR1 * R1.Inverse();
	const TRotation<T, d> TargetQ0 = DR1.Inverse() * Q0;
	const TRotation<T, d> TargetQ1 = DR1 * Q1;

	T Stiffness = (SolverSettings.PBDDriveStiffness > 0) ? SolverSettings.PBDDriveStiffness : JointSettings.Motion.AngularDriveStiffness;
	T DriveStiffness = FMath::Clamp(Stiffness, (T)0, (T)1);
	const T F0 = DriveStiffness * InvM0 / (InvM0 + InvM1);
	const T F1 = DriveStiffness * InvM1 / (InvM0 + InvM1);

	Q0 = TRotation<T, d>::Slerp(Q0, TargetQ0, F0);
	Q1 = TRotation<T, d>::Slerp(Q1, TargetQ1, F1);
	Q1.EnforceShortestArcWith(Q0);
}

template<class T, int d>
void TPBD6DJointConstraints<T, d>::ApplySingleFast(const T Dt, const int32 ConstraintIndex, const int32 It, const int32 NumIts)
{
	const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
	UE_LOG(LogChaos6DJoint, Verbose, TEXT("6DoF FastSolve Constraint %d %s %s (dt = %f; it = %d / %d)"), ConstraintIndex, *Constraint[0]->ToString(), *Constraint[1]->ToString(), Dt, It, NumIts);

	const FJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];

	// Switch particles - internally we assume the first body is the parent (i.e., the space in which constraint limits are specified)
	const int32 Index0 = 1;
	const int32 Index1 = 0;
	TGenericParticleHandle<T, d> Particle0 = TGenericParticleHandle<T, d>(ConstraintParticles[ConstraintIndex][Index0]);
	TGenericParticleHandle<T, d> Particle1 = TGenericParticleHandle<T, d>(ConstraintParticles[ConstraintIndex][Index1]);
	TPBDRigidParticleHandle<T, d>* Rigid0 = ConstraintParticles[ConstraintIndex][Index0]->AsDynamic();
	TPBDRigidParticleHandle<T, d>* Rigid1 = ConstraintParticles[ConstraintIndex][Index1]->AsDynamic();

	TVector<T, d> P0 = Particle0->P();
	TRotation<T, d> Q0 = Particle0->Q();
	TVector<T, d> P1 = Particle1->P();
	TRotation<T, d> Q1 = Particle1->Q();
	float InvM0 = Particle0->InvM();
	float InvM1 = Particle1->InvM();
	PMatrix<T, d, d> InvIL0 = Particle0->InvI();
	PMatrix<T, d, d> InvIL1 = Particle1->InvI();

	Q1.EnforceShortestArcWith(Q0);

	// Adjust mass for stability
	if (Rigid0 && Rigid1)
	{
		if (ConstraintStates[ConstraintIndex].ParticleLevels[Index0] < ConstraintStates[ConstraintIndex].ParticleLevels[Index1])
		{
			GetConditionedInverseMass(Rigid0, Rigid1, InvM0, InvM1, InvIL0, InvIL1, Settings.PBDMinParentMassRatio, Settings.PBDMaxInertiaRatio);
		}
		else if (ConstraintStates[ConstraintIndex].ParticleLevels[Index0] > ConstraintStates[ConstraintIndex].ParticleLevels[Index1])
		{
			GetConditionedInverseMass(Rigid1, Rigid0, InvM1, InvM0, InvIL1, InvIL0, Settings.PBDMinParentMassRatio, Settings.PBDMaxInertiaRatio);
		}
		else
		{
			GetConditionedInverseMass(Rigid1, Rigid0, InvM1, InvM0, InvIL1, InvIL0, (T)0, Settings.PBDMaxInertiaRatio);
		}
	}

	E6DJointMotionType TwistMotion = JointSettings.Motion.AngularMotionTypes[(int32)E6DJointAngularConstraintIndex::Twist];
	E6DJointMotionType Swing1Motion = JointSettings.Motion.AngularMotionTypes[(int32)E6DJointAngularConstraintIndex::Swing1];
	E6DJointMotionType Swing2Motion = JointSettings.Motion.AngularMotionTypes[(int32)E6DJointAngularConstraintIndex::Swing2];

	// Apply angular drives
	if (Settings.bEnableDrives)
	{
		bool bTwistLocked = TwistMotion == E6DJointMotionType::Locked;
		bool bSwing1Locked = Swing1Motion == E6DJointMotionType::Locked;
		bool bSwing2Locked = Swing2Motion == E6DJointMotionType::Locked;

		// No SLerp drive if we have a locked rotation (it will be grayed out in the editor in this case, but could still have been set before the rotation was locked)
		if (JointSettings.Motion.bAngularSLerpDriveEnabled && !bTwistLocked && !bSwing1Locked && !bSwing2Locked)
		{
			ApplyJointSLerpDrive(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
		}

		if (JointSettings.Motion.bAngularTwistDriveEnabled && !bTwistLocked)
		{
			ApplyJointTwistDrive(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
		}

		if (JointSettings.Motion.bAngularSwingDriveEnabled && !bSwing1Locked && !bSwing2Locked)
		{
			ApplyJointConeDrive(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
		}
		else if (JointSettings.Motion.bAngularSwingDriveEnabled && !bSwing1Locked)
		{
			//ApplyJointSwingDrive(Dt, Settings, JointSettings, Index0, Index1, E6DJointAngularConstraintIndex::Swing1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
		}
		else if (JointSettings.Motion.bAngularSwingDriveEnabled && !bSwing2Locked)
		{
			//ApplyJointSwingDrive(Dt, Settings, JointSettings, Index0, Index1, E6DJointAngularConstraintIndex::Swing2, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
		}
	}

	// Apply twist constraint
	if (Settings.bEnableTwistLimits)
	{
		if (TwistMotion != E6DJointMotionType::Free)
		{
			ApplyJointTwistConstraint(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
		}
	}

	// Apply swing constraints
	if (Settings.bEnableSwingLimits)
	{
		if ((Swing1Motion == E6DJointMotionType::Limited) && (Swing2Motion == E6DJointMotionType::Limited))
		{
			// Swing Cone
			ApplyJointConeConstraint(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
		}
		else
		{
			if (Swing1Motion != E6DJointMotionType::Free)
			{
				// Swing Arc/Lock
				ApplyJointSwingConstraint(Dt, Settings, JointSettings, Index0, Index1, E6DJointAngularConstraintIndex::Swing1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}
			if (Swing2Motion != E6DJointMotionType::Free)
			{
				// Swing Arc/Lock
				ApplyJointSwingConstraint(Dt, Settings, JointSettings, Index0, Index1, E6DJointAngularConstraintIndex::Swing2, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}
		}
	}

	// Apply linear constraints
	{
		ApplyJointPositionConstraint(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
	}

	// Update the particles
	if (Rigid0)
	{
		Rigid0->SetP(P0);
		Rigid0->SetQ(Q0);
	}
	if (Rigid1)
	{
		Rigid1->SetP(P1);
		Rigid1->SetQ(Q1);
	}
}

namespace Chaos
{
	template class TPBD6DJointConstraintUtilities<float, 3>;
	template class TPBD6DJointSettings<float, 3>;
	template class TPBD6DJointSolverSettings<float, 3>;
	template class TPBD6DJointConstraintHandle<float, 3>;
	template class TContainerConstraintHandle<TPBD6DJointConstraints<float, 3>>;
	template class TPBD6DJointConstraints<float, 3>;
}
