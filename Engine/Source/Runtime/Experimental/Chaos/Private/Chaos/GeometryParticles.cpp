// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/GeometryParticles.h"

#include "Chaos/CastingUtilities.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Framework/PhysicsSolverBase.h"

namespace Chaos
{
	TUniquePtr<FPerShapeData> CreatePerShapeDataImpl(const int32 ShapeIndex, TSerializablePtr<FImplicitObject> InGeometry, const bool bAllowCachedLeafInfo)
	{
		if (bAllowCachedLeafInfo)
		{
			return FPerShapeData::CreatePerShapeDataInternal(ShapeIndex, InGeometry);
		}
		else
		{
			return FPerShapeData::CreatePerShapeData(ShapeIndex, InGeometry);
		}
	}

	void UpdatePerShapeDataImpl(TUniquePtr<FPerShapeData>& ShapePtr, TSerializablePtr<FImplicitObject> InGeometry, const bool bAllowCachedLeafInfo)
	{
		if (bAllowCachedLeafInfo)
		{
			FPerShapeData::UpdateGeometryInternal(ShapePtr, InGeometry);
		}
		else
		{
			FPerShapeData::UpdateGeometry(ShapePtr, InGeometry);
		}
	}

	// Creare or reuse the shapes in the shapes array and populate them with the Geometry.
	// If we have a Union it will be unpacked into the ShapesArray.
	// On the Physics Thread we set bAllowCachedLeafInfo which caches the shapes world space state to optimize collision detection,
	// but this flag should not be used on the Game Thread because the extended data is not maintained so some PerShapeData funtions
	// will return unitialized values (E.g., GetLeafWorldTransform).
	void UpdateShapesArrayFromGeometryImpl(
		FShapesArray& ShapesArray, 
		TSerializablePtr<FImplicitObject> Geometry, 
		const FRigidTransform3& ActorTM, 
		IPhysicsProxyBase* Proxy, 
		bool bAllowCachedLeafInfo)
	{
		if(Geometry)
		{
			const int32 OldShapeNum = ShapesArray.Num();
			if(const auto* Union = Geometry->template GetObject<FImplicitObjectUnion>())
			{
				ShapesArray.SetNum(Union->GetObjects().Num());

				for (int32 ShapeIndex = 0; ShapeIndex < ShapesArray.Num(); ++ShapeIndex)
				{
					TSerializablePtr<FImplicitObject> ShapeGeometry = MakeSerializable(Union->GetObjects()[ShapeIndex]);

					if (ShapeIndex >= OldShapeNum)
					{
						// If newly allocated shape, initialize it.
						ShapesArray[ShapeIndex] = CreatePerShapeDataImpl(ShapeIndex, ShapeGeometry, bAllowCachedLeafInfo);
					}
					else if (ShapeGeometry != ShapesArray[ShapeIndex]->GetGeometry())
					{
						// Update geometry pointer if it changed
						UpdatePerShapeDataImpl(ShapesArray[ShapeIndex], ShapeGeometry, bAllowCachedLeafInfo);
					}
				}
			}
			else
			{
				ShapesArray.SetNum(1);
				if (OldShapeNum == 0)
				{
					ShapesArray[0] = CreatePerShapeDataImpl(0, Geometry, bAllowCachedLeafInfo);
				}
				else
				{
					UpdatePerShapeDataImpl(ShapesArray[0], Geometry, bAllowCachedLeafInfo);
				}
			}

			if (Geometry->HasBoundingBox())
			{
				for (auto& Shape : ShapesArray)
				{
					Shape->UpdateShapeBounds(ActorTM, FVec3(0));
				}
			}
		}
		else
		{
			ShapesArray.Reset();
		}

		if(Proxy)
		{
			if(FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
			{
				PhysicsSolverBase->SetNumDirtyShapes(Proxy, ShapesArray.Num());
			}
		}
	}

	void UpdateShapesArrayFromGeometry(
		FShapesArray& ShapesArray,
		TSerializablePtr<FImplicitObject> Geometry,
		const FRigidTransform3& ActorTM,
		IPhysicsProxyBase* Proxy)
	{
		UpdateShapesArrayFromGeometryImpl(ShapesArray, Geometry, ActorTM, Proxy, false);
	}

	// Unwrap transformed shapes
	// @todo(chaos): also unwrap Instanced and Scaled but that requires a lot of knock work because Convexes are usually Instanced or
	// Scaled so the Scale and Margin is stored on the wrapper (the convex itself is shared).
	// - support for Margin as per-shape data passed through the collision functions
	// - support for Scale as per-shape data passed through the collision functions (or ideally in the Transforms)
	const FImplicitObject* GetInnerGeometryInstanceData(const FImplicitObject* Implicit, const FRigidTransform3** OutRelativeTransformPtr)
	{
		if (Implicit != nullptr)
		{
			const EImplicitObjectType ImplicitOuterType = Implicit->GetType();
			if (ImplicitOuterType == TImplicitObjectTransformed<FReal, 3>::StaticType())
			{
				// Transformed Implicit
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit = Implicit->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				if (OutRelativeTransformPtr != nullptr)
				{
					*OutRelativeTransformPtr = &TransformedImplicit->GetTransform();
				}
				return GetInnerGeometryInstanceData(TransformedImplicit->GetTransformedObject(), OutRelativeTransformPtr);
			}
			else if ((uint32)ImplicitOuterType & ImplicitObjectType::IsInstanced)
			{
				// Instanced Implicit
				// Currently we only unwrap instanced TriMesh and Heightfields. Convex is the only other type that will be Instanced,
				// but we do not unwrap Convex because the convex margin is stored in the Instanced so we would need to extract
				// it and pass it on to the collision detection.
				const FImplicitObjectInstanced* Instanced = static_cast<const FImplicitObjectInstanced*>(Implicit);
				EImplicitObjectType InnerType = Instanced->GetInnerObject()->GetType();
				if (InnerType != FImplicitConvex3::StaticType())
				{
					return GetInnerGeometryInstanceData(Instanced->GetInnerObject().Get(), OutRelativeTransformPtr);
				}
			}
			else if ((uint32)ImplicitOuterType & ImplicitObjectType::IsScaled)
			{
				// Scaled Implicit
				//const FImplicitObjectScaled* Scaled = static_cast<const FImplicitObjectScaled*>(Implicit);
				//OutTransform.Scale *= Scaled->GetScale();
				//return GetInnerGeometryInstanceData(Scaled->GetInnerObject().Get(), OutRelativeTransformPtr);
			}
		}
		return Implicit;
	}

	bool PerShapeDataRequiresCachedLeafInfo(const FImplicitObject* Geometry)
	{
		// Unwrap the geometry (if it's Transformed or Instanced)
		const FRigidTransform3* LeafRelativeTransform = nullptr;
		const FImplicitObject* LeafGeometry = GetInnerGeometryInstanceData(Geometry, &LeafRelativeTransform);
		return (LeafGeometry != Geometry) && (LeafRelativeTransform != nullptr);
	}



	FPerShapeData::FPerShapeData(int32 InShapeIdx)
		: bHasCachedLeafInfo(false)
		, Proxy(nullptr)
		, ShapeIdx(InShapeIdx)
		, Geometry()
		, WorldSpaceInflatedShapeBounds(FAABB3(FVec3(0), FVec3(0)))
	{
	}

	FPerShapeData::FPerShapeData(int32 InShapeIdx, TSerializablePtr<FImplicitObject> InGeometry)
		: bHasCachedLeafInfo(false)
		, Proxy(nullptr)
		, ShapeIdx(InShapeIdx)
		, Geometry(InGeometry)
		, WorldSpaceInflatedShapeBounds(FAABB3(FVec3(0), FVec3(0)))
	{
	}

	FPerShapeData::FPerShapeData(FPerShapeData&& Other)
		: bHasCachedLeafInfo(Other.bHasCachedLeafInfo)
		, Proxy(MoveTemp(Other.Proxy))
		, DirtyFlags(MoveTemp(Other.DirtyFlags))
		, ShapeIdx(MoveTemp(Other.ShapeIdx))
		, CollisionData(MoveTemp(Other.CollisionData))
		, Materials(MoveTemp(Other.Materials))
		, Geometry(MoveTemp(Other.Geometry))
		, WorldSpaceInflatedShapeBounds(MoveTemp(Other.WorldSpaceInflatedShapeBounds))
	{
	}

	FPerShapeData::~FPerShapeData()
	{
	}

	TUniquePtr<FPerShapeData> FPerShapeData::CreatePerShapeData(int32 InShapeIdx, TSerializablePtr<FImplicitObject> InGeometry)
	{
		return TUniquePtr<FPerShapeData>(new FPerShapeData(InShapeIdx, InGeometry));
	}

	void FPerShapeData::UpdateGeometry(TUniquePtr<FPerShapeData>& ShapePtr, TSerializablePtr<FImplicitObject> InGeometry)
	{
		ShapePtr->Geometry = InGeometry;
	}

	TUniquePtr<FPerShapeData> FPerShapeData::CreatePerShapeDataInternal(int32 InShapeIdx, TSerializablePtr<FImplicitObject> InGeometry)
	{
		const bool bWantLeafGeometryCache = PerShapeDataRequiresCachedLeafInfo(InGeometry.Get());
		if (bWantLeafGeometryCache)
		{
			return TUniquePtr<FPerShapeData>(new FPerShapeDataCachedLeafInfo(InShapeIdx, InGeometry));
		}
		else
		{
			return TUniquePtr<FPerShapeData>(new FPerShapeData(InShapeIdx, InGeometry));
		}
	}

	void FPerShapeData::UpdateGeometryInternal(TUniquePtr<FPerShapeData>& ShapePtr, TSerializablePtr<FImplicitObject> InGeometry)
	{
		const bool bWantLeafGeometryCache = PerShapeDataRequiresCachedLeafInfo(InGeometry.Get());

		// Do we need to add or remove the cached leaf data? If so this requires we recreate the object
		if (bWantLeafGeometryCache != ShapePtr->HasCachedLeafInfo())
		{
			if (bWantLeafGeometryCache)
			{
				ShapePtr = TUniquePtr<FPerShapeData>(new FPerShapeDataCachedLeafInfo(MoveTemp(*ShapePtr.Get())));
			}
			else
			{
				ShapePtr = TUniquePtr<FPerShapeData>(new FPerShapeData(MoveTemp(*ShapePtr.Get())));
			}
		}

		ShapePtr->Geometry = InGeometry;
	}

	FPerShapeDataCachedLeafInfo* FPerShapeData::AsCachedLeafInfo()
	{
		if (HasCachedLeafInfo())
		{
			return static_cast<FPerShapeDataCachedLeafInfo*>(this);
		}
		return nullptr;
	}

	const FPerShapeDataCachedLeafInfo* FPerShapeData::AsCachedLeafInfo() const
	{
		if (HasCachedLeafInfo())
		{
			return static_cast<const FPerShapeDataCachedLeafInfo*>(this);
		}
		return nullptr;
	}

	void FPerShapeData::UpdateShapeBounds(const FRigidTransform3& WorldTM, const FVec3& BoundsExpansion)
	{
		if (Geometry && Geometry->HasBoundingBox())
		{
			WorldSpaceInflatedShapeBounds = Geometry->CalculateTransformedBounds(WorldTM).ThickenSymmetrically(BoundsExpansion);
		}
	}

	void FPerShapeData::UpdateWorldSpaceState(const FRigidTransform3& WorldTransform, const FVec3& BoundsExpansion)
	{
		FRigidTransform3 LeafWorldTransform = WorldTransform;
		const FRigidTransform3* LeafRelativeTransform = nullptr;
		const FImplicitObject* LeafGeometry = GetInnerGeometryInstanceData(Geometry.Get(), &LeafRelativeTransform);

		// Calculate the leaf world transform if different from particle transform
		if (LeafRelativeTransform != nullptr)
		{
			LeafWorldTransform = FRigidTransform3::MultiplyNoScale(*LeafRelativeTransform, WorldTransform);
		}

		// Store the leaf data if we have a cache
		if (FPerShapeDataCachedLeafInfo* LeafCache = AsCachedLeafInfo())
		{
			LeafCache->SetWorldTransform(LeafWorldTransform);
		}

		// Update the bounds at the world transform
		if ((LeafGeometry != nullptr) && LeafGeometry->HasBoundingBox())
		{
			WorldSpaceInflatedShapeBounds = LeafGeometry->CalculateTransformedBounds(LeafWorldTransform).ThickenSymmetrically(BoundsExpansion);;
		}
	}

	const FImplicitObject* FPerShapeData::GetLeafGeometry() const
	{
		return GetInnerGeometryInstanceData(Geometry.Get(), nullptr);
	}

	FRigidTransform3 FPerShapeData::GetLeafRelativeTransform() const
	{
		const FRigidTransform3* LeafRelativeTransform = nullptr;
		GetInnerGeometryInstanceData(Geometry.Get(), &LeafRelativeTransform);

		if (LeafRelativeTransform != nullptr)
		{
			return *LeafRelativeTransform;
		}
		else
		{
			return FRigidTransform3::Identity;
		}
	}

	FRigidTransform3 FPerShapeData::GetLeafWorldTransform(const FGeometryParticleHandle* Particle) const
	{
		if (const FPerShapeDataCachedLeafInfo* LeafCache = AsCachedLeafInfo())
		{
			return LeafCache->GetWorldTransform();
		}
		else
		{
			FRigidTransform3 LeafWorldTransform = FConstGenericParticleHandle(Particle)->GetTransformPQ();

			const FRigidTransform3* LeafRelativeTransform = nullptr;
			GetInnerGeometryInstanceData(Geometry.Get(), &LeafRelativeTransform);
			if (LeafRelativeTransform != nullptr)
			{
				LeafWorldTransform = FRigidTransform3::MultiplyNoScale(*LeafRelativeTransform, LeafWorldTransform);
			}

			return LeafWorldTransform;
		}
	}
	
	void FPerShapeData::UpdateLeafWorldTransform(FGeometryParticleHandle* Particle)
	{
		if (FPerShapeDataCachedLeafInfo* LeafCache = AsCachedLeafInfo())
		{
			FRigidTransform3 LeafWorldTransform = FConstGenericParticleHandle(Particle)->GetTransformPQ();

			const FRigidTransform3* LeafRelativeTransform = nullptr;
			GetInnerGeometryInstanceData(Geometry.Get(), &LeafRelativeTransform);
			if (LeafRelativeTransform != nullptr)
			{
				LeafWorldTransform = FRigidTransform3::MultiplyNoScale(*LeafRelativeTransform, LeafWorldTransform);
			}

			LeafCache->SetWorldTransform(LeafWorldTransform);
		}
	}

	FPerShapeData* FPerShapeData::SerializationFactory(FChaosArchive& Ar, FPerShapeData*)
	{
		//todo: need to rework serialization for shapes, for now just give them all shape idx 0
		return Ar.IsLoading() ? new FPerShapeData(0) : nullptr;
	}

	void FPerShapeData::Serialize(FChaosArchive& Ar)
	{
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		Ar.UsingCustomVersion(FExternalPhysicsMaterialCustomObjectVersion::GUID);

		Ar << Geometry;
		Ar << CollisionData;
		Ar << Materials;

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeShapeWorldSpaceBounds)
		{
			TBox<FReal,3>::SerializeAsAABB(Ar,WorldSpaceInflatedShapeBounds);
		}
		else
		{
			// This should be set by particle serializing this FPerShapeData.
			WorldSpaceInflatedShapeBounds = FAABB3(FVec3(0.0f, 0.0f, 0.0f), FVec3(0.0f, 0.0f, 0.0f));
		}

	}

	FShapeOrShapesArray::FShapeOrShapesArray(const FGeometryParticleHandle* Particle)
	{
		if (Particle)
		{
			const FImplicitObject* Geometry = Particle->Geometry().Get();
			if (Geometry)
			{
				if (Geometry->IsUnderlyingUnion())
				{
					ShapeArray = &Particle->ShapesArray();
					bIsSingleShape = false;
				}
				else
				{
					Shape = Particle->ShapesArray()[0].Get();
					bIsSingleShape = true;
				}

				return;
			}
		}

		Shape = nullptr;
		bIsSingleShape = true;
	}

	template <typename T, int d, EGeometryParticlesSimType SimType>
	void TGeometryParticlesImp<T, d, SimType>::UpdateShapesArray(const int32 Index)
	{
		UpdateShapesArrayFromGeometryImpl(MShapesArray[Index], MGeometry[Index], FRigidTransform3(X(Index), R(Index)), nullptr, true);
	}

	template <typename T, int d, EGeometryParticlesSimType SimType>
	void TGeometryParticlesImp<T, d, SimType>::SetHandle(int32 Index, FGeometryParticleHandle* Handle)
	{
		Handle->SetSOALowLevel(this);
		MGeometryParticleHandle[Index] = AsAlwaysSerializable(Handle);
	}

	template <>
	void TGeometryParticlesImp<FRealSingle, 3, EGeometryParticlesSimType::Other>::SetHandle(int32 Index, FGeometryParticleHandle* Handle)
	{
		check(false);  // TODO: Implement EGeometryParticlesSimType::Other (cloth) particle serialization
	}

	template <>
	void TGeometryParticlesImp<FRealDouble, 3, EGeometryParticlesSimType::Other>::SetHandle(int32 Index, FGeometryParticleHandle* Handle)
	{
		check(false);  // TODO: Implement EGeometryParticlesSimType::Other (cloth) particle serialization
	}

	template<class T, int d, EGeometryParticlesSimType SimType>
	CHAOS_API TGeometryParticlesImp<T, d, SimType>* TGeometryParticlesImp<T, d, SimType>::SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp<T,d,SimType>* Particles)
	{
		int8 ParticleType = Ar.IsLoading() ? 0 : (int8)Particles->ParticleType();
		Ar << ParticleType;
		switch ((EParticleType)ParticleType)
		{
		case EParticleType::Static: return Ar.IsLoading() ? new TGeometryParticlesImp<T, d, SimType>() : nullptr;
		case EParticleType::Kinematic: return Ar.IsLoading() ? new TKinematicGeometryParticlesImp<T, d, SimType>() : nullptr;
		case EParticleType::Rigid: return Ar.IsLoading() ? new TPBDRigidParticles<T, d>() : nullptr;
		case EParticleType::Clustered: return Ar.IsLoading() ? new TPBDRigidClusteredParticles<T, d>() : nullptr;
		default:
			check(false); return nullptr;
		}
	}
	
	template<>
	TGeometryParticlesImp<FRealSingle, 3, EGeometryParticlesSimType::Other>* TGeometryParticlesImp<FRealSingle, 3, EGeometryParticlesSimType::Other>::SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp<FRealSingle, 3, EGeometryParticlesSimType::Other>* Particles)
	{
		check(false);  // TODO: Implement EGeometryParticlesSimType::Other (cloth) particle serialization
		return nullptr;
	}

	template<>
	TGeometryParticlesImp<FRealDouble, 3, EGeometryParticlesSimType::Other>* TGeometryParticlesImp<FRealDouble, 3, EGeometryParticlesSimType::Other>::SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp<FRealDouble, 3, EGeometryParticlesSimType::Other>* Particles)
	{
		check(false);  // TODO: Implement EGeometryParticlesSimType::Other (cloth) particle serialization
		return nullptr;
	}

	template <typename T, int d, EGeometryParticlesSimType SimType>
	void TGeometryParticlesImp<T, d, SimType>::SerializeGeometryParticleHelper(FChaosArchive& Ar, TGeometryParticlesImp<T, d, EGeometryParticlesSimType::RigidBodySim>* GeometryParticles)
	{
		auto& SerializableGeometryParticles = AsAlwaysSerializableArray(GeometryParticles->MGeometryParticle);
		Ar << SerializableGeometryParticles;
	}

	template class TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::RigidBodySim>;
	template class TGeometryParticlesImp<FRealSingle, 3, EGeometryParticlesSimType::Other>;
	template class TGeometryParticlesImp<FRealDouble, 3, EGeometryParticlesSimType::Other>;
}
