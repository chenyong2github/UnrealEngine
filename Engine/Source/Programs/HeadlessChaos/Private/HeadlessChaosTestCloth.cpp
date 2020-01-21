// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestCloth.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"

#include "Chaos/PBDEvolution.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/Utilities.h"

#include "Modules/ModuleManager.h"

namespace ChaosTest {

	using namespace Chaos;
	DEFINE_LOG_CATEGORY_STATIC(LogChaosTestCloth, Verbose, All);

	template <class T>
	TUniquePtr<TPBDEvolution<T, 3>> InitPBDEvolution(
		const int32 NumIterations=1,
		const T CollisionThickness=KINDA_SMALL_NUMBER,
		const T SelfCollisionThickness=KINDA_SMALL_NUMBER,
		const T Friction=0.0,
		const T Damping=0.04)
	{
		Chaos::TPBDParticles<T, 3> Particles;
		Chaos::TKinematicGeometryClothParticles<T, 3> RigidParticles;
		TUniquePtr<Chaos::TPBDEvolution<T, 3>> Evolution(
			new Chaos::TPBDEvolution<T, 3>(
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

	template <class T, class TEvolutionPtr>
	void InitSingleParticle(
		TEvolutionPtr& Evolution,
		const TVector<T, 3>& Position = TVector<T, 3>(0),
		const TVector<T, 3>& Velocity = TVector<T, 3>(0),
		const T Mass = 1.0)
	{
		auto& Particles = Evolution->Particles();
		const uint32 Idx = Particles.Size();
		Particles.AddParticles(1);
		Particles.X(Idx) = Position;
		Particles.V(Idx) = Velocity;
		Particles.M(Idx) = Mass;
		Particles.InvM(Idx) = 1.0 / Mass;
	}

	template <class T, class TEvolutionPtr>
	void InitTriMesh_EquilateralTri(
		Chaos::TTriangleMesh<T>& TriMesh, 
		TEvolutionPtr& Evolution, 
		const TVector<T,3>& XOffset=TVector<T,3>(0))
	{
		auto& Particles = Evolution->Particles();
		const uint32 InitialNumParticles = Particles.Size();
		Chaos::TTriangleMesh<T>::InitEquilateralTriangleYZ(TriMesh, Particles);

		// Initialize particles.  Use 1/3 area of connected triangles for particle mass.
		for (uint32 i = InitialNumParticles; i < Particles.Size(); i++)
		{
			Particles.X(i) += XOffset;
			Particles.V(i) = Chaos::TVector<T, 3>(0);
			Particles.M(i) = 0;
		}
		for (const Chaos::TVector<int32, 3>& Tri : TriMesh.GetElements())
		{
			const T TriArea = 0.5 * Chaos::TVector<T, 3>::CrossProduct(
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

	template <class T, int d, class TEvolutionPtr>
	void AddEdgeLengthConstraint(
		TEvolutionPtr& Evolution,
		const TArray<Chaos::TVector<int32, d>>& Topology,
		const T Stiffness)
	{
		check(Stiffness >= 0. && Stiffness <= 1.);
		Evolution->AddPBDConstraintFunction(
			[SpringConstraints = Chaos::TPBDSpringConstraints<float, 3>(
				Evolution->Particles(), Topology, Stiffness)](
					TPBDParticles<float, 3>& InParticles, const float Dt) 
			{
				SpringConstraints.Apply(InParticles, Dt);
			});
	}

	template <class T, class TEvolutionPtr>
	void AdvanceTime(
		TEvolutionPtr& Evolution,
		const uint32 NumFrames=1,
		const uint32 NumTimeStepsPerFrame=1,
		const uint32 FPS=24)
	{
		check(NumTimeStepsPerFrame > 0);
		Evolution->SetIterations(NumTimeStepsPerFrame);

		check(FPS > 0);
		const T Dt = 1.0 / FPS;
		for (uint32 i = 0; i < NumFrames; i++)
		{
			Evolution->AdvanceOneTimeStep(Dt);
		}
	}

	template <class T, class TParticleContainer>
	TArray<Chaos::TVector<T, 3>> CopyPoints(const TParticleContainer& Particles)
	{
		TArray<Chaos::TVector<T, 3>> Points;
		Points.SetNum(Particles.Size());

		for (uint32 i = 0; i < Particles.Size(); i++)
			Points[i] = Particles.X(i);

		return Points;
	}

	template <class T, class TParticleContainer>
	void Reset(TParticleContainer& Particles, const TArray<Chaos::TVector<T, 3>>& Points)
	{
		for (uint32 i = 0; i < Particles.Size(); i++)
		{
			Particles.X(i) = Points[i];
			Particles.V(i) = Chaos::TVector<T, 3>(0);
		}
	}

	template <class T>
	TArray<T> GetDifference(const TArray<T>& A, const TArray<T>& B)
	{
		TArray<T> C;
		C.SetNum(A.Num());
		for (int32 i = 0; i < A.Num(); i++)
			C[i] = A[i] - B[i];
		return C;
	}

	template <class T>
	TArray<T> GetMagnitude(const TArray<Chaos::TVector<T, 3>>& V)
	{
		TArray<T> M;
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

	template <class T, class TV>
	bool RunDropTest(
		TUniquePtr<TPBDEvolution<T, 3>>& Evolution,
		const T GravMag,
		const TV& GravDir,
		const TArray<TV>& InitialPoints,
		const int32 SubFrameSteps,
		const T DistTolerance,
		const char* TestID)
	{
		const T PreTime = Evolution->GetTime();
		AdvanceTime<T>(Evolution, 24, SubFrameSteps); // 1 second
		const T PostTime = Evolution->GetTime();
		EXPECT_NEAR(PostTime-PreTime, 1.0, KINDA_SMALL_NUMBER)
			<< TestID
			<< "Evolution advanced time by " << (PostTime - PreTime)
			<< " seconds, expected 1.0 seconds.";

		const TArray<Chaos::TVector<T, 3>> PostPoints = CopyPoints<T>(Evolution->Particles());
		const TArray<Chaos::TVector<T, 3>> Diff = GetDifference(PostPoints, InitialPoints);
		const TArray<T> ScalarDiff = GetMagnitude(Diff);

		// All points did the same thing
		int32 Idx = 0;
		EXPECT_TRUE(AllSame(ScalarDiff, Idx, (T)0.1))
			<< TestID
			<< "Points fell different distances - Index 0: "
			<< ScalarDiff[0] << " != Index " 
			<< Idx << ": " 
			<< ScalarDiff[Idx] << " +/- 0.1.";

		// Fell the right amount
		EXPECT_NEAR(ScalarDiff[0], (T)0.5 * GravMag, DistTolerance)
			<< TestID
			<< "Points fell by " << ScalarDiff[0] 
			<< ", expected " << ((T)0.5 * GravMag) 
			<< " +/- " << DistTolerance << "."; 

		// Fell the right direction
		const T DirDot = Chaos::TVector<T, 3>::DotProduct(GravDir, Diff[0].GetSafeNormal());
		EXPECT_NEAR(DirDot, (T)1.0, (T)KINDA_SMALL_NUMBER)
			<< TestID
			<< "Points fell in different directions.";

		return true;
	}

	template <class T>
	void DeformableGravity()
	{
		const T DistTol = 0.0002;

		//
		// Initialize solver and gravity
		//

		TUniquePtr<TPBDEvolution<T, 3>> Evolution = InitPBDEvolution<T>();

		const Chaos::TVector<float, 3> GravDir(0.f, 0.f, -1.f);
		const T GravMag = 980.665;

		//
		// Drop a single particle
		//

		InitSingleParticle<T>(Evolution);
		TArray<Chaos::TVector<T, 3>> InitialPoints = CopyPoints<T>(Evolution->Particles());

		RunDropTest<T>(Evolution, GravMag, GravDir, InitialPoints, 1, DistTol, "Single point falling under gravity, iters: 1 - ");
		Reset(Evolution->Particles(), InitialPoints);
		RunDropTest<T>(Evolution, GravMag, GravDir, InitialPoints, 100, DistTol, "Single point falling under gravity, iters: 100 - ");
		Reset(Evolution->Particles(), InitialPoints);

		//
		// Add a triangle mesh
		//

		Chaos::TTriangleMesh<T> TriMesh;
		InitTriMesh_EquilateralTri(TriMesh, Evolution);
		InitialPoints = CopyPoints<T>(Evolution->Particles());

		// 
		// Points falling under gravity
		//

		RunDropTest<T>(Evolution, GravMag, GravDir, InitialPoints, 1, DistTol, "Points falling under gravity, iters: 1 - ");
		Reset(Evolution->Particles(), InitialPoints);
		RunDropTest<T>(Evolution, GravMag, GravDir, InitialPoints, 100, DistTol, "Points falling under gravity, iters: 100 - ");
		Reset(Evolution->Particles(), InitialPoints);

		// 
		// Points falling under gravity with edge length constraint
		//

		AddEdgeLengthConstraint(Evolution, TriMesh.GetSurfaceElements(), 1.0);

		RunDropTest<T>(Evolution, GravMag, GravDir, InitialPoints, 1, DistTol, "Points falling under gravity & edge cnstr, iters: 1 - ");
		Reset(Evolution->Particles(), InitialPoints);
		RunDropTest<T>(Evolution, GravMag, GravDir, InitialPoints, 100, DistTol, "Points falling under gravity & edge cnstr, iters: 100 - ");
		Reset(Evolution->Particles(), InitialPoints);
	}
	template void DeformableGravity<float>();

} // namespace ChaosTest