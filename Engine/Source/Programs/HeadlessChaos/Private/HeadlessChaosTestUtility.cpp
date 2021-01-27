// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestUtility.h"

#include "HeadlessChaos.h"
#include "Chaos/Box.h"
#include "Chaos/Convex.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Levelset.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Plane.h"
#include "Chaos/Sphere.h"
#include "Chaos/Cylinder.h"
#include "Chaos/TaperedCylinder.h"
#include "Chaos/Utilities.h"

namespace ChaosTest {

	using namespace Chaos;

	template<class T>
	int32 AppendAnalyticSphere(TPBDRigidParticles<T, 3> & InParticles, T Scale)
	{
		InParticles.AddParticles(1);
		int32 RigidBodyIndex = InParticles.Size() - 1;

		InParticles.X(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.V(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.R(RigidBodyIndex) = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.P(RigidBodyIndex) = InParticles.X(RigidBodyIndex);
		InParticles.Q(RigidBodyIndex) = InParticles.R(RigidBodyIndex);

		InParticles.M(RigidBodyIndex) = 1.f;
		InParticles.InvM(RigidBodyIndex) = 1.f;
		InParticles.I(RigidBodyIndex) = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.InvI(RigidBodyIndex) = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.SetDynamicGeometry(RigidBodyIndex, MakeUnique<TSphere<T, 3>>(TVec3<T>(0), Scale));
		InParticles.SetObjectState(RigidBodyIndex, EObjectStateType::Dynamic);

		return RigidBodyIndex;
	}
	template int32 AppendAnalyticSphere(TPBDRigidParticles<float, 3> &, float Scale);

	template<class T>
	int32 AppendAnalyticBox(TPBDRigidParticles<T, 3> & InParticles, TVec3<T> Scale)
	{
		InParticles.AddParticles(1);
		int32 RigidBodyIndex = InParticles.Size() - 1;

		InParticles.X(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.V(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.R(RigidBodyIndex) = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.P(RigidBodyIndex) = InParticles.X(RigidBodyIndex);
		InParticles.Q(RigidBodyIndex) = InParticles.R(RigidBodyIndex);

		InParticles.M(RigidBodyIndex) = 1.f;
		InParticles.InvM(RigidBodyIndex) = 1.f;
		InParticles.I(RigidBodyIndex) = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.InvI(RigidBodyIndex) = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.SetDynamicGeometry(RigidBodyIndex, MakeUnique<TBox<T, 3>>(-Scale / 2.f, Scale / 2.f));
		InParticles.SetObjectState(RigidBodyIndex, EObjectStateType::Dynamic);

		return RigidBodyIndex;
	}
	template int32 AppendAnalyticBox(TPBDRigidParticles<float, 3> &, FVec3);


	template<class T>
	void InitAnalyticBox2(TKinematicGeometryParticleHandle<T, 3>* Particle, TVec3<T> Scale)
	{
		Particle->X() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->V() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->SetDynamicGeometry(MakeUnique<TBox<T, 3>>(-Scale / 2.f, Scale / 2.f));

		TPBDRigidParticleHandle<T, 3>* DynamicParticle = Particle->CastToRigidParticle();
		if(DynamicParticle && DynamicParticle->ObjectState() == EObjectStateType::Dynamic)
		{
			DynamicParticle->P() = Particle->X();
			DynamicParticle->Q() = Particle->R();

			DynamicParticle->M() = 1.f;
			DynamicParticle->InvM() = 1.f;
			DynamicParticle->I() = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
			DynamicParticle->InvI() = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		}
	}

	template<class T>
	int32 AppendParticleBox(TPBDRigidParticles<T, 3> & InParticles, TVec3<T> Scale, TArray<TVec3<int32>> * elements)
	{
		InParticles.AddParticles(1);
		int32 RigidBodyIndex = InParticles.Size() - 1;

		InParticles.X(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.V(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.R(RigidBodyIndex) = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.P(RigidBodyIndex) = InParticles.X(RigidBodyIndex);
		InParticles.Q(RigidBodyIndex) = InParticles.R(RigidBodyIndex);

		check(Scale.X == Scale.Y && Scale.X == Scale.Z);
		T ScaleSq = Scale.X * Scale.X;
		InParticles.M(RigidBodyIndex) = 1.f;
		InParticles.InvM(RigidBodyIndex) = 1.f;
		InParticles.I(RigidBodyIndex) = PMatrix<T, 3, 3>(ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f);
		InParticles.InvI(RigidBodyIndex) = PMatrix<T, 3, 3>(6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq);
		InParticles.SetDynamicGeometry(RigidBodyIndex, MakeUnique<TBox<T, 3>>(-Scale / 2.f, Scale / 2.f));
		InParticles.SetObjectState(RigidBodyIndex, EObjectStateType::Dynamic);

		int32 CollisionIndex = 0;
		InParticles.CollisionParticlesInitIfNeeded(RigidBodyIndex);
		InParticles.CollisionParticles(RigidBodyIndex)->AddParticles(8);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, -Scale[1] / 2.f, -Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, -Scale[1] / 2.f, -Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, +Scale[1] / 2.f, -Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, +Scale[1] / 2.f, -Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, -Scale[1] / 2.f, +Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, -Scale[1] / 2.f, +Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, +Scale[1] / 2.f, +Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, +Scale[1] / 2.f, +Scale[2] / 2.f);

		if (elements != nullptr)
		{
			/*
			//cw
			elements->Add(TVec3<int32>(4, 1, 5)); // Front
			elements->Add(TVec3<int32>(1, 4, 0));
			elements->Add(TVec3<int32>(7, 2, 6)); // Back
			elements->Add(TVec3<int32>(2, 7, 3));
			elements->Add(TVec3<int32>(6, 0, 4)); // Right
			elements->Add(TVec3<int32>(0, 6, 2));
			elements->Add(TVec3<int32>(5, 3, 7)); // Left
			elements->Add(TVec3<int32>(3, 5, 1));
			elements->Add(TVec3<int32>(6, 5, 7)); // Top
			elements->Add(TVec3<int32>(5, 6, 4));
			elements->Add(TVec3<int32>(0, 2, 1)); // Front
			elements->Add(TVec3<int32>(2, 0, 3));
			*/
			//ccw
			elements->Add(TVec3<int32>(1,4,5)); // Front
			elements->Add(TVec3<int32>(4,1,0));
			elements->Add(TVec3<int32>(2,7,6)); // Back
			elements->Add(TVec3<int32>(7,2,3));
			elements->Add(TVec3<int32>(0,6,4)); // Right
			elements->Add(TVec3<int32>(6,0,2));
			elements->Add(TVec3<int32>(3,5,7)); // Left
			elements->Add(TVec3<int32>(5,3,1));
			elements->Add(TVec3<int32>(5,6,7)); // Top
			elements->Add(TVec3<int32>(6,5,4));
			elements->Add(TVec3<int32>(2,0,1)); // Front
			elements->Add(TVec3<int32>(0,2,3));
		}

		return RigidBodyIndex;
	}
	template int32 AppendParticleBox(TPBDRigidParticles<float, 3> &, FVec3, TArray<Chaos::TVec3<int32>> *);


	template<class T>
	void InitDynamicParticleBox2(TPBDRigidParticleHandle<T, 3>* Particle, const TVec3<T>& Scale, FReal Margin, TArray<TVector<int32, 3>>* OutElements)
	{
		Particle->X() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->V() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVec3<T>(0.f, 0.f, 0.f);

		Particle->P() = Particle->X();
		Particle->Q() = Particle->R();

		// Inertia assumes cube so is imcorrect for rectangular boxes
		T MaxScale = Scale.GetMax();
		T ScaleSq = MaxScale * MaxScale;
		Particle->M() = 1.f;
		Particle->InvM() = 1.f;
		Particle->I() = PMatrix<T, 3, 3>(ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f);
		Particle->InvI() = PMatrix<T, 3, 3>(6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq);

		Particle->SetDynamicGeometry(MakeUnique<TBox<T, 3>>(-Scale / 2.f, Scale / 2.f, Margin));

		int32 CollisionIndex = 0;
		Particle->CollisionParticlesInitIfNeeded();
		Particle->CollisionParticles()->AddParticles(8);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, -Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, -Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, +Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, +Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, -Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, -Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, +Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, +Scale[1] / 2.f, +Scale[2] / 2.f);

		// This is needed for calculating contacts (Bounds are bigger than they need to be, even allowing for rotation)
		Particle->SetLocalBounds(TAABB<T, 3>(TVec3<T>(-MaxScale), TVec3<T>(MaxScale)));
		Particle->SetWorldSpaceInflatedBounds(TAABB<T, 3>(TVec3<T>(-MaxScale), TVec3<T>(MaxScale)));
		Particle->SetHasBounds(true);

		if (OutElements != nullptr)
		{
			/*
			//cw
			OutElements->Add(TVec3<int32>(4, 1, 5)); // Front
			OutElements->Add(TVec3<int32>(1, 4, 0));
			OutElements->Add(TVec3<int32>(7, 2, 6)); // Back
			OutElements->Add(TVec3<int32>(2, 7, 3));
			OutElements->Add(TVec3<int32>(6, 0, 4)); // Right
			OutElements->Add(TVec3<int32>(0, 6, 2));
			OutElements->Add(TVec3<int32>(5, 3, 7)); // Left
			OutElements->Add(TVec3<int32>(3, 5, 1));
			OutElements->Add(TVec3<int32>(6, 5, 7)); // Top
			OutElements->Add(TVec3<int32>(5, 6, 4));
			OutElements->Add(TVec3<int32>(0, 2, 1)); // Front
			OutElements->Add(TVec3<int32>(2, 0, 3));
			*/
			//ccw
			OutElements->Add(TVec3<int32>(1, 4, 5)); // Front
			OutElements->Add(TVec3<int32>(4, 1, 0));
			OutElements->Add(TVec3<int32>(2, 7, 6)); // Back
			OutElements->Add(TVec3<int32>(7, 2, 3));
			OutElements->Add(TVec3<int32>(0, 6, 4)); // Right
			OutElements->Add(TVec3<int32>(6, 0, 2));
			OutElements->Add(TVec3<int32>(3, 5, 7)); // Left
			OutElements->Add(TVec3<int32>(5, 3, 1));
			OutElements->Add(TVec3<int32>(5, 6, 7)); // Top
			OutElements->Add(TVec3<int32>(6, 5, 4));
			OutElements->Add(TVec3<int32>(2, 0, 1)); // Front
			OutElements->Add(TVec3<int32>(0, 2, 3));
		}

		::ChaosTest::SetParticleSimDataToCollide({ Particle});

	}

	
	template<class T>
	void InitDynamicParticleSphere2(TPBDRigidParticleHandle<T, 3>* Particle, const TVec3<T>& Scale, TArray<TVec3<int32>>* OutElements) {
		Particle->X() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->V() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVec3<T>(0.f, 0.f, 0.f);

		Particle->P() = Particle->X();
		Particle->Q() = Particle->R();

		check(Scale.X == Scale.Y && Scale.X == Scale.Z);
		T ScaleSq = Scale.X * Scale.X;
		Particle->M() = 1.f;
		Particle->InvM() = 1.f;
		Particle->I() = PMatrix<T, 3, 3>(ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f);
		Particle->InvI() = PMatrix<T, 3, 3>(6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq);

		Particle->SetDynamicGeometry(MakeUnique<TSphere<T, 3>>(TVec3<T>(0), Scale.X / 2.f));

		int32 CollisionIndex = 0;
		Particle->CollisionParticlesInitIfNeeded();
		Particle->CollisionParticles()->AddParticles(6);

		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, 0, 0);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, 0, 0);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(0, -Scale[1] / 2.f, 0);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(0, +Scale[1] / 2.f, 0);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(0, 0, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(0, 0, +Scale[2] / 2.f);

		if (OutElements != nullptr)
		{
			/*
			//cw
			OutElements->Add(TVec3<int32>(4, 1, 5)); // Front
			OutElements->Add(TVec3<int32>(1, 4, 0));
			OutElements->Add(TVec3<int32>(7, 2, 6)); // Back
			OutElements->Add(TVec3<int32>(2, 7, 3));
			OutElements->Add(TVec3<int32>(6, 0, 4)); // Right
			OutElements->Add(TVec3<int32>(0, 6, 2));
			OutElements->Add(TVec3<int32>(5, 3, 7)); // Left
			OutElements->Add(TVec3<int32>(3, 5, 1));
			OutElements->Add(TVec3<int32>(6, 5, 7)); // Top
			OutElements->Add(TVec3<int32>(5, 6, 4));
			OutElements->Add(TVec3<int32>(0, 2, 1)); // Front
			OutElements->Add(TVec3<int32>(2, 0, 3));
			*/
			//ccw
			OutElements->Add(TVec3<int32>(1, 4, 5)); // Front
			OutElements->Add(TVec3<int32>(4, 1, 0));
			OutElements->Add(TVec3<int32>(2, 7, 6)); // Back
			OutElements->Add(TVec3<int32>(7, 2, 3));
			OutElements->Add(TVec3<int32>(0, 6, 4)); // Right
			OutElements->Add(TVec3<int32>(6, 0, 2));
			OutElements->Add(TVec3<int32>(3, 5, 7)); // Left
			OutElements->Add(TVec3<int32>(5, 3, 1));
			OutElements->Add(TVec3<int32>(5, 6, 7)); // Top
			OutElements->Add(TVec3<int32>(6, 5, 4));
			OutElements->Add(TVec3<int32>(2, 0, 1)); // Front
			OutElements->Add(TVec3<int32>(0, 2, 3));
		}
	}


	template<class T>
	void InitDynamicParticleCylinder2(TPBDRigidParticleHandle<T, 3>* Particle, const TVec3<T>& Scale, TArray<TVec3<int32>>* OutElements, bool Tapered) {
		Particle->X() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->V() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVec3<T>(0.f, 0.f, 0.f);

		Particle->P() = Particle->X();
		Particle->Q() = Particle->R();

		check(Scale.X == Scale.Y && Scale.X == Scale.Z);
		T ScaleSq = Scale.X * Scale.X;
		Particle->M() = 1.f;
		Particle->InvM() = 1.f;
		Particle->I() = PMatrix<T, 3, 3>(ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f);
		Particle->InvI() = PMatrix<T, 3, 3>(6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq);
		
		if (Tapered)
		{
			Particle->SetDynamicGeometry(MakeUnique<TTaperedCylinder<T>>(TVec3<T>(0, 0, Scale.X / 2.0f), TVec3<T>(0, 0, -Scale.X / 2.0f), Scale.X / 2.f, Scale.X / 2.f));
		}
		else 
		{
			Particle->SetDynamicGeometry(MakeUnique<TCylinder<T>>(TVec3<T>(0, 0, Scale.X / 2.0f), TVec3<T>(0, 0, -Scale.X / 2.0f), Scale.X / 2.f));
		}

		int32 CollisionIndex = 0;
		Particle->CollisionParticlesInitIfNeeded();
		Particle->CollisionParticles()->AddParticles(8);

		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, 0, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(-Scale[0] / 2.f, 0, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, 0, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(+Scale[0] / 2.f, 0, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(0, -Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(0, -Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(0, +Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVec3<T>(0, +Scale[1] / 2.f, -Scale[2] / 2.f);

		if (OutElements != nullptr)
		{
			/*
			//cw
			OutElements->Add(TVec3<int32>(4, 1, 5)); // Front
			OutElements->Add(TVec3<int32>(1, 4, 0));
			OutElements->Add(TVec3<int32>(7, 2, 6)); // Back
			OutElements->Add(TVec3<int32>(2, 7, 3));
			OutElements->Add(TVec3<int32>(6, 0, 4)); // Right
			OutElements->Add(TVec3<int32>(0, 6, 2));
			OutElements->Add(TVec3<int32>(5, 3, 7)); // Left
			OutElements->Add(TVec3<int32>(3, 5, 1));
			OutElements->Add(TVec3<int32>(6, 5, 7)); // Top
			OutElements->Add(TVec3<int32>(5, 6, 4));
			OutElements->Add(TVec3<int32>(0, 2, 1)); // Front
			OutElements->Add(TVec3<int32>(2, 0, 3));
			*/
			//ccw
			OutElements->Add(TVec3<int32>(1, 4, 5)); // Front
			OutElements->Add(TVec3<int32>(4, 1, 0));
			OutElements->Add(TVec3<int32>(2, 7, 6)); // Back
			OutElements->Add(TVec3<int32>(7, 2, 3));
			OutElements->Add(TVec3<int32>(0, 6, 4)); // Right
			OutElements->Add(TVec3<int32>(6, 0, 2));
			OutElements->Add(TVec3<int32>(3, 5, 7)); // Left
			OutElements->Add(TVec3<int32>(5, 3, 1));
			OutElements->Add(TVec3<int32>(5, 6, 7)); // Top
			OutElements->Add(TVec3<int32>(6, 5, 4));
			OutElements->Add(TVec3<int32>(2, 0, 1)); // Front
			OutElements->Add(TVec3<int32>(0, 2, 3));
		}
	}

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVec3<T>& Scale, TArray<TVec3<int32>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		InitDynamicParticleBox2(Particles[0], Scale, 0.0f, OutElements);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleBox(TPBDRigidsSOAs<float, 3>& Evolution, const FVec3& Scale, TArray<Chaos::TVec3<int32>>* OutElements);

	// Create a particle with box collision of specified size and margin (size includes margin)
	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleBoxMargin(TPBDRigidsSOAs<T, 3>& SOAs, const TVec3<T>& Scale, FReal Margin, TArray<TVec3<int32>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		InitDynamicParticleBox2(Particles[0], Scale, Margin, OutElements);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleBoxMargin(TPBDRigidsSOAs<float, 3>& Evolution, const FVec3& Scale, FReal Margin, TArray<Chaos::TVec3<int32>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleSphere(TPBDRigidsSOAs<T, 3>& SOAs, const TVec3<T>& Scale, TArray<TVec3<int32>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		InitDynamicParticleSphere2(Particles[0], Scale, OutElements);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleSphere(TPBDRigidsSOAs<float, 3>& Evolution, const FVec3& Scale, TArray<Chaos::TVec3<int32>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleCylinder(TPBDRigidsSOAs<T, 3>& SOAs, const TVec3<T>& Scale, TArray<TVec3<int32>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		InitDynamicParticleCylinder2(Particles[0], Scale, OutElements, false);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleCylinder(TPBDRigidsSOAs<float, 3>& Evolution, const FVec3& Scale, TArray<Chaos::TVec3<int32>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleTaperedCylinder(TPBDRigidsSOAs<T, 3>& SOAs, const TVec3<T>& Scale, TArray<TVec3<int32>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		InitDynamicParticleCylinder2(Particles[0], Scale, OutElements, true);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleTaperedCylinder(TPBDRigidsSOAs<float, 3>& Evolution, const FVec3& Scale, TArray<Chaos::TVec3<int32>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendClusteredParticleBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVec3<T>& Scale, TArray<TVec3<int32>>* OutElements)
	{
		auto Particles = SOAs.CreateClusteredParticles(1);
		InitDynamicParticleBox2(Particles[0], Scale, 0.0f, OutElements);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendClusteredParticleBox(TPBDRigidsSOAs<float, 3>& SOAs, const FVec3& Scale, TArray<Chaos::TVec3<int32>>* OutElements);

	template<class T>
	void InitStaticParticleBox(TGeometryParticleHandle<T, 3>* Particle, const TVec3<T>& Scale, TArray<TVec3<int32>>* OutElements)
	{
		Particle->X() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();

		check(Scale.X == Scale.Y && Scale.X == Scale.Z);
		T ScaleSq = Scale.X * Scale.X;

		Particle->SetDynamicGeometry(MakeUnique<TBox<T, 3>>(-Scale / 2.f, Scale / 2.f));

		// This is needed for calculating contacts (Bounds are bigger than they need to be, even allowing for rotation)
		Particle->SetLocalBounds(TAABB<T, 3>(TVec3<T>(-Scale[0]), TVec3<T>(Scale[0])));
		Particle->SetWorldSpaceInflatedBounds(TAABB<T, 3>(TVec3<T>(-Scale[0]), TVec3<T>(Scale[0])));
		Particle->SetHasBounds(true);

		if (OutElements != nullptr)
		{
			/*
			//cw
			OutElements->Add(TVec3<int32>(4, 1, 5)); // Front
			OutElements->Add(TVec3<int32>(1, 4, 0));
			OutElements->Add(TVec3<int32>(7, 2, 6)); // Back
			OutElements->Add(TVec3<int32>(2, 7, 3));
			OutElements->Add(TVec3<int32>(6, 0, 4)); // Right
			OutElements->Add(TVec3<int32>(0, 6, 2));
			OutElements->Add(TVec3<int32>(5, 3, 7)); // Left
			OutElements->Add(TVec3<int32>(3, 5, 1));
			OutElements->Add(TVec3<int32>(6, 5, 7)); // Top
			OutElements->Add(TVec3<int32>(5, 6, 4));
			OutElements->Add(TVec3<int32>(0, 2, 1)); // Front
			OutElements->Add(TVec3<int32>(2, 0, 3));
			*/
			//ccw
			OutElements->Add(TVec3<int32>(1, 4, 5)); // Front
			OutElements->Add(TVec3<int32>(4, 1, 0));
			OutElements->Add(TVec3<int32>(2, 7, 6)); // Back
			OutElements->Add(TVec3<int32>(7, 2, 3));
			OutElements->Add(TVec3<int32>(0, 6, 4)); // Right
			OutElements->Add(TVec3<int32>(6, 0, 2));
			OutElements->Add(TVec3<int32>(3, 5, 7)); // Left
			OutElements->Add(TVec3<int32>(5, 3, 1));
			OutElements->Add(TVec3<int32>(5, 6, 7)); // Top
			OutElements->Add(TVec3<int32>(6, 5, 4));
			OutElements->Add(TVec3<int32>(2, 0, 1)); // Front
			OutElements->Add(TVec3<int32>(0, 2, 3));
		}

		::ChaosTest::SetParticleSimDataToCollide({ Particle });

	}

	template<class T>
	TGeometryParticleHandle<T, 3>* AppendStaticParticleBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVec3<T>& Scale, TArray<TVec3<int32>>* OutElements)
	{
		TArray<TGeometryParticleHandle<T, 3>*> Particles = SOAs.CreateStaticParticles(1);
		InitStaticParticleBox(Particles[0], Scale, OutElements);
		return Particles[0];
	}
	template TGeometryParticleHandle<float, 3>* AppendStaticParticleBox(TPBDRigidsSOAs<float, 3>& Evolution, const FVec3& Scale, TArray<Chaos::TVec3<int32>>* OutElements);

	template<class T>
	int AppendStaticAnalyticFloor(TPBDRigidParticles<T, 3> & InParticles)
	{
		InParticles.AddParticles(1);
		int32 RigidBodyIndex = InParticles.Size() - 1;

		InParticles.X(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.V(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.R(RigidBodyIndex) = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W(RigidBodyIndex) = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.M(RigidBodyIndex) = 1.f;
		InParticles.InvM(RigidBodyIndex) = 0.f;
		InParticles.I(RigidBodyIndex) = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.InvI(RigidBodyIndex) = PMatrix<T, 3, 3>(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
		InParticles.SetDynamicGeometry(RigidBodyIndex, MakeUnique<TPlane<T, 3>>(TVec3<T>(0.f, 0.f, 0.f), TVec3<T>(0.f, 0.f, 1.f)));
		InParticles.SetObjectState(RigidBodyIndex, EObjectStateType::Kinematic);

		InParticles.P(RigidBodyIndex) = InParticles.X(RigidBodyIndex);
		InParticles.Q(RigidBodyIndex) = InParticles.R(RigidBodyIndex);

		return RigidBodyIndex;
	}
	template int AppendStaticAnalyticFloor(TPBDRigidParticles<float, 3> &);

	template<class T>
	TKinematicGeometryParticleHandle<T, 3>* AppendStaticAnalyticFloor(TPBDRigidsSOAs<T, 3>& SOAs)
	{
		TArray<TKinematicGeometryParticleHandle<T, 3>*> Particles = SOAs.CreateKinematicParticles(1);
		TKinematicGeometryParticleHandle<T, 3>* Particle = Particles[0];

		Particle->X() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->V() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->SetDynamicGeometry(MakeUnique<TPlane<T, 3>>(TVec3<T>(0.f, 0.f, 0.f), TVec3<T>(0.f, 0.f, 1.f)));

		::ChaosTest::SetParticleSimDataToCollide({ Particle });

		return Particle;
	}
	template TKinematicGeometryParticleHandle<float, 3>* AppendStaticAnalyticFloor(TPBDRigidsSOAs<float, 3>& Evolution);


	template<class T>
	TKinematicGeometryParticleHandle<T, 3>* AppendStaticConvexFloor(TPBDRigidsSOAs<T, 3>& SOAs)
	{
		TArray<TKinematicGeometryParticleHandle<T, 3>*> Particles = SOAs.CreateKinematicParticles(1);
		TKinematicGeometryParticleHandle<T, 3>* Particle = Particles[0];

		Particle->X() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->V() = TVec3<T>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVec3<T>(0.f, 0.f, 0.f);

		Chaos::TParticles<T, 3> Cube;
		Cube.AddParticles(9);
		Cube.X(0) = FVec3(-1000, -1000, -20);
		Cube.X(1) = FVec3(-1000, -1000, 0);
		Cube.X(2) = FVec3(-1000, 1000, -20);
		Cube.X(3) = FVec3(-1000, 1000, 0);
		Cube.X(4) = FVec3(1000, -1000, -20);
		Cube.X(5) = FVec3(1000, -1000, 0);
		Cube.X(6) = FVec3(1000, 1000, -20);
		Cube.X(7) = FVec3(1000, 1000, 0);
		Cube.X(8) = FVec3(0, 0, 0);

		Particle->SetDynamicGeometry(MakeUnique<FConvex>(Cube, 0.0f));

		::ChaosTest::SetParticleSimDataToCollide({ Particle });

		return Particle;
	}
	template TKinematicGeometryParticleHandle<float, 3>* AppendStaticConvexFloor(TPBDRigidsSOAs<float, 3>& Evolution);

	/**/
	template<class T>
	TLevelSet<T, 3> ConstructLevelset(TParticles<T, 3> & SurfaceParticles, TArray<TVec3<int32>> & Elements)
	{
		// build Particles and bounds
		Chaos::TAABB<float, 3> BoundingBox(TVec3<T>(0), TVec3<T>(0));
		for (int32 CollisionParticleIndex = 0; CollisionParticleIndex < (int32)SurfaceParticles.Size(); CollisionParticleIndex++)
		{
			BoundingBox.GrowToInclude(SurfaceParticles.X(CollisionParticleIndex));
		}

		// build cell domain
		int32 MaxAxisSize = 10;
		int MaxAxis = BoundingBox.LargestAxis();
		TVec3<T> Extents = BoundingBox.Extents();
		Chaos::TVec3<int32> Counts(MaxAxisSize * Extents[0] / Extents[MaxAxis], MaxAxisSize * Extents[1] / Extents[MaxAxis], MaxAxisSize * Extents[2] / Extents[MaxAxis]);
		Counts[0] = Counts[0] < 1 ? 1 : Counts[0];
		Counts[1] = Counts[1] < 1 ? 1 : Counts[1];
		Counts[2] = Counts[2] < 1 ? 1 : Counts[2];

		TUniformGrid<float, 3> Grid(BoundingBox.Min(), BoundingBox.Max(), Counts, 1);
		TTriangleMesh<float> CollisionMesh(MoveTemp(Elements));
		FErrorReporter ErrorReporter;
		return TLevelSet<T,3>(ErrorReporter, Grid, SurfaceParticles, CollisionMesh);
	}
	template TLevelSet<float, 3> ConstructLevelset(TParticles<float, 3> &, TArray<TVec3<int32>> &);

	/**/
	template<class T>
	void AppendDynamicParticleConvexBox(TPBDRigidParticleHandle<T, 3> & InParticles, const TVec3<T>& Scale, FReal Margin)
	{
		Chaos::TParticles<T, 3> Cube;
		Cube.AddParticles(9);
		Cube.X(0) = FVec3(-1, -1, -1)*Scale;
		Cube.X(1) = FVec3(-1, -1, 1)*Scale;
		Cube.X(2) = FVec3(-1, 1, -1)*Scale;
		Cube.X(3) = FVec3(-1, 1, 1)*Scale;
		Cube.X(4) = FVec3(1, -1, -1)*Scale;
		Cube.X(5) = FVec3(1, -1, 1)*Scale;
		Cube.X(6) = FVec3(1, 1, -1)*Scale;
		Cube.X(7) = FVec3(1, 1, 1)*Scale;
		Cube.X(8) = FVec3(0, 0, 0);

		InParticles.X() = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.V() = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.R() = TRotation<T, 3>::MakeFromEuler(TVec3<T>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W() = TVec3<T>(0.f, 0.f, 0.f);
		InParticles.P() = InParticles.X();
		InParticles.Q() = InParticles.R();

		// TODO: Change this error prone API to set bounds more automatically. This is easy to forget
		InParticles.SetLocalBounds(TAABB<T, 3>(Cube.X(0), Cube.X(7)));
		InParticles.SetWorldSpaceInflatedBounds(TAABB<T, 3>(Cube.X(0), Cube.X(7)));
		InParticles.SetHasBounds(true);

		InParticles.M() = 1.f;
		InParticles.InvM() = 1.f;
		InParticles.I() = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.InvI() = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.SetDynamicGeometry(MakeUnique<FConvex>(Cube, Margin));
		InParticles.SetObjectStateLowLevel(EObjectStateType::Dynamic);

		::ChaosTest::SetParticleSimDataToCollide({ &InParticles });
		//for (const TUniquePtr<Chaos::FPerShapeData>& Shape : InParticles.ShapesArray())
		//{
		//	Shape->SimData.Word3 = 1;
		//	Shape->SimData.Word1 = 1;
		//}
	}
	template void AppendDynamicParticleConvexBox(TPBDRigidParticleHandle<float, 3> &, const FVec3& Scale, FReal Margin);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleConvexBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVec3<T>& Scale)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		AppendDynamicParticleConvexBox(*Particles[0], Scale, 0.0f);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleConvexBox(TPBDRigidsSOAs<float, 3>& Evolution, const FVec3& Scale);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleConvexBoxMargin(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale, FReal Margin)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		AppendDynamicParticleConvexBox(*Particles[0], Scale, Margin);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleConvexBoxMargin(TPBDRigidsSOAs<float, 3>& Evolution, const TVector<float, 3>& Scale, FReal Margin);

	/**/
	template<class T>
	TVec3<T> ObjectSpacePoint(TPBDRigidParticles<T, 3> & InParticles, const int32 Index, const TVec3<T>& WorldSpacePoint)
	{
		TRigidTransform<T, 3> LocalToWorld(InParticles.X(Index), InParticles.R(Index));
		return LocalToWorld.InverseTransformPosition(WorldSpacePoint);
	}
	template FVec3 ObjectSpacePoint(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const FVec3& WorldSpacePoint);

	template<class T>
	TVec3<T> ObjectSpacePoint(TGeometryParticleHandle<T, 3>& Particle, const TVec3<T>& WorldSpacePoint)
	{
		TRigidTransform<T, 3> LocalToWorld(Particle.X(), Particle.R());
		return LocalToWorld.InverseTransformPosition(WorldSpacePoint);
	}
	template FVec3 ObjectSpacePoint(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const FVec3& WorldSpacePoint);


	/**/
	template<class T>
	T PhiWithNormal(TPBDRigidParticles<T, 3> & InParticles, const int32 Index, const TVec3<T>& WorldSpacePoint, TVec3<T>& Normal)
	{
		TRigidTransform<T, 3>(InParticles.X(Index), InParticles.R(Index));
		TVec3<T> BodySpacePoint = ObjectSpacePoint(InParticles, Index, WorldSpacePoint);
		T LocalPhi = InParticles.Geometry(Index)->PhiWithNormal(BodySpacePoint, Normal);
		Normal = TRigidTransform<T, 3>(InParticles.X(Index), InParticles.R(Index)).TransformVector(Normal);
		return LocalPhi;
	}
	template float PhiWithNormal(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const FVec3& WorldSpacePoint, FVec3& Normal);

	/**/
	template<class T>
	T SignedDistance(TPBDRigidParticles<T, 3> & InParticles, const int32 Index, const TVec3<T>& WorldSpacePoint)
	{
		TVec3<T> Normal;
		return PhiWithNormal(InParticles, Index, WorldSpacePoint, Normal);
	}
	template float SignedDistance(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const FVec3& WorldSpacePoint);


	/**/
	template<class T>
	T PhiWithNormal(TGeometryParticleHandle<T, 3> & Particle, const TVec3<T>& WorldSpacePoint, TVec3<T>& Normal)
	{
		TRigidTransform<T, 3>(Particle.X(), Particle.R());
		TVec3<T> BodySpacePoint = ObjectSpacePoint(Particle, WorldSpacePoint);
		T LocalPhi = Particle.Geometry()->PhiWithNormal(BodySpacePoint, Normal);
		Normal = TRigidTransform<T, 3>(Particle.X(), Particle.R()).TransformVector(Normal);
		return LocalPhi;
	}
	template float PhiWithNormal(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const FVec3& WorldSpacePoint, FVec3& Normal);

	/**/
	template<class T>
	T SignedDistance(TGeometryParticleHandle<T, 3>& Particle, const TVec3<T>& WorldSpacePoint)
	{
		TVec3<T> Normal;
		return PhiWithNormal(Particle, WorldSpacePoint, Normal);
	}
	template float SignedDistance(TGeometryParticleHandle<float, 3> & Particle, const FVec3& WorldSpacePoint);

	/**/
	FVec3 RandAxis()
	{
		for (int32 It = 0; It < 1000; ++It)
		{
			FVec3 Point = FVec3(FMath::RandRange(-1.0f, 1.0f), FMath::RandRange(-1.0f, 1.0f), FMath::RandRange(-1.0f, 1.0f));
			if (Point.Size() > KINDA_SMALL_NUMBER)
			{
				return FVec3(Point.GetSafeNormal());
			}
		}
		return FVec3(FVector::UpVector);
	}

	void SetParticleSimDataToCollide(TArray< Chaos::FGeometryParticle* > ParticleArray)
	{
		for (Chaos::FGeometryParticle* Particle : ParticleArray)
		{
			for (const TUniquePtr<Chaos::FPerShapeData>& Shape :Particle->ShapesArray())
			{
				Shape->ModifySimData([](auto& SimData)
				{
					SimData.Word3 = 1;
					SimData.Word1 = 1;
				});
			}
		}
	}
	void SetParticleSimDataToCollide(TArray< Chaos::TGeometryParticleHandle<float, 3>* > ParticleArray)
	{
		for (Chaos::TGeometryParticleHandle<float, 3>* Particle : ParticleArray)
		{
			for (const TUniquePtr<Chaos::FPerShapeData>& Shape : Particle->ShapesArray())
			{
				Shape->ModifySimData([](auto& SimData)
				{
					SimData.Word3 = 1;
					SimData.Word1 = 1;
				});
			}
		}
	}

	TImplicitObjectScaled<FImplicitConvex3> CreateScaledConvexBox(const FVec3& BoxSize, const FVec3 BoxScale, const FReal Margin)
	{
		const FVec3 HalfSize = 0.5f * BoxSize;

		TArray<FVec3> BoxVerts =
		{
			FVec3(-HalfSize.X, -HalfSize.Y, -HalfSize.Z),
			FVec3(-HalfSize.X,  HalfSize.Y, -HalfSize.Z),
			FVec3(HalfSize.X,  HalfSize.Y, -HalfSize.Z),
			FVec3(HalfSize.X, -HalfSize.Y, -HalfSize.Z),
			FVec3(-HalfSize.X, -HalfSize.Y,  HalfSize.Z),
			FVec3(-HalfSize.X,  HalfSize.Y,  HalfSize.Z),
			FVec3(HalfSize.X,  HalfSize.Y,  HalfSize.Z),
			FVec3(HalfSize.X, -HalfSize.Y,  HalfSize.Z),
		};
		TParticles<FReal, 3> BoxParticles(MoveTemp(BoxVerts));

		TSharedPtr<FImplicitConvex3, ESPMode::ThreadSafe> BoxConvex = MakeShared<FImplicitConvex3, ESPMode::ThreadSafe>(BoxParticles, 0.0f);

		return TImplicitObjectScaled<FImplicitConvex3>(BoxConvex, BoxScale, Margin);
	}
}
