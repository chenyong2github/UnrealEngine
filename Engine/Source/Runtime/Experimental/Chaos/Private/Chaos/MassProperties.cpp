// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/MassProperties.h"
#include "Chaos/Rotation.h"
#include "Chaos/Matrix.h"
#include "Chaos/Particles.h"
#include "Chaos/TriangleMesh.h"

namespace Chaos
{
	template<class T, int d>
	TRotation<T, d> TransformToLocalSpace(PMatrix<T, d, d>& Inertia)
	{
		TRotation<T, d> FinalRotation;

		// Extract Eigenvalues
		T OffDiagSize = FMath::Square(Inertia.M[1][0]) + FMath::Square(Inertia.M[2][0]) + FMath::Square(Inertia.M[2][1]);
		T Trace = (Inertia.M[0][0] + Inertia.M[1][1] + Inertia.M[2][2]) / 3;

		if (!ensure(Trace > SMALL_NUMBER))
		{
			// Tiny inertia - numerical instability would follow. We should not get this unless we have bad input.
			return TRotation<T, d>::FromElements(TVector<T, d>(0), 1);
		}

		if ((OffDiagSize / Trace) < SMALL_NUMBER)
		{
			// Almost diagonal matrix - we are already in local space.
			return TRotation<T, d>::FromElements(TVector<T, d>(0), 1);
		}

		T Size = FMath::Sqrt((FMath::Square(Inertia.M[0][0] - Trace) + FMath::Square(Inertia.M[1][1] - Trace) + FMath::Square(Inertia.M[2][2] - Trace) + 2 * OffDiagSize) / 6);
		PMatrix<T, d, d> NewMat = (Inertia - FMatrix::Identity * Trace) * (1 / Size);
		T HalfDeterminant = NewMat.Determinant() / 2;
		T Angle = HalfDeterminant <= -1 ? PI / 3 : (HalfDeterminant >= 1 ? 0 : acos(HalfDeterminant) / 3);
		T m00 = Trace + 2 * Size * cos(Angle), m11 = Trace + 2 * Size * cos(Angle + (2 * PI / 3)), m22 = 3 * Trace - m00 - m11;

		// Extract Eigenvectors
		bool DoSwap = ((m00 - m11) > (m11 - m22)) ? false : true;
		TVector<T, d> Eigenvector0 = (Inertia.SubtractDiagonal(DoSwap ? m22 : m00)).SymmetricCofactorMatrix().LargestColumnNormalized();
		TVector<T, d> Orthogonal = Eigenvector0.GetOrthogonalVector().GetSafeNormal();
		PMatrix<T, d, d - 1> Cofactors(Orthogonal, TVector<T, d>::CrossProduct(Eigenvector0, Orthogonal));
		PMatrix<T, d, d - 1> CofactorsScaled = Inertia * Cofactors;
		PMatrix<T, d - 1, d - 1> IR(
			CofactorsScaled.M[0] * Cofactors.M[0] + CofactorsScaled.M[1] * Cofactors.M[1] + CofactorsScaled.M[2] * Cofactors.M[2],
			CofactorsScaled.M[3] * Cofactors.M[0] + CofactorsScaled.M[4] * Cofactors.M[1] + CofactorsScaled.M[5] * Cofactors.M[2],
			CofactorsScaled.M[3] * Cofactors.M[3] + CofactorsScaled.M[4] * Cofactors.M[4] + CofactorsScaled.M[5] * Cofactors.M[5]);
		PMatrix<T, d - 1, d - 1> IM1 = IR.SubtractDiagonal(DoSwap ? m00 : m22);
		T OffDiag = IM1.M[1] * IM1.M[1];
		T IM1Scale0 = FMath::Max(T(0), IM1.M[3] * IM1.M[3] + OffDiag);
		T IM1Scale1 = FMath::Max(T(0), IM1.M[0] * IM1.M[0] + OffDiag);
		T SqrtIM1Scale0 = FMath::Sqrt(IM1Scale0);
		T SqrtIM1Scale1 = FMath::Sqrt(IM1Scale1);

		if (!ensure((SqrtIM1Scale0 > KINDA_SMALL_NUMBER) || (SqrtIM1Scale1 > KINDA_SMALL_NUMBER)))
		{
			// We hit numerical accuracy, despite the early off-diagonal check. We should not see this any more.
			return TRotation<T, d>::FromElements(TVector<T, d>(0), 1);
		}

		TVector<T, d - 1> SmallEigenvector2 = IM1Scale0 > IM1Scale1 ? (TVector<T, d - 1>(IM1.M[3], -IM1.M[1]) / SqrtIM1Scale0) : (IM1Scale1 > 0 ? (TVector<T, d - 1>(-IM1.M[1], IM1.M[0]) / SqrtIM1Scale1) : TVector<T, d - 1>(1, 0));
		TVector<T, d> Eigenvector2 = (Cofactors * SmallEigenvector2).GetSafeNormal();
		TVector<T, d> Eigenvector1 = TVector<T, d>::CrossProduct(Eigenvector2, Eigenvector0).GetSafeNormal();

		// Return results
		Inertia = PMatrix<T, d, d>(m00, 0, 0, m11, 0, m22);
		PMatrix<T, d, d> RotationMatrix = DoSwap ? PMatrix<T, d, d>(Eigenvector2, Eigenvector1, -Eigenvector0) : PMatrix<T, d, d>(Eigenvector0, Eigenvector1, Eigenvector2);
		FinalRotation = TRotation<T,d>(RotationMatrix);
		check(FMath::IsNearlyEqual(FinalRotation.Size(), 1.0f, KINDA_SMALL_NUMBER));
		
		return FinalRotation;
	}
	template TRotation<float, 3> TransformToLocalSpace(PMatrix<float, 3, 3>& Inertia);

	template<typename T, int d, typename TSurfaces>
	void CalculateVolumeAndCenterOfMass(const TParticles<T, d>& Vertices, const TSurfaces& Surfaces, T& OutVolume, TVector<T, d>& OutCenterOfMass)
	{
		if (!Surfaces.Num())
		{
			OutVolume = 0;
			return;
		}
		
		T Volume = 0;
		TVector<T, d> VolumeTimesSum(0);
		TVector<T, d> Center = Vertices.X(Surfaces[0][0]);
		for (const auto& Element : Surfaces)
		{
			// For now we only support triangular elements
			ensure(Element.Num() == 3);

			PMatrix<T, d, d> DeltaMatrix;
			TVector<T, d> PerElementSize;
			for (int32 i = 0; i < Element.Num(); ++i)
			{
				TVector<T, d> DeltaVector = Vertices.X(Element[i]) - Center;
				DeltaMatrix.M[0][i] = DeltaVector[0];
				DeltaMatrix.M[1][i] = DeltaVector[1];
				DeltaMatrix.M[2][i] = DeltaVector[2];
			}
			PerElementSize[0] = DeltaMatrix.M[0][0] + DeltaMatrix.M[0][1] + DeltaMatrix.M[0][2];
			PerElementSize[1] = DeltaMatrix.M[1][0] + DeltaMatrix.M[1][1] + DeltaMatrix.M[1][2];
			PerElementSize[2] = DeltaMatrix.M[2][0] + DeltaMatrix.M[2][1] + DeltaMatrix.M[2][2];
			T Det = DeltaMatrix.M[0][0] * (DeltaMatrix.M[1][1] * DeltaMatrix.M[2][2] - DeltaMatrix.M[1][2] * DeltaMatrix.M[2][1]) -
				DeltaMatrix.M[0][1] * (DeltaMatrix.M[1][0] * DeltaMatrix.M[2][2] - DeltaMatrix.M[1][2] * DeltaMatrix.M[2][0]) +
				DeltaMatrix.M[0][2] * (DeltaMatrix.M[1][0] * DeltaMatrix.M[2][1] - DeltaMatrix.M[1][1] * DeltaMatrix.M[2][0]);
			Volume += Det;
			VolumeTimesSum += Det * PerElementSize;
		}
		// @todo(mlentine): Should add suppoert for thin shell mass properties
		if (Volume < KINDA_SMALL_NUMBER)	//handle negative volume using fallback for now. Need to investigate cases where this happens
		{
			OutVolume = 0;
			return;
		}
		OutCenterOfMass = Center + VolumeTimesSum / (4 * Volume);
		OutVolume = Volume / 6;
	}

	template <typename T, int d, typename TSurfaces>
	void CalculateInertiaAndRotationOfMass(const TParticles<T, d>& Vertices, const TSurfaces& Surfaces, const T Density, const TVector<T,d>& CenterOfMass,
	PMatrix<T,d,d>& OutInertiaTensor, TRotation<T,d>& OutRotationOfMass)
	{
		check(Density > 0);

		static const PMatrix<T, d, d> Standard(2, 1, 1, 2, 1, 2);
		PMatrix<T, d, d> Covariance(0);
		for (const auto& Element : Surfaces)
		{
			PMatrix<T, d, d> DeltaMatrix(0);
			for (int32 i = 0; i < Element.Num(); ++i)
			{
				TVector<T, d> DeltaVector = Vertices.X(Element[i]) - CenterOfMass;
				DeltaMatrix.M[0][i] = DeltaVector[0];
				DeltaMatrix.M[1][i] = DeltaVector[1];
				DeltaMatrix.M[2][i] = DeltaVector[2];
			}
			T Det = DeltaMatrix.M[0][0] * (DeltaMatrix.M[1][1] * DeltaMatrix.M[2][2] - DeltaMatrix.M[1][2] * DeltaMatrix.M[2][1]) -
				DeltaMatrix.M[0][1] * (DeltaMatrix.M[1][0] * DeltaMatrix.M[2][2] - DeltaMatrix.M[1][2] * DeltaMatrix.M[2][0]) +
				DeltaMatrix.M[0][2] * (DeltaMatrix.M[1][0] * DeltaMatrix.M[2][1] - DeltaMatrix.M[1][1] * DeltaMatrix.M[2][0]);
			const PMatrix<T, d, d> ScaledStandard = Standard * Det;
			Covariance += DeltaMatrix * ScaledStandard * DeltaMatrix.GetTransposed();
		}
		T Trace = Covariance.M[0][0] + Covariance.M[1][1] + Covariance.M[2][2];
		PMatrix<T, d, d> TraceMat(Trace, Trace, Trace);
		OutInertiaTensor = (TraceMat - Covariance) * (1 / (T)120) * Density;
		OutRotationOfMass = TransformToLocalSpace(OutInertiaTensor);
	}


	template<class T, int d, typename TSurfaces>
	TMassProperties<T, d> CalculateMassProperties(
		const TParticles<T, d> & Vertices,
		const TSurfaces& Surfaces,
		const T Mass)
	{
		TMassProperties<T, d> MassProperties;
		CalculateVolumeAndCenterOfMass(Vertices, Surfaces, MassProperties.Volume, MassProperties.CenterOfMass);

		check(Mass > 0);
		check(MassProperties.Volume > SMALL_NUMBER);
		CalculateInertiaAndRotationOfMass(Vertices, Surfaces, Mass / MassProperties.Volume, MassProperties.CenterOfMass, MassProperties.InertiaTensor, MassProperties.RotationOfMass);
		
		return MassProperties;
	}

	template<class T, int d>
	TMassProperties<T, d> Combine(const TArray<TMassProperties<T, d>>& MPArray)
	{
		check(MPArray.Num() > 0);
		if (MPArray.Num() == 1)
			return MPArray[0];
		TMassProperties<T, d> NewMP;
		for (const TMassProperties<T, d>& Child : MPArray)
		{
			NewMP.Volume += Child.Volume;
			const PMatrix<T, d, d> ChildRI = Child.RotationOfMass * FMatrix::Identity;
			const PMatrix<T, d, d> ChildWorldSpaceI = ChildRI.GetTransposed() * Child.InertiaTensor * ChildRI;
			NewMP.InertiaTensor += ChildWorldSpaceI;
			NewMP.CenterOfMass += Child.CenterOfMass * Child.Volume;
		}
		check(NewMP.Volume > SMALL_NUMBER);
		NewMP.CenterOfMass /= NewMP.Volume;
		for (const TMassProperties<T, d>& Child : MPArray)
		{
			const T M = Child.Volume;
			const TVector<T, d> ParentToChild = Child.CenterOfMass - NewMP.CenterOfMass;
			const T P0 = ParentToChild[0];
			const T P1 = ParentToChild[1];
			const T P2 = ParentToChild[2];
			const T MP0P0 = M * P0 * P0;
			const T MP1P1 = M * P1 * P1;
			const T MP2P2 = M * P2 * P2;
			NewMP.InertiaTensor += PMatrix<T, d, d>(MP1P1+MP2P2, -M*P1*P0, -M*P2*P0, MP2P2+MP0P0, -M*P2*P1, MP1P1+MP0P0);
		}
		NewMP.RotationOfMass = TransformToLocalSpace<T, d>(NewMP.InertiaTensor);
		return NewMP;
	}

	template CHAOS_API TMassProperties<float, 3> CalculateMassProperties(const TParticles<float, 3> & Vertices, 
		const TArray<TVector<int32, 3>>& Surfaces, const float Mass);

	template CHAOS_API TMassProperties<float, 3> CalculateMassProperties(const TParticles<float, 3> & Vertices, 
		const TArray<TArray<int32>>& Surfaces, const float Mass);

	template CHAOS_API void CalculateVolumeAndCenterOfMass(const TParticles<float, 3>& Vertices,
		const TArray<TVector<int32, 3>>& Surfaces, float& OutVolume, TVector<float, 3>& OutCenterOfMass);

	template CHAOS_API void CalculateVolumeAndCenterOfMass(const TParticles<float, 3>& Vertices,
		const TArray<TArray<int32>>& Surfaces, float& OutVolume, TVector<float, 3>& OutCenterOfMass);

	template CHAOS_API void CalculateInertiaAndRotationOfMass(const TParticles<float, 3>& Vertices, const TArray<TVector<int32, 3>>& Surface, const float Density,
		const TVector<float, 3>& CenterOfMass, PMatrix<float, 3, 3>& OutInertiaTensor, TRotation<float, 3>& OutRotationOfMass);

	template CHAOS_API void CalculateInertiaAndRotationOfMass(const TParticles<float, 3>& Vertices, const TArray<TArray<int32>>& Surface, const float Density,
		const TVector<float, 3>& CenterOfMass, PMatrix<float, 3, 3>& OutInertiaTensor, TRotation<float, 3>& OutRotationOfMass);

	template CHAOS_API TMassProperties<float, 3> Combine(const TArray<TMassProperties<float, 3>>& MPArray);
}
