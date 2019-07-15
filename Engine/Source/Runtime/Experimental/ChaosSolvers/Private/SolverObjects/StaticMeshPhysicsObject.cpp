// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SolverObjects/StaticMeshPhysicsObject.h"
#include "PBDRigidsSolver.h"
#include "GeometryCollection/GeometryCollectionCollisionStructureManager.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Sphere.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Serializable.h"
#include "ChaosStats.h"

#if INCLUDE_CHAOS

FStaticMeshPhysicsObject::FStaticMeshPhysicsObject(UObject* InOwner, FCallbackInitFunc InInitFunc, FSyncDynamicFunc InSyncFunc)
	: TSolverObject<FStaticMeshPhysicsObject>(InOwner)
	, bInitializedState(false)
	, RigidBodyId(INDEX_NONE)
	, CenterOfMass(FVector::ZeroVector)
	, Scale(FVector::ZeroVector)
	, SimTransform(FTransform::Identity)
	, InitialiseCallbackParamsFunc(InInitFunc)
	, SyncDynamicTransformFunc(InSyncFunc)
{
	check(IsInGameThread());

	Results.Get(0) = FTransform::Identity;
	Results.Get(1) = FTransform::Identity;
}

void FStaticMeshPhysicsObject::Initialize()
{
	check(IsInGameThread());

	// Safe here - we're not in the solver yet if we're creating callbacks
	Results.Get(0) = FTransform::Identity;
	Results.Get(1) = FTransform::Identity;

	InitialiseCallbackParamsFunc(Parameters);
	Parameters.TargetTransform = &SimTransform;

	Reset();
}

void FStaticMeshPhysicsObject::Reset()
{
	bInitializedState = false;
}

void FStaticMeshPhysicsObject::BufferKinematicUpdate(const FSolverObjectKinematicUpdate& InParamUpdate)
{
	BufferedKinematicUpdate = InParamUpdate;
	bPendingKinematicUpdate = true;
};

bool FStaticMeshPhysicsObject::IsSimulating() const
{
	return Parameters.bSimulating;
}

void FStaticMeshPhysicsObject::UpdateKinematicBodiesCallback(const FParticlesType& Particles, const float Dt, const float Time, FKinematicProxy& Proxy)
{
	bool IsControlled = Parameters.ObjectType == EObjectStateTypeEnum::Chaos_Object_Kinematic;
	if(IsControlled && Parameters.bSimulating)
	{
		bool bFirst = !Proxy.Ids.Num();
		if(bFirst)
		{
			Proxy.Ids.Add(RigidBodyId);
			Proxy.Position.SetNum(1);
			Proxy.NextPosition.SetNum(1);
			Proxy.Rotation.SetNum(1);
			Proxy.NextRotation.SetNum(1);

			const FTransform& Transform = Parameters.InitialTransform;
			Proxy.Position[0] = Chaos::TVector<float, 3>(Transform.GetTranslation());
			Proxy.NextPosition[0] = Proxy.Position[0] + Chaos::TVector<float, 3>(Parameters.InitialLinearVelocity) * Dt;
			Proxy.Rotation[0] = Chaos::TRotation<float, 3>(Transform.GetRotation().GetNormalized());
			Proxy.NextRotation[0] = Proxy.Rotation[0];
		}

		if (bPendingKinematicUpdate)
		{
			Proxy.Position[0] = Particles.X(RigidBodyId);
			Proxy.NextPosition[0] = BufferedKinematicUpdate.NewTransform.GetTranslation();
			Proxy.Rotation[0] = Particles.R(RigidBodyId);
			Proxy.NextRotation[0] = BufferedKinematicUpdate.NewTransform.GetRotation().GetNormalized();

			bPendingKinematicUpdate = false;
		}
	}
}

void FStaticMeshPhysicsObject::StartFrameCallback(const float InDt, const float InTime)
{

}

void FStaticMeshPhysicsObject::EndFrameCallback(const float InDt)
{
	bool IsControlled = Parameters.ObjectType == EObjectStateTypeEnum::Chaos_Object_Kinematic;
	if(Parameters.bSimulating && !IsControlled && Parameters.TargetTransform)
	{
		const Chaos::TPBDRigidParticles<float, 3>& Particles = GetSolver()->GetRigidParticles();

		Parameters.TargetTransform->SetTranslation((FVector)Particles.X(RigidBodyId));
		Parameters.TargetTransform->SetRotation((FQuat)Particles.R(RigidBodyId));
	}
}

void FStaticMeshPhysicsObject::BindParticleCallbackMapping(Chaos::TArrayCollectionArray<SolverObjectWrapper> & SolverObjectReverseMap, Chaos::TArrayCollectionArray<int32> & ParticleIDReverseMap)
{
	if (bInitializedState)
	{
		SolverObjectReverseMap[RigidBodyId] = { this, ESolverObjectType::StaticMeshType };
		ParticleIDReverseMap[RigidBodyId] = 0;
	}
}

void FStaticMeshPhysicsObject::CreateRigidBodyCallback(FParticlesType& Particles)
{
	if(!bInitializedState && Parameters.bSimulating)
	{
		RigidBodyId = Particles.Size();
		Particles.AddParticles(1);

		FBox Bounds(ForceInitToZero);
		if (Parameters.ShapeType == EImplicitTypeEnum::Chaos_Implicit_LevelSet)
		{
			// Build implicit surface
			const ECollisionTypeEnum CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			check(sizeof(Chaos::TVector<float, 3>) == sizeof(FVector));
			TArray<Chaos::TVector<float, 3>> &Vertices = *reinterpret_cast<TArray<Chaos::TVector<float, 3>>*>(&Parameters.MeshVertexPositions);

			Chaos::TParticles<float, 3> MeshParticles(Vertices);

			TUniquePtr<Chaos::TTriangleMesh<float>> TriangleMesh(new Chaos::TTriangleMesh<float>(MoveTemp(Parameters.TriIndices)));
			Chaos::FErrorReporter ErrorReporter(Parameters.Name + " | RigidBodyId: " + FString::FromInt(RigidBodyId));;
			Particles.SetDynamicGeometry(
				RigidBodyId,
				TUniquePtr<Chaos::TImplicitObject<float, 3>>(
					FCollisionStructureManager::NewImplicit(
						ErrorReporter,
						MeshParticles, *TriangleMesh,
						Bounds,
						FVector::Distance(FVector(0.f, 0.f, 0.f), Bounds.GetExtent())*0.5,
						Parameters.MinRes, Parameters.MaxRes, 0.f,
						CollisionType, Parameters.ShapeType)));
			if (!ensure(Parameters.MeshVertexPositions.Num()))
			{
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(0, 0, 0));
			}

			for (const FVector& VertPosition : Parameters.MeshVertexPositions)
			{
				Bounds += VertPosition;
			}
		}
		else if (Parameters.ShapeType == EImplicitTypeEnum::Chaos_Implicit_Sphere)
		{
			Chaos::TSphere<float,3>* Sphere = new Chaos::TSphere<float, 3>(Chaos::TVector<float, 3>(0), Parameters.ShapeParams.SphereRadius);
			const Chaos::TBox<float, 3> BBox = Sphere->BoundingBox();
			Bounds.Min = BBox.Min();
			Bounds.Max = BBox.Max();
			Particles.SetDynamicGeometry(RigidBodyId, TUniquePtr<Chaos::TImplicitObject<float, 3>>(Sphere));
			if (!Parameters.MeshVertexPositions.Num())
			{
				float Radius = Parameters.ShapeParams.SphereRadius;

				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(Radius, 0, 0));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(-Radius, 0, 0));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(0, Radius, Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(0, -Radius, Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(0, -Radius, -Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(0, Radius, -Radius));
			}
		}
		else if (Parameters.ShapeType == EImplicitTypeEnum::Chaos_Implicit_Box)
		{
			Chaos::TVector<float, 3> HalfExtents = Parameters.ShapeParams.BoxExtents / 2;
			Chaos::TBox<float,3>* Box = new Chaos::TBox<float, 3>(-HalfExtents, HalfExtents);
			Bounds.Min = Box->Min();
			Bounds.Max = Box->Max();
			Particles.SetDynamicGeometry(RigidBodyId, TUniquePtr<Chaos::TImplicitObject<float, 3>>(Box));
			if (!Parameters.MeshVertexPositions.Num())
			{
				Chaos::TVector<float, 3> x1(-HalfExtents);
				Chaos::TVector<float, 3> x2(HalfExtents);

				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(x1.X, x1.Y, x1.Z));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(x1.X, x1.Y, x2.Z));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(x1.X, x2.Y, x1.Z));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(x2.X, x1.Y, x1.Z));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(x2.X, x2.Y, x2.Z));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(x2.X, x2.Y, x1.Z));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(x2.X, x1.Y, x2.Z));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(x1.X, x2.Y, x2.Z));
			}
		}
		else if (Parameters.ShapeType == EImplicitTypeEnum::Chaos_Implicit_Capsule)
		{
			Chaos::TVector<float, 3> x1(0, -Parameters.ShapeParams.CapsuleHalfHeightAndRadius.X, 0);
			Chaos::TVector<float, 3> x2(0, Parameters.ShapeParams.CapsuleHalfHeightAndRadius.X, 0);
			Chaos::TCapsule<float>* Capsule = new Chaos::TCapsule<float>(x1, x2, Parameters.ShapeParams.CapsuleHalfHeightAndRadius.Y);
			const Chaos::TBox<float, 3> BBox = Capsule->BoundingBox();
			Bounds.Min = BBox.Min();
			Bounds.Max = BBox.Max();
			Particles.SetDynamicGeometry(RigidBodyId, TUniquePtr<Chaos::TImplicitObject<float, 3>>(Capsule));
			if (!Parameters.MeshVertexPositions.Num())
			{
				float HalfHeight = Parameters.ShapeParams.CapsuleHalfHeightAndRadius.X;
				float Radius = Parameters.ShapeParams.CapsuleHalfHeightAndRadius.Y;

				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(HalfHeight + Radius, 0, 0));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(-HalfHeight - Radius, 0, 0));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(HalfHeight, Radius, Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(HalfHeight, -Radius, Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(HalfHeight, -Radius, -Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(HalfHeight, Radius, -Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(0, Radius, Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(0, -Radius, Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(0, -Radius, -Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(0, Radius, -Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(-HalfHeight, Radius, Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(-HalfHeight, -Radius, Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(-HalfHeight, -Radius, -Radius));
				Parameters.MeshVertexPositions.Add(Chaos::TVector<float, 3>(-HalfHeight, Radius, -Radius));
			}
		}
		else
		{
			Bounds.Min = Chaos::TVector<float, 3>(-KINDA_SMALL_NUMBER);
			Bounds.Max = Chaos::TVector<float, 3>(KINDA_SMALL_NUMBER);
			Particles.SetGeometry(RigidBodyId, Chaos::TSerializablePtr<Chaos::TImplicitObject<float, 3>>());
		}

		FTransform WorldTransform = Parameters.InitialTransform;

		Scale = WorldTransform.GetScale3D();
		CenterOfMass = Bounds.GetCenter();
		Bounds = Bounds.InverseTransformBy(FTransform(CenterOfMass));
		Bounds.Min *= Scale;
		Bounds.Max *= Scale;
		checkSlow((Bounds.Max + Bounds.Min).Size() < KINDA_SMALL_NUMBER);

		Particles.InvM(RigidBodyId) = 0.f;
		ensure(Parameters.Mass >= 0.f);
		Particles.M(RigidBodyId) = Parameters.Mass;
		if(Parameters.Mass < FLT_EPSILON)
		{
			Particles.InvM(RigidBodyId) = 1.f;
		}
		else
		{
			Particles.InvM(RigidBodyId) = 1.f / Parameters.Mass;
		}

		Particles.X(RigidBodyId) = WorldTransform.TransformPosition(CenterOfMass);
		Particles.V(RigidBodyId) = Chaos::TVector<float, 3>(Parameters.InitialLinearVelocity);
		Particles.R(RigidBodyId) = WorldTransform.GetRotation().GetNormalized();
		Particles.W(RigidBodyId) = Chaos::TVector<float, 3>(Parameters.InitialAngularVelocity);
		Particles.P(RigidBodyId) = Particles.X(RigidBodyId);
		Particles.Q(RigidBodyId) = Particles.R(RigidBodyId);

		const FVector SideSquared(Bounds.GetSize()[0] * Bounds.GetSize()[0], Bounds.GetSize()[1] * Bounds.GetSize()[1], Bounds.GetSize()[2] * Bounds.GetSize()[2]);
		const FVector Inertia(Parameters.Mass * (SideSquared.Y + SideSquared.Z) / 12.f, Parameters.Mass * (SideSquared.X + SideSquared.Z) / 12.f, Parameters.Mass * (SideSquared.X + SideSquared.Y) / 12.f);
		Particles.I(RigidBodyId) = Chaos::PMatrix<float, 3, 3>(Inertia.X, 0.f, 0.f, 0.f, Inertia.Y, 0.f, 0.f, 0.f, Inertia.Z);
		Particles.InvI(RigidBodyId) = Chaos::PMatrix<float, 3, 3>(1.f / Inertia.X, 0.f, 0.f, 0.f, 1.f / Inertia.Y, 0.f, 0.f, 0.f, 1.f / Inertia.Z);

		if(Parameters.ObjectType == EObjectStateTypeEnum::Chaos_Object_Sleeping)
		{
			Particles.SetObjectState(RigidBodyId, Chaos::EObjectStateType::Sleeping);
			Particles.SetSleeping(RigidBodyId, true);
		}
		else if(Parameters.ObjectType != EObjectStateTypeEnum::Chaos_Object_Dynamic)
		{
			Particles.InvM(RigidBodyId) = 0.f;
			Particles.InvI(RigidBodyId) = Chaos::PMatrix<float, 3, 3>(0);
			Particles.SetObjectState(RigidBodyId, Chaos::EObjectStateType::Kinematic);
		}
		else
		{
			Particles.SetObjectState(RigidBodyId, Chaos::EObjectStateType::Dynamic);
		}

		if (ensure(Parameters.MeshVertexPositions.Num()))
		{
			TArray<Chaos::TVector<float, 3>> &Vertices = *reinterpret_cast<TArray<Chaos::TVector<float, 3>>*>(&Parameters.MeshVertexPositions);

			// Add collision vertices
			check(Particles.CollisionParticles(RigidBodyId) == nullptr);
			Particles.CollisionParticlesInitIfNeeded(RigidBodyId, &Vertices);
			if (Particles.CollisionParticles(RigidBodyId)->Size())
			{
				Particles.CollisionParticles(RigidBodyId)->UpdateAccelerationStructures();
			}
		}

		GetSolver()->SetPhysicsMaterial(RigidBodyId, Parameters.PhysicalMaterial);

		bInitializedState = true;
	}
}

void FStaticMeshPhysicsObject::ParameterUpdateCallback(FParticlesType& InParticles, const float InTime)
{

}

void FStaticMeshPhysicsObject::DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs)
{

}

void FStaticMeshPhysicsObject::AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex)
{

}

void FStaticMeshPhysicsObject::OnRemoveFromScene()
{
	// Disable the particle we added
	Chaos::FPBDRigidsSolver* CurrSolver = GetSolver();

	if(CurrSolver && RigidBodyId != INDEX_NONE)
	{
		// #BG TODO Special case here because right now we reset/realloc the evolution per geom component
		// in endplay which clears this out. That needs to not happen and be based on world shutdown
		if(CurrSolver->GetRigidParticles().Size() == 0)
		{
			return;
		}

		CurrSolver->GetEvolution()->DisableParticle(RigidBodyId);
		CurrSolver->GetRigidClustering().GetTopLevelClusterParents().Remove(RigidBodyId);
	}
}

void FStaticMeshPhysicsObject::CacheResults()
{
	SCOPE_CYCLE_COUNTER(STAT_CacheResultStaticMesh);
	Results.GetPhysicsDataForWrite() = SimTransform;
}

void FStaticMeshPhysicsObject::FlipCache()
{
	Results.Flip();
}

void FStaticMeshPhysicsObject::SyncToCache()
{
	if(Parameters.ObjectType == EObjectStateTypeEnum::Chaos_Object_Dynamic && Parameters.bSimulating && SyncDynamicTransformFunc)
	{
		// Send transform to update callable
		SyncDynamicTransformFunc(Results.GetGameDataForRead());
	}
}

#endif
