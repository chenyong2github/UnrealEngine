// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSpringConstraintsBase.h"
#include "ChaosStats.h"
#include "Chaos/ImplicitQRSVD.h"
#include "Chaos/GraphColoring.h"
#include "Chaos/MPMTransfer.h"
#include "Chaos/Framework/Parallel.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Chaos/GraphColoring.h"

//DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Corotated Constraint"), STAT_ChaosXPBDCorotated, STATGROUP_Chaos);
//DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Corotated Constraint Polar Compute"), STAT_ChaosXPBDCorotatedPolar, STATGROUP_Chaos);
//DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Corotated Constraint Det Compute"), STAT_ChaosXPBDCorotatedDet, STATGROUP_Chaos);

namespace Chaos::Softs
{

	using Chaos::TVec3;

	template <typename T, typename ParticleType>
	class FXPBDWeakConstraints
	{

	public:
		//this one only accepts tetmesh input and mesh
		FXPBDWeakConstraints(
			const ParticleType& InParticles,
			const TArray<TArray<int32>>& InIndices,
			const TArray<TArray<T>>& InWeights,
			const TArray<T>& InStiffness
		)
			: Stiffness(InStiffness), Indices(InIndices), Weights(InWeights)
		{
			SecondIndices.SetNum(0);
			SecondWeights.SetNum(0);
			InitColor(InParticles);
			LambdaArray.Init((T)0., Indices.Num() * 3);
		}

		FXPBDWeakConstraints(
			const ParticleType& InParticles,
			const TArray<TArray<int32>>& InIndices,
			const TArray<TArray<T>>& InWeights,
			const TArray<T>& InStiffness,
			const TArray<TArray<int32>>& InSecondIndices,
			const TArray<TArray<T>>& InSecondWeights
		)
			: Indices(InIndices), Weights(InWeights), SecondIndices(InSecondIndices), SecondWeights(InSecondWeights), Stiffness(InStiffness)
		{
			ensureMsgf(Indices.Num() == SecondIndices.Num(), TEXT("Input Double Bindings have wrong size"));
			
			for (int32 i = 0; i < Indices.Num(); i++)
			{
				TSet<int32> IndicesSet = TSet<int32>(Indices[i]);
				for (int32 j = 0; j < SecondIndices[i].Num(); j++)
				{
					ensureMsgf(!IndicesSet.Contains(SecondIndices[i][j]), TEXT("Indices and Second Indices overlaps. Currently not supported"));
				}
			}
			InitColor(InParticles);
			LambdaArray.Init((T)0., Indices.Num() * 3);
		}


		virtual ~FXPBDWeakConstraints() {}

		void ApplyInParallel(ParticleType& Particles, const T Dt) const
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("STAT_ChaosXPBDWeakConstraintApply"));

			if ((ConstraintsPerColor.Num() > 0))
			{
				if (SecondIndices.Num() == 0)
				{
					for (int32 Color = 0; Color < ConstraintsPerColor.Num(); Color++)
					{
						PhysicsParallelFor(ConstraintsPerColor[Color].Num(), [&](const int32 Index)
							{
								const int32 ConstraintIndex = ConstraintsPerColor[Color][Index];
								ApplySingleConstraintWithoutSelfTarget(Particles, Dt, ConstraintIndex);
							});
					}
				}
				else 
				{
					for (int32 Color = 0; Color < ConstraintsPerColor.Num(); Color++)
					{
						PhysicsParallelFor(ConstraintsPerColor[Color].Num(), [&](const int32 Index)
							{
								const int32 ConstraintIndex = ConstraintsPerColor[Color][Index];
								ApplySingleConstraintWithSelfTarget(Particles, Dt, ConstraintIndex);
							});
					}
				}
			}
		}


		const TArray<TArray<int32>>& GetIndices()
		{
			return Indices;
		}

		virtual void Init() const
		{
			for (T& Lambdas : LambdaArray) { Lambdas = (T)0.; }
		}

		void UpdateTargets(TArray<TVector<T, 3>>&& InTargets)
		{
			Constraints = MoveTemp(InTargets);
		}


	private:

		void InitColor(const ParticleType& Particles)
		{
			Chaos::ComputeWeakConstraintsColoring(Indices, SecondIndices, Particles, ConstraintsPerColor);
		}

		void ApplySingleConstraintWithoutSelfTarget(ParticleType& Particles, const T Dt, const int32 ConstraintIndex) const
		{
			T AlphaTilde = T(2) / (Stiffness[ConstraintIndex] * Dt * Dt);

			if (Stiffness[ConstraintIndex] > 1e14)
			{
				AlphaTilde = T(0);
			}

			TVec3<T> SpringEdge((T)0.);
			for (int32 j = 0; j < Indices[ConstraintIndex].Num(); j++) {
				for (int32 alpha = 0; alpha < 3; alpha++) {
					SpringEdge[alpha] += Weights[ConstraintIndex][j] * Particles.P(Indices[ConstraintIndex][j])[alpha];
				}
			}

			SpringEdge -= Constraints[ConstraintIndex];

			T Denom = AlphaTilde;
			for (int32 j = 0; j < Indices[ConstraintIndex].Num(); j++) {
				Denom += Weights[ConstraintIndex][j] * Weights[ConstraintIndex][j] * Particles.InvM(Indices[ConstraintIndex][j]);
				
			}

			for (int32 Beta = 0; Beta < 3; Beta++)
			{
				T Cj = SpringEdge[Beta];
				T DLambda = -Cj - AlphaTilde * LambdaArray[ConstraintIndex * 3 + Beta];

				DLambda /= Denom;
				LambdaArray[ConstraintIndex * 3 + Beta] += DLambda;
				for (int32 j = 0; j < Indices[ConstraintIndex].Num(); j++) {
					Particles.P(Indices[ConstraintIndex][j])[Beta] += DLambda * Weights[ConstraintIndex][j] * Particles.InvM(Indices[ConstraintIndex][j]);
				}
			}
		}

		void ApplySingleConstraintWithSelfTarget(ParticleType& Particles, const T Dt, const int32 ConstraintIndex) const
		{
			ensure(SecondIndices.Num() > 0);

			T AlphaTilde = T(2) / (Stiffness[ConstraintIndex] * Dt * Dt);

			if (Stiffness[ConstraintIndex] > 1e14)
			{
				AlphaTilde = T(0);
			}

			TVec3<T> SpringEdge((T)0.);
			for (int32 j = 0; j < Indices[ConstraintIndex].Num(); j++) {
				for (int32 alpha = 0; alpha < 3; alpha++) {
					SpringEdge[alpha] += Weights[ConstraintIndex][j] * Particles.P(Indices[ConstraintIndex][j])[alpha];
				}
			}

			for (int32 j = 0; j < SecondIndices[ConstraintIndex].Num(); j++) {

					SpringEdge -= SecondWeights[ConstraintIndex][j] * Particles.P(SecondIndices[ConstraintIndex][j]);

			}

			T Denom = AlphaTilde;
			for (int32 j = 0; j < Indices[ConstraintIndex].Num(); j++) {
				Denom += Weights[ConstraintIndex][j] * Weights[ConstraintIndex][j] * Particles.InvM(Indices[ConstraintIndex][j]);
			}

			for (int32 j = 0; j < SecondIndices[ConstraintIndex].Num(); j++) {
				Denom += SecondWeights[ConstraintIndex][j] * SecondWeights[ConstraintIndex][j] * Particles.InvM(SecondIndices[ConstraintIndex][j]);
			}


			for (int32 Beta = 0; Beta < 3; Beta++)
			{
				T Cj = SpringEdge[Beta];
				T DLambda = -Cj - AlphaTilde * LambdaArray[ConstraintIndex * 3 + Beta];

				DLambda /= Denom;
				LambdaArray[ConstraintIndex * 3 + Beta] += DLambda;
				for (int32 j = 0; j < Indices[ConstraintIndex].Num(); j++) {
					Particles.P(Indices[ConstraintIndex][j])[Beta] += DLambda * Weights[ConstraintIndex][j] * Particles.InvM(Indices[ConstraintIndex][j]);
				}

				for (int32 j = 0; j < SecondIndices[ConstraintIndex].Num(); j++) {
					Particles.P(SecondIndices[ConstraintIndex][j])[Beta] -= DLambda * SecondWeights[ConstraintIndex][j] * Particles.InvM(SecondIndices[ConstraintIndex][j]);
				}
			}
		}


	protected:
		TArray<TArray<int32>> Indices;
		TArray<TArray<T>> Weights;
		TArray<TVector<T, 3>> Constraints;
		TArray<TArray<int32>> SecondIndices;
		TArray<TArray<T>> SecondWeights;
		TArray<T> Stiffness;
		TArray<TArray<int32>> ConstraintsPerColor;
		mutable TArray<T> LambdaArray;
	};

}  // End namespace Chaos::Softs

