// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Math/Quat.isph"
#include "Chaos/Matrix33.isph"

struct FRigidTransform
{
	FVector4 Rotation;
	FVector Translation;
	FVector Scale3D;
};

static const uniform int32 MaxConstrainedBodies = 2;

struct FJointSolverGaussSeidel
{
	// Local-space constraint settings
	FRigidTransform XLs[MaxConstrainedBodies];	// Local-space joint connector transforms
	FVector InvILs[MaxConstrainedBodies];		// Local-space inverse inertias
	float InvMs[MaxConstrainedBodies];			// Inverse masses

	// World-space constraint state
	FVector Xs[MaxConstrainedBodies];			// World-space joint connector positions
	float LinearSoftLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
	float LinearDriveLambda;					// XPBD constraint multipliers (net applied constraint-space deltas)
	FVector4 Rs[MaxConstrainedBodies];			// World-space joint connector rotations

	// World-space body state
	FVector Ps[MaxConstrainedBodies];			// World-space particle CoM positions
	float TwistSoftLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
	float SwingSoftLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
	FVector4 Qs[MaxConstrainedBodies];			// World-space particle CoM rotations
	FVector Vs[MaxConstrainedBodies];			// World-space particle CoM velocities
	FVector Ws[MaxConstrainedBodies];			// World-space particle CoM angular velocities

	// XPBD Previous iteration world-space body state
	FVector PrevPs[MaxConstrainedBodies];		// World-space particle CoM positions
	float TwistDriveLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
	float SwingDriveLambda;						// XPBD constraint multipliers (net applied constraint-space deltas)
	FVector4 PrevQs[MaxConstrainedBodies];		// World-space particle CoM rotations
	FVector PrevXs[MaxConstrainedBodies];		// World-space joint connector positions

	// Post-angular constraint position fixup (to reduce iterations required for stiff angular constraints)
	float AngularPositionCorrection;
};

static inline uniform FMatrix33 ComputeWorldSpaceInertia(const uniform FVector4 &Q, const uniform FVector &I)
{
	const uniform FMatrix33 QM = MakeQuatRotationTranslationMatrix(Q);
	const uniform FMatrix33 L = SetMatrix33(I.V[0], I.V[1], I.V[2]);
	return MultiplyAB(QM, MultiplyABt(L, QM));
}

static inline uniform FMatrix33 ComputeJointFactorMatrix(const uniform FVector& V, const uniform FMatrix33& M, const uniform float Im)
{
	// Rigid objects rotational contribution to the impulse.
	// Vx*M*VxT+Im
	const uniform FVector V221 = SetVector(-V.V[2], V.V[2], -V.V[1]);
	const uniform FVector V222 = SetVector(-V.V[2], -V.V[2], -V.V[2]);
	const uniform FVector M544 = SetVector(M.M[5], M.M[4], M.M[4]);
	const uniform FVector V111 = SetVector(V.V[1], V.V[1], V.V[1]);
	const uniform FVector M988 = SetVector(M.M[9], M.M[8], M.M[8]);
	const uniform FVector V100 = SetVector(V.V[1], -V.V[0], V.V[0]);
	const uniform FVector M995 = SetVector(M.M[9], M.M[9], M.M[5]);
	const uniform FVector M10109 = SetVector(M.M[10], M.M[10], M.M[9]);
	const uniform FVector IM = SetVector(Im, 0, 0);

	const uniform FVector R1 = (V221 * (V222 * M544 + V111 * M988)) + (V100 * (V222 * M995 + V111 * M10109)) + IM;

	const uniform FVector V211 = SetVector(V.V[2], -V.V[1], -V.V[1]);
	const uniform FVector V221_2 = SetVector(V.V[2], V.V[2], -V.V[1]);
	const uniform FVector M000 = SetVector(M.M[0], M.M[0], M.M[0]);
	const uniform FVector V000 = SetVector(-V.V[0], -V.V[0], V.V[0]);
	const uniform FVector M884 = SetVector(M.M[8], M.M[8], M.M[4]);
	const uniform FVector V000_2 = SetVector(-V.V[0], V.V[0], V.V[0]);
	const uniform FVector M844 = SetVector(M.M[8], M.M[4], M.M[4]);
	const uniform FVector M1095 = SetVector(M.M[10], M.M[9], M.M[5]);
	const uniform FVector IM_2 = SetVector(Im, 0, Im);

	const uniform FVector R2 = (V211 * (V221_2 * M000 + V000 * M884)) + (V000_2 * (V221_2 * M844 + V000 * M1095)) + IM_2;

	return SetMatrix33(R1.V[0], R1.V[1], R1.V[2], R2.V[0], R2.V[1], R2.V[2]);
}

static inline uniform FVector CalculateAngularVelocity(const uniform FVector4 &R0, const uniform FVector4 &R1, const uniform float Dt)
{
	const uniform FVector4 R1Shortest = VectorQuaternionEnforceShortestArcWith(R1, R0);

	// W = 2 * dQ/dT * Qinv
	const uniform FVector4 DRDt = (R1Shortest - R0) * (1.0f / Dt);
	const uniform FVector4 RInv = QuatInverse(R0);
	const uniform FVector4 W = VectorQuaternionMultiply2(DRDt, RInv) * 2.0f;

	return SetVector(W);
}

static inline uniform FVector CalculateVelocity(const uniform FVector& P0, const uniform FVector& P1, const uniform float Dt)
{
	return (P1 - P0) / Dt;
}

static inline void UpdateDerivedState(uniform FJointSolverGaussSeidel * uniform M)
{
	M->Xs[0] = M->Ps[0] + VectorQuaternionRotateVector(M->Qs[0], M->XLs[0].Translation);
	M->Xs[1] = M->Ps[1] + VectorQuaternionRotateVector(M->Qs[1], M->XLs[1].Translation);
	M->Rs[0] = VectorQuaternionMultiply2(M->Qs[0], M->XLs[0].Rotation);
	M->Rs[1] = VectorQuaternionMultiply2(M->Qs[1], M->XLs[1].Rotation);
}

static inline void ApplyPositionDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& DP0,
	const uniform FVector& DP1)
{
	M->Ps[0] = M->Ps[0] + (Stiffness * DP0);
	M->Ps[1] = M->Ps[1] + (Stiffness * DP1);
}

static inline void ApplyRotationDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& DR0,
	const uniform FVector& DR1)
{
	const uniform FVector4 Qs0 = M->Qs[0];
	const uniform FVector4 Qs1 = M->Qs[1];
	const uniform FVector4 DR0Stiffness = SetVector4(DR0 * Stiffness, 0);
	const uniform FVector4 DR1Stiffness = SetVector4(DR1 * Stiffness, 0);

	const uniform FVector4 DQ0 = VectorQuaternionMultiply2(DR0Stiffness, Qs0) * 0.5f;
	const uniform FVector4 DQ1 = VectorQuaternionMultiply2(DR1Stiffness, Qs1) * 0.5f;

	const uniform FVector4 Qs0Norm = VectorNormalizeQuaternion(Qs0 + DQ0);
	const uniform FVector4 Qs1Norm = VectorNormalizeQuaternion(Qs1 + DQ1);

	const uniform FVector4 Qs1ShortestArcWith = VectorQuaternionEnforceShortestArcWith(Qs1Norm, Qs0Norm);

	M->Qs[0] = Qs0Norm;
	M->Qs[1] = Qs1ShortestArcWith;
}

static inline void ApplyVelocityDelta(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Stiffness,
	const uniform FVector& DV0,
	const uniform FVector& DW0,
	const uniform FVector& DV1,
	const uniform FVector& DW1)
{
	M->Vs[0] = M->Vs[0] + Stiffness * DV0;
	M->Vs[1] = M->Vs[1] + Stiffness * DV1;
	M->Ws[0] = M->Ws[0] + Stiffness * DW0;
	M->Ws[1] = M->Ws[1] + Stiffness * DW1;
}

export uniform int32 SizeofFJointSolverGaussSeidel()
{
	return sizeof(uniform FJointSolverGaussSeidel);
}

export void ApplyPositionConstraintSoft(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Dt,
	const uniform float Stiffness,
	const uniform float Damping,
	const uniform bool bAccelerationMode,
	const uniform FVector& Axis,
	const uniform float Delta,
	uniform float& Lambda)
{
	// World-space inverse mass
	const uniform FMatrix33 InvI0 = ComputeWorldSpaceInertia(M->Qs[0], M->InvILs[0]);
	const uniform FMatrix33 InvI1 = ComputeWorldSpaceInertia(M->Qs[1], M->InvILs[1]);
	
	// Joint-space inverse mass
	const uniform FVector AngularAxis0 = VectorCross(M->Xs[0] - M->Ps[0], Axis);
	const uniform FVector AngularAxis1 = VectorCross(M->Xs[1] - M->Ps[1], Axis);
	const uniform FVector IA0 = Multiply(InvI0, AngularAxis0);
	const uniform FVector IA1 = Multiply(InvI1, AngularAxis1);
	const uniform float II0 = VectorDot(AngularAxis0, IA0);
	const uniform float II1 = VectorDot(AngularAxis1, IA1);
	const uniform float II = (M->InvMs[0] + II0 + M->InvMs[1] + II1);

	uniform float VelDt = 0;
	if (Damping > KINDA_SMALL_NUMBER)
	{
		const uniform FVector V0 = CalculateVelocity(M->PrevXs[0], M->Xs[0], 1.0f);
		const uniform FVector V1 = CalculateVelocity(M->PrevXs[1], M->Xs[1], 1.0f);
		VelDt = VectorDot(V0 - V1, Axis);
	}

	const uniform float SpringMassScale = (bAccelerationMode) ? 1.0f / (M->InvMs[0] + M->InvMs[1]) : 1.0f;
	const uniform float S = SpringMassScale * Stiffness * Dt * Dt;
	const uniform float D = SpringMassScale * Damping * Dt;
	const uniform float Multiplier = 1.0f / ((S + D) * II + 1.0f);
	const uniform float DLambda = Multiplier * (S * Delta - D * VelDt - Lambda);
	
	const uniform FVector DP0 = (M->InvMs[0] * DLambda) * Axis;
	const uniform FVector DP1 = (-M->InvMs[1] * DLambda) * Axis;
	const uniform FVector DR0 = DLambda * Multiply(InvI0, AngularAxis0);
	const uniform FVector DR1 = -DLambda * Multiply(InvI1, AngularAxis1);

	Lambda += DLambda;
	ApplyPositionDelta(M, 1.0f, DP0, DP1);
	ApplyRotationDelta(M, 1.0f, DR0, DR1);
	UpdateDerivedState(M);
}

// See "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"
export void ApplyRotationConstraintSoft(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform float Dt,
	const uniform float Stiffness,
	const uniform float Damping,
	const uniform bool bAccelerationMode,
	const uniform FVector& Axis0,
	const uniform FVector& Axis1,
	const uniform float Angle,
	uniform float& Lambda)
{
	// World-space inverse mass
	const uniform FMatrix33 InvI0 = ComputeWorldSpaceInertia(M->Qs[0], M->InvILs[0]);
	const uniform FMatrix33 InvI1 = ComputeWorldSpaceInertia(M->Qs[1], M->InvILs[1]);
	const uniform FVector IA0 = Multiply(InvI0, Axis0);
	const uniform FVector IA1 = Multiply(InvI1, Axis1);

	// Joint-space inverse mass
	uniform float II0 = VectorDot(Axis0, IA0);
	uniform float II1 = VectorDot(Axis1, IA1);

	// If we are correcting the position, we need to adjust the constraint effective mass using parallel axis theorem
	// @todo(ccaulfield): the IMs here are constant per constraint...pre-build and cache it
	if (M->AngularPositionCorrection > 0)
	{
		if (M->InvMs[0] > 0)
		{
			const uniform FVector LinearAxis0 = VectorCross(M->Xs[0] - M->Ps[0], Axis0);
			const uniform float LinearAxisLen0Sq = VectorSizeSquared(LinearAxis0);
			if (LinearAxisLen0Sq > KINDA_SMALL_NUMBER)
			{
				const uniform float IM0 = M->InvMs[0] / LinearAxisLen0Sq;
				II0 = II0 * IM0 / (II0 + IM0);
			}
		}
		if (M->InvMs[1] > 0)
		{
			const uniform FVector LinearAxis1 = VectorCross(M->Xs[1] - M->Ps[1], Axis1);
			const uniform float LinearAxisLen1Sq = VectorSizeSquared(LinearAxis1);
			if (LinearAxisLen1Sq > KINDA_SMALL_NUMBER)
			{
				const uniform float IM1 = M->InvMs[1] / LinearAxisLen1Sq;
				II1 = II1 * IM1 / (II1 + IM1);
			}
		}
	}
	const uniform float II = (II0 + II1);

	// Damping angular velocity
	uniform float AngVelDt = 0;
	if (Damping > KINDA_SMALL_NUMBER)
	{
		const uniform FVector W0 = CalculateAngularVelocity(M->PrevQs[0], M->Qs[0], 1.0f);
		const uniform FVector W1 = CalculateAngularVelocity(M->PrevQs[1], M->Qs[1], 1.0f);
		AngVelDt = VectorDot(Axis0, W0) - VectorDot(Axis1, W1);
	}

	const uniform float SpringMassScale = (bAccelerationMode) ? 1.0f / II : 1.0f;
	const uniform float S = SpringMassScale * Stiffness * Dt * Dt;
	const uniform float D = SpringMassScale * Damping * Dt;
	const uniform float Multiplier = 1.0f / ((S + D) * II + 1.0f);
	const uniform float DLambda = Multiplier * (S * Angle - D * AngVelDt - Lambda);

	//const FVec3 DR0 = IA0 * DLambda;
	//const FVec3 DR1 = IA1 * -DLambda;
	const uniform FVector DR0 = Axis0 * (DLambda * II0);
	const uniform FVector DR1 = Axis1 * -(DLambda * II1);

	Lambda += DLambda;
	ApplyRotationDelta(M, 1.0f, DR0, DR1);

	// Correct the positional error that was introduced by the rotation correction. This is
	// correct when the position dofs are all locked, but not otherwise (fixable).
	// This significantly improves angular stiffness at lower iterations, although the same effect
	// is achieved by increasing iterations.
	// @todo(ccaulfield): this position correction needs to have components in direction of inactive position constraints removed
	if (M->AngularPositionCorrection > 0)
	{
		const uniform FVector PrevX0 = M->Xs[0];
		const uniform FVector PrevX1 = M->Xs[1];
		UpdateDerivedState(M);

		const uniform FVector DX = (M->Xs[1] - M->Xs[0]) - (PrevX1 - PrevX0);
		const uniform FVector DP0 = DX * (M->InvMs[0] / (M->InvMs[0] + M->InvMs[1]));
		const uniform FVector DP1 = DX * (-M->InvMs[1] / (M->InvMs[0] + M->InvMs[1]));
		ApplyPositionDelta(M, M->AngularPositionCorrection, DP0, DP1);
	}

	UpdateDerivedState(M);
}

export void ApplyPointPositionConstraint(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform FVector &CX,
	const uniform float LinearStiffness)
{
	const uniform FMatrix33 InvI0 = ComputeWorldSpaceInertia(M->Qs[0], M->InvILs[0]);
	const uniform FMatrix33 InvI1 = ComputeWorldSpaceInertia(M->Qs[1], M->InvILs[1]);

	// Calculate constraint correction
	uniform FMatrix33 M0 = Matrix33Zero;
	uniform FMatrix33 M1 = Matrix33Zero;
	if (M->InvMs[0] > 0)
	{
		M0 = ComputeJointFactorMatrix(M->Xs[0] - M->Ps[0], InvI0, M->InvMs[0]);
	}
	if (M->InvMs[1] > 0)
	{
		M1 = ComputeJointFactorMatrix(M->Xs[1] - M->Ps[1], InvI1, M->InvMs[1]);
	}
	const uniform FMatrix33 MI = MatrixInverse(AddAB(M0, M1));
	const uniform FVector DX = Multiply(MI, CX);

	// Apply constraint correction
	const uniform FVector DP0 = DX * M->InvMs[0];
	const uniform FVector DP1 = DX * -M->InvMs[1];
	const uniform FVector DR0 = Multiply(InvI0, VectorCross(M->Xs[0] - M->Ps[0], DX));
	const uniform FVector DR1 = Multiply(InvI1, VectorCross(M->Xs[1] - M->Ps[1], VectorNegate(DX)));

	ApplyPositionDelta(M, LinearStiffness, DP0, DP1);
	ApplyRotationDelta(M, LinearStiffness, DR0, DR1);
	UpdateDerivedState(M);
}

export void ApplyPositionProjection(
	uniform FJointSolverGaussSeidel * uniform M,
	const uniform FVector &CX,
	const uniform float CXLen,
	const uniform float ParentMassScale,
	const uniform float Stiffness)
{
	const uniform FVector CXDir = CX / CXLen;
	const uniform FVector V0 = M->Vs[0] + VectorCross(M->Ws[0], M->Xs[0] - M->Ps[0]);
	const uniform FVector V1 = M->Vs[1] + VectorCross(M->Ws[1], M->Xs[1] - M->Ps[1]);
	const uniform FVector CV = VectorDot(V1 - V0, CXDir) * CXDir;

	const uniform float IM0 = ParentMassScale * M->InvMs[0];
	const uniform float IM1 = M->InvMs[1];
	const uniform FVector IIL0 = ParentMassScale * M->InvILs[0];
	const uniform FVector& IIL1 = M->InvILs[1];
	const uniform FMatrix33 II0 = ComputeWorldSpaceInertia(M->Qs[0], IIL0);
	const uniform FMatrix33 II1 = ComputeWorldSpaceInertia(M->Qs[1], IIL1);
	uniform FMatrix33 J0 = Matrix33Zero;
	if(IM0 > 0)
	{
		J0 = ComputeJointFactorMatrix(M->Xs[0] - M->Ps[0], II0, IM0);
	}
	const uniform FMatrix33 J1 = ComputeJointFactorMatrix(M->Xs[1] - M->Ps[1], II1, IM1);
	const uniform FMatrix33 IJ = MatrixInverse(AddAB(J0, J1));

	const uniform FVector DX = Multiply(IJ, CX);
	const uniform FVector DV = Multiply(IJ, CV);

	const uniform FVector DP0 = IM0 * DX;
	const uniform FVector DP1 = -IM1 * DX;
	const uniform FVector DR0 = Multiply(II0, VectorCross(M->Xs[0] - M->Ps[0], DX));
	const uniform FVector DR1 = Multiply(II1, VectorCross(M->Xs[1] - M->Ps[1], VectorNegate(DX)));

	const uniform FVector DV0 = IM0 * DV;
	const uniform FVector DV1 = -IM1 * DV;
	const uniform FVector DW0 = Multiply(II0, VectorCross(M->Xs[0] - M->Ps[0], DV));
	const uniform FVector DW1 = Multiply(II1, VectorCross(M->Xs[1] - M->Ps[1], VectorNegate(DV)));

	ApplyPositionDelta(M, Stiffness, DP0, DP1);
	ApplyRotationDelta(M, Stiffness, DR0, DR1);
	ApplyVelocityDelta(M, Stiffness, DV0, DW0, DV1, DW1);
	UpdateDerivedState(M);
}
