// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Particles.h"
#include "Chaos/Rotation.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/Box.h"
#include "Chaos/PhysicalMaterials.h"
#include "UObject/PhysicsObjectVersion.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"
#include "UObject/ExternalPhysicsMaterialCustomObjectVersion.h"
#include "Chaos/Properties.h"

#ifndef CHAOS_DETERMINISTIC
#define CHAOS_DETERMINISTIC 1
#endif

namespace Chaos
{

	/** Data that is associated with geometry. If a union is used an entry is created per internal geometry */
	class CHAOS_API FPerShapeData
	{
	public:

		static constexpr bool AlwaysSerializable = true;
		static TUniquePtr<FPerShapeData> CreatePerShapeData(int32 InShapeIdx);

		~FPerShapeData();
		FPerShapeData(const FPerShapeData& Other) = delete;
		
		void UpdateShapeBounds(const FRigidTransform3& WorldTM);

		static FPerShapeData* SerializationFactory(FChaosArchive& Ar, FPerShapeData*);
		void Serialize(FChaosArchive& Ar);

		void* GetUserData() const { return CollisionData.Read().UserData; }
		void SetUserData(void* InUserData)
		{
			CollisionData.Modify(true, DirtyFlags, Proxy, ShapeIdx, [InUserData](FCollisionData& Data){ Data.UserData = InUserData; });
		}

		const FCollisionFilterData& GetQueryData() const { return CollisionData.Read().QueryData; }
		void SetQueryData(const FCollisionFilterData& InQueryData)
		{
			CollisionData.Modify(true, DirtyFlags, Proxy, ShapeIdx, [InQueryData](FCollisionData& Data){ Data.QueryData = InQueryData; });
		}

		const FCollisionFilterData& GetSimData() const { return CollisionData.Read().SimData; }
		void SetSimData(const FCollisionFilterData& InSimData)
		{
			CollisionData.Modify(true, DirtyFlags, Proxy, ShapeIdx, [InSimData](FCollisionData& Data){ Data.SimData = InSimData; });
		}

		template <typename Lambda>
		void ModifySimData(const Lambda& LambdaFunc)
		{
			CollisionData.Modify(true,DirtyFlags,Proxy, ShapeIdx,[&LambdaFunc](FCollisionData& Data){ LambdaFunc(Data.SimData);});
		}

		TSerializablePtr<FImplicitObject> GetGeometry() const { return Geometry; }
		void SetGeometry(TSerializablePtr<FImplicitObject> InGeometry)
		{
			Geometry = InGeometry;
		}

		const TAABB<FReal,3>& GetWorldSpaceInflatedShapeBounds() const { return WorldSpaceInflatedShapeBounds; }
		void SetWorldSpaceInflatedShapeBounds(const TAABB<FReal,3>& InWorldSpaceInflatedShapeBounds)
		{
			WorldSpaceInflatedShapeBounds = InWorldSpaceInflatedShapeBounds;
		}

		const TArray<FMaterialHandle>& GetMaterials() const { return Materials.Read().Materials; }
		const TArray<FMaterialMaskHandle>& GetMaterialMasks() const { return Materials.Read().MaterialMasks; }
		const TArray<uint32>& GetMaterialMaskMaps() const { return Materials.Read().MaterialMaskMaps; }
		const TArray<FMaterialHandle>& GetMaterialMaskMapMaterials() const { return Materials.Read().MaterialMaskMapMaterials; }

		const FShapeDirtyFlags GetDirtyFlags() const { return DirtyFlags; }

		void SetMaterials(const TArray<FMaterialHandle>& InMaterials)
		{
			Materials.Modify(true,DirtyFlags,Proxy, ShapeIdx,[&InMaterials](FMaterialData& Data)
			{
				Data.Materials = InMaterials;
			});
		}

		void SetMaterialMasks(const TArray<FMaterialMaskHandle>& InMaterialMasks)
		{
			Materials.Modify(true,DirtyFlags,Proxy, ShapeIdx,[&InMaterialMasks](FMaterialData& Data)
			{
				Data.MaterialMasks = InMaterialMasks;
			});
		}

		void SetMaterialMaskMaps(const TArray<uint32>& InMaterialMaskMaps)
		{
			Materials.Modify(true,DirtyFlags,Proxy, ShapeIdx,[&InMaterialMaskMaps](FMaterialData& Data)
			{
				Data.MaterialMaskMaps = InMaterialMaskMaps;
			});
		}

		void SetMaterialMaskMapMaterials(const TArray<FMaterialHandle>& InMaterialMaskMapMaterials)
		{
			Materials.Modify(true,DirtyFlags,Proxy, ShapeIdx,[&InMaterialMaskMapMaterials](FMaterialData& Data)
			{
				Data.MaterialMaskMapMaterials = InMaterialMaskMapMaterials;
			});
		}

		template <typename Lambda>
		void ModifyMaterials(const Lambda& LambdaFunc)
		{
			Materials.Modify(true,DirtyFlags,Proxy, ShapeIdx,[&LambdaFunc](FMaterialData& Data)
			{
				LambdaFunc(Data.Materials);
			});
		}

		template <typename Lambda>
		void ModifyMaterialMasks(const Lambda& LambdaFunc)
		{
			Materials.Modify(true,DirtyFlags,Proxy, ShapeIdx,[&LambdaFunc](FMaterialData& Data)
			{
				LambdaFunc(Data.MaterialMasks);
			});
		}

		template <typename Lambda>
		void ModifyMaterialMaskMaps(const Lambda& LambdaFunc)
		{
			Materials.Modify(true,DirtyFlags,Proxy, ShapeIdx,[&LambdaFunc](FMaterialData& Data)
			{
				LambdaFunc(Data.MaterialMaskMaps);
			});
		}

		template <typename Lambda>
		void ModifyMaterialMaskMapMaterials(const Lambda& LambdaFunc)
		{
			Materials.Modify(true,DirtyFlags,Proxy, ShapeIdx,[&LambdaFunc](FMaterialData& Data)
			{
				LambdaFunc(Data.MaterialMaskMapMaterials);
			});
		}

		bool GetQueryEnabled() const { return CollisionData.Read().bQueryCollision; }
		void SetQueryEnabled(const bool bEnable)
		{
			CollisionData.Modify(true, DirtyFlags, Proxy, ShapeIdx, [bEnable](FCollisionData& Data){ Data.bQueryCollision = bEnable; });
		}

		bool GetSimEnabled() const { return CollisionData.Read().bSimCollision; }
		void SetSimEnabled(const bool bEnable)
		{
			CollisionData.Modify(true, DirtyFlags, Proxy, ShapeIdx, [bEnable](FCollisionData& Data){ Data.bSimCollision = bEnable; });
		}

		/*
		// TODO: Deprecate GetDisable() and SetDisable()!
		bool GetDisable() const { return !CollisionData.Read().bSimCollision; }
		void SetDisable(const bool bDisable)
		{
			CollisionData.Modify(true, DirtyFlags, Proxy, ShapeIdx, [bDisable](FCollisionData& Data){ Data.bSimCollision = !bDisable; });
		}
		*/

		bool GetSimulate() const { return CollisionData.Read().bSimulate; }
		void SetSimulate(const bool bSimulate)
		{
			CollisionData.Modify(true,DirtyFlags,Proxy, ShapeIdx,[bSimulate](FCollisionData& Data){ Data.bSimulate = bSimulate; });
		}

		EChaosCollisionTraceFlag GetCollisionTraceType() const { return CollisionData.Read().CollisionTraceType; }
		void SetCollisionTraceType(const EChaosCollisionTraceFlag InTraceFlag)
		{
			CollisionData.Modify(true,DirtyFlags,Proxy, ShapeIdx,[InTraceFlag](FCollisionData& Data){ Data.CollisionTraceType = InTraceFlag; });
		}

		void SetCollisionData(const FCollisionData& Data)
		{
			CollisionData.Write(Data,true,DirtyFlags,Proxy, ShapeIdx);
		}

		void SetMaterialData(const FMaterialData& Data)
		{
			Materials.Write(Data,true,DirtyFlags,Proxy,ShapeIdx);
		}

		void SyncRemoteData(FDirtyPropertiesManager& Manager, int32 ShapeDataIdx, FShapeDirtyData& RemoteData)
		{
			RemoteData.SetFlags(DirtyFlags);
			CollisionData.SyncRemote(Manager, ShapeDataIdx, RemoteData);
			Materials.SyncRemote(Manager, ShapeDataIdx, RemoteData);
			DirtyFlags.Clear();
		}

		void SetProxy(class IPhysicsProxyBase* InProxy)
		{
			Proxy = InProxy;
			if(Proxy)
			{
				if(DirtyFlags.IsDirty())
				{
					if(FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
					{
						PhysicsSolverBase->AddDirtyProxyShape(Proxy,ShapeIdx);
					}
				}
			}
		}

		int32 GetShapeIndex() const
		{
			return ShapeIdx;
		}

	private:

		class IPhysicsProxyBase* Proxy;
		FShapeDirtyFlags DirtyFlags;
		int32 ShapeIdx;

		TShapeProperty<FCollisionData,EShapeProperty::CollisionData> CollisionData;
		TShapeProperty<FMaterialData,EShapeProperty::Materials> Materials;

		TSerializablePtr<FImplicitObject> Geometry;
		TAABB<FReal,3> WorldSpaceInflatedShapeBounds;
		
		// use CreatePerShapeData
		FPerShapeData(int32 InShapeIdx);
	};

	inline FChaosArchive& operator<<(FChaosArchive& Ar, FPerShapeData& Shape)
	{
		Shape.Serialize(Ar);
		return Ar;
	}

	using FShapesArray = TArray<TUniquePtr<FPerShapeData>, TInlineAllocator<1>>;

	void CHAOS_API UpdateShapesArrayFromGeometry(FShapesArray& ShapesArray, TSerializablePtr<FImplicitObject> Geometry, const FRigidTransform3& ActorTM, IPhysicsProxyBase* Proxy);


#if CHAOS_DETERMINISTIC
	using FParticleID = int32;	//Used to break ties when determinism is needed. Should not be used for anything else
#endif
	//Used for down casting when iterating over multiple SOAs.
	enum class EParticleType : uint8
	{
		Static,
		Kinematic,
		Rigid,
		Clustered,	//only applicable on physics thread side
		StaticMesh,
		SkeletalMesh,
		GeometryCollection
	};

	
	template<class T, int d, EGeometryParticlesSimType SimType>
	class TGeometryParticlesImp : public TParticles<T, d>
	{
	public:

		using TArrayCollection::Size;
		using TParticles<T,d>::X;

		CHAOS_API static TGeometryParticlesImp<T, d, SimType>* SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp < T, d, SimType>* Particles);
		
		CHAOS_API TGeometryParticlesImp()
		    : TParticles<T, d>()
		{
			MParticleType = EParticleType::Static;
			TArrayCollection::AddArray(&MUniqueIdx);
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MSharedGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MParticleIDs);
			TArrayCollection::AddArray(&MShapesArray);
			TArrayCollection::AddArray(&ImplicitShapeMap);
			TArrayCollection::AddArray(&MLocalBounds);
			TArrayCollection::AddArray(&MWorldSpaceInflatedBounds);
			TArrayCollection::AddArray(&MHasBounds);
			TArrayCollection::AddArray(&MSpatialIdx);
			TArrayCollection::AddArray(&MUserData);
#if CHAOS_CHECKED
			TArrayCollection::AddArray(&MDebugName);
#endif

			if (IsRigidBodySim())
			{
				TArrayCollection::AddArray(&MGeometryParticleHandle);
				TArrayCollection::AddArray(&MGeometryParticle);
			}

		}
		TGeometryParticlesImp(const TGeometryParticlesImp<T, d, SimType>& Other) = delete;
		CHAOS_API TGeometryParticlesImp(TGeometryParticlesImp<T, d, SimType>&& Other)
		    : TParticles<T, d>(MoveTemp(Other))
			, MUniqueIdx(MoveTemp(Other.MUniqueIdx))
			, MR(MoveTemp(Other.MR))
			, MGeometry(MoveTemp(Other.MGeometry))
			, MSharedGeometry(MoveTemp(Other.MSharedGeometry))
			, MDynamicGeometry(MoveTemp(Other.MDynamicGeometry))
			, MGeometryParticleHandle(MoveTemp(Other.MGeometryParticleHandle))
			, MGeometryParticle(MoveTemp(Other.MGeometryParticle))
			, MShapesArray(MoveTemp(Other.MShapesArray))
			, ImplicitShapeMap(MoveTemp(Other.ImplicitShapeMap))
			, MLocalBounds(MoveTemp(Other.MLocalBounds))
			, MWorldSpaceInflatedBounds(MoveTemp(Other.MWorldSpaceInflatedBounds))
			, MHasBounds(MoveTemp(Other.MHasBounds))
			, MSpatialIdx(MoveTemp(Other.MSpatialIdx))
			, MUserData(MoveTemp(Other.MUserData))
#if CHAOS_DETERMINISTIC
			, MParticleIDs(MoveTemp(Other.MParticleIDs))
#endif
		{
			MParticleType = EParticleType::Static;
			TArrayCollection::AddArray(&MUniqueIdx);
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MSharedGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MShapesArray);
			TArrayCollection::AddArray(&ImplicitShapeMap);
			TArrayCollection::AddArray(&MLocalBounds);
			TArrayCollection::AddArray(&MWorldSpaceInflatedBounds);
			TArrayCollection::AddArray(&MHasBounds);
			TArrayCollection::AddArray(&MSpatialIdx);
			TArrayCollection::AddArray(&MUserData);
#if CHAOS_DETERMINISTIC
			TArrayCollection::AddArray(&MParticleIDs);
#endif
#if CHAOS_CHECKED
			TArrayCollection::AddArray(&MDebugName);
#endif

			if (IsRigidBodySim())
			{
				TArrayCollection::AddArray(&MGeometryParticleHandle);
				TArrayCollection::AddArray(&MGeometryParticle);
			}
		}

		static constexpr bool IsRigidBodySim() { return SimType == EGeometryParticlesSimType::RigidBodySim; }

		CHAOS_API TGeometryParticlesImp(TParticles<T, d>&& Other)
		    : TParticles<T, d>(MoveTemp(Other))
		{
			MParticleType = EParticleType::Static;
			TArrayCollection::AddArray(&MUniqueIdx);
			TArrayCollection::AddArray(&MR);
			TArrayCollection::AddArray(&MGeometry);
			TArrayCollection::AddArray(&MSharedGeometry);
			TArrayCollection::AddArray(&MDynamicGeometry);
			TArrayCollection::AddArray(&MShapesArray);
			TArrayCollection::AddArray(&ImplicitShapeMap);
			TArrayCollection::AddArray(&MLocalBounds);
			TArrayCollection::AddArray(&MWorldSpaceInflatedBounds);
			TArrayCollection::AddArray(&MHasBounds);
			TArrayCollection::AddArray(&MSpatialIdx);
			TArrayCollection::AddArray(&MUserData);
#if CHAOS_DETERMINISTIC
			TArrayCollection::AddArray(&MParticleIDs);
#endif
#if CHAOS_CHECKED
			TArrayCollection::AddArray(&MDebugName);
#endif

			if (IsRigidBodySim())
			{
				TArrayCollection::AddArray(&MGeometryParticleHandle);
				TArrayCollection::AddArray(&MGeometryParticle);
			}
		}

		CHAOS_API virtual ~TGeometryParticlesImp()
		{}

		CHAOS_API const TRotation<T, d>& R(const int32 Index) const { return MR[Index]; }
		CHAOS_API TRotation<T, d>& R(const int32 Index) { return MR[Index]; }

		CHAOS_API FUniqueIdx UniqueIdx(const int32 Index) const { return MUniqueIdx[Index]; }
		CHAOS_API FUniqueIdx& UniqueIdx(const int32 Index) { return MUniqueIdx[Index]; }

		CHAOS_API void*& UserData(const int32 Index) { return MUserData[Index]; }
		CHAOS_API const void* UserData(const int32 Index) const { return MUserData[Index]; }

		CHAOS_API TSerializablePtr<FImplicitObject> Geometry(const int32 Index) const { return MGeometry[Index]; }

		CHAOS_API const TUniquePtr<FImplicitObject>& DynamicGeometry(const int32 Index) const { return MDynamicGeometry[Index]; }

		CHAOS_API const TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>& SharedGeometry(const int32 Index) const { return MSharedGeometry[Index]; }

		CHAOS_API const FShapesArray& ShapesArray(const int32 Index) const { return MShapesArray[Index]; }

#if CHAOS_DETERMINISTIC
		CHAOS_API FParticleID ParticleID(const int32 Idx) const { return MParticleIDs[Idx]; }
		CHAOS_API FParticleID& ParticleID(const int32 Idx) { return MParticleIDs[Idx]; }
#endif
		// Set a dynamic geometry. Note that X and R must be initialized before calling this function.
		CHAOS_API void SetDynamicGeometry(const int32 Index, TUniquePtr<FImplicitObject>&& InUnique)
		{
			check(!SharedGeometry(Index));	// If shared geometry exists we should not be setting dynamic geometry on top
			MGeometry[Index] = MakeSerializable(InUnique);
			MDynamicGeometry[Index] = MoveTemp(InUnique);
			UpdateShapesArray(Index);
			MapImplicitShapes(Index);
		}

		// Set a shared geometry. Note that X and R must be initialized before calling this function.
		CHAOS_API void SetSharedGeometry(const int32 Index, TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> InShared)
		{
			check(!DynamicGeometry(Index));	// If dynamic geometry exists we should not be setting shared geometry on top
			MGeometry[Index] = MakeSerializable(InShared);
			MSharedGeometry[Index] = InShared;
			UpdateShapesArray(Index);
			MapImplicitShapes(Index);
		}
		
		CHAOS_API void SetGeometry(const int32 Index, TSerializablePtr<FImplicitObject> InGeometry)
		{
			check(!DynamicGeometry(Index));
			check(!SharedGeometry(Index));
			MGeometry[Index] = InGeometry;
			UpdateShapesArray(Index);
			MapImplicitShapes(Index);
		}

		CHAOS_API const TAABB<T,d>& LocalBounds(const int32 Index) const
		{
			return MLocalBounds[Index];
		}

		CHAOS_API TAABB<T, d>& LocalBounds(const int32 Index)
		{
			return MLocalBounds[Index];
		}

		CHAOS_API bool HasBounds(const int32 Index) const
		{
			return MHasBounds[Index];
		}

		CHAOS_API bool& HasBounds(const int32 Index)
		{
			return MHasBounds[Index];
		}

		CHAOS_API FSpatialAccelerationIdx SpatialIdx(const int32 Index) const
		{
			return MSpatialIdx[Index];
		}

		CHAOS_API FSpatialAccelerationIdx& SpatialIdx(const int32 Index)
		{
			return MSpatialIdx[Index];
		}

#if CHAOS_CHECKED
		const FName& DebugName(const int32 Index) const
		{
			return MDebugName[Index];
		}

		FName& DebugName(const int32 Index)
		{
			return MDebugName[Index];
		}
#endif

		CHAOS_API const TAABB<T, d>& WorldSpaceInflatedBounds(const int32 Index) const
		{
			return MWorldSpaceInflatedBounds[Index];
		}

		CHAOS_API void SetWorldSpaceInflatedBounds(const int32 Index, const TAABB<T, d>& Bounds)
		{
			MWorldSpaceInflatedBounds[Index] = Bounds;

			const FShapesArray& Shapes = ShapesArray(Index);
			for (const auto& Shape : Shapes)
			{
				if (Shape->GetGeometry()->HasBoundingBox())
				{
					const TRigidTransform<FReal, 3> ActorTM(X(Index), R(Index));
					Shape->UpdateShapeBounds(ActorTM);
				}
			}
		}

		const TArray<TSerializablePtr<FImplicitObject>>& GetAllGeometry() const { return MGeometry; }

		typedef TGeometryParticleHandle<T, d> THandleType;
		CHAOS_API THandleType* Handle(int32 Index) const { return const_cast<THandleType*>(MGeometryParticleHandle[Index].Get()); }

		CHAOS_API void SetHandle(int32 Index, TGeometryParticleHandle<T, d>* Handle);
		
		CHAOS_API TGeometryParticle<T, d>* GTGeometryParticle(const int32 Index) const { return MGeometryParticle[Index]; }
		CHAOS_API TGeometryParticle<T, d>*& GTGeometryParticle(const int32 Index) { return MGeometryParticle[Index]; }

		FString ToString(int32 index) const
		{
			FString BaseString = TParticles<T, d>::ToString(index);
			return FString::Printf(TEXT("%s, MUniqueIdx:%d MR:%s, MGeometry:%s, IsDynamic:%d"), *BaseString, UniqueIdx(index).Idx, *R(index).ToString(), (Geometry(index) ? *(Geometry(index)->ToString()) : TEXT("none")), (DynamicGeometry(index) != nullptr));
		}

		CHAOS_API virtual void Serialize(FChaosArchive& Ar)
		{
			LLM_SCOPE(ELLMTag::ChaosParticles);
			TParticles<T, d>::Serialize(Ar);
			Ar << MGeometry << MDynamicGeometry << MR;
			Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
			if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::PerShapeData)
			{
				Ar << MShapesArray;

				if(Ar.IsLoading())
				{
					MapImplicitShapes();
				}
			}

			if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::SerializeGTGeometryParticles)
			{
				SerializeGeometryParticleHelper(Ar, this);
			}

			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeParticleBounds)
			{
				TBox<FReal, 3>::SerializeAsAABBs(Ar, MLocalBounds);
				TBox<FReal, 3>::SerializeAsAABBs(Ar, MWorldSpaceInflatedBounds);
				Ar << MHasBounds;

				if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::SerializeShapeWorldSpaceBounds)
				{
					for (int32 Idx = 0; Idx < MShapesArray.Num(); ++Idx)
					{
						SetWorldSpaceInflatedBounds(Idx, MWorldSpaceInflatedBounds[Idx]);
					}
				}
			}
			else
			{
				//just assume all bounds come from geometry (technically wrong for pbd rigids with only sample points, but backwards compat is not that important right now)
				for (int32 Idx = 0; Idx < MGeometry.Num(); ++Idx)
				{
					MHasBounds[Idx] = MGeometry[Idx] && MGeometry[Idx]->HasBoundingBox();
					if (MHasBounds[Idx])
					{
						MLocalBounds[Idx] = MGeometry[Idx]->BoundingBox();
						//ignore velocity too, really just trying to get something reasonable)
						SetWorldSpaceInflatedBounds(Idx, MLocalBounds[Idx].TransformedAABB(TRigidTransform<T,d>(X(Idx), R(Idx))));
					}
				}
			}

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::SpatialIdxSerialized)
			{
				MSpatialIdx.AddZeroed(MGeometry.Num());
			}
			else
			{
				Ar << MSpatialIdx;
			}

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::SerializeHashResult)
			{
				//no longer care about hash so don't read it and don't do anything
			}
		}

		CHAOS_API EParticleType ParticleType() const { return MParticleType; }

		const FPerShapeData* GetImplicitShape(int32 Index, const FImplicitObject* InObject)
		{
			checkSlow(Index >= 0 && Index < ImplicitShapeMap.Num());
			TMap<const FImplicitObject*, int32>& Mapping = ImplicitShapeMap[Index];
			FShapesArray& ShapeArray = MShapesArray[Index];
			int32* ShapeIndex = Mapping.Find(InObject);

			if(ShapeIndex && ShapeArray.IsValidIndex(*ShapeIndex))
			{
				return ShapeArray[*ShapeIndex].Get();
			}

			return nullptr;
		}

	protected:
		EParticleType MParticleType;

	private:
		TArrayCollectionArray<FUniqueIdx> MUniqueIdx;
		TArrayCollectionArray<TRotation<T, d>> MR;
		// MGeometry contains raw ptrs to every entry in both MSharedGeometry and MDynamicGeometry.
		// It may also contain raw ptrs to geometry which is managed outside of Chaos.
		TArrayCollectionArray<TSerializablePtr<FImplicitObject>> MGeometry;
		// MSharedGeometry entries are owned by the solver, shared between *representations* of a particle.
		// This is NOT for sharing geometry resources between particle's A and B, this is for sharing the
		// geometry between particle A's various representations.
		TArrayCollectionArray<TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>> MSharedGeometry;
		// MDynamicGeometry entries are used for geo which is by the evolution. It is not set from the game side.
		TArrayCollectionArray<TUniquePtr<FImplicitObject>> MDynamicGeometry;
		TArrayCollectionArray<TSerializablePtr<TGeometryParticleHandle<T, d>>> MGeometryParticleHandle;
		TArrayCollectionArray<TGeometryParticle<T, d>*> MGeometryParticle;
		TArrayCollectionArray<FShapesArray> MShapesArray;
		TArrayCollectionArray<TMap<const FImplicitObject*, int32>> ImplicitShapeMap;
		TArrayCollectionArray<TAABB<T,d>> MLocalBounds;
		TArrayCollectionArray<TAABB<T, d>> MWorldSpaceInflatedBounds;
		TArrayCollectionArray<bool> MHasBounds;
		TArrayCollectionArray<FSpatialAccelerationIdx> MSpatialIdx;
		TArrayCollectionArray<void*> MUserData;

		void UpdateShapesArray(const int32 Index)
		{
			UpdateShapesArrayFromGeometry(MShapesArray[Index], MGeometry[Index], FRigidTransform3(X(Index), R(Index)), nullptr);
			MapImplicitShapes(Index);
		}

		void MapImplicitShapes();

		void MapImplicitShapes(int32 Index);

		template <typename T2, int d2, EGeometryParticlesSimType SimType2>
		friend class TGeometryParticlesImp;

		CHAOS_API void SerializeGeometryParticleHelper(FChaosArchive& Ar, TGeometryParticlesImp<T, d, EGeometryParticlesSimType::RigidBodySim>* GeometryParticles);
		
		void SerializeGeometryParticleHelper(FChaosArchive& Ar, TGeometryParticlesImp<T, d, EGeometryParticlesSimType::Other>* GeometryParticles)
		{
			check(false);	//cannot serialize this sim type
		}

#if CHAOS_DETERMINISTIC
		TArrayCollectionArray<FParticleID> MParticleIDs;
#endif
#if CHAOS_CHECKED
		TArrayCollectionArray<FName> MDebugName;
#endif
	};

	template <typename T, int d, EGeometryParticlesSimType SimType>
	FChaosArchive& operator<<(FChaosArchive& Ar, TGeometryParticlesImp<T, d, SimType>& Particles)
	{
		Particles.Serialize(Ar);
		return Ar;
	}

	template <>
	void TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>::SetHandle(int32 Index, TGeometryParticleHandle<float, 3>* Handle);

	template<>
	TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>* TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>::SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>* Particles);

#if PLATFORM_MAC || PLATFORM_LINUX
	extern template class CHAOS_API TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::RigidBodySim>;
	extern template class CHAOS_API TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>;
#else
	extern template class TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::RigidBodySim>;
	extern template class TGeometryParticlesImp<float, 3, EGeometryParticlesSimType::Other>;
#endif

}
