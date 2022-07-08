// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSpringConstraintsBase.h"
#include "ChaosStats.h"
#include "Chaos/ImplicitQRSVD.h"
#include "Chaos/GraphColoring.h"
#include "Chaos/Framework/Parallel.h"

DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Corotated Constraint"), STAT_ChaosXPBDCorotated, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Corotated Constraint Polar Compute"), STAT_ChaosXPBDCorotatedPolar, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Corotated Constraint Det Compute"), STAT_ChaosXPBDCorotatedDet, STATGROUP_Chaos);

namespace Chaos::Softs
{

	class FXPBDCorotatedConstraints 
	{

		//TODO(Yizhou Chen): COuld be optimized . The SVD is using double so float -> double -> float all the time
		//should change data type in accordance 

	public:
		//this one only accepts tetmesh input and mesh
		FXPBDCorotatedConstraints(
			const Chaos::Softs::FSolverParticles& InParticles,
			const TArray<TVector<int32, 4>>& InMesh,
			const bool bRecordMetricIn = true,
			const Chaos::Softs::FSolverReal& EMesh = (FSolverReal)10.0,
			const Chaos::Softs::FSolverReal& NuMesh = (FSolverReal).3
			)
			: bRecordMetric(bRecordMetricIn), MeshConstraints(InMesh)
		{	

			LambdaArray.Init((FSolverReal)0., 2 * MeshConstraints.Num());
			DmInverse.Init((FSolverReal)0., 9 * MeshConstraints.Num());
			Measure.Init((FSolverReal)0.,  MeshConstraints.Num());
			Lambda = EMesh * NuMesh / (((FSolverReal)1. + NuMesh) * ((FSolverReal)1. - (FSolverReal)2. * NuMesh));
			Mu = EMesh / ((FSolverReal)2. * ((FSolverReal)1. + NuMesh));
			for (int e = 0; e < InMesh.Num(); e++)
			{
				PMatrix<FSolverReal, 3, 3> Dm = DsInit(e, InParticles);
				PMatrix<FSolverReal, 3, 3> DmInv = Dm.Inverse();
				for (int r = 0; r < 3; r++) {
					for (int c = 0; c < 3; c++) {
						DmInverse[(3 * 3) * e + 3 * r + c] = DmInv.GetAt(r, c);
					}
				}

				Measure[e] = Dm.Determinant() / (FSolverReal)6.;

				//part of preprocessing: if inverted element is found, 
				//invert it so that the measure is positive
				if (Measure[e] < (FSolverReal)0.)
				{

					Measure[e] = -Measure[e];
				}

			}

			InitColor(InParticles);
		}

		virtual ~FXPBDCorotatedConstraints() {}

		PMatrix<FSolverReal, 3, 3> DsInit(const int e, const FSolverParticles& InParticles) const {
			PMatrix<FSolverReal, 3, 3> Result((FSolverReal)0.);
			for (int i = 0; i < 3; i++) {
				for (int c = 0; c < 3; c++) {
					Result.SetAt(c, i, InParticles.X(MeshConstraints[e][i + 1])[c] - InParticles.X(MeshConstraints[e][0])[c]);
				}
			}
			return Result;
		}


		PMatrix<FSolverReal, 3, 3> Ds(const int e, const FSolverParticles& InParticles) const {
			PMatrix<FSolverReal, 3, 3> Result((FSolverReal)0.);
			for (int i = 0; i < 3; i++) {
				for (int c = 0; c < 3; c++) {
					Result.SetAt(c, i, InParticles.P(MeshConstraints[e][i+1])[c] - InParticles.P(MeshConstraints[e][0])[c]);
				}
			}
			return Result;
		}


		PMatrix<FSolverReal, 3, 3> F(const int e, const FSolverParticles& InParticles) const {
			return ElementDmInv(e) * Ds(e, InParticles);
		}

		PMatrix<FSolverReal, 3, 3> ElementDmInv(const int e) const {
			PMatrix<FSolverReal, 3, 3> DmInv((FSolverReal)0.);
			for (int r = 0; r < 3; r++) {
				for (int c = 0; c < 3; c++) {
					DmInv.SetAt(r, c, DmInverse[(3 * 3) * e + 3 * r + c]);
				}
			}
			return DmInv;
		}
		
		void Init() const 
		{
			for (FSolverReal& Lambdas : LambdaArray) { Lambdas = (FSolverReal)0.; }
		}

		virtual void ApplyInSerial(FSolverParticles& Particles, const FSolverReal Dt, const int32 ElementIndex) const
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("STAT_ChaosXPBDCorotatedApplySingle"));
			TVec4<FSolverVec3> PolarDelta = GetPolarDelta(Particles, Dt, ElementIndex);

			for (int i = 0; i < 4; i++) 
			{
				Particles.P(MeshConstraints[ElementIndex][i]) += PolarDelta[i];
			}
			
			TVec4<FSolverVec3> DetDelta = GetDeterminantDelta(Particles, Dt, ElementIndex);

			for (int i = 0; i < 4; i++)
			{
				Particles.P(MeshConstraints[ElementIndex][i]) += DetDelta[i];
			}


		}

		void ApplyInSerial(FSolverParticles& Particles, const FSolverReal Dt) const
		{
			SCOPE_CYCLE_COUNTER(STAT_ChaosXPBDCorotated);
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("STAT_ChaosXPBDCorotatedApplySerial"));
			for (int32 ElementIndex = 0; ElementIndex < MeshConstraints.Num(); ++ElementIndex)
			{
				ApplyInSerial(Particles, Dt, ElementIndex);
			}


		}

		void ApplyInParallel(FSolverParticles& Particles, const FSolverReal Dt) const
		{	
			//code for error metric:
			if (bRecordMetric)
			{
				GError.Init((FSolverReal)0., 3 * Particles.Size());
				HErrorArray.Init((FSolverReal)0., 2 * MeshConstraints.Num());
			}
			
			{
				SCOPE_CYCLE_COUNTER(STAT_ChaosXPBDCorotated);
				TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("STAT_ChaosXPBDCorotatedApply"));
				if ((ConstraintsPerColorStartIndex.Num() > 1))//&& (MeshConstraints.Num() > Chaos_Spring_ParallelConstraintCount))
				{
					const int32 ConstraintColorNum = ConstraintsPerColorStartIndex.Num() - 1;

					for (int32 ConstraintColorIndex = 0; ConstraintColorIndex < ConstraintColorNum; ++ConstraintColorIndex)
					{
						const int32 ColorStart = ConstraintsPerColorStartIndex[ConstraintColorIndex];
						const int32 ColorSize = ConstraintsPerColorStartIndex[ConstraintColorIndex + 1] - ColorStart;
						PhysicsParallelFor(ColorSize, [&](const int32 Index)
							{
								const int32 ConstraintIndex = ColorStart + Index;
								ApplyInSerial(Particles, Dt, ConstraintIndex);
							});
					}
				}
			}


		}

	private:

		void InitColor(const FSolverParticles& Particles)
		{

			{
				const TArray<TArray<int32>> ConstraintsPerColor = FGraphColoring::ComputeGraphColoring(MeshConstraints, Particles);

				// Reorder constraints based on color so each array in ConstraintsPerColor contains contiguous elements.
				TArray<TVec4<int32>> ReorderedConstraints;
				TArray<FSolverReal> ReorderedMeasure;
				TArray<FSolverReal> ReorderedDmInverse;
				TArray<int32> OrigToReorderedIndices; // used to reorder stiffness indices
				ReorderedConstraints.SetNumUninitialized(MeshConstraints.Num());
				ReorderedMeasure.SetNumUninitialized(Measure.Num());
				ReorderedDmInverse.SetNumUninitialized(DmInverse.Num());
				OrigToReorderedIndices.SetNumUninitialized(MeshConstraints.Num());

				ConstraintsPerColorStartIndex.Reset(ConstraintsPerColor.Num() + 1);

				int32 ReorderedIndex = 0;
				for (const TArray<int32>& ConstraintsBatch : ConstraintsPerColor)
				{
					ConstraintsPerColorStartIndex.Add(ReorderedIndex);
					for (const int32& BatchConstraint : ConstraintsBatch)
					{
						const int32 OrigIndex = BatchConstraint;
						ReorderedConstraints[ReorderedIndex] = MeshConstraints[OrigIndex];
						ReorderedMeasure[ReorderedIndex] = Measure[OrigIndex];
						for (int32 kk = 0; kk < 9; kk++)
						{
							ReorderedDmInverse[9 * ReorderedIndex + kk] = DmInverse[9 * OrigIndex + kk];
						}
						OrigToReorderedIndices[OrigIndex] = ReorderedIndex;

						++ReorderedIndex;
					}
				}
				ConstraintsPerColorStartIndex.Add(ReorderedIndex);

				MeshConstraints = MoveTemp(ReorderedConstraints);
				Measure = MoveTemp(ReorderedMeasure);
				DmInverse = MoveTemp(ReorderedDmInverse);
			}
		}

	protected:


		TVec4<FSolverVec3> GetDeterminantDelta(const FSolverParticles& Particles, const FSolverReal Dt, const int32 ElementIndex, const FSolverReal Tol = 1e-3) const
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("STAT_ChaosXPBDCorotatedApplyDet"));
			SCOPE_CYCLE_COUNTER(STAT_ChaosXPBDCorotatedDet);
			const TVec4<int32>& Constraint = MeshConstraints[ElementIndex];

			const PMatrix<FSolverReal, 3, 3> Fe = F(ElementIndex, Particles);
			PMatrix<FSolverReal, 3, 3> DmInvT = ElementDmInv(ElementIndex).GetTransposed();
			TVec4<FSolverVec3> dC2(FSolverVec3((FSolverReal)0.));

			FSolverReal J = Fe.Determinant();
			if (J - 1 < Tol)
			{
				return TVec4<FSolverVec3>(FSolverVec3((FSolverReal)0.));
			}

			PMatrix<FSolverReal, 3, 3> JFinvT;
			JFinvT.SetAt(0, 0, Fe.GetAt(1, 1) * Fe.GetAt(2, 2) - Fe.GetAt(2, 1) * Fe.GetAt(1, 2));
			JFinvT.SetAt(0, 1, Fe.GetAt(2, 0) * Fe.GetAt(1, 2) - Fe.GetAt(1, 0) * Fe.GetAt(2, 2));
			JFinvT.SetAt(0, 2, Fe.GetAt(1, 0) * Fe.GetAt(2, 1) - Fe.GetAt(2, 0) * Fe.GetAt(1, 1));
			JFinvT.SetAt(1, 0, Fe.GetAt(2, 1) * Fe.GetAt(0, 2) - Fe.GetAt(0, 1) * Fe.GetAt(2, 2));
			JFinvT.SetAt(1, 1, Fe.GetAt(0, 0) * Fe.GetAt(2, 2) - Fe.GetAt(2, 0) * Fe.GetAt(0, 2));
			JFinvT.SetAt(1, 2, Fe.GetAt(2, 0) * Fe.GetAt(0, 1) - Fe.GetAt(0, 0) * Fe.GetAt(2, 1));
			JFinvT.SetAt(2, 0, Fe.GetAt(0, 1) * Fe.GetAt(1, 2) - Fe.GetAt(1, 1) * Fe.GetAt(0, 2));
			JFinvT.SetAt(2, 1, Fe.GetAt(1, 0) * Fe.GetAt(0, 2) - Fe.GetAt(0, 0) * Fe.GetAt(1, 2));
			JFinvT.SetAt(2, 2, Fe.GetAt(0, 0) * Fe.GetAt(1, 1) - Fe.GetAt(1, 0) * Fe.GetAt(0, 1));

			PMatrix<FSolverReal, 3, 3> JinvTDmInvT = DmInvT * JFinvT;

			for (int ie = 0; ie < 3; ie++) {
				for (int alpha = 0; alpha < 3; alpha++) {
					dC2[ie + 1][alpha] = JinvTDmInvT.GetAt(alpha, ie);
				}
			}
			for (int alpha = 0; alpha < 3; alpha++) {
				for (int l = 0; l < 3; l++) {
					dC2[0][alpha] -= JinvTDmInvT.GetAt(alpha, l);
				}
			}

			FSolverReal AlphaTilde = (FSolverReal)2. / (Dt * Dt * Lambda * Measure[ElementIndex]);

			if (bRecordMetric)
			{
				HError += J - 1 + AlphaTilde * LambdaArray[2 * ElementIndex + 1];
				for (int32 ie = 0; ie < 4; ie++)
				{
					for (int32 Alpha = 0; Alpha < 3; Alpha++)
					{
						GError[MeshConstraints[ElementIndex][ie] * 3 + Alpha] -= dC2[ie][Alpha] * LambdaArray[2 * ElementIndex + 1];
					}
				}
				HErrorArray[2 * ElementIndex + 1] = J - 1 + AlphaTilde * LambdaArray[2 * ElementIndex + 1];
			}
			

			FSolverReal DLambda = (1 - J) - AlphaTilde * LambdaArray[2 * ElementIndex + 1];

			FSolverReal Denom = AlphaTilde;
			for (int i = 0; i < 4; i++)
			{
				for (int j = 0; j < 3; j++)
				{
					Denom += dC2[i][j] * Particles.InvM(MeshConstraints[ElementIndex][i]) * dC2[i][j];
				}
			}
			DLambda /= Denom;
			LambdaArray[2 * ElementIndex + 1] += DLambda;
			TVec4<FSolverVec3> Delta(FSolverVec3((FSolverReal)0.));
			for (int i = 0; i < 4; i++)
			{
				for (int j = 0; j < 3; j++)
				{
					Delta[i][j] = Particles.InvM(MeshConstraints[ElementIndex][i]) * dC2[i][j] * DLambda;
				}
			}
			return Delta;
		}

		TVec4<FSolverVec3> GetPolarDelta(const FSolverParticles& Particles, const FSolverReal Dt, const int32 ElementIndex, const FSolverReal Tol = 1e-3) const
		{	
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("STAT_ChaosXPBDCorotatedApplyPolar"));
			SCOPE_CYCLE_COUNTER(STAT_ChaosXPBDCorotatedPolar);
			const PMatrix<FSolverReal, 3, 3> Fe = F(ElementIndex, Particles);

			PMatrix<FSolverReal, 3, 3> Re((FSolverReal)0.), Se((FSolverReal)0.);

			Chaos::PolarDecomposition(Fe, Re, Se);

			FSolverReal C1 = (FSolverReal)0.;
			for (int i = 0; i < 3; i++)
			{
				for (int j = 0; j < 3; j++)
				{
					C1 += FMath::Square((Fe - Re).GetAt(i, j));
				}
			}
			C1 = FMath::Sqrt(C1);

			if (C1 < Tol)
			{
				return TVec4<FSolverVec3>(FSolverVec3((FSolverReal)0.));
			}

			TVector<FSolverReal, 81> dRdF((FSolverReal)0.);
			Chaos::dRdFCorotated(Fe, dRdF);

			PMatrix<FSolverReal, 3, 3> DmInvT = ElementDmInv(ElementIndex).GetTransposed();

			//TODO: deifnitely test the initialization
			TVec4<FSolverVec3> dC1(FSolverVec3((FSolverReal)0.));
			//dC1 = dC1dF * dFdX
			for (int alpha = 0; alpha < 3; alpha++) {
				for (int l = 0; l < 3; l++) {

					dC1[0][alpha] += (DmInvT * Re).GetAt(alpha, l) - (DmInvT * Fe).GetAt(alpha, l);

				}
			}
			for (int ie = 0; ie < 3; ie++) {
				for (int alpha = 0; alpha < 3; alpha++) {
					dC1[ie+1][alpha] = (DmInvT * Fe).GetAt(alpha, ie) - (DmInvT * Re).GetAt(alpha, ie);
				}
			}
			//it's really ie-1 here
			for (int ie = 0; ie < 3; ie++) {
				for (int alpha = 0; alpha < 3; alpha++) {
					for (int m = 0; m < 3; m++) {
						for (int n = 0; n < 3; n++) {
							for (int j = 0; j < 3; j++) {
								dC1[ie + 1][alpha] -= (Fe.GetAt(m, n) - Re.GetAt(m, n)) * dRdF[9*(alpha*3+j)+ m * 3 + n] * DmInvT.GetAt(j, ie);
							}
						}
					}
				}
			}
			for (int alpha = 0; alpha < 3; alpha++) {
				for (int m = 0; m < 3; m++) {
					for (int n = 0; n < 3; n++) {
						for (int l = 0; l < 3; l++) {
							for (int j = 0; j < 3; j++) {
								dC1[0][alpha] += (Fe.GetAt(m, n) - Re.GetAt(m, n)) * dRdF[9*(alpha*3+j) + m * 3 + n] * DmInvT.GetAt(j, l);
							}
						}
					}
				}
			}

			
			if (C1 != 0)
			{
				for (int i = 0; i < 4; i++)
				{
					for (int j = 0; j < 3; j++)
					{
						dC1[i][j] /= C1;
					}
				}
			}

			FSolverReal AlphaTilde = (FSolverReal)1. / (Dt * Dt * Mu * Measure[ElementIndex]);

			if (bRecordMetric)
			{
				HError += C1 + AlphaTilde * LambdaArray[2 * ElementIndex + 0];
				for (int32 ie = 0; ie < 4; ie++)
				{
					for (int32 Alpha = 0; Alpha < 3; Alpha++)
					{
						GError[MeshConstraints[ElementIndex][ie] * 3 + Alpha] -= dC1[ie][Alpha] * LambdaArray[2 * ElementIndex + 0];
					}
				}
				HErrorArray[2 * ElementIndex + 0] = C1 + AlphaTilde * LambdaArray[2 * ElementIndex + 0];
			}

			FSolverReal DLambda = -C1 - AlphaTilde* LambdaArray[2 * ElementIndex + 0];

			FSolverReal Denom = AlphaTilde;
			for (int i = 0; i < 4; i++)
			{
				for (int j = 0; j < 3; j++)
				{
					Denom += dC1[i][j] * Particles.InvM(MeshConstraints[ElementIndex][i]) * dC1[i][j];
				}
			}
			DLambda /= Denom;
			LambdaArray[2 * ElementIndex + 0] += DLambda;
			TVec4<FSolverVec3> Delta(FSolverVec3((FSolverReal)0.));
			for (int i = 0; i < 4; i++) 
			{
				for (int j = 0; j < 3; j++)
				{
					Delta[i][j] = Particles.InvM(MeshConstraints[ElementIndex][i]) * dC1[i][j] * DLambda;
				}
			}
			return Delta;

		}


	protected:
		mutable TArray<FSolverReal> LambdaArray;
		mutable TArray<FSolverReal> DmInverse;

		//material constants calculated from E:
		FSolverReal Mu;
		FSolverReal Lambda;
		mutable FSolverReal HError;
		mutable TArray<FSolverReal> HErrorArray;
		bool bRecordMetric;

		TArray<TVector<int32, 4>> MeshConstraints;
		mutable TArray<FSolverReal> Measure;
		FSolverParticles RestParticles;
		TArray<int32> ConstraintsPerColorStartIndex; // Constraints are ordered so each batch is contiguous. This is ColorNum + 1 length so it can be used as start and end.
		mutable TArray<FSolverReal> GError;
};

}  // End namespace Chaos::Softs

