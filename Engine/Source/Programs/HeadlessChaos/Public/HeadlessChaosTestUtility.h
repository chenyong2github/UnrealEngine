// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HeadlessChaos.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Vector.h"
#include "Math/Vector.h"

#include "Chaos/Box.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Plane.h"
#include "Chaos/Sphere.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Levelset.h"
#include "Chaos/UniformGrid.h"
#include "Chaos/Utilities.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"
#include "Chaos/EvolutionTraits.h"

namespace Chaos
{
	class FPBDRigidsSOAs;
}

namespace ChaosTest {
	
	using namespace Chaos;

	template <typename... T>
	struct TTypesWithoutVoid
	{
		using Types = ::testing::Types<T...>;
	};

	template <typename... T>
	struct TTypesWithoutVoid<void,T...>
	{
		using Types = ::testing::Types<T...>;
	};

	//should be TAllEvolutions, but used in macros and logs and this makes it more readable
	template <typename T>
	class AllEvolutions : public ::testing::Test {};

#define EVOLUTION_TRAIT(Trait) ,TPBDRigidsEvolutionGBF<Trait>
	using AllEvolutionTypesTmp = TTypesWithoutVoid <
		void
#include "Chaos/EvolutionTraits.inl"
	>;
	using AllEvolutionTypes = AllEvolutionTypesTmp::Types;
#undef EVOLUTION_TRAIT
	TYPED_TEST_SUITE(AllEvolutions,AllEvolutionTypes);

	//should be TAllTraits, but used in macros and logs and this makes it more readable
	template <typename T>
	class AllTraits : public ::testing::Test {};

#define EVOLUTION_TRAIT(Trait) ,Trait
	using AllTraitsTypesTmp = TTypesWithoutVoid <
		void
#include "Chaos/EvolutionTraits.inl"
	>;
	using AllTraitsTypes = AllTraitsTypesTmp::Types;
#undef EVOLUTION_TRAIT
	TYPED_TEST_SUITE(AllTraits,AllTraitsTypes);

	MATCHER_P2(VectorNear, V, Tolerance, "") { return arg.Equals(V, Tolerance); }

	// Expects each component of the vector is within T of its corresponding component in A. 
#define EXPECT_VECTOR_NEAR(A,B,T) EXPECT_THAT(A, VectorNear(B, T)) << *FString("Expected: " + B.ToString() + "\nActual:   " + A.ToString());
	// Default comparison to KINDA_SMALL_NUMBER.
#define EXPECT_VECTOR_NEAR_DEFAULT(A,B) EXPECT_THAT(A, VectorNear(B, KINDA_SMALL_NUMBER)) << *FString("Expected: " + B.ToString() + "\nActual:   " + A.ToString())
	// Print an additional error string if the expect fails.
#define EXPECT_VECTOR_NEAR_ERR(A,B,T,E) EXPECT_THAT(A, VectorNear(B, T)) << *(FString("Expected: " + B.ToString() + "\nActual:   " + A.ToString() + "\n") + E);

	// Similar to EXPECT_VECTOR_NEAR_DEFAULT but only prints the component(s) that are wrong, and prints with more precision.
#define EXPECT_VECTOR_FLOAT_EQ(A, B) EXPECT_FLOAT_EQ(A.X, B.X); EXPECT_FLOAT_EQ(A.Y, B.Y); EXPECT_FLOAT_EQ(A.Z, B.Z);
	// Print an additional error string if the expect fails. 
#define EXPECT_VECTOR_FLOAT_EQ_ERR(A, B, E) EXPECT_FLOAT_EQ(A.X, B.X) << *E; EXPECT_FLOAT_EQ(A.Y, B.Y) << *E; EXPECT_FLOAT_EQ(A.Z, B.Z) << *E;

	/**/
	int32 AppendAnalyticSphere(FPBDRigidParticles& InParticles, FReal Scale = (FReal)1);

	/**/
	int32 AppendAnalyticBox(FPBDRigidParticles& InParticles, FVec3 Scale = FVec3(1));

	/**/
	int32 AppendParticleBox(FPBDRigidParticles& InParticles, FVec3 Scale = FVec3(1), TArray<TVec3<int32>>* OutElements = nullptr);

	FPBDRigidParticleHandle* AppendDynamicParticleBox(FPBDRigidsSOAs& SOAs, const FVec3& Scale = FVec3(1), TArray<TVec3<int32>>* OutElements = nullptr);

	FPBDRigidParticleHandle* AppendDynamicParticleBoxMargin(FPBDRigidsSOAs& SOAs, const FVec3& Scale, FReal Margin, TArray<TVec3<int32>>* OutElements = nullptr);

	FPBDRigidParticleHandle* AppendDynamicParticleSphere(FPBDRigidsSOAs& SOAs, const FVec3& Scale = FVec3(1), TArray<TVec3<int32>>* OutElements = nullptr);

	FPBDRigidParticleHandle* AppendDynamicParticleCylinder(FPBDRigidsSOAs& SOAs, const FVec3& Scale = FVec3(1), TArray<TVec3<int32>>* OutElements = nullptr);
	
	FPBDRigidParticleHandle* AppendDynamicParticleTaperedCylinder(FPBDRigidsSOAs& SOAs, const FVec3& Scale = FVec3(1), TArray<TVec3<int32>>* OutElements = nullptr);

	FGeometryParticleHandle* AppendStaticParticleBox(FPBDRigidsSOAs& SOAs, const FVec3& Scale = FVec3(1), TArray<TVec3<int32>>* OutElements = nullptr);

	FPBDRigidParticleHandle* AppendClusteredParticleBox(FPBDRigidsSOAs& SOAs, const FVec3& Scale = FVec3(1), TArray<TVec3<int32>>* OutElements = nullptr);
		
	/**/
	int32 AppendStaticAnalyticFloor(FPBDRigidParticles& InParticles);

	FKinematicGeometryParticleHandle* AppendStaticAnalyticFloor(FPBDRigidsSOAs& SOAs);

	FKinematicGeometryParticleHandle* AppendStaticConvexFloor(FPBDRigidsSOAs& SOAs);
		
	/**/
	TLevelSet<FReal, 3> ConstructLevelset(FParticles& SurfaceParticles, TArray<TVec3<int32>> & Elements);

	/**/
	void AppendDynamicParticleConvexBox(FPBDRigidParticleHandle& InParticles, const FVec3& Scale, FReal Margin);
	
	FPBDRigidParticleHandle* AppendDynamicParticleConvexBox(FPBDRigidsSOAs& SOAs, const FVec3& Scale = FVec3(1));

	FPBDRigidParticleHandle* AppendDynamicParticleConvexBoxMargin(FPBDRigidsSOAs& SOAs, const FVec3& Scale, FReal Margin);

	/**/
	FVec3 ObjectSpacePoint(FPBDRigidParticles& InParticles, const int32 Index, const FVec3& WorldSpacePoint);

	/**/
	FReal PhiWithNormal(FPBDRigidParticles& InParticles, const int32 Index, const FVec3& WorldSpacePoint, FVec3& Normal);

	/**/
	FReal SignedDistance(FPBDRigidParticles& InParticles, const int32 Index, const FVec3& WorldSpacePoint);

	/**/
	FVec3  ObjectSpacePoint(FGeometryParticleHandle& Particle, const FVec3& WorldSpacePoint);

	/**/
	FReal PhiWithNormal(FGeometryParticleHandle& Particle, const FVec3& WorldSpacePoint, FVec3& Normal);

	/**/
	FReal SignedDistance(FGeometryParticleHandle& Particle, const FVec3& WorldSpacePoint);

	/**
	 * Return a random normalized axis.
	 * @note: not spherically distributed - actually calculates a point on a box and normalizes.
	 */
	FVec3 RandAxis();


	/**/
	inline FVec3 RandomVector(FReal MinValue, FReal MaxValue)
	{
		FVec3 V;
		V.X = FMath::RandRange(MinValue, MaxValue);
		V.Y = FMath::RandRange(MinValue, MaxValue);
		V.Z = FMath::RandRange(MinValue, MaxValue);
		return V;
	}

	/**/
	inline FMatrix33 RandomMatrix(FReal MinValue, FReal MaxValue)
	{
		return FMatrix33(
			RandomVector(MinValue, MaxValue),
			RandomVector(MinValue, MaxValue),
			RandomVector(MinValue, MaxValue));
	}

	/**/
	inline FRotation3 RandomRotation(FReal XMax, FReal YMax, FReal ZMax)
	{
		FReal X = FMath::DegreesToRadians(FMath::RandRange(-XMax, XMax));
		FReal Y = FMath::DegreesToRadians(FMath::RandRange(-YMax, YMax));
		FReal Z = FMath::DegreesToRadians(FMath::RandRange(-ZMax, ZMax));
		return FRotation3::FromAxisAngle(FVec3(1, 0, 0), X) * FRotation3::FromAxisAngle(FVec3(0, 1, 0), Y) * FRotation3::FromAxisAngle(FVec3(0, 0, 1), Z);
	}

	/**/
	void SetParticleSimDataToCollide(TArray< Chaos::FGeometryParticle* > ParticleArray);
	void SetParticleSimDataToCollide(TArray< Chaos::FGeometryParticleHandle* > ParticleArray);


	/**
	 * Set settings on Evolution to those used by the tests
	 */
	template<typename T_Evolution>
	void InitEvolutionSettings(T_Evolution& Evolution)
	{
		// Settings used for unit tests
		const float CullDistance = 0.0f;
		Evolution.GetCollisionConstraints().SetCullDistance(CullDistance);
		Evolution.GetBroadPhase().SetCullDistance(CullDistance);
		Evolution.GetBroadPhase().SetBoundsThickness(CullDistance);
	}

	template<typename T_SOLVER>
	void InitSolverSettings(T_SOLVER& Solver)
	{
		InitEvolutionSettings(*Solver->GetEvolution());
	}


	extern FImplicitConvex3 CreateConvexBox(const FVec3& BoxSize, const FReal Margin);
	extern TImplicitObjectInstanced<FImplicitConvex3> CreateInstancedConvexBox(const FVec3& BoxSize, const FReal Margin);
	extern TImplicitObjectScaled<FImplicitConvex3> CreateScaledConvexBox(const FVec3& BoxSize, const FVec3 BoxScale, const FReal Margin);
}
