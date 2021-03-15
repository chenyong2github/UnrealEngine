// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/Experimental/ChaosInterfaceUtils.h"

#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/CastingUtilities.h"
#include "Chaos/Convex.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Levelset.h"
#include "Chaos/Sphere.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/UniformGrid.h"

#include "Physics/PhysicsInterfaceTypes.h"

#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/SphylElem.h"
#include "PhysicsEngine/SphereElem.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXIncludes.h"
#endif

#define FORCE_ANALYTICS 0

namespace ChaosInterface
{
	float Chaos_Collision_MarginFraction = -1.0f;
	FAutoConsoleVariableRef CVarChaosCollisionMarginFraction(TEXT("p.Chaos.Collision.MarginFraction"), Chaos_Collision_MarginFraction, TEXT("Override the collision margin fraction set in Physics Settings (if >= 0)"));

	float Chaos_Collision_MarginMax = -1.0f;
	FAutoConsoleVariableRef CVarChaosCollisionMarginMax(TEXT("p.Chaos.Collision.MarginMax"), Chaos_Collision_MarginMax, TEXT("Override the max collision margin set in Physics Settings (if >= 0)"));


	template<class PHYSX_MESH>
	TArray<Chaos::TVec3<int32>> GetMeshElements(const PHYSX_MESH* PhysXMesh)
	{
		check(false);
	}

#if PHYSICS_INTERFACE_PHYSX

	template<>
	TArray<Chaos::TVec3<int32>> GetMeshElements(const physx::PxConvexMesh* PhysXMesh)
	{
		TArray<Chaos::TVec3<int32>> CollisionMeshElements;
#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
		int32 offset = 0;
		int32 NbPolygons = static_cast<int32>(PhysXMesh->getNbPolygons());
		for (int32 i = 0; i < NbPolygons; i++)
		{
			physx::PxHullPolygon Poly;
			bool status = PhysXMesh->getPolygonData(i, Poly);
			const auto Indices = PhysXMesh->getIndexBuffer() + Poly.mIndexBase;

			for (int32 j = 2; j < static_cast<int32>(Poly.mNbVerts); j++)
			{
				CollisionMeshElements.Add(Chaos::TVec3<int32>(Indices[offset], Indices[offset + j], Indices[offset + j - 1]));
			}
		}
#endif
		return CollisionMeshElements;
	}

	template<>
	TArray<Chaos::TVec3<int32>> GetMeshElements(const physx::PxTriangleMesh* PhysXMesh)
	{
		TArray<Chaos::TVec3<int32>> CollisionMeshElements;
		const auto MeshFlags = PhysXMesh->getTriangleMeshFlags();
		for (int32 j = 0; j < static_cast<int32>(PhysXMesh->getNbTriangles()); ++j)
		{
			if (MeshFlags | physx::PxTriangleMeshFlag::e16_BIT_INDICES)
			{
				const physx::PxU16* Indices = reinterpret_cast<const physx::PxU16*>(PhysXMesh->getTriangles());
				CollisionMeshElements.Add(Chaos::TVec3<int32>(Indices[3 * j], Indices[3 * j + 1], Indices[3 * j + 2]));
			}
			else
			{
				const physx::PxU32* Indices = reinterpret_cast<const physx::PxU32*>(PhysXMesh->getTriangles());
				CollisionMeshElements.Add(Chaos::TVec3<int32>(Indices[3 * j], Indices[3 * j + 1], Indices[3 * j + 2]));
			}
		}
		return CollisionMeshElements;
	}

	template<class PHYSX_MESH>
	TUniquePtr<Chaos::FImplicitObject> ConvertPhysXMeshToLevelset(const PHYSX_MESH* PhysXMesh, const FVector& Scale)
	{
#if WITH_CHAOS && !WITH_CHAOS_NEEDS_TO_BE_FIXED
		TArray<Chaos::TVec3<int32>> CollisionMeshElements = GetMeshElements(PhysXMesh);
		Chaos::FParticles CollisionMeshParticles;
		CollisionMeshParticles.AddParticles(PhysXMesh->getNbVertices());
		for (uint32 j = 0; j < CollisionMeshParticles.Size(); ++j)
		{
			const auto& Vertex = PhysXMesh->getVertices()[j];
			CollisionMeshParticles.X(j) = Scale * Chaos::FVec3(Vertex.x, Vertex.y, Vertex.z);
		}
		Chaos::FAABB3 BoundingBox(CollisionMeshParticles.X(0), CollisionMeshParticles.X(0));
		for (uint32 j = 1; j < CollisionMeshParticles.Size(); ++j)
		{
			BoundingBox.GrowToInclude(CollisionMeshParticles.X(j));
		}
#if FORCE_ANALYTICS
		return TUniquePtr<Chaos::FImplicitObject>(new Chaos::TBox<FReal, 3>(BoundingBox));
#else
		int32 MaxAxisSize = 10;
		int32 MaxAxis;
		const auto Extents = BoundingBox.Extents();
		if (Extents[0] > Extents[1] && Extents[0] > Extents[2])
		{
			MaxAxis = 0;
		}
		else if (Extents[1] > Extents[2])
		{
			MaxAxis = 1;
		}
		else
		{
			MaxAxis = 2;
		}
		Chaos::TVec3<int32> Counts(MaxAxisSize * Extents[0] / Extents[MaxAxis], MaxAxisSize * Extents[1] / Extents[MaxAxis], MaxAxisSize * Extents[2] / Extents[MaxAxis]);
		Counts[0] = Counts[0] < 1 ? 1 : Counts[0];
		Counts[1] = Counts[1] < 1 ? 1 : Counts[1];
		Counts[2] = Counts[2] < 1 ? 1 : Counts[2];
		Chaos::TUniformGrid<float, 3> Grid(BoundingBox.Min(), BoundingBox.Max(), Counts, 1);
		Chaos::FTriangleMesh CollisionMesh(MoveTemp(CollisionMeshElements));
		return TUniquePtr<Chaos::FImplicitObject>(new Chaos::FLevelSet(Grid, CollisionMeshParticles, CollisionMesh));
#endif

#else
		return TUniquePtr<Chaos::FImplicitObject>();
#endif // !WITH_CHAOS_NEEDS_TO_BE_FIXED

	}

#endif

	Chaos::EChaosCollisionTraceFlag ConvertCollisionTraceFlag(ECollisionTraceFlag Flag)
	{
		if (Flag == ECollisionTraceFlag::CTF_UseDefault)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseDefault;
		if (Flag == ECollisionTraceFlag::CTF_UseSimpleAndComplex)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseSimpleAndComplex;
		if (Flag == ECollisionTraceFlag::CTF_UseSimpleAsComplex)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseSimpleAsComplex;
		if (Flag == ECollisionTraceFlag::CTF_UseComplexAsSimple)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseComplexAsSimple;
		if (Flag == ECollisionTraceFlag::CTF_MAX)
			return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_MAX;
		ensure(false);
		return Chaos::EChaosCollisionTraceFlag::Chaos_CTF_UseDefault;
	}

	void CreateGeometry(const FGeometryAddParams& InParams, TArray<TUniquePtr<Chaos::FImplicitObject>>& OutGeoms, Chaos::FShapesArray& OutShapes)
	{
		LLM_SCOPE(ELLMTag::ChaosGeometry);
		const FVector& Scale = InParams.Scale;
		TArray<TUniquePtr<Chaos::FImplicitObject>>& Geoms = OutGeoms;
		Chaos::FShapesArray& Shapes = OutShapes;

		ECollisionTraceFlag CollisionTraceType = InParams.CollisionTraceType;
		if (CollisionTraceType == CTF_UseDefault)
		{
			CollisionTraceType = UPhysicsSettings::Get()->DefaultShapeComplexity;
		}

		float CollisionMarginFraction = FMath::Max(0.0f, UPhysicsSettingsCore::Get()->SolverOptions.CollisionMarginFraction);
		float CollisionMarginMax = FMath::Max(0.0f, UPhysicsSettingsCore::Get()->SolverOptions.CollisionMarginMax);

		// Test margins without changing physics settings
		if (Chaos_Collision_MarginFraction >= 0.0f)
		{
			CollisionMarginFraction = Chaos_Collision_MarginFraction;
		}
		if (Chaos_Collision_MarginMax >= 0.0f)
		{
			CollisionMarginMax = Chaos_Collision_MarginMax;
		}

#if WITH_CHAOS
		// Complex as simple should not create simple geometry, unless there is no complex geometry.  Otherwise both get queried against.
		bool bMakeSimpleGeometry = (CollisionTraceType != CTF_UseComplexAsSimple) || (InParams.ChaosTriMeshes.Num() == 0);

		// The reverse is true for Simple as Complex.
		const int32 SimpleShapeCount = InParams.Geometry->SphereElems.Num() + InParams.Geometry->BoxElems.Num() + InParams.Geometry->ConvexElems.Num() + InParams.Geometry->SphylElems.Num();
		bool bMakeComplexGeometry = (CollisionTraceType != CTF_UseSimpleAsComplex) || (SimpleShapeCount == 0);
#else
		bool bMakeSimpleGeometry = true;
		bool bMakeComplexGeometry = true;
#endif

		ensure(bMakeComplexGeometry || bMakeSimpleGeometry);

		auto NewShapeHelper = [&InParams, &CollisionTraceType](Chaos::TSerializablePtr<Chaos::FImplicitObject> InGeom, int32 ShapeIdx, void* UserData, ECollisionEnabled::Type ShapeCollisionEnabled, bool bComplexShape = false)
		{
			TUniquePtr<Chaos::FPerShapeData> NewShape = Chaos::FPerShapeData::CreatePerShapeData(ShapeIdx);
			NewShape->SetGeometry(InGeom);
			NewShape->SetQueryData(bComplexShape ? InParams.CollisionData.CollisionFilterData.QueryComplexFilter : InParams.CollisionData.CollisionFilterData.QuerySimpleFilter);
			NewShape->SetSimData(InParams.CollisionData.CollisionFilterData.SimFilter);
			NewShape->SetCollisionTraceType(ConvertCollisionTraceFlag(CollisionTraceType));
			NewShape->UpdateShapeBounds(InParams.WorldTransform);
			NewShape->SetUserData(UserData);

			// The following does nearly the same thing that happens in UpdatePhysicsFilterData.
			// TODO: Refactor so that this code is not duplicated
			const bool bBodyEnableSim = InParams.CollisionData.CollisionFlags.bEnableSimCollisionSimple || InParams.CollisionData.CollisionFlags.bEnableSimCollisionComplex;
			const bool bBodyEnableQuery = InParams.CollisionData.CollisionFlags.bEnableQueryCollision;
			const bool bShapeEnableSim = ShapeCollisionEnabled == ECollisionEnabled::QueryAndPhysics || ShapeCollisionEnabled == ECollisionEnabled::PhysicsOnly;
			const bool bShapeEnableQuery = ShapeCollisionEnabled == ECollisionEnabled::QueryAndPhysics || ShapeCollisionEnabled == ECollisionEnabled::QueryOnly;
			NewShape->SetSimEnabled(bBodyEnableSim && bShapeEnableSim);
			NewShape->SetQueryEnabled(bBodyEnableQuery && bShapeEnableQuery);

			return NewShape;
		};

		if (bMakeSimpleGeometry)
		{
			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->SphereElems.Num()); ++i)
			{
				const FKSphereElem& SphereElem = InParams.Geometry->SphereElems[i];
				const FKSphereElem ScaledSphereElem = SphereElem.GetFinalScaled(Scale, InParams.LocalTransform);
				const float UseRadius = FMath::Max(ScaledSphereElem.Radius, KINDA_SMALL_NUMBER);
				auto ImplicitSphere = MakeUnique<Chaos::TSphere<Chaos::FReal, 3>>(ScaledSphereElem.Center, UseRadius);
				TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(ImplicitSphere), Shapes.Num(), (void*)SphereElem.GetUserData(), SphereElem.GetCollisionEnabled());
				Shapes.Emplace(MoveTemp(NewShape));
				Geoms.Add(MoveTemp(ImplicitSphere));
			}

			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->BoxElems.Num()); ++i)
			{
				const FKBoxElem& BoxElem = InParams.Geometry->BoxElems[i];
				const FKBoxElem ScaledBoxElem = BoxElem.GetFinalScaled(Scale, InParams.LocalTransform);
				const FTransform& BoxTransform = ScaledBoxElem.GetTransform();
				Chaos::FVec3 HalfExtents = Chaos::FVec3(ScaledBoxElem.X * 0.5f, ScaledBoxElem.Y * 0.5f, ScaledBoxElem.Z * 0.5f);

				HalfExtents.X = FMath::Max(HalfExtents.X, KINDA_SMALL_NUMBER);
				HalfExtents.Y = FMath::Max(HalfExtents.Y, KINDA_SMALL_NUMBER);
				HalfExtents.Z = FMath::Max(HalfExtents.Z, KINDA_SMALL_NUMBER);

				const Chaos::FReal CollisionMargin = FMath::Min(2.0f * HalfExtents.GetMin() * CollisionMarginFraction, CollisionMarginMax);

				// AABB can handle translations internally but if we have a rotation we need to wrap it in a transform
				TUniquePtr<Chaos::FImplicitObject> Implicit;
				if (!BoxTransform.GetRotation().IsIdentity())
				{
					auto ImplicitBox = MakeUnique<Chaos::TBox<Chaos::FReal, 3>>(-HalfExtents, HalfExtents, CollisionMargin);
					Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectTransformed<Chaos::FReal, 3>(MoveTemp(ImplicitBox), BoxTransform));
				}
				else
				{
					Implicit = MakeUnique<Chaos::TBox<Chaos::FReal, 3>>(BoxTransform.GetTranslation() - HalfExtents, BoxTransform.GetTranslation() + HalfExtents, CollisionMargin);
				}

				TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(Implicit),Shapes.Num(), (void*)BoxElem.GetUserData(), BoxElem.GetCollisionEnabled());
				Shapes.Emplace(MoveTemp(NewShape));
				Geoms.Add(MoveTemp(Implicit));
			}
			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->SphylElems.Num()); ++i)
			{
				const FKSphylElem& UnscaledSphyl = InParams.Geometry->SphylElems[i];
				const FKSphylElem ScaledSphylElem = UnscaledSphyl.GetFinalScaled(Scale, InParams.LocalTransform);
				Chaos::FReal HalfHeight = FMath::Max(ScaledSphylElem.Length * 0.5f, KINDA_SMALL_NUMBER);
				const Chaos::FReal Radius = FMath::Max(ScaledSphylElem.Radius, KINDA_SMALL_NUMBER);

				if (HalfHeight < KINDA_SMALL_NUMBER)
				{
					//not a capsule just use a sphere
					auto ImplicitSphere = MakeUnique<Chaos::TSphere<Chaos::FReal, 3>>(ScaledSphylElem.Center, Radius);
					TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(ImplicitSphere),Shapes.Num(), (void*)UnscaledSphyl.GetUserData(), UnscaledSphyl.GetCollisionEnabled());
					Shapes.Emplace(MoveTemp(NewShape));
					Geoms.Add(MoveTemp(ImplicitSphere));

				}
				else
				{
					Chaos::FVec3 HalfExtents = ScaledSphylElem.Rotation.RotateVector(Chaos::FVec3(0, 0, HalfHeight));

					auto ImplicitCapsule = MakeUnique<Chaos::FCapsule>(ScaledSphylElem.Center - HalfExtents, ScaledSphylElem.Center + HalfExtents, Radius);
					TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(ImplicitCapsule),Shapes.Num(), (void*)UnscaledSphyl.GetUserData(), UnscaledSphyl.GetCollisionEnabled());
					Shapes.Emplace(MoveTemp(NewShape));
					Geoms.Add(MoveTemp(ImplicitCapsule));
				}
			}
#if 0
			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->TaperedCapsuleElems.Num()); ++i)
			{
				ensure(FMath::IsNearlyEqual(Scale[0], Scale[1]) && FMath::IsNearlyEqual(Scale[1], Scale[2]));
				const auto& TaperedCapsule = InParams.Geometry->TaperedCapsuleElems[i];
				if (TaperedCapsule.Length == 0)
				{
					Chaos::TSphere<FReal, 3>* ImplicitSphere = new Chaos::TSphere<FReal, 3>(-half_extents, TaperedCapsule.Radius * Scale[0]);
					if (PhysicsProxy) PhysicsProxy->ImplicitObjects_GameThread.Add(ImplicitSphere);
					else if (OutOptShapes) OutOptShapes->Add({ ImplicitSphere,true,true,InActor });
				}
				else
				{
					Chaos::FVec3 half_extents(0, 0, TaperedCapsule.Length / 2 * Scale[0]);
					auto ImplicitCylinder = MakeUnique<Chaos::FCylinder>(-half_extents, half_extents, TaperedCapsule.Radius * Scale[0]);
					if (PhysicsProxy) PhysicsProxy->ImplicitObjects_GameThread.Add(MoveTemp(ImplicitSphere));
					else if (OutOptShapes) OutOptShapes->Add({ ImplicitSphere,true,true,InActor });

					auto ImplicitSphereA = MakeUnique<Chaos::TSphere<FReal, 3>>(-half_extents, TaperedCapsule.Radius * Scale[0]);
					if (PhysicsProxy) PhysicsProxy->ImplicitObjects_GameThread.Add(MoveTemp(ImplicitSphereA));
					else if (OutOptShapes) OutOptShapes->Add({ ImplicitSphereA,true,true,InActor });

					auto ImplicitSphereB = MakeUnique<Chaos::TSphere<FReal, 3>>(half_extents, TaperedCapsule.Radius * Scale[0]);
					if (PhysicsProxy) PhysicsProxy->ImplicitObjects_GameThread.Add(MoveTemp(ImplicitSphereB));
					else if (OutOptShapes) OutOptShapes->Add({ ImplicitSphereB,true,true,InActor });
				}
			}
#endif
#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
			for (uint32 i = 0; i < static_cast<uint32>(InParams.Geometry->ConvexElems.Num()); ++i)
			{
				const FKConvexElem& CollisionBody = InParams.Geometry->ConvexElems[i];
				if (const auto& ConvexImplicit = CollisionBody.GetChaosConvexMesh())
				{
					// Extract the scale from the transform - we have separate wrapper classes for scale versus translate/rotate 
					const FVector NetScale = Scale * InParams.LocalTransform.GetScale3D();
					FTransform ConvexTransform = FTransform(InParams.LocalTransform.GetRotation(), Scale * InParams.LocalTransform.GetLocation(), FVector(1, 1, 1));
					const FVector ScaledSize = (NetScale.GetAbs() * CollisionBody.ElemBox.GetSize());	// Note: Scale can be negative
					const Chaos::FReal CollisionMargin = FMath::Min(ScaledSize.GetMin() * CollisionMarginFraction, CollisionMarginMax);

					// Wrap the convex in a scaled or instanced wrapper depending on scale value, and add a margin
					// NOTE: CollisionMargin is on the Instance/Scaled wrapper, not the inner convex (which is shared and should not have a margin).
					TUniquePtr<Chaos::FImplicitObject> Implicit;
					if (NetScale == FVector(1))
					{
						Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectInstanced<Chaos::FConvex>(ConvexImplicit, CollisionMargin));
					}
					else
					{
						Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectScaled<Chaos::FConvex>(ConvexImplicit, NetScale, CollisionMargin));
					}

					// Wrap the convex in a non-scaled transform if necessary (the scale is pulled out above)
					if (!ConvexTransform.GetTranslation().IsNearlyZero() || !ConvexTransform.GetRotation().IsIdentity())
					{
						Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectTransformed<float, 3>(MoveTemp(Implicit), ConvexTransform));
					}

					TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(Implicit), Shapes.Num(), (void*)CollisionBody.GetUserData(), CollisionBody.GetCollisionEnabled());
					Shapes.Emplace(MoveTemp(NewShape));
					Geoms.Add(MoveTemp(Implicit));
				}
			}
		}

		if (bMakeComplexGeometry)
		{
			for (auto& ChaosTriMesh : InParams.ChaosTriMeshes)
			{
				TUniquePtr<Chaos::FImplicitObject> Implicit;
				if (Scale == FVector(1))
				{
					Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectInstanced<Chaos::FTriangleMeshImplicitObject>(ChaosTriMesh));
				}
				else
				{
					Implicit = TUniquePtr<Chaos::FImplicitObject>(new Chaos::TImplicitObjectScaled<Chaos::FTriangleMeshImplicitObject>(ChaosTriMesh, Scale));
				}

				ChaosTriMesh->SetCullsBackFaceRaycast(!InParams.bDoubleSided);

				TUniquePtr<Chaos::FPerShapeData> NewShape = NewShapeHelper(MakeSerializable(Implicit),Shapes.Num(), nullptr, ECollisionEnabled::QueryAndPhysics, true);
				Shapes.Emplace(MoveTemp(NewShape));
				Geoms.Add(MoveTemp(Implicit));
			}
#endif
		}
#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
		for (const auto& PhysXMesh : InParams.TriMeshes)
		{
			auto Implicit = ConvertPhysXMeshToLevelset(PhysXMesh, Scale);
			auto NewShape = NewShapeHelper(MakeSerializable(Implicit), Shapes.Num(), nullptr, ECollisionEnabled::QueryAndPhysics, true);
			Shapes.Emplace(MoveTemp(NewShape));
			Geoms.Add(MoveTemp(Implicit));

		}
#endif
	}

#if WITH_CHAOS
	bool CalculateMassPropertiesOfImplicitType(
		Chaos::FMassProperties& OutMassProperties,
		const Chaos::FRigidTransform3& WorldTransform,
		const Chaos::FImplicitObject* ImplicitObject,
		Chaos::FReal InDensityKGPerCM)
	{
		using namespace Chaos;

		if (ImplicitObject)
		{
			// Hack to handle Transformed and Scaled<ImplicitObjectTriangleMesh> until CastHelper can properly support transformed
			// Commenting this out temporarily as it breaks vehicles
			/*	if (Chaos::IsScaled(ImplicitObject->GetType(true)) && Chaos::GetInnerType(ImplicitObject->GetType(true)) & Chaos::ImplicitObjectType::TriangleMesh)
				{
					OutMassProperties.Volume = 0.f;
					OutMassProperties.Mass = FLT_MAX;
					OutMassProperties.InertiaTensor = FMatrix33(0, 0, 0);
					OutMassProperties.CenterOfMass = FVector(0);
					OutMassProperties.RotationOfMass = Chaos::FRotation3::FromIdentity();
					return false;
				}
				else if (ImplicitObject->GetType(true) & Chaos::ImplicitObjectType::TriangleMesh)
				{
					OutMassProperties.Volume = 0.f;
					OutMassProperties.Mass = FLT_MAX;
					OutMassProperties.InertiaTensor = FMatrix33(0, 0, 0);
					OutMassProperties.CenterOfMass = FVector(0);
					OutMassProperties.RotationOfMass = Chaos::FRotation3::FromIdentity();
					return false;
				}
			else*/

			Chaos::Utilities::CastHelper(*ImplicitObject, FTransform::Identity, [&OutMassProperties, InDensityKGPerCM](const auto& Object, const auto& LocalTM)
				{
					OutMassProperties.Volume = Object.GetVolume();
					OutMassProperties.Mass = OutMassProperties.Volume * InDensityKGPerCM;
					OutMassProperties.InertiaTensor = Object.GetInertiaTensor(OutMassProperties.Mass);
					OutMassProperties.CenterOfMass = LocalTM.TransformPosition(Object.GetCenterOfMass());
					OutMassProperties.RotationOfMass = LocalTM.GetRotation() * Object.GetRotationOfMass();
				});
		}

		// If the implicit is null, or it is scaled to zero it will have zero volume, mass or inertia
		return (OutMassProperties.Mass > 0);
	}

	void CalculateMassPropertiesFromShapeCollectionImp(
		Chaos::FMassProperties& OutProperties, 
		int32 InNumShapes, 
		Chaos::FReal InDensityKGPerCM,
		const TArray<bool>& bContributesToMass,
		TFunction<Chaos::FPerShapeData* (int32 ShapeIndex)> GetShapeDelegate)
	{
		Chaos::FReal TotalMass = 0;
		Chaos::FReal TotalVolume = 0;
		Chaos::FVec3 TotalCenterOfMass(0);
		TArray< Chaos::FMassProperties > MassPropertiesList;
		for (int32 ShapeIndex = 0; ShapeIndex < InNumShapes; ++ShapeIndex)
		{
			const Chaos::FPerShapeData* Shape = GetShapeDelegate(ShapeIndex);

			const bool bHassMass = (ShapeIndex < bContributesToMass.Num()) ? bContributesToMass[ShapeIndex] : true;
			if (bHassMass)
			{
				if (const Chaos::FImplicitObject* ImplicitObject = Shape->GetGeometry().Get())
				{
					Chaos::FMassProperties MassProperties;
					if (CalculateMassPropertiesOfImplicitType(MassProperties, FTransform::Identity, ImplicitObject, InDensityKGPerCM))
					{
						MassPropertiesList.Add(MassProperties);
						TotalMass += MassProperties.Mass;
						TotalVolume += MassProperties.Volume;
						TotalCenterOfMass += MassProperties.CenterOfMass * MassProperties.Mass;
					}
				}
			}
		}

		Chaos::FMatrix33 Tensor;
		Chaos::FRotation3 RotationOfMass;

		// If no shapes contribute to mass, or they are scaled to zero, we may end up with zero mass here
		if ((TotalMass > 0.f) && (MassPropertiesList.Num() > 0))
		{
			TotalCenterOfMass /= TotalMass;

			// NOTE: CombineWorldSpace returns a world-space inertia with zero rotation, unless there's only one item
			// in the list, in which case it returns it as-is and it's rotation may be non-zero
			Chaos::FMassProperties CombinedMassProperties = Chaos::CombineWorldSpace(MassPropertiesList);
			Tensor = CombinedMassProperties.InertiaTensor;
			RotationOfMass = CombinedMassProperties.RotationOfMass;
		}
		else
		{
			// @todo(chaos): We should support shape-less particles as long as their mass an inertia are set directly
			// For now hard-code a 50cm sphere with density 1g/cc
			Tensor = Chaos::FMatrix33(5.24e5f, 5.24e5f, 5.24e5f);
			RotationOfMass = Chaos::FRotation3::Identity;
			TotalMass = 523.0f;
			TotalVolume = 523000;
		}

		OutProperties.InertiaTensor = Tensor;
		OutProperties.Mass = TotalMass;
		OutProperties.Volume = TotalVolume;
		OutProperties.CenterOfMass = TotalCenterOfMass;
		OutProperties.RotationOfMass = RotationOfMass;
	}


	void CalculateMassPropertiesFromShapeCollection(Chaos::FMassProperties& OutProperties, const TArray<FPhysicsShapeHandle>& InShapes, float InDensityKGPerCM)
	{
		CalculateMassPropertiesFromShapeCollectionImp(
			OutProperties,
			InShapes.Num(),
			InDensityKGPerCM,
			TArray<bool>(),
			[&InShapes](int32 ShapeIndex) { return InShapes[ShapeIndex].Shape; }
		);
	}

	void CalculateMassPropertiesFromShapeCollection(Chaos::FMassProperties& OutProperties, const Chaos::FShapesArray& InShapes, const TArray<bool>& bContributesToMass, float InDensityKGPerCM)
	{
		CalculateMassPropertiesFromShapeCollectionImp(
			OutProperties,
			InShapes.Num(),
			InDensityKGPerCM,
			bContributesToMass,
			[&InShapes](int32 ShapeIndex) { return InShapes[ShapeIndex].Get(); }
		);
	}

#endif // WITH_CHAOS

}