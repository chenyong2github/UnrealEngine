// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Chaos/PBDSofts.isph"

static inline varying float SafeDivide(const varying float Numerator, const varying float Denominator)
{
	return select(Denominator <= FLOAT_SMALL_NUMBER, (uniform float)0, Numerator / Denominator);
}

static void CalcConstraintsAndGrads(const FVector3f& P0, const FVector3f& P1, const FVector3f& P2, const FVector4f& DeltaUVInv,
	const FVector2f& RestStretchLengths, const float WarpScale, const float WeftScale,
	float& Cu, float& Cv, float& Cs, 
	FVector3f& dCu_dX1, FVector3f& dCu_dX2, FVector3f& dCv_dX1, FVector3f& dCv_dX2, FVector3f& dCs_dX1, FVector3f& dCs_dX2)
{
	const FVector3f P01 = P1 - P0;
	const FVector3f P02 = P2 - P0;
	const FVector3f dX_dU = DeltaUVInv.V[0] * P01 + DeltaUVInv.V[1] * P02;
	const FVector3f dX_dV = DeltaUVInv.V[2] * P01 + DeltaUVInv.V[3] * P02;

	const float dX_dU_length = VectorSize(dX_dU);
	const float dX_dV_length = VectorSize(dX_dV);

	const float OneOverDxDuLen = SafeDivide(1.f, dX_dU_length);
	const float OneOverDxDvLen = SafeDivide(1.f, dX_dV_length);

	const FVector3f dX_dU_normalized = OneOverDxDuLen * dX_dU;
	const FVector3f dX_dV_normalized = OneOverDxDvLen * dX_dV;

	Cu = dX_dU_length - RestStretchLengths.V[0] * WarpScale;
	Cv = dX_dV_length - RestStretchLengths.V[1] * WeftScale;
	Cs = VectorDot(dX_dU_normalized, dX_dV_normalized);

	dCu_dX1 = dX_dU_normalized * DeltaUVInv.V[0];
	dCu_dX2 = dX_dU_normalized * DeltaUVInv.V[1];
	dCv_dX1 = dX_dV_normalized * DeltaUVInv.V[2];
	dCv_dX2 = dX_dV_normalized * DeltaUVInv.V[3];
	dCs_dX1 = (dX_dV_normalized * OneOverDxDuLen * DeltaUVInv.V[0] + dX_dU_normalized * OneOverDxDvLen * DeltaUVInv.V[2]);
	dCs_dX2 = (dX_dV_normalized * OneOverDxDuLen * DeltaUVInv.V[1] + dX_dU_normalized * OneOverDxDvLen * DeltaUVInv.V[3]);
}

static inline float CalcDLambda(const float StiffnessValue, const float Dt, const float MinStiffness, const float Lambda,
	const float InvM0, const float InvM1, const float InvM2,
	const float C, const FVector3f& dC_dX1, const FVector3f& dC_dX2)
{
	const float Alpha = 1.f / (StiffnessValue  * Dt * Dt);
	const float Denom = InvM0 * VectorSizeSquared(dC_dX1 + dC_dX2) + InvM1 * VectorSizeSquared(dC_dX1) + InvM2 * VectorSizeSquared(dC_dX2) + Alpha;
	const float DLambda = (C - Alpha * Lambda) / Denom;
	return select(StiffnessValue < MinStiffness, (uniform float)0, DLambda);
}

static inline float CalcDamping(const float StiffnessValue, const float CombinedInvMass, const float DampingRatio)
{
	return 2.f * DampingRatio * sqrt(StiffnessValue / CombinedInvMass);
}

static inline float CalcDLambda(const float StiffnessValue, const float Dt, const float MinStiffness, const float Lambda, const float Damping,
	const float InvM0, const float InvM1, const float InvM2,
	const FVector3f& V0TimesDt, const FVector3f& V1TimesDt, const FVector3f& V2TimesDt, 
	const float C, const FVector3f& dC_dX1, const FVector3f& dC_dX2)
{
	const float Alpha = 1.f / (StiffnessValue  * Dt * Dt);
	const float Gamma = Alpha * Damping * Dt;
	const float DampingTerm = Gamma * (VectorDot(V1TimesDt, dC_dX1) + VectorDot(V2TimesDt, dC_dX2) - VectorDot(V0TimesDt, dC_dX1 + dC_dX2));
	const float Denom = (1. + Gamma) * (InvM0 * VectorSizeSquared(dC_dX1 + dC_dX2) + InvM1 * VectorSizeSquared(dC_dX1) + InvM2 * VectorSizeSquared(dC_dX2)) + Alpha;
	const float DLambda = (C - Alpha * Lambda + DampingTerm) / Denom;
	return select(StiffnessValue < MinStiffness, (uniform float)0, DLambda);
}

export void ApplyXPBDStretchBiasConstraints(uniform FVector4f PandInvM[],
									const uniform FIntVector Constraints[],
									const uniform FVector2f RestStretchLengths[],
									const uniform FVector4f DeltaUVInverses[],
									const uniform FVector3f StiffnessScales[],
									uniform FVector3f Lambdas[],
									const uniform float Dt,
									const uniform float MinStiffness,
									const uniform FVector3f& Stiffness,
									const uniform float WarpScale,
									const uniform float WeftScale,
									const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying int32 i0 = Constraint.V[0];
		const varying int32 i1 = Constraint.V[1];
		const varying int32 i2 = Constraint.V[2];
		
		const varying FVector4f PandInvM0 = VectorGather(&PandInvM[i0]);
		const varying FVector4f PandInvM1 = VectorGather(&PandInvM[i1]);
		const varying FVector4f PandInvM2 = VectorGather(&PandInvM[i2]);

		varying FVector3f P0, P1, P2;
		varying float M0, M1, M2;
		
		UnzipPandInvM(PandInvM0, P0, M0);
		UnzipPandInvM(PandInvM1, P1, M1);
		UnzipPandInvM(PandInvM2, P2, M2);

		const varying FVector4f DeltaUVInverse = VectorLoad(&DeltaUVInverses[extract(i,0)]);
		const varying FVector2f RestStretchLength = VectorLoad(&RestStretchLengths[extract(i,0)]);

		varying float Cu, Cv, Cs;
		varying FVector3f dCu_dX1, dCu_dX2, dCv_dX1, dCv_dX2, dCs_dX1, dCs_dX2;
		CalcConstraintsAndGrads(P0, P1, P2, DeltaUVInverse, RestStretchLength, WarpScale, WeftScale, Cu, Cv, Cs,
			dCu_dX1, dCu_dX2, dCv_dX1, dCv_dX2, dCs_dX1, dCs_dX2);
		
		FVector3f Lambda = VectorLoad(&Lambdas[extract(i,0)]);
		const varying FVector3f StiffnessScale = VectorLoad(&StiffnessScales[extract(i,0)]);
		const varying FVector3f FinalStiffness = Stiffness * StiffnessScale;

		const varying float DLambdaU = CalcDLambda(FinalStiffness.V[0], Dt, MinStiffness, Lambda.V[0], M0, M1, M2, Cu, dCu_dX1, dCu_dX2);
		const varying float DLambdaV = CalcDLambda(FinalStiffness.V[1], Dt, MinStiffness, Lambda.V[1], M0, M1, M2, Cv, dCv_dX1, dCv_dX2);
		const varying float DLambdaS = CalcDLambda(FinalStiffness.V[2], Dt, MinStiffness, Lambda.V[2], M0, M1, M2, Cs, dCs_dX1, dCs_dX2);
		const varying FVector3f DLambda = {{DLambdaU, DLambdaV, DLambdaS}};

		Lambda = Lambda + DLambda;
		VectorStore(&Lambdas[extract(i,0)], Lambda);
		
		const varying FVector3f DP1 = DLambdaU * dCu_dX1 + DLambdaV * dCv_dX1 + DLambdaS * dCs_dX1;
		const varying FVector3f DP2 = DLambdaU * dCu_dX2 + DLambdaV * dCv_dX2 + DLambdaS * dCs_dX2;

		if (M0 > 0)
		{
			VectorScatter(&PandInvM[i0], SetVector4(P0 + M0 * (DP1 + DP2), M0));
		}
		if (M1 > 0)
		{
			VectorScatter(&PandInvM[i1], SetVector4(P1 - M1 * DP1, M1));
		}
		if (M2 > 0)
		{
			VectorScatter(&PandInvM[i2], SetVector4(P2 - M2 * DP2, M2));
		}
	}
}

export void ApplyXPBDStretchBiasConstraintsWithDamping(uniform FVector4f PandInvM[],
									const uniform FVector3f X[],
									const uniform FIntVector Constraints[],
									const uniform FVector2f RestStretchLengths[],
									const uniform FVector4f DeltaUVInverses[],
									const uniform FVector3f StiffnessScales[],
									uniform FVector3f Lambdas[],
									const uniform float Dt,
									const uniform float MinStiffness,
									const uniform FVector3f& Stiffness,
									const uniform float DampingRatio,
									const uniform float WarpScale,
									const uniform float WeftScale,
									const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying int32 i0 = Constraint.V[0];
		const varying int32 i1 = Constraint.V[1];
		const varying int32 i2 = Constraint.V[2];
		
		const varying FVector4f PandInvM0 = VectorGather(&PandInvM[i0]);
		const varying FVector4f PandInvM1 = VectorGather(&PandInvM[i1]);
		const varying FVector4f PandInvM2 = VectorGather(&PandInvM[i2]);

		varying FVector3f P0, P1, P2;
		varying float M0, M1, M2;
		
		UnzipPandInvM(PandInvM0, P0, M0);
		UnzipPandInvM(PandInvM1, P1, M1);
		UnzipPandInvM(PandInvM2, P2, M2);

		const varying FVector4f DeltaUVInverse = VectorLoad(&DeltaUVInverses[extract(i,0)]);
		const varying FVector2f RestStretchLength = VectorLoad(&RestStretchLengths[extract(i,0)]);

		varying float Cu, Cv, Cs;
		varying FVector3f dCu_dX1, dCu_dX2, dCv_dX1, dCv_dX2, dCs_dX1, dCs_dX2;
		CalcConstraintsAndGrads(P0, P1, P2, DeltaUVInverse, RestStretchLength, WarpScale, WeftScale, Cu, Cv, Cs,
			dCu_dX1, dCu_dX2, dCv_dX1, dCv_dX2, dCs_dX1, dCs_dX2);
		
		const varying FVector3f X0 = VectorGather(&X[i0]);
		const varying FVector3f X1 = VectorGather(&X[i1]);
		const varying FVector3f X2 = VectorGather(&X[i2]);
		const varying FVector3f V0TimesDt = P0 - X0;
		const varying FVector3f V1TimesDt = P1 - X1;
		const varying FVector3f V2TimesDt = P2 - X2;
		const varying float CombinedInvMass = M0 + M1 + M2;
		
		FVector3f Lambda = VectorLoad(&Lambdas[extract(i,0)]);
		const varying FVector3f StiffnessScale = VectorLoad(&StiffnessScales[extract(i,0)]);
		const varying FVector3f FinalStiffness = Stiffness * StiffnessScale;
		const varying float DLambdaU = CalcDLambda(FinalStiffness.V[0], Dt, MinStiffness, Lambda.V[0], 
			CalcDamping(FinalStiffness.V[0], CombinedInvMass, DampingRatio),
			M0, M1, M2, V0TimesDt, V1TimesDt, V2TimesDt, Cu, dCu_dX1, dCu_dX2);
		const varying float DLambdaV = CalcDLambda(FinalStiffness.V[1], Dt, MinStiffness, Lambda.V[1], 
			CalcDamping(FinalStiffness.V[1], CombinedInvMass, DampingRatio),
			M0, M1, M2, V0TimesDt, V1TimesDt, V2TimesDt, Cv, dCv_dX1, dCv_dX2);
		const varying float DLambdaS = CalcDLambda(FinalStiffness.V[2], Dt, MinStiffness, Lambda.V[2], 
			CalcDamping(FinalStiffness.V[2], CombinedInvMass, DampingRatio),
			M0, M1, M2, V0TimesDt, V1TimesDt, V2TimesDt, Cs, dCs_dX1, dCs_dX2);
		const varying FVector3f DLambda = {{DLambdaU, DLambdaV, DLambdaS}};

		Lambda = Lambda + DLambda;
		VectorStore(&Lambdas[extract(i,0)], Lambda);
		
		const varying FVector3f DP1 = DLambdaU * dCu_dX1 + DLambdaV * dCv_dX1 + DLambdaS * dCs_dX1;
		const varying FVector3f DP2 = DLambdaU * dCu_dX2 + DLambdaV * dCv_dX2 + DLambdaS * dCs_dX2;

		if (M0 > 0)
		{
			VectorScatter(&PandInvM[i0], SetVector4(P0 + M0 * (DP1 + DP2), M0));
		}
		if (M1 > 0)
		{
			VectorScatter(&PandInvM[i1], SetVector4(P1 - M1 * DP1, M1));
		}
		if (M2 > 0)
		{
			VectorScatter(&PandInvM[i2], SetVector4(P2 - M2 * DP2, M2));
		}
	}
}

export void ApplyXPBDStretchBiasConstraintsWithMaps(uniform FVector4f PandInvM[],
											const uniform FIntVector Constraints[],
											const uniform FVector2f RestStretchLengths[],
											const uniform FVector4f DeltaUVInverses[],
											const uniform FVector3f StiffnessScales[],
											uniform FVector3f Lambdas[],
											const uniform float Dt,
											const uniform float MinStiffness,
											const uniform bool StiffnessWarpHasMap,
											const uniform uint8 StiffnessWarpIndices[],
											const uniform float StiffnessWarpTable[],
											const uniform bool StiffnessWeftHasMap,
											const uniform uint8 StiffnessWeftIndices[],
											const uniform float StiffnessWeftTable[],
											const uniform bool StiffnessBiasHasMap,
											const uniform uint8 StiffnessBiasIndices[],
											const uniform float StiffnessBiasTable[],
											const uniform bool WarpScaleHasMap,
											const uniform uint8 WarpScaleIndices[],
											const uniform float WarpScaleTable[],
											const uniform bool WeftScaleHasMap,
											const uniform uint8 WeftScaleIndices[],
											const uniform float WeftScaleTable[],
											const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying int32 i0 = Constraint.V[0];
		const varying int32 i1 = Constraint.V[1];
		const varying int32 i2 = Constraint.V[2];
		const varying int8 StiffnessWarpIndex = select(StiffnessWarpHasMap, StiffnessWarpIndices[i], 0);
		const varying int8 StiffnessWeftIndex = select(StiffnessWeftHasMap, StiffnessWeftIndices[i], 0);
		const varying int8 StiffnessBiasIndex = select(StiffnessBiasHasMap, StiffnessBiasIndices[i], 0);
		const varying int8 WarpScaleIndex = select(WarpScaleHasMap, WarpScaleIndices[i], 0);
		const varying int8 WeftScaleIndex = select(WeftScaleHasMap, WeftScaleIndices[i], 0);
		
		const varying FVector4f PandInvM0 = VectorGather(&PandInvM[i0]);
		const varying FVector4f PandInvM1 = VectorGather(&PandInvM[i1]);
		const varying FVector4f PandInvM2 = VectorGather(&PandInvM[i2]);

		varying FVector3f P0, P1, P2;
		varying float M0, M1, M2;
		
		UnzipPandInvM(PandInvM0, P0, M0);
		UnzipPandInvM(PandInvM1, P1, M1);
		UnzipPandInvM(PandInvM2, P2, M2);

		const varying FVector4f DeltaUVInverse = VectorLoad(&DeltaUVInverses[extract(i,0)]);
		const varying FVector2f RestStretchLength = VectorLoad(&RestStretchLengths[extract(i,0)]);
		
		#pragma ignore warning(perf)
		const varying float WarpScale = WarpScaleTable[WarpScaleIndex];
		#pragma ignore warning(perf)
		const varying float WeftScale = WeftScaleTable[WeftScaleIndex];

		varying float Cu, Cv, Cs;
		varying FVector3f dCu_dX1, dCu_dX2, dCv_dX1, dCv_dX2, dCs_dX1, dCs_dX2;
		CalcConstraintsAndGrads(P0, P1, P2, DeltaUVInverse, RestStretchLength, WarpScale, WeftScale, Cu, Cv, Cs,
			dCu_dX1, dCu_dX2, dCv_dX1, dCv_dX2, dCs_dX1, dCs_dX2);		
		
		#pragma ignore warning(perf)
		const varying float StiffnessWarp = StiffnessWarpTable[StiffnessWarpIndex];
		#pragma ignore warning(perf)
		const varying float StiffnessWeft = StiffnessWeftTable[StiffnessWeftIndex];
		#pragma ignore warning(perf)
		const varying float StiffnessBias = StiffnessBiasTable[StiffnessBiasIndex];
		const varying FVector3f Stiffness = {{StiffnessWarp, StiffnessWeft, StiffnessBias}};

		FVector3f Lambda = VectorLoad(&Lambdas[extract(i,0)]);
		const varying FVector3f StiffnessScale = VectorLoad(&StiffnessScales[extract(i,0)]);
		const varying FVector3f FinalStiffness = Stiffness * StiffnessScale;

		const varying float DLambdaU = CalcDLambda(FinalStiffness.V[0], Dt, MinStiffness, Lambda.V[0], M0, M1, M2, Cu, dCu_dX1, dCu_dX2);
		const varying float DLambdaV = CalcDLambda(FinalStiffness.V[1], Dt, MinStiffness, Lambda.V[1], M0, M1, M2, Cv, dCv_dX1, dCv_dX2);
		const varying float DLambdaS = CalcDLambda(FinalStiffness.V[2], Dt, MinStiffness, Lambda.V[2], M0, M1, M2, Cs, dCs_dX1, dCs_dX2);
		const varying FVector3f DLambda = {{DLambdaU, DLambdaV, DLambdaS}};

		Lambda = Lambda + DLambda;
		VectorStore(&Lambdas[extract(i,0)], Lambda);
		
		const varying FVector3f DP1 = DLambdaU * dCu_dX1 + DLambdaV * dCv_dX1 + DLambdaS * dCs_dX1;
		const varying FVector3f DP2 = DLambdaU * dCu_dX2 + DLambdaV * dCv_dX2 + DLambdaS * dCs_dX2;

		if (M0 > 0)
		{
			VectorScatter(&PandInvM[i0], SetVector4(P0 + M0 * (DP1 + DP2), M0));
		}
		if (M1 > 0)
		{
			VectorScatter(&PandInvM[i1], SetVector4(P1 - M1 * DP1, M1));
		}
		if (M2 > 0)
		{
			VectorScatter(&PandInvM[i2], SetVector4(P2 - M2 * DP2, M2));
		}
	}
}

export void ApplyXPBDStretchBiasConstraintsWithDampingAndMaps(uniform FVector4f PandInvM[],
											const uniform FVector3f X[],
											const uniform FIntVector Constraints[],
											const uniform FVector2f RestStretchLengths[],
											const uniform FVector4f DeltaUVInverses[],
											const uniform FVector3f StiffnessScales[],
											uniform FVector3f Lambdas[],
											const uniform float Dt,
											const uniform float MinStiffness,
											const uniform bool StiffnessWarpHasMap,
											const uniform uint8 StiffnessWarpIndices[],
											const uniform float StiffnessWarpTable[],
											const uniform bool StiffnessWeftHasMap,
											const uniform uint8 StiffnessWeftIndices[],
											const uniform float StiffnessWeftTable[],
											const uniform bool StiffnessBiasHasMap,
											const uniform uint8 StiffnessBiasIndices[],
											const uniform float StiffnessBiasTable[],
											const uniform bool DampingHasMap,
											const uniform uint8 DampingIndices[],
											const uniform float DampingTable[],
											const uniform bool WarpScaleHasMap,
											const uniform uint8 WarpScaleIndices[],
											const uniform float WarpScaleTable[],
											const uniform bool WeftScaleHasMap,
											const uniform uint8 WeftScaleIndices[],
											const uniform float WeftScaleTable[],
											const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying int32 i0 = Constraint.V[0];
		const varying int32 i1 = Constraint.V[1];
		const varying int32 i2 = Constraint.V[2];
		const varying int8 StiffnessWarpIndex = select(StiffnessWarpHasMap, StiffnessWarpIndices[i], 0);
		const varying int8 StiffnessWeftIndex = select(StiffnessWeftHasMap, StiffnessWeftIndices[i], 0);
		const varying int8 StiffnessBiasIndex = select(StiffnessBiasHasMap, StiffnessBiasIndices[i], 0);
		const varying int8 WarpScaleIndex = select(WarpScaleHasMap, WarpScaleIndices[i], 0);
		const varying int8 WeftScaleIndex = select(WeftScaleHasMap, WeftScaleIndices[i], 0);
		const varying int8 DampingIndex = select(DampingHasMap, DampingIndices[i], 0);
		
		const varying FVector4f PandInvM0 = VectorGather(&PandInvM[i0]);
		const varying FVector4f PandInvM1 = VectorGather(&PandInvM[i1]);
		const varying FVector4f PandInvM2 = VectorGather(&PandInvM[i2]);

		varying FVector3f P0, P1, P2;
		varying float M0, M1, M2;
		
		UnzipPandInvM(PandInvM0, P0, M0);
		UnzipPandInvM(PandInvM1, P1, M1);
		UnzipPandInvM(PandInvM2, P2, M2);

		const varying FVector4f DeltaUVInverse = VectorLoad(&DeltaUVInverses[extract(i,0)]);
		const varying FVector2f RestStretchLength = VectorLoad(&RestStretchLengths[extract(i,0)]);		
		
		#pragma ignore warning(perf)
		const varying float WarpScale = WarpScaleTable[WarpScaleIndex];
		#pragma ignore warning(perf)
		const varying float WeftScale = WeftScaleTable[WeftScaleIndex];

		varying float Cu, Cv, Cs;
		varying FVector3f dCu_dX1, dCu_dX2, dCv_dX1, dCv_dX2, dCs_dX1, dCs_dX2;
		CalcConstraintsAndGrads(P0, P1, P2, DeltaUVInverse, RestStretchLength, WarpScale, WeftScale, Cu, Cv, Cs,
			dCu_dX1, dCu_dX2, dCv_dX1, dCv_dX2, dCs_dX1, dCs_dX2);
		
		const varying FVector3f X0 = VectorGather(&X[i0]);
		const varying FVector3f X1 = VectorGather(&X[i1]);
		const varying FVector3f X2 = VectorGather(&X[i2]);
		const varying FVector3f V0TimesDt = P0 - X0;
		const varying FVector3f V1TimesDt = P1 - X1;
		const varying FVector3f V2TimesDt = P2 - X2;
		const varying float CombinedInvMass = M0 + M1 + M2;
		
		#pragma ignore warning(perf)
		const varying float StiffnessWarp = StiffnessWarpTable[StiffnessWarpIndex];
		#pragma ignore warning(perf)
		const varying float StiffnessWeft = StiffnessWeftTable[StiffnessWeftIndex];
		#pragma ignore warning(perf)
		const varying float StiffnessBias = StiffnessBiasTable[StiffnessBiasIndex];
		const varying FVector3f Stiffness = {{StiffnessWarp, StiffnessWeft, StiffnessBias}};

		#pragma ignore warning(perf)
		const varying float DampingRatio = DampingTable[DampingIndex];
		
		FVector3f Lambda = VectorLoad(&Lambdas[extract(i,0)]);
		const varying FVector3f StiffnessScale = VectorLoad(&StiffnessScales[extract(i,0)]);
		const varying FVector3f FinalStiffness = Stiffness * StiffnessScale;
		const varying float DLambdaU = CalcDLambda(FinalStiffness.V[0], Dt, MinStiffness, Lambda.V[0], 
			CalcDamping(FinalStiffness.V[0], CombinedInvMass, DampingRatio),
			M0, M1, M2, V0TimesDt, V1TimesDt, V2TimesDt, Cu, dCu_dX1, dCu_dX2);
		const varying float DLambdaV = CalcDLambda(FinalStiffness.V[1], Dt, MinStiffness, Lambda.V[1], 
			CalcDamping(FinalStiffness.V[1], CombinedInvMass, DampingRatio),
			M0, M1, M2, V0TimesDt, V1TimesDt, V2TimesDt, Cv, dCv_dX1, dCv_dX2);
		const varying float DLambdaS = CalcDLambda(FinalStiffness.V[2], Dt, MinStiffness, Lambda.V[2], 
			CalcDamping(FinalStiffness.V[2], CombinedInvMass, DampingRatio),
			M0, M1, M2, V0TimesDt, V1TimesDt, V2TimesDt, Cs, dCs_dX1, dCs_dX2);
		const varying FVector3f DLambda = {{DLambdaU, DLambdaV, DLambdaS}};

		Lambda = Lambda + DLambda;
		VectorStore(&Lambdas[extract(i,0)], Lambda);
		
		const varying FVector3f DP1 = DLambdaU * dCu_dX1 + DLambdaV * dCv_dX1 + DLambdaS * dCs_dX1;
		const varying FVector3f DP2 = DLambdaU * dCu_dX2 + DLambdaV * dCv_dX2 + DLambdaS * dCs_dX2;

		if (M0 > 0)
		{
			VectorScatter(&PandInvM[i0], SetVector4(P0 + M0 * (DP1 + DP2), M0));
		}
		if (M1 > 0)
		{
			VectorScatter(&PandInvM[i1], SetVector4(P1 - M1 * DP1, M1));
		}
		if (M2 > 0)
		{
			VectorScatter(&PandInvM[i2], SetVector4(P2 - M2 * DP2, M2));
		}
	}
}