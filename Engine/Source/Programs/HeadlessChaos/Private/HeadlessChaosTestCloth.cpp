// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestCloth.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"

#include "Chaos/PBDEvolution.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/PBDAxialSpringConstraints.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/Utilities.h"

#include "Modules/ModuleManager.h"

namespace ChaosTest {

	using namespace Chaos;
	DEFINE_LOG_CATEGORY_STATIC(LogChaosTestCloth, Verbose, All);

	using FPBDEvolution = FPBDEvolution;

	TUniquePtr<FPBDEvolution> InitPBDEvolution(
		const int32 NumIterations=1,
		const FReal CollisionThickness=KINDA_SMALL_NUMBER,
		const FReal SelfCollisionThickness=KINDA_SMALL_NUMBER,
		const FReal Friction=0.0,
		const FReal Damping=0.04)
	{
		Chaos::FPBDParticles Particles;
		Chaos::FKinematicGeometryClothParticles RigidParticles;
		TUniquePtr<FPBDEvolution> Evolution(
			new FPBDEvolution(
				MoveTemp(Particles),
				MoveTemp(RigidParticles),
				{},
				NumIterations,
				CollisionThickness,
				SelfCollisionThickness,
				Friction,
				Damping));
		return Evolution;
	}

	template <class TEvolutionPtr>
	void InitSingleParticle(
		TEvolutionPtr& Evolution,
		const FVec3& Position = FVec3(0),
		const FVec3& Velocity = FVec3(0),
		const FReal Mass = 1.0)
	{
		auto& Particles = Evolution->Particles();
		const uint32 Idx = Particles.Size();
		Particles.AddParticles(1);
		Particles.X(Idx) = Position;
		Particles.V(Idx) = Velocity;
		Particles.M(Idx) = Mass;
		Particles.InvM(Idx) = 1.0 / Mass;
	}

	template <class TEvolutionPtr>
	void InitTriMesh_EquilateralTri(
		FTriangleMesh& TriMesh,
		TEvolutionPtr& Evolution, 
		const FVec3& XOffset=FVec3(0))
	{
		auto& Particles = Evolution->Particles();
		const uint32 InitialNumParticles = Particles.Size();
		FTriangleMesh::InitEquilateralTriangleYZ(TriMesh, Particles);

		// Initialize particles.  Use 1/3 area of connected triangles for particle mass.
		for (uint32 i = InitialNumParticles; i < Particles.Size(); i++)
		{
			Particles.X(i) += XOffset;
			Particles.V(i) = Chaos::FVec3(0);
			Particles.M(i) = 0;
		}
		for (const Chaos::TVec3<int32>& Tri : TriMesh.GetElements())
		{
			const FReal TriArea = 0.5 * Chaos::FVec3::CrossProduct(
				Particles.X(Tri[1]) - Particles.X(Tri[0]),
				Particles.X(Tri[2]) - Particles.X(Tri[0])).Size();
			for (int32 i = 0; i < 3; i++)
			{
				Particles.M(Tri[i]) += TriArea / 3;
			}
		}
		for (uint32 i = InitialNumParticles; i < Particles.Size(); i++)
		{
			check(Particles.M(i) > SMALL_NUMBER);
			Particles.InvM(i) = 1.0 / Particles.M(i);
		}
	}

	template <class TEvolutionPtr>
	void AddEdgeLengthConstraint(
		TEvolutionPtr& Evolution,
		const TArray<Chaos::TVec3<int32>>& Topology,
		const FReal Stiffness)
	{
		check(Stiffness >= 0. && Stiffness <= 1.);
		// TODO: Use Add AddConstraintRuleRange
		//Evolution->AddPBDConstraintFunction(
		//	[SpringConstraints = Chaos::FPBDSpringConstraints(
		//		Evolution->Particles(), Topology, Stiffness)](
		//			TPBDParticles<float, 3>& InParticles, const float Dt) 
		//	{
		//		SpringConstraints.Apply(InParticles, Dt);
		//	});
	}
	
	template <class TEvolutionPtr>
	void AddAxialConstraint(
		TEvolutionPtr& Evolution,
		TArray<Chaos::TVec3<int32>>&& Topology,
		const FReal Stiffness)
	{
		check(Stiffness >= 0. && Stiffness <= 1.);
		// TODO: Use Add AddConstraintRuleRange
		//Evolution->AddPBDConstraintFunction(
		//	[SpringConstraints = Chaos::FPBDAxialSpringConstraints(
		//		Evolution->Particles(), MoveTemp(Topology), Stiffness)](
		//			TPBDParticles<float, 3>& InParticles, const float Dt) 
		//	{
		//		SpringConstraints.Apply(InParticles, Dt);
		//	});
	}

	template <class TEvolutionPtr>
	void AdvanceTime(
		TEvolutionPtr& Evolution,
		const uint32 NumFrames=1,
		const uint32 NumTimeStepsPerFrame=1,
		const uint32 FPS=24)
	{
		check(NumTimeStepsPerFrame > 0);
		Evolution->SetIterations(NumTimeStepsPerFrame);

		check(FPS > 0);
		const FReal Dt = 1.0 / FPS;
		for (uint32 i = 0; i < NumFrames; i++)
		{
			Evolution->AdvanceOneTimeStep(Dt);
		}
	}

	template <class TParticleContainer>
	TArray<Chaos::FVec3> CopyPoints(const TParticleContainer& Particles)
	{
		TArray<Chaos::FVec3> Points;
		Points.SetNum(Particles.Size());

		for (uint32 i = 0; i < Particles.Size(); i++)
			Points[i] = Particles.X(i);

		return Points;
	}

	template <class TParticleContainer>
	void Reset(TParticleContainer& Particles, const TArray<Chaos::FVec3>& Points)
	{
		for (uint32 i = 0; i < Particles.Size(); i++)
		{
			Particles.X(i) = Points[i];
			Particles.V(i) = Chaos::FVec3(0);
		}
	}

	TArray<FVec3> GetDifference(const TArray<FVec3>& A, const TArray<FVec3>& B)
	{
		TArray<FVec3> C;
		C.SetNum(A.Num());
		for (int32 i = 0; i < A.Num(); i++)
			C[i] = A[i] - B[i];
		return C;
	}

	TArray<FReal> GetMagnitude(const TArray<Chaos::FVec3>& V)
	{
		TArray<FReal> M;
		M.SetNum(V.Num());
		for (int32 i = 0; i < V.Num(); i++)
			M[i] = V[i].Size();
		return M;
	}

	template <class T>
	bool AllSame(const TArray<T>& V, int32& Idx, const T Tolerance=KINDA_SMALL_NUMBER)
	{
		if (!V.Num())
			return true;
		const T& V0 = V[0];
		for (int32 i = 1; i < V.Num(); i++)
		{
			if (!FMath::IsNearlyEqual(V0, V[i], Tolerance))
			{
				Idx = i;
				return false;
			}
		}
		return true;
	}

	template <class TV>
	bool RunDropTest(
		TUniquePtr<FPBDEvolution>& Evolution,
		const FReal GravMag,
		const TV& GravDir,
		const TArray<TV>& InitialPoints,
		const int32 SubFrameSteps,
		const FReal DistTolerance,
		const char* TestID)
	{
		const FReal PreTime = Evolution->GetTime();
		AdvanceTime(Evolution, 24, SubFrameSteps); // 1 second
		const FReal PostTime = Evolution->GetTime();
		EXPECT_NEAR(PostTime-PreTime, 1.0, KINDA_SMALL_NUMBER)
			<< TestID
			<< "Evolution advanced time by " << (PostTime - PreTime)
			<< " seconds, expected 1.0 seconds.";

		const TArray<FVec3> PostPoints = CopyPoints(Evolution->Particles());
		const TArray<FVec3> Diff = GetDifference(PostPoints, InitialPoints);
		const TArray<FReal> ScalarDiff = GetMagnitude(Diff);

		// All points did the same thing
		int32 Idx = 0;
		EXPECT_TRUE(AllSame(ScalarDiff, Idx, (FReal)0.1))
			<< TestID
			<< "Points fell different distances - Index 0: "
			<< ScalarDiff[0] << " != Index " 
			<< Idx << ": " 
			<< ScalarDiff[Idx] << " +/- 0.1.";

		// Fell the right amount
		EXPECT_NEAR(ScalarDiff[0], (FReal)0.5 * GravMag, DistTolerance)
			<< TestID
			<< "Points fell by " << ScalarDiff[0] 
			<< ", expected " << ((FReal)0.5 * GravMag)
			<< " +/- " << DistTolerance << "."; 

		// Fell the right direction
		const FReal DirDot = Chaos::FVec3::DotProduct(GravDir, Diff[0].GetSafeNormal());
		EXPECT_NEAR(DirDot, (FReal)1.0, (FReal)KINDA_SMALL_NUMBER)
			<< TestID
			<< "Points fell in different directions.";

		return true;
	}

	void DeformableGravity()
	{
		const FReal DistTol = 0.0002;

		//
		// Initialize solver and gravity
		//

		TUniquePtr<FPBDEvolution> Evolution = InitPBDEvolution();

		const FVec3 GravDir(0, 0, -1);
		const FReal GravMag = 980.665;

		//
		// Drop a single particle
		//

		InitSingleParticle(Evolution);
		TArray<FVec3> InitialPoints = CopyPoints(Evolution->Particles());

		RunDropTest(Evolution, GravMag, GravDir, InitialPoints, 1, DistTol, "Single point falling under gravity, iters: 1 - ");
		Reset(Evolution->Particles(), InitialPoints);
		RunDropTest(Evolution, GravMag, GravDir, InitialPoints, 100, DistTol, "Single point falling under gravity, iters: 100 - ");
		Reset(Evolution->Particles(), InitialPoints);

		//
		// Add a triangle mesh
		//

		Chaos::FTriangleMesh TriMesh;
		InitTriMesh_EquilateralTri(TriMesh, Evolution);
		InitialPoints = CopyPoints(Evolution->Particles());

		// 
		// Points falling under gravity
		//

		RunDropTest(Evolution, GravMag, GravDir, InitialPoints, 1, DistTol, "Points falling under gravity, iters: 1 - ");
		Reset(Evolution->Particles(), InitialPoints);
		RunDropTest(Evolution, GravMag, GravDir, InitialPoints, 100, DistTol, "Points falling under gravity, iters: 100 - ");
		Reset(Evolution->Particles(), InitialPoints);

		// 
		// Points falling under gravity with edge length constraint
		//

		AddEdgeLengthConstraint(Evolution, TriMesh.GetSurfaceElements(), 1.0);

		RunDropTest(Evolution, GravMag, GravDir, InitialPoints, 1, DistTol, "Points falling under gravity & edge cnstr, iters: 1 - ");
		Reset(Evolution->Particles(), InitialPoints);
		RunDropTest(Evolution, GravMag, GravDir, InitialPoints, 100, DistTol, "Points falling under gravity & edge cnstr, iters: 100 - ");
		Reset(Evolution->Particles(), InitialPoints);
	}

	void EdgeConstraints()
	{
		TUniquePtr<FPBDEvolution> Evolution = InitPBDEvolution();
		FTriangleMesh TriMesh;
		auto& Particles = Evolution->Particles();
		Particles.AddParticles(2145);
		TArray<TVec3<int32>> Triangles;
		Triangles.SetNum(2048);
		// 32 n, 32 m
		// 6 + 4*(n-1) + (m - 1)(3 + 2*(n-1)) = 2*n*m
		for (int32 TriIndex = 0; TriIndex < 2048; TriIndex++)
		{
			int32 i = ((float)rand()) / ((float)RAND_MAX) * 2144;
			int32 j = ((float)rand()) / ((float)RAND_MAX) * 2144;
			int32 k = ((float)rand()) / ((float)RAND_MAX) * 2144;
			Triangles[TriIndex] = TVec3<int32>(i, j, k);
		}
		AddEdgeLengthConstraint(Evolution, Triangles, 1.0);
		AddAxialConstraint(Evolution, MoveTemp(Triangles), 1.0);
	}

} // namespace ChaosTest