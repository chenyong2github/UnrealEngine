// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/MassProperties.h"
//#include "Chaos/Core.h"
#include "Chaos/Rotation.h"
#include "Chaos/Matrix.h"
#include "Chaos/Particles.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/Utilities.h"
#include "ChaosCheck.h"


//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	FRotation3 TransformToLocalSpace(FMatrix33& Inertia)
	{
		FRotation3 FinalRotation;

		// Extract Eigenvalues
		FReal OffDiagSize = FMath::Square(Inertia.M[1][0]) + FMath::Square(Inertia.M[2][0]) + FMath::Square(Inertia.M[2][1]);
		FRealDouble Trace = (Inertia.M[0][0] + Inertia.M[1][1] + Inertia.M[2][2]) / 3;

		if (Trace <= UE_SMALL_NUMBER)
		{
			// Tiny inertia - numerical instability would follow. We should not get this unless we have bad input.
			return FRotation3::FromIdentity();
		}

		if ((OffDiagSize / Trace) < UE_SMALL_NUMBER)
		{
			// Almost diagonal matrix - we are already in local space.
			return FRotation3::FromIdentity();
		}

		FReal Size = static_cast<FReal>(FMath::Sqrt((FMath::Square(Inertia.M[0][0] - Trace) + FMath::Square(Inertia.M[1][1] - Trace) + FMath::Square(Inertia.M[2][2] - Trace) + 2. * OffDiagSize) / 6.));
		FMatrix33 NewMat = (Inertia - FMatrix::Identity * static_cast<FReal>(Trace)) * static_cast<FReal>((1 / Size));
		FReal HalfDeterminant = NewMat.Determinant() / 2;
		FReal Angle = HalfDeterminant <= -1 ? UE_PI / 3 : (HalfDeterminant >= 1 ? 0 : FMath::Acos(HalfDeterminant) / 3);
		
		FReal m00 = static_cast<FReal>(Trace + 2 * Size * FMath::Cos(Angle));
		FReal m11 = static_cast<FReal>(Trace + 2 * Size * FMath::Cos(Angle + (2 * UE_PI / 3)));
		FReal m22 = static_cast<FReal>(3 * Trace - m00 - m11);

		// Extract Eigenvectors
		bool DoSwap = ((m00 - m11) > (m11 - m22)) ? false : true;
		FVec3 Eigenvector0 = (Inertia.SubtractDiagonal(DoSwap ? m22 : m00)).SymmetricCofactorMatrix().LargestColumnNormalized();
		FVec3 Orthogonal = Eigenvector0.GetOrthogonalVector().GetSafeNormal();
		
		PMatrix<FReal, 3, 2> Cofactors(Orthogonal, FVec3::CrossProduct(Eigenvector0, Orthogonal));
		PMatrix<FReal, 3, 2> CofactorsScaled = Inertia * Cofactors;
		
		PMatrix<FReal, 2, 2> IR(
			CofactorsScaled.M[0] * Cofactors.M[0] + CofactorsScaled.M[1] * Cofactors.M[1] + CofactorsScaled.M[2] * Cofactors.M[2],
			CofactorsScaled.M[3] * Cofactors.M[0] + CofactorsScaled.M[4] * Cofactors.M[1] + CofactorsScaled.M[5] * Cofactors.M[2],
			CofactorsScaled.M[3] * Cofactors.M[3] + CofactorsScaled.M[4] * Cofactors.M[4] + CofactorsScaled.M[5] * Cofactors.M[5]);

		PMatrix<FReal, 2, 2> IM1 = IR.SubtractDiagonal(DoSwap ? m00 : m22);
		FReal OffDiag = IM1.M[1] * IM1.M[1];
		FReal IM1Scale0 = FMath::Max(FReal(0), IM1.M[3] * IM1.M[3] + OffDiag);
		FReal IM1Scale1 = FMath::Max(FReal(0), IM1.M[0] * IM1.M[0] + OffDiag);
		FReal SqrtIM1Scale0 = FMath::Sqrt(IM1Scale0);
		FReal SqrtIM1Scale1 = FMath::Sqrt(IM1Scale1);

		FVec3 Eigenvector2, Eigenvector1;
		if ((SqrtIM1Scale0 < UE_KINDA_SMALL_NUMBER) && (SqrtIM1Scale1 < UE_KINDA_SMALL_NUMBER))
		{
			Eigenvector1 = Orthogonal;
			Eigenvector2 = FVec3::CrossProduct(Eigenvector0, Orthogonal).GetSafeNormal();
		}
		else
		{
			FVec2 SmallEigenvector2 = IM1Scale0 > IM1Scale1 
				? (FVec2(IM1.M[3], -IM1.M[1]) / SqrtIM1Scale0) 
				: (IM1Scale1 > 0 ? (FVec2(-IM1.M[1], IM1.M[0]) / SqrtIM1Scale1) : FVec2(1, 0));
			Eigenvector2 = (Cofactors * SmallEigenvector2).GetSafeNormal();
			Eigenvector1 = FVec3::CrossProduct(Eigenvector2, Eigenvector0).GetSafeNormal();
		}

		// Return results
		Inertia = FMatrix33(m00, 0, 0, m11, 0, m22);
		FMatrix33 RotationMatrix = DoSwap ? FMatrix33(Eigenvector2, Eigenvector1, -Eigenvector0) : FMatrix33(Eigenvector0, Eigenvector1, Eigenvector2);

		// NOTE: UE Matrix are column-major, so the PMatrix constructor is not setting eigenvectors - we need to transpose it to get a UE rotation matrix.
		FinalRotation = FRotation3(RotationMatrix.GetTransposed());
		if (!ensure(FMath::IsNearlyEqual((float)FinalRotation.Size(), 1.0f, UE_KINDA_SMALL_NUMBER)))
		{
			return FRotation3::FromIdentity();
		}
		
		return FinalRotation;
	}

	void TransformToLocalSpace(FMassProperties& MassProperties)
	{
		// Diagonalize inertia
		const FRotation3 InertiaRotation = TransformToLocalSpace(MassProperties.InertiaTensor);

		// Calculate net rotation
		MassProperties.RotationOfMass = MassProperties.RotationOfMass * InertiaRotation;
	}


	void CalculateVolumeAndCenterOfMass(const FBox& BoundingBox, FVector::FReal& OutVolume, FVector& OutCenterOfMass)
	{
		const FVector Extents = static_cast<FVector::FReal>(2) * BoundingBox.GetExtent(); // FBox::GetExtent() returns half the size, but FAABB::Extents() returns total size
		OutVolume = Extents.X * Extents.Y * Extents.Z;
		OutCenterOfMass = BoundingBox.GetCenter();
	}

	void CalculateInertiaAndRotationOfMass(const FBox& BoundingBox, const FVector::FReal Density, FMatrix33& OutInertiaTensor, FRotation3& OutRotationOfMass)
	{
		const FVector Extents = static_cast<FVector::FReal>(2) * BoundingBox.GetExtent(); // FBox::GetExtent() returns half the size, but FAABB::Extents() returns total size
		const FVector::FReal Volume = Extents.X * Extents.Y * Extents.Z;
		const FVector::FReal Mass = Volume * Density;
		const FVector::FReal ExtentsYZ = Extents.Y * Extents.Y + Extents.Z * Extents.Z;
		const FVector::FReal ExtentsXZ = Extents.X * Extents.X + Extents.Z * Extents.Z;
		const FVector::FReal ExtentsXY = Extents.X * Extents.X + Extents.Y * Extents.Y;
		OutInertiaTensor = FMatrix33((Mass * ExtentsYZ) / 12., (Mass * ExtentsXZ) / 12., (Mass * ExtentsXY) / 12.);
		OutRotationOfMass = FRotation3::Identity;
	}
	
   	FMassProperties CalculateMassProperties(const FBox& BoundingBox, const FVector::FReal Density)
	{
    	FMassProperties MassProperties;
    	CalculateVolumeAndCenterOfMass(BoundingBox, MassProperties.Volume, MassProperties.CenterOfMass);
    	check(Density > 0);
		MassProperties.Mass = MassProperties.Volume * Density;
		CalculateInertiaAndRotationOfMass(BoundingBox, Density, MassProperties.InertiaTensor, MassProperties.RotationOfMass);
   		return MassProperties;
   	}
	
	template<typename T, typename TSurfaces>
	void CHAOS_API CalculateVolumeAndCenterOfMass(const TParticles<T, 3>& Vertices, const TSurfaces& Surfaces, T& OutVolume, TVec3<T>& OutCenterOfMass)
	{
		CalculateVolumeAndCenterOfMass(Vertices.AllX(), Surfaces, OutVolume, OutCenterOfMass);
	}

	template<typename T, typename TSurfaces>
	void CalculateVolumeAndCenterOfMass(const TArray<TVec3<T>>& Vertices, const TSurfaces& Surfaces, T& OutVolume, TVec3<T>& OutCenterOfMass)
	{
		if (!Surfaces.Num())
		{
			OutVolume = 0;
			return;
		}
		
		T Volume = 0;
		TVec3<T> VolumeTimesSum(0);
		TVec3<T> Center = Vertices[Surfaces[0][0]];
		for (const auto& Element : Surfaces)
		{
			// For now we only support triangular elements
			ensure(Element.Num() == 3);

			PMatrix<T,3,3> DeltaMatrix;
			TVec3<T> PerElementSize;
			for (int32 i = 0; i < Element.Num(); ++i)
			{
				TVec3<T> DeltaVector = Vertices[Element[i]] - Center;
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
		if (Volume < UE_KINDA_SMALL_NUMBER)	//handle negative volume using fallback for now. Need to investigate cases where this happens
		{
			OutVolume = 0;
			return;
		}
		OutCenterOfMass = Center + VolumeTimesSum / (4 * Volume);
		OutVolume = Volume / 6;
	}

	template <typename TSurfaces>
	void CalculateInertiaAndRotationOfMass(const FParticles& Vertices, const TSurfaces& Surfaces, const FReal Density, const FVec3& CenterOfMass,
		FMatrix33& OutInertiaTensor, FRotation3& OutRotationOfMass)
	{
		check(Density > 0);

		static const FMatrix33 Standard(2, 1, 1, 2, 1, 2);
		FMatrix33 Covariance(0);
		for (const auto& Element : Surfaces)
		{
			FMatrix33 DeltaMatrix(0);
			for (int32 i = 0; i < Element.Num(); ++i)
			{
				FVec3 DeltaVector = Vertices.X(Element[i]) - CenterOfMass;
				DeltaMatrix.M[0][i] = DeltaVector[0];
				DeltaMatrix.M[1][i] = DeltaVector[1];
				DeltaMatrix.M[2][i] = DeltaVector[2];
			}
			FReal Det = DeltaMatrix.M[0][0] * (DeltaMatrix.M[1][1] * DeltaMatrix.M[2][2] - DeltaMatrix.M[1][2] * DeltaMatrix.M[2][1]) -
				DeltaMatrix.M[0][1] * (DeltaMatrix.M[1][0] * DeltaMatrix.M[2][2] - DeltaMatrix.M[1][2] * DeltaMatrix.M[2][0]) +
				DeltaMatrix.M[0][2] * (DeltaMatrix.M[1][0] * DeltaMatrix.M[2][1] - DeltaMatrix.M[1][1] * DeltaMatrix.M[2][0]);
			const FMatrix33 ScaledStandard = Standard * Det;
			Covariance += DeltaMatrix * ScaledStandard * DeltaMatrix.GetTransposed();
		}
		FReal Trace = Covariance.M[0][0] + Covariance.M[1][1] + Covariance.M[2][2];
		FMatrix33 TraceMat(Trace, Trace, Trace);
		OutInertiaTensor = (TraceMat - Covariance) * (1 / (FReal)120) * Density;
		OutRotationOfMass = TransformToLocalSpace(OutInertiaTensor);
	}

	template<typename TSurfaces>
	FMassProperties CalculateMassProperties(
		const FParticles & Vertices,
		const TSurfaces& Surfaces,
		const FReal Mass)
	{
		FMassProperties MassProperties;
		CalculateVolumeAndCenterOfMass(Vertices, Surfaces, MassProperties.Volume, MassProperties.CenterOfMass);

		check(Mass > 0);
		check(MassProperties.Volume > UE_SMALL_NUMBER);
		CalculateInertiaAndRotationOfMass(Vertices, Surfaces, Mass / MassProperties.Volume, MassProperties.CenterOfMass, MassProperties.InertiaTensor, MassProperties.RotationOfMass);
		
		return MassProperties;
	}

	FMassProperties Combine(const TArray<FMassProperties>& MPArray)
	{
		FMassProperties NewMP = CombineWorldSpace(MPArray);
		TransformToLocalSpace(NewMP);
		return NewMP;
	}

	FMassProperties CombineWorldSpace(const TArray<FMassProperties>& MPArray)
	{
		check(MPArray.Num() > 0);

		if ((MPArray.Num() == 1) && MPArray[0].RotationOfMass.IsIdentity())
		{
			return MPArray[0];
		}

		FMassProperties NewMP;

		for (const FMassProperties& Child : MPArray)
		{
			NewMP.Volume += Child.Volume;
			NewMP.InertiaTensor += Utilities::ComputeWorldSpaceInertia(Child.RotationOfMass, Child.InertiaTensor);
			NewMP.CenterOfMass += Child.CenterOfMass * Child.Mass;
			NewMP.Mass += Child.Mass;
		}

		// Default to 100cm cube of water for zero mass and volume objects
		if (!ensureMsgf(NewMP.Mass > UE_SMALL_NUMBER, TEXT("CombineWorldSpace: zero total mass detected")))
		{
			const FReal Dim = 100;	// cm
			const FReal Density = FReal(0.001); // kg/cm3
			NewMP.Volume = Dim * Dim * Dim;
			NewMP.Mass = NewMP.Volume * Density;
			NewMP.InertiaTensor = (NewMP.Mass * Dim * Dim / FReal(6)) * FMatrix33::Identity;
			NewMP.CenterOfMass = FVec3(0);
			return NewMP;
		}

		NewMP.CenterOfMass /= NewMP.Mass;

		if (MPArray.Num() > 1)
		{
			for (const FMassProperties& Child : MPArray)
			{
				const FReal M = Child.Mass;
				const FVec3 ParentToChild = Child.CenterOfMass - NewMP.CenterOfMass;
				const FReal P0 = ParentToChild[0];
				const FReal P1 = ParentToChild[1];
				const FReal P2 = ParentToChild[2];
				const FReal MP0P0 = M * P0 * P0;
				const FReal MP1P1 = M * P1 * P1;
				const FReal MP2P2 = M * P2 * P2;
				NewMP.InertiaTensor += FMatrix33(MP1P1 + MP2P2, -M * P1 * P0, -M * P2 * P0, MP2P2 + MP0P0, -M * P2 * P1, MP1P1 + MP0P0);
			}
		}

		return NewMP;
	}

	template CHAOS_API FMassProperties CalculateMassProperties(const FParticles& Vertices, const TArray<TVec3<int32>>& Surfaces, const FReal Mass);
	template CHAOS_API FMassProperties CalculateMassProperties(const FParticles & Vertices, const TArray<TArray<int32>>& Surfaces, const FReal Mass);

	template CHAOS_API void CalculateVolumeAndCenterOfMass(const TParticles<FRealDouble, 3>& Vertices, const TArray<TVec3<int32>>& Surfaces, FRealDouble& OutVolume, TVec3<FRealDouble>& OutCenterOfMass);
	template CHAOS_API void CalculateVolumeAndCenterOfMass(const TParticles<FRealDouble, 3>& Vertices, const TArray<TArray<int32>>& Surfaces, FRealDouble& OutVolume, TVec3<FRealDouble>& OutCenterOfMass);

	template CHAOS_API void CalculateVolumeAndCenterOfMass(const TParticles<FRealSingle, 3>& Vertices, const TArray<TVec3<int32>>& Surfaces, FRealSingle& OutVolume, TVec3<FRealSingle>& OutCenterOfMass);
	template CHAOS_API void CalculateVolumeAndCenterOfMass(const TParticles<FRealSingle, 3>& Vertices, const TArray<TArray<int32>>& Surfaces, FRealSingle& OutVolume, TVec3<FRealSingle>& OutCenterOfMass);
	
	template CHAOS_API void CalculateVolumeAndCenterOfMass(const TArray<TVec3<FRealSingle>>& Vertices, const TArray<TArray<int32>>& Surfaces, FRealSingle& OutVolume, TVec3<FRealSingle>& OutCenterOfMass);
	template CHAOS_API void CalculateVolumeAndCenterOfMass(const TArray<TVec3<FRealDouble>>& Vertices, const TArray<TArray<int32>>& Surfaces, FRealDouble& OutVolume, TVec3<FRealDouble>& OutCenterOfMass);


	template CHAOS_API void CalculateInertiaAndRotationOfMass(const FParticles& Vertices, const TArray<TVec3<int32>>& Surface, const FReal Density,
		const FVec3& CenterOfMass, FMatrix33& OutInertiaTensor, FRotation3& OutRotationOfMass);
	template CHAOS_API void CalculateInertiaAndRotationOfMass(const FParticles& Vertices, const TArray<TArray<int32>>& Surface, const FReal Density,
		const FVec3& CenterOfMass, FMatrix33& OutInertiaTensor, FRotation3& OutRotationOfMass);
}
