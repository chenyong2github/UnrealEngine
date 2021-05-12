// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/GeometryParticles.h"

#include "Chaos/CastingUtilities.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Framework/PhysicsSolverBase.h"

namespace Chaos
{
	void UpdateShapesArrayFromGeometry(FShapesArray& ShapesArray, TSerializablePtr<FImplicitObject> Geometry, const FRigidTransform3& ActorTM, IPhysicsProxyBase* Proxy)
	{
		if(Geometry)
		{
			if(const auto* Union = Geometry->template GetObject<FImplicitObjectUnion>())
			{
				const int32 OldShapeNum = ShapesArray.Num();

				ShapesArray.SetNum(Union->GetObjects().Num());

				for (int32 ShapeIndex = 0; ShapeIndex < ShapesArray.Num(); ++ShapeIndex)
				{
					if (ShapeIndex >= OldShapeNum)
					{
						// If newly allocated shape, initialize it.
						ShapesArray[ShapeIndex] = FPerShapeData::CreatePerShapeData(ShapeIndex);
					}

					ShapesArray[ShapeIndex]->SetGeometry(MakeSerializable(Union->GetObjects()[ShapeIndex]));
				}
			}
			else
			{
				ShapesArray.SetNum(1);
				ShapesArray[0] = FPerShapeData::CreatePerShapeData(0);
				ShapesArray[0]->SetGeometry(Geometry);
			}

			if (Geometry->HasBoundingBox())
			{
				for (auto& Shape : ShapesArray)
				{
					Shape->UpdateShapeBounds(ActorTM);
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

	FPerShapeData::FPerShapeData(int32 InShapeIdx)
		: Proxy(nullptr)
		, ShapeIdx(InShapeIdx)
		, Geometry()
		, WorldSpaceInflatedShapeBounds(FAABB3(FVec3(0), FVec3(0)))
	{
	}

	FPerShapeData::~FPerShapeData()
	{
	}

	TUniquePtr<FPerShapeData> FPerShapeData::CreatePerShapeData(int32 ShapeIdx)
	{
		return TUniquePtr<FPerShapeData>(new FPerShapeData(ShapeIdx));
	}

	void FPerShapeData::UpdateShapeBounds(const FRigidTransform3& WorldTM)
	{
		if (Geometry && Geometry->HasBoundingBox())
		{
			SetWorldSpaceInflatedShapeBounds(Geometry->BoundingBox().TransformedAABB(WorldTM));
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
			SetWorldSpaceInflatedShapeBounds(FAABB3(FVec3(0.0f, 0.0f, 0.0f), FVec3(0.0f, 0.0f, 0.0f)));
		}

	}


	template <typename T, int d, EGeometryParticlesSimType SimType>
	void TGeometryParticlesImp<T, d, SimType>::SetHandle(int32 Index, TGeometryParticleHandle<T, d>* Handle)
	{
		Handle->SetSOALowLevel(this);
		MGeometryParticleHandle[Index] = AsAlwaysSerializable(Handle);
	}

	template <>
	void TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>::SetHandle(int32 Index, TGeometryParticleHandle<FReal, 3>* Handle)
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
	TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>* TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>::SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>* Particles)
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

	template <typename T, int d, EGeometryParticlesSimType SimType>
	void TGeometryParticlesImp<T, d, SimType>::MapImplicitShapes()
	{
		int32 NumShapeArrays = MShapesArray.Num();
		ImplicitShapeMap.Resize(NumShapeArrays);
		for (int32 Index = 0; Index < NumShapeArrays; ++Index)
		{
			MapImplicitShapes(Index);
		}
	}

	template <typename T, int d, EGeometryParticlesSimType SimType>
	void TGeometryParticlesImp<T, d, SimType>::MapImplicitShapes(int32 Index)
	{
		checkSlow(Index >= 0 && Index < ImplicitShapeMap.Num());

		TMap<const FImplicitObject*, int32>& Mapping = ImplicitShapeMap[Index];
		FShapesArray& ShapeArray = MShapesArray[Index];
		Mapping.Reset();

		bool bHasCollision = false;
		for (int32 ShapeIndex = 0; ShapeIndex < ShapeArray.Num(); ++ ShapeIndex)
		{
			const FImplicitObject* ImplicitObject = ShapeArray[ShapeIndex]->GetGeometry().Get();
			Mapping.Add(ImplicitObject, ShapeIndex);

			const FImplicitObject* ImplicitChildObject = Utilities::ImplicitChildHelper(ImplicitObject);
			if (ImplicitChildObject != ImplicitObject)
			{
				Mapping.Add(ImplicitChildObject, ShapeIndex);
			}

			const FCollisionData& CollisionData = ShapeArray[ShapeIndex]->GetCollisionData();
			bHasCollision |= CollisionData.HasCollisionData();
		}

		HasCollision(Index) = bHasCollision;

		if (MGeometry[Index])
		{
			int32 CurrentShapeIndex = INDEX_NONE;

			if (const auto* Union = MGeometry[Index]->template GetObject<FImplicitObjectUnion>())
			{
				for (const TUniquePtr<FImplicitObject>& ImplicitObject : Union->GetObjects())
				{
					if (ImplicitObject.Get())
					{
						if (const FImplicitObject* ImplicitChildObject = Utilities::ImplicitChildHelper(ImplicitObject.Get()))
						{
							if (ImplicitShapeMap[Index].Contains(ImplicitObject.Get()))
							{
								ImplicitShapeMap[Index].Add(ImplicitChildObject, CopyTemp(ImplicitShapeMap[Index][ImplicitObject.Get()]));
							}
							else if (ImplicitShapeMap[Index].Contains(ImplicitChildObject))
							{
								ImplicitShapeMap[Index].Add(ImplicitObject.Get(), CopyTemp(ImplicitShapeMap[Index][ImplicitChildObject]));
							}
							
						}
					}
				}
			}
			else
			{
				if (const FImplicitObject* ImplicitChildObject = Utilities::ImplicitChildHelper(MGeometry[Index].Get()))
				{
					if (ImplicitShapeMap[Index].Contains(MGeometry[Index].Get()))
					{
						ImplicitShapeMap[Index].Add(ImplicitChildObject, CopyTemp(ImplicitShapeMap[Index][MGeometry[Index].Get()]));
					}
					else if (ImplicitShapeMap[Index].Contains(ImplicitChildObject))
					{
						ImplicitShapeMap[Index].Add(MGeometry[Index].Get(), CopyTemp(ImplicitShapeMap[Index][ImplicitChildObject]));
					}
					
				}
			}

		}
	}



	
	template class TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::RigidBodySim>;
	template class TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>;
}
