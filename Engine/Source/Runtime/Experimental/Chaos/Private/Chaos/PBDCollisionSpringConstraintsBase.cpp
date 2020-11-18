// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDCollisionSpringConstraintsBase.h"

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/Framework/Parallel.h"
#if PLATFORM_DESKTOP && PLATFORM_64BITS
#include "kDOP.h"
#endif

using namespace Chaos;

#if PLATFORM_DESKTOP && PLATFORM_64BITS
class FMeshBuildDataProvider
{
  public:
	/** Initialization constructor. */
	FMeshBuildDataProvider(
	    const TkDOPTree<const FMeshBuildDataProvider, uint32>& InkDopTree)
	    : kDopTree(InkDopTree)
	{
	}

	// kDOP data provider interface.

	FORCEINLINE const TkDOPTree<const FMeshBuildDataProvider, uint32>& GetkDOPTree(void) const
	{
		return kDopTree;
	}

	FORCEINLINE const FMatrix& GetLocalToWorld(void) const
	{
		return FMatrix::Identity;
	}

	FORCEINLINE const FMatrix& GetWorldToLocal(void) const
	{
		return FMatrix::Identity;
	}

	FORCEINLINE FMatrix GetLocalToWorldTransposeAdjoint(void) const
	{
		return FMatrix::Identity;
	}

	FORCEINLINE float GetDeterminant(void) const
	{
		return 1.0f;
	}

  private:
	const TkDOPTree<const FMeshBuildDataProvider, uint32>& kDopTree;
};
#endif

template<class T, int d>
TPBDCollisionSpringConstraintsBase<T, d>::TPBDCollisionSpringConstraintsBase(
	const int32 InOffset,
	const int32 InNumParticles,
	const TArray<TVector<int32, 3>>& InElements,
	TSet<TVector<int32, 2>>&& InDisabledCollisionElements,
	const T InThickness,
	const T InStiffness)
	: MElements(InElements)
	, MDisabledCollisionElements(InDisabledCollisionElements)
	, MOffset(InOffset)
	, MNumParticles(InNumParticles)
	, MThickness(InThickness)
	, MStiffness(InStiffness)
{
}

template<class T, int d>
void TPBDCollisionSpringConstraintsBase<T, d>::Init(const TPBDParticles<T, d>& Particles)
{
	if (!MElements.Num())
		return;
#if PLATFORM_DESKTOP && PLATFORM_64BITS
	MConstraints.Reset();
	MBarys.Reset();
	MNormals.Reset();

	const T Height = MThickness + MThickness;

	TkDOPTree<const FMeshBuildDataProvider, uint32> DopTree;
	TArray<FkDOPBuildCollisionTriangle<uint32>> BuildTriangleArray;
	BuildTriangleArray.Reserve(MElements.Num());
	for (int32 i = 0; i < MElements.Num(); ++i)
	{
		const auto& Elem = MElements[i];
		BuildTriangleArray.Add(FkDOPBuildCollisionTriangle<uint32>(i, Particles.X(Elem[0]), Particles.X(Elem[1]), Particles.X(Elem[2])));
	}
	DopTree.Build(BuildTriangleArray);
	FMeshBuildDataProvider DopDataProvider(DopTree);
	FCriticalSection CriticalSection;

	PhysicsParallelFor(MNumParticles,
		[this, &Particles, &CriticalSection, Height, &DopDataProvider, &DopTree](int32 i)
		{
			const int32 Index = i + MOffset;

			FkHitResult Result;
			const TVector<T, d>& Start = Particles.X(Index);
			const TVector<T, d> Direction = Particles.V(Index).GetSafeNormal();
			const TVector<T, d> End = Particles.P(Index) + Direction * Height;
			FVector4 Start4(Start.X, Start.Y, Start.Z, 0);
			FVector4 End4(End.X, End.Y, End.Z, 0);
			TkDOPLineCollisionCheck<const FMeshBuildDataProvider, uint32> Ray(Start4, End4, true, DopDataProvider, &Result);
			if (DopTree.LineCheck(Ray))
			{
				const auto& Elem = MElements[Result.Item];
				if (MDisabledCollisionElements.Contains({Index, Elem[0]}) || MDisabledCollisionElements.Contains({Index, Elem[1]}) || MDisabledCollisionElements.Contains({Index, Elem[2]}))
					return;
				TVector<T, 3> Bary;
				TVector<T, d> P10 = Particles.X(Elem[1]) - Particles.X(Elem[0]);
				TVector<T, d> P20 = Particles.X(Elem[2]) - Particles.X(Elem[0]);
				TVector<T, d> PP0 = Particles.X(Index) - Particles.X(Elem[0]);
				T Size10 = P10.SizeSquared();
				T Size20 = P20.SizeSquared();
				if (Size10 < SMALL_NUMBER)
				{
					if (Size20 < SMALL_NUMBER)
					{
						Bary.Y = Bary.Z = 0.f;
						Bary.X = 1.f;
					}
					else
					{
						const T Size = PP0.SizeSquared();
						if (Size < SMALL_NUMBER)
						{
							Bary.Y = Bary.Z = 0.f;
							Bary.X = 1.f;
						}
						else
						{
							const T ProjP2 = TVector<T, d>::DotProduct(PP0, P20);
							Bary.Y = 0.f;
							Bary.Z = ProjP2 * FMath::InvSqrt(Size20 * Size);
							Bary.X = 1.0f - Bary.Z;
						}
					}
				}
				else if (Size20 < SMALL_NUMBER)
				{
					const T Size = PP0.SizeSquared();
					if (Size < SMALL_NUMBER)
					{
						Bary.Y = Bary.Z = 0.f;
						Bary.X = 1.f;
					}
					else
					{
						const T ProjP1 = TVector<T, d>::DotProduct(PP0, P10);
						Bary.Y = ProjP1 * FMath::InvSqrt(Size10 * PP0.SizeSquared());
						Bary.Z = 0.f;
						Bary.X = 1.0f - Bary.Y;
					}
				}
				else
				{
					const T ProjSides = TVector<T, d>::DotProduct(P10, P20);
					const T ProjP1 = TVector<T, d>::DotProduct(PP0, P10);
					const T ProjP2 = TVector<T, d>::DotProduct(PP0, P20);
					const T Denom = Size10 * Size20 - ProjSides * ProjSides;
					Bary.Y = (Size20 * ProjP1 - ProjSides * ProjP2) / Denom;
					Bary.Z = (Size10 * ProjP2 - ProjSides * ProjP1) / Denom;
					Bary.X = 1.0f - Bary.Z - Bary.Y;
				}

				TVector<T, d> Normal = TVector<T, d>::CrossProduct(P10, P20).GetSafeNormal();
				Normal = TVector<T, d>::DotProduct(Normal, PP0) > 0 ? Normal : -Normal;
				CriticalSection.Lock();
				MConstraints.Add({Index, Elem[0], Elem[1], Elem[2]});
				MBarys.Add(Bary);
				MNormals.Add(Normal);
				CriticalSection.Unlock();
			}
		});
#endif
}

template<class T, int d>
TVector<T, d> TPBDCollisionSpringConstraintsBase<T, d>::GetDelta(const TPBDParticles<T, d>& InParticles, const int32 i) const
{
	const auto& Constraint = MConstraints[i];
	int32 i1 = Constraint[0];
	int32 i2 = Constraint[1];
	int32 i3 = Constraint[2];
	int32 i4 = Constraint[3];
	const T CombinedMass = InParticles.InvM(i3) * MBarys[i][1] + InParticles.InvM(i2) * MBarys[i][0] + InParticles.InvM(i4) * MBarys[i][2] + InParticles.InvM(i1);
	if (CombinedMass <= (T)1e-7)
		return TVector<T, d>(0);
	const TVector<T, d>& P1 = InParticles.P(i1);
	const TVector<T, d>& P2 = InParticles.P(i2);
	const TVector<T, d>& P3 = InParticles.P(i3);
	const TVector<T, d>& P4 = InParticles.P(i4);
	const T Height = MThickness + MThickness;
	const TVector<T, d> P = MBarys[i][0] * P2 + MBarys[i][1] * P3 + MBarys[i][2] * P4 + Height * MNormals[i];
	TVector<T, d> Difference = P1 - P;
	if (TVector<T, d>::DotProduct(Difference, MNormals[i]) > 0)
		return TVector<T, d>(0);
	float Distance = Difference.Size();
	TVector<T, d> Delta = Distance * MNormals[i];
	return MStiffness * Delta / CombinedMass;
}

template class CHAOS_API Chaos::TPBDCollisionSpringConstraintsBase<float, 3>;
#endif
