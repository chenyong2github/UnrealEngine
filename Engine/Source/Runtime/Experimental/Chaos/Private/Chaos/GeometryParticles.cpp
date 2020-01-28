// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/GeometryParticles.h"

#include "Chaos/CastingUtilities.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	template <typename T, int d>
	void UpdateShapesArrayFromGeometry(TShapesArray<T, d>& ShapesArray, TSerializablePtr<FImplicitObject> Geometry, const FRigidTransform3& ActorTM)
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
						ShapesArray[ShapeIndex] = TPerShapeData<T, d>::CreatePerShapeData();
					}

					ShapesArray[ShapeIndex]->Geometry = MakeSerializable(Union->GetObjects()[ShapeIndex]);
				}
			}
			else
			{
				ShapesArray.SetNum(1);
				ShapesArray[0] = TPerShapeData<T, d>::CreatePerShapeData();
				ShapesArray[0]->Geometry = Geometry;
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
	}

	template <typename T, int d>
	TPerShapeData<T, d>::TPerShapeData()
		: QueryData()
		, SimData()
		, UserData(nullptr)
		, Geometry()
		, WorldSpaceInflatedShapeBounds(TAABB<FReal, 3>(FVec3(0), FVec3(0)))
		, Materials()
		, MaterialMasks()
		, MaterialMaskMaps()
		, MaterialMaskMapMaterials()
		, bDisable(false)
		, bSimulate(true)
		, CollisionTraceType(EChaosCollisionTraceFlag::Chaos_CTF_UseDefault)
	{
	}

	template <typename T, int d>
	TPerShapeData<T, d>::~TPerShapeData()
	{
	}

	template <typename T, int d>
	TUniquePtr<TPerShapeData<T, d>> TPerShapeData<T, d>::CreatePerShapeData()
	{
		return TUniquePtr<TPerShapeData<T, d>>(new TPerShapeData<T, d>());
	}

	template<typename T, int d>
	void TPerShapeData<T, d>::UpdateShapeBounds(const TRigidTransform<T, d>& WorldTM)
	{
		if (Geometry && Geometry->HasBoundingBox())
		{
			WorldSpaceInflatedShapeBounds = Geometry->BoundingBox().TransformedAABB(WorldTM);
		}
	}

	template <typename T, int d>
	TPerShapeData<T, d>* TPerShapeData<T, d>::SerializationFactory(FChaosArchive& Ar, TPerShapeData<T, d>*)
	{
		return Ar.IsLoading() ? new TPerShapeData<T, d>() : nullptr;
	}

	template <typename T, int d>
	void TPerShapeData<T, d>::Serialize(FChaosArchive& Ar)
	{
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		Ar.UsingCustomVersion(FExternalPhysicsMaterialCustomObjectVersion::GUID);

		Ar << Geometry;
		Ar << QueryData;
		Ar << SimData;

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeShapeWorldSpaceBounds)
		{
			TBox<T, d>::SerializeAsAABB(Ar, WorldSpaceInflatedShapeBounds);
		}
		else
		{
			// This should be set by particle serializing this TPerShapeData.
			WorldSpaceInflatedShapeBounds = TAABB<FReal, 3>(FVec3(0.0f, 0.0f, 0.0f), FVec3(0.0f, 0.0f, 0.0f));
		}

		if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddedMaterialManager)
		{
			Ar << Materials;
		}

		if(Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::AddShapeCollisionDisable)
		{
			Ar << bDisable;
		}

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializePerShapeDataSimulateFlag)
		{
			Ar << bSimulate;
		}

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeCollisionTraceType)
		{
			int32 Data = (int32)CollisionTraceType;
			Ar << Data;
			CollisionTraceType = (EChaosCollisionTraceFlag)Data;
		}

		if (Ar.CustomVer(FExternalPhysicsMaterialCustomObjectVersion::GUID) >= FExternalPhysicsMaterialCustomObjectVersion::AddedMaterialMasks)
		{
			Ar << MaterialMasks << MaterialMaskMaps << MaterialMaskMapMaterials;
		}

	}


	template <typename T, int d, EGeometryParticlesSimType SimType>
	void TGeometryParticlesImp<T, d, SimType>::SetHandle(int32 Index, TGeometryParticleHandle<T, d>* Handle)
	{
		Handle->SetSOALowLevel(this);
		MGeometryParticleHandle[Index] = AsAlwaysSerializable(Handle);
	}

	template <>
	void TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>::SetHandle(int32 Index, TGeometryParticleHandle<float, 3>* Handle)
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
	TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>* TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>::SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>* Particles)
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
		TShapesArray<T, d>& ShapeArray = MShapesArray[Index];
		Mapping.Reset();

		for (int32 ShapeIndex = 0; ShapeIndex < ShapeArray.Num(); ++ ShapeIndex)
		{
			const FImplicitObject* ImplicitObject = ShapeArray[ShapeIndex]->Geometry.Get();
			Mapping.Add(ImplicitObject, ShapeIndex);

			const FImplicitObject* ImplicitChildObject = Utilities::ImplicitChildHelper(ImplicitObject);
			if (ImplicitChildObject != ImplicitObject)
			{
				Mapping.Add(ImplicitChildObject, ShapeIndex);
			}
		}

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



	
	template class TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::RigidBodySim>;
	template class TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>;
	template class TPerShapeData<float, 3>;
	template void UpdateShapesArrayFromGeometry(TShapesArray<float, 3>& ShapesArray, TSerializablePtr<FImplicitObject> Geometry, const FRigidTransform3& ActorTM);
}
