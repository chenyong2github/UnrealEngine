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

		InParticles.X(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.V(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.R(RigidBodyIndex) = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.P(RigidBodyIndex) = InParticles.X(RigidBodyIndex);
		InParticles.Q(RigidBodyIndex) = InParticles.R(RigidBodyIndex);

		InParticles.M(RigidBodyIndex) = 1.f;
		InParticles.InvM(RigidBodyIndex) = 1.f;
		InParticles.I(RigidBodyIndex) = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.InvI(RigidBodyIndex) = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.SetDynamicGeometry(RigidBodyIndex, MakeUnique<TSphere<T, 3>>(TVector<T, 3>(0), Scale));
		InParticles.SetObjectState(RigidBodyIndex, EObjectStateType::Dynamic);

		return RigidBodyIndex;
	}
	template int32 AppendAnalyticSphere(TPBDRigidParticles<float, 3> &, float Scale);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendAnalyticSphere2(TPBDRigidsEvolutionGBF<T, 3>& Evolution, T Scale)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = Evolution.CreateDynamicParticles(1);
		TPBDRigidParticleHandle<T, 3>* Particle = Particles[0];

		Particle->X() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->V() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->P() = Particle->X();
		Particle->Q() = Particle->R();

		Particle->M() = 1.f;
		Particle->InvM() = 1.f;
		Particle->I() = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		Particle->InvI() = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		Particle->SetDynamicGeometry(MakeUnique<TSphere<T, 3>>(TVector<T, 3>(0), Scale));

		return Particle;
	}
	template TPBDRigidParticleHandle<float, 3>* AppendAnalyticSphere2(TPBDRigidsEvolutionGBF<float, 3>& Evolution, float Scale);


	template<class T>
	int32 AppendAnalyticBox(TPBDRigidParticles<T, 3> & InParticles, TVector<T, 3> Scale)
	{
		InParticles.AddParticles(1);
		int32 RigidBodyIndex = InParticles.Size() - 1;

		InParticles.X(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.V(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.R(RigidBodyIndex) = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
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
	template int32 AppendAnalyticBox(TPBDRigidParticles<float, 3> &, TVector<float, 3>);


	template<class T>
	void InitAnalyticBox2(TKinematicGeometryParticleHandle<T, 3>* Particle, TVector<T, 3> Scale)
	{
		Particle->X() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->V() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVector<T, 3>(0.f, 0.f, 0.f);
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
	TKinematicGeometryParticleHandle<T, 3>* AppendKinematicAnalyticBox2(TPBDRigidsEvolutionGBF<T, 3>& Evolution, TVector<T, 3> Scale)
	{
		TArray<TKinematicGeometryParticleHandle<T, 3>*> Particles = Evolution.CreateKinematicParticles(1);
		InitAnalyticBox2(Particles[0], Scale);
		return Particles[0];
	}
	template TKinematicGeometryParticleHandle<float, 3>* AppendKinematicAnalyticBox2(TPBDRigidsEvolutionGBF<float, 3>& Evolution, TVector<float, 3> Scale);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicAnalyticBox2(TPBDRigidsEvolutionGBF<T, 3>& Evolution, TVector<T, 3> Scale)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = Evolution.CreateDynamicParticles(1);
		InitAnalyticBox2(Particles[0], Scale);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicAnalyticBox2(TPBDRigidsEvolutionGBF<float, 3>& Evolution, TVector<float, 3> Scale);


	template<class T>
	int32 AppendParticleBox(TPBDRigidParticles<T, 3> & InParticles, TVector<T, 3> Scale, TArray<TVector<int32, 3>> * elements)
	{
		InParticles.AddParticles(1);
		int32 RigidBodyIndex = InParticles.Size() - 1;

		InParticles.X(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.V(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.R(RigidBodyIndex) = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
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
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, -Scale[1] / 2.f, -Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, -Scale[1] / 2.f, -Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, +Scale[1] / 2.f, -Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, +Scale[1] / 2.f, -Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, -Scale[1] / 2.f, +Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, -Scale[1] / 2.f, +Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, +Scale[1] / 2.f, +Scale[2] / 2.f);
		InParticles.CollisionParticles(RigidBodyIndex)->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, +Scale[1] / 2.f, +Scale[2] / 2.f);

		if (elements != nullptr)
		{
			/*
			//cw
			elements->Add(TVector<int32, 3>(4, 1, 5)); // Front
			elements->Add(TVector<int32, 3>(1, 4, 0));
			elements->Add(TVector<int32, 3>(7, 2, 6)); // Back
			elements->Add(TVector<int32, 3>(2, 7, 3));
			elements->Add(TVector<int32, 3>(6, 0, 4)); // Right
			elements->Add(TVector<int32, 3>(0, 6, 2));
			elements->Add(TVector<int32, 3>(5, 3, 7)); // Left
			elements->Add(TVector<int32, 3>(3, 5, 1));
			elements->Add(TVector<int32, 3>(6, 5, 7)); // Top
			elements->Add(TVector<int32, 3>(5, 6, 4));
			elements->Add(TVector<int32, 3>(0, 2, 1)); // Front
			elements->Add(TVector<int32, 3>(2, 0, 3));
			/**/
			//ccw
			elements->Add(TVector<int32, 3>(1,4,5)); // Front
			elements->Add(TVector<int32, 3>(4,1,0));
			elements->Add(TVector<int32, 3>(2,7,6)); // Back
			elements->Add(TVector<int32, 3>(7,2,3));
			elements->Add(TVector<int32, 3>(0,6,4)); // Right
			elements->Add(TVector<int32, 3>(6,0,2));
			elements->Add(TVector<int32, 3>(3,5,7)); // Left
			elements->Add(TVector<int32, 3>(5,3,1));
			elements->Add(TVector<int32, 3>(5,6,7)); // Top
			elements->Add(TVector<int32, 3>(6,5,4));
			elements->Add(TVector<int32, 3>(2,0,1)); // Front
			elements->Add(TVector<int32, 3>(0,2,3));
		}

		return RigidBodyIndex;
	}
	template int32 AppendParticleBox(TPBDRigidParticles<float, 3> &, TVector<float, 3>, TArray<Chaos::TVector<int32, 3>> *);


	template<class T>
	void InitDynamicParticleBox2(TPBDRigidParticleHandle<T, 3>* Particle, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		Particle->X() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->V() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVector<T, 3>(0.f, 0.f, 0.f);

		Particle->P() = Particle->X();
		Particle->Q() = Particle->R();

		check(Scale.X == Scale.Y && Scale.X == Scale.Z);
		T ScaleSq = Scale.X * Scale.X;
		Particle->M() = 1.f;
		Particle->InvM() = 1.f;
		Particle->I() = PMatrix<T, 3, 3>(ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f);
		Particle->InvI() = PMatrix<T, 3, 3>(6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq);

		Particle->SetDynamicGeometry(MakeUnique<TBox<T, 3>>(-Scale / 2.f, Scale / 2.f));

		int32 CollisionIndex = 0;
		Particle->CollisionParticlesInitIfNeeded();
		Particle->CollisionParticles()->AddParticles(8);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, -Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, -Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, +Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, +Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, -Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, -Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, +Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, +Scale[1] / 2.f, +Scale[2] / 2.f);

		if (OutElements != nullptr)
		{
			/*
			//cw
			OutElements->Add(TVector<int32, 3>(4, 1, 5)); // Front
			OutElements->Add(TVector<int32, 3>(1, 4, 0));
			OutElements->Add(TVector<int32, 3>(7, 2, 6)); // Back
			OutElements->Add(TVector<int32, 3>(2, 7, 3));
			OutElements->Add(TVector<int32, 3>(6, 0, 4)); // Right
			OutElements->Add(TVector<int32, 3>(0, 6, 2));
			OutElements->Add(TVector<int32, 3>(5, 3, 7)); // Left
			OutElements->Add(TVector<int32, 3>(3, 5, 1));
			OutElements->Add(TVector<int32, 3>(6, 5, 7)); // Top
			OutElements->Add(TVector<int32, 3>(5, 6, 4));
			OutElements->Add(TVector<int32, 3>(0, 2, 1)); // Front
			OutElements->Add(TVector<int32, 3>(2, 0, 3));
			/**/
			//ccw
			OutElements->Add(TVector<int32, 3>(1, 4, 5)); // Front
			OutElements->Add(TVector<int32, 3>(4, 1, 0));
			OutElements->Add(TVector<int32, 3>(2, 7, 6)); // Back
			OutElements->Add(TVector<int32, 3>(7, 2, 3));
			OutElements->Add(TVector<int32, 3>(0, 6, 4)); // Right
			OutElements->Add(TVector<int32, 3>(6, 0, 2));
			OutElements->Add(TVector<int32, 3>(3, 5, 7)); // Left
			OutElements->Add(TVector<int32, 3>(5, 3, 1));
			OutElements->Add(TVector<int32, 3>(5, 6, 7)); // Top
			OutElements->Add(TVector<int32, 3>(6, 5, 4));
			OutElements->Add(TVector<int32, 3>(2, 0, 1)); // Front
			OutElements->Add(TVector<int32, 3>(0, 2, 3));
		}
	}

	
	template<class T>
	void InitDynamicParticleSphere2(TPBDRigidParticleHandle<T, 3>* Particle, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements) {
		Particle->X() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->V() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVector<T, 3>(0.f, 0.f, 0.f);

		Particle->P() = Particle->X();
		Particle->Q() = Particle->R();

		check(Scale.X == Scale.Y && Scale.X == Scale.Z);
		T ScaleSq = Scale.X * Scale.X;
		Particle->M() = 1.f;
		Particle->InvM() = 1.f;
		Particle->I() = PMatrix<T, 3, 3>(ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f, 0.f, 0.f, 0.f, ScaleSq / 6.f);
		Particle->InvI() = PMatrix<T, 3, 3>(6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq, 0.f, 0.f, 0.f, 6.f / ScaleSq);

		Particle->SetDynamicGeometry(MakeUnique<TSphere<T, 3>>(TVector<T, 3>(0), Scale.X / 2.f));

		int32 CollisionIndex = 0;
		Particle->CollisionParticlesInitIfNeeded();
		Particle->CollisionParticles()->AddParticles(6);

		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, 0, 0);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, 0, 0);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(0, -Scale[1] / 2.f, 0);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(0, +Scale[1] / 2.f, 0);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(0, 0, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(0, 0, +Scale[2] / 2.f);

		if (OutElements != nullptr)
		{
			/*
			//cw
			OutElements->Add(TVector<int32, 3>(4, 1, 5)); // Front
			OutElements->Add(TVector<int32, 3>(1, 4, 0));
			OutElements->Add(TVector<int32, 3>(7, 2, 6)); // Back
			OutElements->Add(TVector<int32, 3>(2, 7, 3));
			OutElements->Add(TVector<int32, 3>(6, 0, 4)); // Right
			OutElements->Add(TVector<int32, 3>(0, 6, 2));
			OutElements->Add(TVector<int32, 3>(5, 3, 7)); // Left
			OutElements->Add(TVector<int32, 3>(3, 5, 1));
			OutElements->Add(TVector<int32, 3>(6, 5, 7)); // Top
			OutElements->Add(TVector<int32, 3>(5, 6, 4));
			OutElements->Add(TVector<int32, 3>(0, 2, 1)); // Front
			OutElements->Add(TVector<int32, 3>(2, 0, 3));
			/**/
			//ccw
			OutElements->Add(TVector<int32, 3>(1, 4, 5)); // Front
			OutElements->Add(TVector<int32, 3>(4, 1, 0));
			OutElements->Add(TVector<int32, 3>(2, 7, 6)); // Back
			OutElements->Add(TVector<int32, 3>(7, 2, 3));
			OutElements->Add(TVector<int32, 3>(0, 6, 4)); // Right
			OutElements->Add(TVector<int32, 3>(6, 0, 2));
			OutElements->Add(TVector<int32, 3>(3, 5, 7)); // Left
			OutElements->Add(TVector<int32, 3>(5, 3, 1));
			OutElements->Add(TVector<int32, 3>(5, 6, 7)); // Top
			OutElements->Add(TVector<int32, 3>(6, 5, 4));
			OutElements->Add(TVector<int32, 3>(2, 0, 1)); // Front
			OutElements->Add(TVector<int32, 3>(0, 2, 3));
		}
	}


	template<class T>
	void InitDynamicParticleCylinder2(TPBDRigidParticleHandle<T, 3>* Particle, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements, bool Tapered) {
		Particle->X() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->V() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVector<T, 3>(0.f, 0.f, 0.f);

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
			Particle->SetDynamicGeometry(MakeUnique<TTaperedCylinder<T>>(TVector<T, 3>(0, 0, Scale.X / 2.0f), TVector<T, 3>(0, 0, -Scale.X / 2.0f), Scale.X / 2.f, Scale.X / 2.f));
		}
		else 
		{
			Particle->SetDynamicGeometry(MakeUnique<TCylinder<T>>(TVector<T, 3>(0, 0, Scale.X / 2.0f), TVector<T, 3>(0, 0, -Scale.X / 2.0f), Scale.X / 2.f));
		}

		int32 CollisionIndex = 0;
		Particle->CollisionParticlesInitIfNeeded();
		Particle->CollisionParticles()->AddParticles(8);

		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, 0, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(-Scale[0] / 2.f, 0, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, 0, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(+Scale[0] / 2.f, 0, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(0, -Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(0, -Scale[1] / 2.f, -Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(0, +Scale[1] / 2.f, +Scale[2] / 2.f);
		Particle->CollisionParticles()->X(CollisionIndex++) = TVector<T, 3>(0, +Scale[1] / 2.f, -Scale[2] / 2.f);

		if (OutElements != nullptr)
		{
			/*
			//cw
			OutElements->Add(TVector<int32, 3>(4, 1, 5)); // Front
			OutElements->Add(TVector<int32, 3>(1, 4, 0));
			OutElements->Add(TVector<int32, 3>(7, 2, 6)); // Back
			OutElements->Add(TVector<int32, 3>(2, 7, 3));
			OutElements->Add(TVector<int32, 3>(6, 0, 4)); // Right
			OutElements->Add(TVector<int32, 3>(0, 6, 2));
			OutElements->Add(TVector<int32, 3>(5, 3, 7)); // Left
			OutElements->Add(TVector<int32, 3>(3, 5, 1));
			OutElements->Add(TVector<int32, 3>(6, 5, 7)); // Top
			OutElements->Add(TVector<int32, 3>(5, 6, 4));
			OutElements->Add(TVector<int32, 3>(0, 2, 1)); // Front
			OutElements->Add(TVector<int32, 3>(2, 0, 3));
			/**/
			//ccw
			OutElements->Add(TVector<int32, 3>(1, 4, 5)); // Front
			OutElements->Add(TVector<int32, 3>(4, 1, 0));
			OutElements->Add(TVector<int32, 3>(2, 7, 6)); // Back
			OutElements->Add(TVector<int32, 3>(7, 2, 3));
			OutElements->Add(TVector<int32, 3>(0, 6, 4)); // Right
			OutElements->Add(TVector<int32, 3>(6, 0, 2));
			OutElements->Add(TVector<int32, 3>(3, 5, 7)); // Left
			OutElements->Add(TVector<int32, 3>(5, 3, 1));
			OutElements->Add(TVector<int32, 3>(5, 6, 7)); // Top
			OutElements->Add(TVector<int32, 3>(6, 5, 4));
			OutElements->Add(TVector<int32, 3>(2, 0, 1)); // Front
			OutElements->Add(TVector<int32, 3>(0, 2, 3));
		}
	}


	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleBox(TPBDRigidsEvolutionGBF<T, 3>& Evolution, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = Evolution.CreateDynamicParticles(1);
		InitDynamicParticleBox2(Particles[0], Scale, OutElements);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleBox(TPBDRigidsEvolutionGBF<float, 3>& Evolution, const TVector<float, 3>& Scale, TArray<Chaos::TVector<int32, 3>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		InitDynamicParticleBox2(Particles[0], Scale, OutElements);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleBox(TPBDRigidsSOAs<float, 3>& Evolution, const TVector<float, 3>& Scale, TArray<Chaos::TVector<int32, 3>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleSphere(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		InitDynamicParticleSphere2(Particles[0], Scale, OutElements);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleSphere(TPBDRigidsSOAs<float, 3>& Evolution, const TVector<float, 3>& Scale, TArray<Chaos::TVector<int32, 3>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleCylinder(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		InitDynamicParticleCylinder2(Particles[0], Scale, OutElements, false);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleCylinder(TPBDRigidsSOAs<float, 3>& Evolution, const TVector<float, 3>& Scale, TArray<Chaos::TVector<int32, 3>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleTaperedCylinder(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		InitDynamicParticleCylinder2(Particles[0], Scale, OutElements, true);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleTaperedCylinder(TPBDRigidsSOAs<float, 3>& Evolution, const TVector<float, 3>& Scale, TArray<Chaos::TVector<int32, 3>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendClusteredParticleBox(TPBDRigidsEvolutionGBF<T, 3>& Evolution, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		auto Particles = Evolution.CreateClusteredParticles(1);
		InitDynamicParticleBox2(Particles[0], Scale, OutElements);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendClusteredParticleBox(TPBDRigidsEvolutionGBF<float, 3>& Evolution, const TVector<float, 3>& Scale, TArray<Chaos::TVector<int32, 3>>* OutElements);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendClusteredParticleBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		auto Particles = SOAs.CreateClusteredParticles(1);
		InitDynamicParticleBox2(Particles[0], Scale, OutElements);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendClusteredParticleBox(TPBDRigidsSOAs<float, 3>& SOAs, const TVector<float, 3>& Scale, TArray<Chaos::TVector<int32, 3>>* OutElements);

	template<class T>
	void InitStaticParticleBox(TGeometryParticleHandle<T, 3>* Particle, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		Particle->X() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();

		check(Scale.X == Scale.Y && Scale.X == Scale.Z);
		T ScaleSq = Scale.X * Scale.X;

		Particle->SetDynamicGeometry(MakeUnique<TBox<T, 3>>(-Scale / 2.f, Scale / 2.f));

		if (OutElements != nullptr)
		{
			/*
			//cw
			OutElements->Add(TVector<int32, 3>(4, 1, 5)); // Front
			OutElements->Add(TVector<int32, 3>(1, 4, 0));
			OutElements->Add(TVector<int32, 3>(7, 2, 6)); // Back
			OutElements->Add(TVector<int32, 3>(2, 7, 3));
			OutElements->Add(TVector<int32, 3>(6, 0, 4)); // Right
			OutElements->Add(TVector<int32, 3>(0, 6, 2));
			OutElements->Add(TVector<int32, 3>(5, 3, 7)); // Left
			OutElements->Add(TVector<int32, 3>(3, 5, 1));
			OutElements->Add(TVector<int32, 3>(6, 5, 7)); // Top
			OutElements->Add(TVector<int32, 3>(5, 6, 4));
			OutElements->Add(TVector<int32, 3>(0, 2, 1)); // Front
			OutElements->Add(TVector<int32, 3>(2, 0, 3));
			/**/
			//ccw
			OutElements->Add(TVector<int32, 3>(1, 4, 5)); // Front
			OutElements->Add(TVector<int32, 3>(4, 1, 0));
			OutElements->Add(TVector<int32, 3>(2, 7, 6)); // Back
			OutElements->Add(TVector<int32, 3>(7, 2, 3));
			OutElements->Add(TVector<int32, 3>(0, 6, 4)); // Right
			OutElements->Add(TVector<int32, 3>(6, 0, 2));
			OutElements->Add(TVector<int32, 3>(3, 5, 7)); // Left
			OutElements->Add(TVector<int32, 3>(5, 3, 1));
			OutElements->Add(TVector<int32, 3>(5, 6, 7)); // Top
			OutElements->Add(TVector<int32, 3>(6, 5, 4));
			OutElements->Add(TVector<int32, 3>(2, 0, 1)); // Front
			OutElements->Add(TVector<int32, 3>(0, 2, 3));
		}
	}

	template<class T>
	TGeometryParticleHandle<T, 3>* AppendStaticParticleBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale, TArray<TVector<int32, 3>>* OutElements)
	{
		TArray<TGeometryParticleHandle<T, 3>*> Particles = SOAs.CreateStaticParticles(1);
		InitStaticParticleBox(Particles[0], Scale, OutElements);
		return Particles[0];
	}
	template TGeometryParticleHandle<float, 3>* AppendStaticParticleBox(TPBDRigidsSOAs<float, 3>& Evolution, const TVector<float, 3>& Scale, TArray<Chaos::TVector<int32, 3>>* OutElements);

	template<class T>
	int AppendStaticAnalyticFloor(TPBDRigidParticles<T, 3> & InParticles)
	{
		InParticles.AddParticles(1);
		int32 RigidBodyIndex = InParticles.Size() - 1;

		InParticles.X(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.V(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.R(RigidBodyIndex) = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W(RigidBodyIndex) = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.M(RigidBodyIndex) = 1.f;
		InParticles.InvM(RigidBodyIndex) = 0.f;
		InParticles.I(RigidBodyIndex) = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.InvI(RigidBodyIndex) = PMatrix<T, 3, 3>(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
		InParticles.SetDynamicGeometry(RigidBodyIndex, MakeUnique<TPlane<T, 3>>(TVector<T, 3>(0.f, 0.f, 0.f), TVector<T, 3>(0.f, 0.f, 1.f)));
		InParticles.SetObjectState(RigidBodyIndex, EObjectStateType::Kinematic);

		InParticles.P(RigidBodyIndex) = InParticles.X(RigidBodyIndex);
		InParticles.Q(RigidBodyIndex) = InParticles.R(RigidBodyIndex);

		return RigidBodyIndex;
	}
	template int AppendStaticAnalyticFloor(TPBDRigidParticles<float, 3> &);

	template<class T>
	TKinematicGeometryParticleHandle<T, 3>* AppendStaticAnalyticFloor(TPBDRigidsEvolutionGBF<T, 3>& Evolution)
	{
		TArray<TKinematicGeometryParticleHandle<T, 3>*> Particles = Evolution.CreateKinematicParticles(1);
		TKinematicGeometryParticleHandle<T, 3>* Particle = Particles[0];

		Particle->X() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->V() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->SetDynamicGeometry( MakeUnique<TPlane<T, 3>>(TVector<T, 3>(0.f, 0.f, 0.f), TVector<T, 3>(0.f, 0.f, 1.f)));

		return Particle;
	}
	template TKinematicGeometryParticleHandle<float, 3>* AppendStaticAnalyticFloor(TPBDRigidsEvolutionGBF<float, 3>& Evolution);

	template<class T>
	TKinematicGeometryParticleHandle<T, 3>* AppendStaticAnalyticFloor(TPBDRigidsSOAs<T, 3>& SOAs)
	{
		TArray<TKinematicGeometryParticleHandle<T, 3>*> Particles = SOAs.CreateKinematicParticles(1);
		TKinematicGeometryParticleHandle<T, 3>* Particle = Particles[0];

		Particle->X() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->V() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->SetDynamicGeometry(MakeUnique<TPlane<T, 3>>(TVector<T, 3>(0.f, 0.f, 0.f), TVector<T, 3>(0.f, 0.f, 1.f)));

		return Particle;
	}
	template TKinematicGeometryParticleHandle<float, 3>* AppendStaticAnalyticFloor(TPBDRigidsSOAs<float, 3>& Evolution);


	template<class T>
	TKinematicGeometryParticleHandle<T, 3>* AppendStaticConvexFloor(TPBDRigidsSOAs<T, 3>& SOAs)
	{
		TArray<TKinematicGeometryParticleHandle<T, 3>*> Particles = SOAs.CreateKinematicParticles(1);
		TKinematicGeometryParticleHandle<T, 3>* Particle = Particles[0];

		Particle->X() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->V() = TVector<T, 3>(0.f, 0.f, 0.f);
		Particle->R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		Particle->W() = TVector<T, 3>(0.f, 0.f, 0.f);

		Chaos::TParticles<T, 3> Cube;
		Cube.AddParticles(9);
		Cube.X(0) = TVector<float, 3>(-1000, -1000, -20);
		Cube.X(1) = TVector<float, 3>(-1000, -1000, 0);
		Cube.X(2) = TVector<float, 3>(-1000, 1000, -20);
		Cube.X(3) = TVector<float, 3>(-1000, 1000, 0);
		Cube.X(4) = TVector<float, 3>(1000, -1000, -20);
		Cube.X(5) = TVector<float, 3>(1000, -1000, 0);
		Cube.X(6) = TVector<float, 3>(1000, 1000, -20);
		Cube.X(7) = TVector<float, 3>(1000, 1000, 0);
		Cube.X(8) = TVector<float, 3>(0, 0, 0);

		Particle->SetDynamicGeometry(MakeUnique<FConvex>(Cube));

		return Particle;
	}
	template TKinematicGeometryParticleHandle<float, 3>* AppendStaticConvexFloor(TPBDRigidsSOAs<float, 3>& Evolution);

	/**/
	template<class T>
	TLevelSet<T, 3> ConstructLevelset(TParticles<T, 3> & SurfaceParticles, TArray<TVector<int32, 3>> & Elements)
	{
		// build Particles and bounds
		Chaos::TAABB<float, 3> BoundingBox(TVector<T, 3>(0), TVector<T, 3>(0));
		for (int32 CollisionParticleIndex = 0; CollisionParticleIndex < (int32)SurfaceParticles.Size(); CollisionParticleIndex++)
		{
			BoundingBox.GrowToInclude(SurfaceParticles.X(CollisionParticleIndex));
		}

		// build cell domain
		int32 MaxAxisSize = 10;
		int MaxAxis = BoundingBox.LargestAxis();
		TVector<T, 3> Extents = BoundingBox.Extents();
		Chaos::TVector<int32, 3> Counts(MaxAxisSize * Extents[0] / Extents[MaxAxis], MaxAxisSize * Extents[1] / Extents[MaxAxis], MaxAxisSize * Extents[2] / Extents[MaxAxis]);
		Counts[0] = Counts[0] < 1 ? 1 : Counts[0];
		Counts[1] = Counts[1] < 1 ? 1 : Counts[1];
		Counts[2] = Counts[2] < 1 ? 1 : Counts[2];

		TUniformGrid<float, 3> Grid(BoundingBox.Min(), BoundingBox.Max(), Counts, 1);
		TTriangleMesh<float> CollisionMesh(MoveTemp(Elements));
		FErrorReporter ErrorReporter;
		return TLevelSet<T,3>(ErrorReporter, Grid, SurfaceParticles, CollisionMesh);
	}
	template TLevelSet<float, 3> ConstructLevelset(TParticles<float, 3> &, TArray<TVector<int32, 3>> &);

	/**/
	template<class T>
	void AppendDynamicParticleConvexBox(TPBDRigidParticleHandle<T, 3> & InParticles, const TVector<T, 3>& Scale)
	{
		Chaos::TParticles<T, 3> Cube;
		Cube.AddParticles(9);
		Cube.X(0) = TVector<float, 3>(-1, -1, -1)*Scale;
		Cube.X(1) = TVector<float, 3>(-1, -1, 1)*Scale;
		Cube.X(2) = TVector<float, 3>(-1, 1, -1)*Scale;
		Cube.X(3) = TVector<float, 3>(-1, 1, 1)*Scale;
		Cube.X(4) = TVector<float, 3>(1, -1, -1)*Scale;
		Cube.X(5) = TVector<float, 3>(1, -1, 1)*Scale;
		Cube.X(6) = TVector<float, 3>(1, 1, -1)*Scale;
		Cube.X(7) = TVector<float, 3>(1, 1, 1)*Scale;
		Cube.X(8) = TVector<float, 3>(0, 0, 0);

		InParticles.X() = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.V() = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.R() = TRotation<T, 3>::MakeFromEuler(TVector<T, 3>(0.f, 0.f, 0.f)).GetNormalized();
		InParticles.W() = TVector<T, 3>(0.f, 0.f, 0.f);
		InParticles.P() = InParticles.X();
		InParticles.Q() = InParticles.R();

		InParticles.M() = 1.f;
		InParticles.InvM() = 1.f;
		InParticles.I() = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.InvI() = PMatrix<T, 3, 3>(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
		InParticles.SetDynamicGeometry(MakeUnique<FConvex>(Cube));
		InParticles.SetObjectStateLowLevel(EObjectStateType::Dynamic);
	}
	template void AppendDynamicParticleConvexBox(TPBDRigidParticleHandle<float, 3> &, const TVector<float, 3>& Scale);

	template<class T>
	TPBDRigidParticleHandle<T, 3>* AppendDynamicParticleConvexBox(TPBDRigidsSOAs<T, 3>& SOAs, const TVector<T, 3>& Scale)
	{
		TArray<TPBDRigidParticleHandle<T, 3>*> Particles = SOAs.CreateDynamicParticles(1);
		AppendDynamicParticleConvexBox(*Particles[0], Scale);
		return Particles[0];
	}
	template TPBDRigidParticleHandle<float, 3>* AppendDynamicParticleConvexBox(TPBDRigidsSOAs<float, 3>& Evolution, const TVector<float, 3>& Scale);


	/**/
	template<class T>
	TVector<T,3> ObjectSpacePoint(TPBDRigidParticles<T, 3> & InParticles, const int32 Index, const TVector<T, 3>& WorldSpacePoint)
	{
		TRigidTransform<T, 3> LocalToWorld(InParticles.X(Index), InParticles.R(Index));
		return LocalToWorld.InverseTransformPosition(WorldSpacePoint);
	}
	template TVector<float, 3> ObjectSpacePoint(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const TVector <float , 3 > & WorldSpacePoint);

	template<class T>
	TVector<T, 3> ObjectSpacePoint(TGeometryParticleHandle<T, 3>& Particle, const TVector<T, 3>& WorldSpacePoint)
	{
		TRigidTransform<T, 3> LocalToWorld(Particle.X(), Particle.R());
		return LocalToWorld.InverseTransformPosition(WorldSpacePoint);
	}
	template TVector<float, 3> ObjectSpacePoint(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const TVector <float, 3 > & WorldSpacePoint);


	/**/
	template<class T>
	T PhiWithNormal(TPBDRigidParticles<T, 3> & InParticles, const int32 Index, const TVector<T, 3>& WorldSpacePoint, TVector<T, 3>& Normal)
	{
		TRigidTransform<T, 3>(InParticles.X(Index), InParticles.R(Index));
		TVector<T, 3> BodySpacePoint = ObjectSpacePoint(InParticles, Index, WorldSpacePoint);
		T LocalPhi = InParticles.Geometry(Index)->PhiWithNormal(BodySpacePoint, Normal);
		Normal = TRigidTransform<T, 3>(InParticles.X(Index), InParticles.R(Index)).TransformVector(Normal);
		return LocalPhi;
	}
	template float PhiWithNormal(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const TVector<float, 3>& WorldSpacePoint, TVector<float, 3>& Normal);

	/**/
	template<class T>
	T SignedDistance(TPBDRigidParticles<T, 3> & InParticles, const int32 Index, const TVector<T, 3>& WorldSpacePoint)
	{
		TVector<T, 3> Normal;
		return PhiWithNormal(InParticles, Index, WorldSpacePoint, Normal);
	}
	template float SignedDistance(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const TVector<float, 3>& WorldSpacePoint);


	/**/
	template<class T>
	T PhiWithNormal(TGeometryParticleHandle<T, 3> & Particle, const TVector<T, 3>& WorldSpacePoint, TVector<T, 3>& Normal)
	{
		TRigidTransform<T, 3>(Particle.X(), Particle.R());
		TVector<T, 3> BodySpacePoint = ObjectSpacePoint(Particle, WorldSpacePoint);
		T LocalPhi = Particle.Geometry()->PhiWithNormal(BodySpacePoint, Normal);
		Normal = TRigidTransform<T, 3>(Particle.X(), Particle.R()).TransformVector(Normal);
		return LocalPhi;
	}
	template float PhiWithNormal(TPBDRigidParticles<float, 3> & InParticles, const int32 Index, const TVector<float, 3>& WorldSpacePoint, TVector<float, 3>& Normal);

	/**/
	template<class T>
	T SignedDistance(TGeometryParticleHandle<T, 3>& Particle, const TVector<T, 3>& WorldSpacePoint)
	{
		TVector<T, 3> Normal;
		return PhiWithNormal(Particle, WorldSpacePoint, Normal);
	}
	template float SignedDistance(TGeometryParticleHandle<float, 3> & Particle, const TVector<float, 3>& WorldSpacePoint);

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


}
