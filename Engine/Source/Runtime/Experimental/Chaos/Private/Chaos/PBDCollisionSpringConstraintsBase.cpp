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

	FORCEINLINE FReal GetDeterminant() const
	{
		return 1.0f;
	}

private:
	const TkDOPTree<const FMeshBuildDataProvider, uint32>& kDopTree;
};
#endif

FPBDCollisionSpringConstraintsBase::FPBDCollisionSpringConstraintsBase(
	const int32 InOffset,
	const int32 InNumParticles,
	const TArray<TVec3<int32>>& InElements,
	TSet<TVec2<int32>>&& InDisabledCollisionElements,
	const FReal InThickness,
	const FReal InStiffness)
	: Elements(InElements)
	, DisabledCollisionElements(InDisabledCollisionElements)
	, Offset(InOffset)
	, NumParticles(InNumParticles)
	, Thickness(InThickness)
	, Stiffness(InStiffness)
{
}

void FPBDCollisionSpringConstraintsBase::Init(const FPBDParticles& Particles)
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
		const FVec3& P0 = Particles.X(Elem[0]);
		const FVec3& P1 = Particles.X(Elem[1]);
		const FVec3& P2 = Particles.X(Elem[2]);

		BuildTriangleArray.Add(FkDOPBuildCollisionTriangle<uint32>(i, P0, P1, P2));
	}
	DopTree.Build(BuildTriangleArray);

	FMeshBuildDataProvider DopDataProvider(DopTree);
	FCriticalSection CriticalSection;

	const FReal Height = Thickness + Thickness;

	PhysicsParallelFor(NumParticles,
		[this, &Particles, &CriticalSection, Height, &DopDataProvider, &DopTree](int32 i)
		{
			const int32 Index = i + Offset;

			FkHitResult Result;
			const FVec3& Start = Particles.X(Index);
			const FVec3 Direction = Particles.V(Index).GetSafeNormal();
			const FVec3 End = Particles.P(Index) + Direction * Height;
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

				const FVec3& P = Particles.X(Index);
				const FVec3& P0 = Particles.X(Elem[0]);
				const FVec3& P1 = Particles.X(Elem[1]);
				const FVec3& P2 = Particles.X(Elem[2]);
				const FVec3 P10 = P1 - P0;
				const FVec3 P20 = P2 - P0;
				const FVec3 PP0 = P - P0;
				const FReal Size10 = P10.SizeSquared();
				const FReal Size20 = P20.SizeSquared();
				FVec3 Bary;
				if (Size10 < SMALL_NUMBER)
				{
					if (Size20 < SMALL_NUMBER)
					{
						Bary.Y = Bary.Z = 0.f;
						Bary.X = 1.f;
					}
					else
					{
						const FReal Size = PP0.SizeSquared();
						if (Size < SMALL_NUMBER)
						{
							Bary.Y = Bary.Z = 0.f;
							Bary.X = 1.f;
						}
						else
						{
							const FReal ProjP2 = FVec3::DotProduct(PP0, P20);
							Bary.Y = 0.f;
							Bary.Z = ProjP2 * FMath::InvSqrt(Size20 * Size);
							Bary.X = 1.0f - Bary.Z;
						}
					}
				}
				else if (Size20 < SMALL_NUMBER)
				{
					const FReal Size = PP0.SizeSquared();
					if (Size < SMALL_NUMBER)
					{
						Bary.Y = Bary.Z = 0.f;
						Bary.X = 1.f;
					}
					else
					{
						const FReal ProjP1 = FVec3::DotProduct(PP0, P10);
						Bary.Y = ProjP1 * FMath::InvSqrt(Size10 * PP0.SizeSquared());
						Bary.Z = 0.f;
						Bary.X = 1.0f - Bary.Y;
					}
				}
				else
				{
					const FReal ProjSides = FVec3::DotProduct(P10, P20);
					const FReal ProjP1 = FVec3::DotProduct(PP0, P10);
					const FReal ProjP2 = FVec3::DotProduct(PP0, P20);
					const FReal Denom = Size10 * Size20 - ProjSides * ProjSides;
					Bary.Y = (Size20 * ProjP1 - ProjSides * ProjP2) / Denom;
					Bary.Z = (Size10 * ProjP2 - ProjSides * ProjP1) / Denom;
					Bary.X = 1.0f - Bary.Z - Bary.Y;
				}

				FVec3 Normal = FVec3::CrossProduct(P10, P20).GetSafeNormal();
				Normal = (FVec3::DotProduct(Normal, PP0) > 0) ? Normal : -Normal;

				CriticalSection.Lock();
				Constraints.Add({Index, Elem[0], Elem[1], Elem[2]});
				Barys.Add(Bary);
				Normals.Add(Normal);
				CriticalSection.Unlock();
			}
		});
#endif  // #if PLATFORM_DESKTOP && PLATFORM_64BITS
}

FVec3 FPBDCollisionSpringConstraintsBase::GetDelta(const FPBDParticles& Particles, const int32 i) const
{
	const TVec4<int32>& Constraint = Constraints[i];
	const int32 i1 = Constraint[0];
	const int32 i2 = Constraint[1];
	const int32 i3 = Constraint[2];
	const int32 i4 = Constraint[3];

	const FReal CombinedMass = Particles.InvM(i1) +
		Particles.InvM(i2) * Barys[i][0] +
		Particles.InvM(i3) * Barys[i][1] +
		Particles.InvM(i4) * Barys[i][2];
	if (CombinedMass <= (FReal)1e-7)
	{
		return FVec3(0);
	}

	const FVec3& P1 = Particles.P(i1);
	const FVec3& P2 = Particles.P(i2);
	const FVec3& P3 = Particles.P(i3);
	const FVec3& P4 = Particles.P(i4);

	const FReal Height = Thickness + Thickness;
	const FVec3 P = Barys[i][0] * P2 + Barys[i][1] * P3 + Barys[i][2] * P4 + Height * Normals[i];

	const FVec3 Difference = P1 - P;
	if (FVec3::DotProduct(Difference, Normals[i]) > 0)
	{
		return FVec3(0);
	}
	const FReal Distance = Difference.Size();
	const FVec3 Delta = Distance * Normals[i];
	return Stiffness * Delta / CombinedMass;
}

#endif
