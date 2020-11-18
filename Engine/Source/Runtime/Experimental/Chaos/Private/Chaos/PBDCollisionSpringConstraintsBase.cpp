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
	FMeshBuildDataProvider(const TkDOPTree<const FMeshBuildDataProvider, uint32>& InkDopTree)
	    : kDopTree(InkDopTree)
	{}

	// kDOP data provider interface.

	FORCEINLINE const TkDOPTree<const FMeshBuildDataProvider, uint32>& GetkDOPTree() const
	{
		return kDopTree;
	}

	FORCEINLINE const FMatrix& GetLocalToWorld() const
	{
		return FMatrix::Identity;
	}

	FORCEINLINE const FMatrix& GetWorldToLocal() const
	{
		return FMatrix::Identity;
	}

	FORCEINLINE FMatrix GetLocalToWorldTransposeAdjoint() const
	{
		return FMatrix::Identity;
	}

	FORCEINLINE float GetDeterminant() const
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
	: Elements(InElements)
	, DisabledCollisionElements(InDisabledCollisionElements)
	, Offset(InOffset)
	, NumParticles(InNumParticles)
	, Thickness(InThickness)
	, Stiffness(InStiffness)
{
}

template<class T, int d>
void TPBDCollisionSpringConstraintsBase<T, d>::Init(const TPBDParticles<T, d>& Particles)
{
#if PLATFORM_DESKTOP && PLATFORM_64BITS
	if (!Elements.Num())
	{
		return;
	}

	Constraints.Reset();
	Barys.Reset();
	Normals.Reset();

	TkDOPTree<const FMeshBuildDataProvider, uint32> DopTree;
	TArray<FkDOPBuildCollisionTriangle<uint32>> BuildTriangleArray;
	BuildTriangleArray.Reserve(Elements.Num());
	for (int32 i = 0; i < Elements.Num(); ++i)
	{
		const TVector<int32, 3>& Elem = Elements[i];
		const TVector<T, d>& P0 = Particles.X(Elem[0]);
		const TVector<T, d>& P1 = Particles.X(Elem[1]);
		const TVector<T, d>& P2 = Particles.X(Elem[2]);

		BuildTriangleArray.Add(FkDOPBuildCollisionTriangle<uint32>(i, P0, P1, P2));
	}
	DopTree.Build(BuildTriangleArray);

	FMeshBuildDataProvider DopDataProvider(DopTree);
	FCriticalSection CriticalSection;

	const T Height = Thickness + Thickness;

	PhysicsParallelFor(NumParticles,
		[this, &Particles, &CriticalSection, Height, &DopDataProvider, &DopTree](int32 i)
		{
			const int32 Index = i + Offset;

			FkHitResult Result;
			const TVector<T, d>& Start = Particles.X(Index);
			const TVector<T, d> Direction = Particles.V(Index).GetSafeNormal();
			const TVector<T, d> End = Particles.P(Index) + Direction * Height;
			const FVector4 Start4(Start.X, Start.Y, Start.Z, 0);
			const FVector4 End4(End.X, End.Y, End.Z, 0);
			TkDOPLineCollisionCheck<const FMeshBuildDataProvider, uint32> Ray(Start4, End4, true, DopDataProvider, &Result);
			if (DopTree.LineCheck(Ray))
			{
				const TVector<int32, 3>& Elem = Elements[Result.Item];
				if (DisabledCollisionElements.Contains({Index, Elem[0]}) ||
					DisabledCollisionElements.Contains({Index, Elem[1]}) ||
					DisabledCollisionElements.Contains({Index, Elem[2]}))
				{
					return;
				}

				const TVector<T, d>& P = Particles.X(Index);
				const TVector<T, d>& P0 = Particles.X(Elem[0]);
				const TVector<T, d>& P1 = Particles.X(Elem[1]);
				const TVector<T, d>& P2 = Particles.X(Elem[2]);
				const TVector<T, d> P10 = P1 - P0;
				const TVector<T, d> P20 = P2 - P0;
				const TVector<T, d> PP0 = P - P0;
				const T Size10 = P10.SizeSquared();
				const T Size20 = P20.SizeSquared();
				TVector<T, 3> Bary;
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
				Normal = (TVector<T, d>::DotProduct(Normal, PP0) > 0) ? Normal : -Normal;

				CriticalSection.Lock();
				Constraints.Add({Index, Elem[0], Elem[1], Elem[2]});
				Barys.Add(Bary);
				Normals.Add(Normal);
				CriticalSection.Unlock();
			}
		});
#endif  // #if PLATFORM_DESKTOP && PLATFORM_64BITS
}

template<class T, int d>
TVector<T, d> TPBDCollisionSpringConstraintsBase<T, d>::GetDelta(const TPBDParticles<T, d>& Particles, const int32 i) const
{
	const TVector<int32, 4>& Constraint = Constraints[i];
	const int32 i1 = Constraint[0];
	const int32 i2 = Constraint[1];
	const int32 i3 = Constraint[2];
	const int32 i4 = Constraint[3];

	const T CombinedMass = Particles.InvM(i1) +
		Particles.InvM(i2) * Barys[i][0] +
		Particles.InvM(i3) * Barys[i][1] +
		Particles.InvM(i4) * Barys[i][2];
	if (CombinedMass <= (T)1e-7)
	{
		return TVector<T, d>(0);
	}

	const TVector<T, d>& P1 = Particles.P(i1);
	const TVector<T, d>& P2 = Particles.P(i2);
	const TVector<T, d>& P3 = Particles.P(i3);
	const TVector<T, d>& P4 = Particles.P(i4);

	const T Height = Thickness + Thickness;
	const TVector<T, d> P = Barys[i][0] * P2 + Barys[i][1] * P3 + Barys[i][2] * P4 + Height * Normals[i];

	const TVector<T, d> Difference = P1 - P;
	if (TVector<T, d>::DotProduct(Difference, Normals[i]) > 0)
	{
		return TVector<T, d>(0);
	}
	const float Distance = Difference.Size();
	const TVector<T, d> Delta = Distance * Normals[i];
	return Stiffness * Delta / CombinedMass;
}

template class CHAOS_API Chaos::TPBDCollisionSpringConstraintsBase<float, 3>;
#endif
