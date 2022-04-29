// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSpringConstraintsBase.h"
#include "ChaosStats.h"
#include "Chaos/ImplicitQRSVD.h"
#include "Chaos/GraphColoring.h"
#include "Chaos/Framework/Parallel.h"

//DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Corotated Constraint"), STAT_XPBD_Spring, STATGROUP_Chaos);

namespace Chaos::Softs
{

	class FXPBDCorotatedFiberConstraints : public FXPBDCorotatedConstraints
	{

		typedef FXPBDCorotatedConstraints Base;
		using Base::MeshConstraints;
		using Base::LambdaArray;
		using Base::Measure;
		using Base::DmInverse;

	public:
		//this one only accepts tetmesh input and mesh
		FXPBDCorotatedFiberConstraints(
			const FSolverParticles& InParticles,
			int32 ParticleOffset,
			int32 ParticleCount,
			const TArray<TVector<int32, 4>>& InMesh,
			const bool bRecordMetricIn = true,
			const FSolverReal& EMesh = (FReal)10.0,
			const FSolverReal& NuMesh = (FReal).3,
			const FSolverVec3 InFiberDir = FSolverVec3((FSolverReal)1., (FSolverReal)0., (FSolverReal)0.),
			const FSolverReal InSigmaMax = (FSolverReal)3e5
		)
			: Base(InParticles, ParticleOffset, ParticleCount, InMesh, bRecordMetricIn, EMesh, NuMesh), SigmaMax(InSigmaMax), FiberDir(InFiberDir)
		{
			LambdaArray.Init((FSolverReal)0., 3 * MeshConstraints.Num());
		}

		virtual ~FXPBDCorotatedFiberConstraints() {}

		void SetTime(const float Time) const {
			float FinalTime = 4.0f;
			float CurrentTime = Time;
			while (CurrentTime > FinalTime)
			{
				CurrentTime -= FinalTime;
			}
			AlphaActivation = (FReal)1. - (FReal)4. / FinalTime * FMath::Abs(CurrentTime - FinalTime / (FReal)2.);
		}

		virtual void ApplyInSerial(FSolverParticles& Particles, const FSolverReal Dt, const int32 ElementIndex) const override
		{
			TVec4<FSolverVec3> PolarDelta = Base::GetPolarDelta(Particles, Dt, ElementIndex);

			for (int i = 0; i < 4; i++)
			{
				Particles.P(MeshConstraints[ElementIndex][i]) += PolarDelta[i];
			}

			TVec4<FSolverVec3> DetDelta = Base::GetDeterminantDelta(Particles, Dt, ElementIndex);

			for (int i = 0; i < 4; i++)
			{
				Particles.P(MeshConstraints[ElementIndex][i]) += DetDelta[i];
			}

			TVec4<FSolverVec3> FiberDelta = GetFiberDelta(Particles, Dt, ElementIndex);

			for (int i = 0; i < 4; i++)
			{
				Particles.P(MeshConstraints[ElementIndex][i]) += FiberDelta[i];
			}
			

		}



	private:

		TVec4<FSolverVec3> GetFiberDelta(const FSolverParticles& Particles, const FSolverReal Dt, const int32 ElementIndex, const FSolverReal Tol = 1e-3) const
		{
			const PMatrix<FReal, 3, 3> Fe = F(ElementIndex, Particles);

			PMatrix<FReal, 3, 3> Re((FReal)0.), Se((FReal)0.);

			Chaos::PolarDecomposition(Fe, Re, Se);

			// l: fiber stretch, f = (vTCv)^(1/2)
			FSolverVec3 FeV = Fe.GetTransposed() * FiberDir;
			FSolverVec3 DmInverseV = ElementDmInv(ElementIndex).GetTransposed() * FiberDir;
			FSolverReal L = FeV.Size();
			TVec4<FSolverVec3> dLdX(FSolverVec3((FReal)0.));
			for (int32 alpha = 0; alpha < 3; alpha++)
			{
				for (int32 s = 0; s < 3; s++)
				{
					dLdX[0][alpha] -= FeV[alpha] * DmInverseV[s] / L;
				}

			}
			for (int32 ie = 1; ie < 4; ie++)
			{
				for (int32 alpha = 0; alpha < 3; alpha++)
				{
					dLdX[ie][alpha] = FeV[alpha] * DmInverseV[ie - 1] / L;
				}
			}

			FReal LambdaOFL = (FReal)1.4;
			FReal P1 = (FReal)0.05;
			FReal P2 = (FReal)6.6;
			FReal FpIntegral = (FReal)0.;
			FReal FaIntegral = (FReal)0.;

			FReal C3 = (FReal)0.;
			FReal AlphaTilde = LambdaOFL / (SigmaMax * Dt * Dt * Measure[ElementIndex]);
			FReal dFpdL = (FReal)0.;
			FReal dFadL = (FReal)0.;

			if (L > LambdaOFL)
			{
				FpIntegral = P1 * LambdaOFL / P2 * FMath::Exp(P2 * (L / LambdaOFL - (FReal)1.)) - P1 * (L - LambdaOFL);
				dFpdL = P1 * FMath::Exp(P2 * (L / LambdaOFL - (FReal)1.)) - P1;
			}
			if (L > (FReal)0.4 * LambdaOFL && L < (FReal)0.6 * LambdaOFL)
			{
				FaIntegral = (FReal)3. * LambdaOFL * FMath::Pow((L / LambdaOFL - (FReal)0.4), 3);
				dFadL = (FReal)9. * FMath::Pow((L / LambdaOFL - (FReal)0.4), 2);
			}
			else if (L >= (FReal)0.6 * LambdaOFL && L <= (FReal)1.4 * LambdaOFL)
			{
				FaIntegral = (FReal)3. * LambdaOFL * (FReal)0.008 + L - (FReal)4. / (FReal)3. * LambdaOFL * FMath::Pow(L / LambdaOFL - (FReal)1., 3) - (FReal)0.6 * LambdaOFL - (FReal)4. / (FReal)3. * LambdaOFL * FMath::Pow((FReal)0.4, 3);
				dFadL = (FReal)1. - (FReal)4. * FMath::Pow(L / LambdaOFL - (FReal)1., 2);
			}
			else if (L > (FReal)1.4 * LambdaOFL && L <= (FReal)1.6 * LambdaOFL)
			{
				FaIntegral = (FReal)3. * LambdaOFL * (FReal)0.008 + (FReal)0.8 * LambdaOFL - (FReal)8. / (FReal)3. * LambdaOFL * FMath::Pow((FReal)0.4, 3) + (FReal)3. * LambdaOFL * FMath::Pow(L / LambdaOFL - (FReal)1.6, 3) + (FReal)3. * LambdaOFL * (FReal)0.008;
				dFadL = (FReal)9. * FMath::Pow((L / LambdaOFL - (FReal)1.6), 2);

			}
			else if (L > (FReal)1.6 * LambdaOFL)
			{
				FaIntegral = (FReal)3. * LambdaOFL * (FReal)0.008 + (FReal)0.8 * LambdaOFL - (FReal)8. / (FReal)3. * LambdaOFL * FMath::Pow((FReal)0.4, 3) + (FReal)3. * LambdaOFL * (FReal)0.008;
				dFadL = (FReal)0.;
			}


			C3 = FMath::Sqrt(FpIntegral + AlphaActivation * FaIntegral);


			FReal dC3dL = (FReal)0.5 * (dFpdL + AlphaActivation * dFadL);
			if (C3 == 0)
			{
				return TVec4<FSolverVec3>(FSolverVec3((FSolverReal)0.));
			}
			else
			{
				dC3dL /= C3;
			}

			TVec4<FSolverVec3> dC3(FSolverVec3((FReal)0.));
			for (int32 i = 0; i < 4; i++)
			{
				for (int32 j = 0; j < 3; j++)
				{
					dC3[i][j] = dC3dL * dLdX[i][j];
				}
			}

			FReal DLambda = -C3 - AlphaTilde * LambdaArray[2 * ElementIndex + 2];

			FReal Denom = AlphaTilde;
			for (int i = 0; i < 4; i++)
			{
				for (int j = 0; j < 3; j++)
				{
					Denom += dC3[i][j] * Particles.InvM(MeshConstraints[ElementIndex][i]) * dC3[i][j];
				}
			}
			DLambda /= Denom;
			LambdaArray[2 * ElementIndex + 2] += DLambda;
			TVec4<FSolverVec3> Delta(FSolverVec3((FReal)0.));
			for (int i = 0; i < 4; i++)
			{
				for (int j = 0; j < 3; j++)
				{
					Delta[i][j] = Particles.InvM(MeshConstraints[ElementIndex][i]) * dC3[i][j] * DLambda;
				}
			}
			return Delta;



		}

	private:

		//material constants calculated from E:
		FReal SigmaMax;
		mutable FReal AlphaActivation;
		FSolverVec3 FiberDir;

	};

}  // End namespace Chaos::Softs

