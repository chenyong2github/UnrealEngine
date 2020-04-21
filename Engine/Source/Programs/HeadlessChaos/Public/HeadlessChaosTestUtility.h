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
	template <typename T, int d> class TPBDRigidsSOAs;
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
	template<class T>
	int32 AppendAnalyticSphere(TPBDRigidParticles<T, 3> & InParticles, T Scale = (T)1);
	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendAnalyticSphere2(FPBDRigidsEvolution& Evolution, T Scale = (T)1);

	/**/
	template<class T>
	int32 AppendAnalyticBox(TPBDRigidParticles<T, 3>& InParticles, TVector<T, 3> Scale = TVector<T, 3>(1));
	template<class T>
	TKinematicGeometryParticleHandle<T, 3>* AppendKinematicAnalyticBox2(FPBDRigidsEvolution& Evolution, TVector<T, 3> Scale = TVector<T, 3>(1));
	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicAnalyticBox2(FPBDRigidsEvolution& Evolution, TVector<T, 3> Scale = TVector<T, 3>(1));

	/**/
	template<class T>
	int32 AppendParticleBox(TPBDRigidParticles<T, 3>& InParticles, TVector<T, 3> Scale = TVector<T, 3>(1), TArray<TVector<int32, 3>>* OutElements = nullptr);
	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleBox(FPBDRigidsEvolution& Evolution, const TVector<T, 3>& Scale = TVector<T, 3>(1), TArray<TVector<int32, 3>>* OutElements = nullptr);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale = TVector<T, 3>(1), TArray<TVector<int32, 3>>* OutElements = nullptr);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleSphere(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale = TVector<T, 3>(1), TArray<TVector<int32, 3>>* OutElements = nullptr);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleCylinder(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale = TVector<T, 3>(1), TArray<TVector<int32, 3>>* OutElements = nullptr);
	
	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleTaperedCylinder(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale = TVector<T, 3>(1), TArray<TVector<int32, 3>>* OutElements = nullptr);

	template<class T>
	TGeometryParticleHandle<T, 3>* AppendStaticParticleBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale = TVector<T, 3>(1), TArray<TVector<int32, 3>>* OutElements = nullptr);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendClusteredParticleBox(FPBDRigidsEvolution& Evolution, const TVector<T, 3>& Scale = TVector<T, 3>(1), TArray<TVector<int32, 3>>* OutElements = nullptr);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendClusteredParticleBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale = TVector<T, 3>(1), TArray<TVector<int32, 3>>* OutElements = nullptr);
		
	/**/
	template<class T>
	int32 AppendStaticAnalyticFloor(TPBDRigidParticles<T, 3>& InParticles);
	template<class T>
	TKinematicGeometryParticleHandle<T, 3>* AppendStaticAnalyticFloor(FPBDRigidsEvolution& Evolution);

	template<class T>
	TKinematicGeometryParticleHandle<T, 3>* AppendStaticAnalyticFloor(TPBDRigidsSOAs<T, 3>& SOAs);

	template<class T>
	TKinematicGeometryParticleHandle<T, 3>* AppendStaticConvexFloor(TPBDRigidsSOAs<T, 3>& SOAs);
		
	/**/
	template<class T>
	TLevelSet<T, 3> ConstructLevelset(TParticles<T, 3> & SurfaceParticles, TArray<TVector<int32, 3>> & Elements);

	/**/
	template<class T>
	void AppendDynamicParticleConvexBox(TPBDRigidParticleHandle<T, 3> & InParticles, const TVector<T, 3>& Scale);
	
	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleConvexBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale = TVector<T, 3>(1));

	/**/
	template<class T>
	TVector<T,3>  ObjectSpacePoint(TPBDRigidParticles<T, 3> & InParticles, const int32 Index, const TVector<T, 3>& WorldSpacePoint);

	/**/
	template<class T>
	T PhiWithNormal(TPBDRigidParticles<T, 3> & InParticles, const int32 Index, const TVector<T, 3>& WorldSpacePoint, TVector<T, 3>& Normal);

	/**/
	template<class T>
	T SignedDistance(TPBDRigidParticles<T, 3> & InParticles, const int32 Index, const TVector<T, 3>& WorldSpacePoint);

	/**/
	template<class T>
	TVector<T, 3>  ObjectSpacePoint(TGeometryParticleHandle<T, 3> & Particle, const TVector<T, 3>& WorldSpacePoint);

	/**/
	template<class T>
	T PhiWithNormal(TGeometryParticleHandle<T, 3> & Particle, const TVector<T, 3>& WorldSpacePoint, TVector<T, 3>& Normal);

	/**/
	template<class T>
	T SignedDistance(TGeometryParticleHandle<T, 3> & Particle, const TVector<T, 3>& WorldSpacePoint);

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
	void SetParticleSimDataToCollide(TArray< Chaos::TGeometryParticle<float, 3>* > ParticleArray);
	void SetParticleSimDataToCollide(TArray< Chaos::TGeometryParticleHandle<float, 3>* > ParticleArray);

	
}
