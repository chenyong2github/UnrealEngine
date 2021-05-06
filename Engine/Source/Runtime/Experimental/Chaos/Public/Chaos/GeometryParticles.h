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
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/Framework/PhysicsSolverBase.h"

#ifndef CHAOS_DETERMINISTIC
#define CHAOS_DETERMINISTIC 1
#endif

namespace Chaos
{
	class FConstraintHandle;

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

		void SetMaterial(FMaterialHandle InMaterial)
		{
			Materials.Modify(true, DirtyFlags, Proxy, ShapeIdx, [InMaterial](FMaterialData& Data)
			{
				Data.Materials.Reset(1);
				Data.Materials.Add(InMaterial);
			});
		}

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

		EChaosCollisionTraceFlag GetCollisionTraceType() const { return CollisionData.Read().CollisionTraceType; }
		void SetCollisionTraceType(const EChaosCollisionTraceFlag InTraceFlag)
		{
			CollisionData.Modify(true,DirtyFlags,Proxy, ShapeIdx,[InTraceFlag](FCollisionData& Data){ Data.CollisionTraceType = InTraceFlag; });
		}

		const FCollisionData& GetCollisionData() const { return CollisionData.Read(); }

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

		void ModifyShapeIndex(int32 NewShapeIndex)
		{
			ShapeIdx = NewShapeIndex;
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


	struct FParticleID
	{
		int32 GlobalID;	//Set by global ID system
		int32 LocalID;		//Set by local client. This can only be used in cases where the LocalID will be set in the same way (for example we always spawn N client only particles)

		bool operator<(const FParticleID& Other) const
		{
			if(GlobalID == Other.GlobalID)
			{
				return LocalID < Other.LocalID;
			}
			return GlobalID < Other.GlobalID;
		}

		bool operator==(const FParticleID& Other) const
		{
			return GlobalID == Other.GlobalID && LocalID == Other.LocalID;
		}

		FParticleID()
		: GlobalID(INDEX_NONE)
		, LocalID(INDEX_NONE)
		{
		}
	};


	FORCEINLINE uint32 GetTypeHash(const FParticleID& Unique)
	{
		return ::GetTypeHash(Unique.GlobalID);
	}

	//Holds the data for getting back at the real handle if it's still valid
	//Systems should not use this unless clean-up of direct handle is slow, this uses thread safe shared ptr which is not cheap
	class FWeakParticleHandle
	{
	public:

		FWeakParticleHandle() = default;
		FWeakParticleHandle(TGeometryParticleHandle<FReal,3>* InHandle) : SharedData(MakeShared<FData, ESPMode::ThreadSafe>(FData{InHandle})){}

		//Assumes the weak particle handle has been initialized so SharedData must exist
		TGeometryParticleHandle<FReal,3>* GetHandleUnsafe() const
		{
			return SharedData->Handle;
		}

		TGeometryParticleHandle<FReal,3>* GetHandle() const { return SharedData ? SharedData->Handle : nullptr; }
		void ResetHandle()
		{
			if(SharedData)
			{
				SharedData->Handle = nullptr;
			}
		}

		bool IsInitialized()
		{
			return SharedData != nullptr;
		}

	private:

		struct FData
		{
			TGeometryParticleHandle<FReal,3>* Handle;
		};
		TSharedPtr<FData,ESPMode::ThreadSafe> SharedData;
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
			TArrayCollection::AddArray(&MHasCollision);
			TArrayCollection::AddArray(&MShapesArray);
			TArrayCollection::AddArray(&ImplicitShapeMap);
			TArrayCollection::AddArray(&MLocalBounds);
			TArrayCollection::AddArray(&MWorldSpaceInflatedBounds);
			TArrayCollection::AddArray(&MHasBounds);
			TArrayCollection::AddArray(&MSpatialIdx);
			TArrayCollection::AddArray(&MUserData);
			TArrayCollection::AddArray(&MSyncState);
			TArrayCollection::AddArray(&MWeakParticleHandle);
			TArrayCollection::AddArray(&MParticleConstraints);

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
			, MHasCollision(MoveTemp(Other.MHasCollision))
			, MShapesArray(MoveTemp(Other.MShapesArray))
			, ImplicitShapeMap(MoveTemp(Other.ImplicitShapeMap))
			, MLocalBounds(MoveTemp(Other.MLocalBounds))
			, MWorldSpaceInflatedBounds(MoveTemp(Other.MWorldSpaceInflatedBounds))
			, MHasBounds(MoveTemp(Other.MHasBounds))
			, MSpatialIdx(MoveTemp(Other.MSpatialIdx))
			, MUserData(MoveTemp(Other.MUserData))
			, MSyncState(MoveTemp(Other.MSyncState))
			, MWeakParticleHandle(MoveTemp(Other.MWeakParticleHandle))
			, MParticleConstraints(MoveTemp(Other.MParticleConstraints))

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
			TArrayCollection::AddArray(&MHasCollision);
			TArrayCollection::AddArray(&MShapesArray);
			TArrayCollection::AddArray(&ImplicitShapeMap);
			TArrayCollection::AddArray(&MLocalBounds);
			TArrayCollection::AddArray(&MWorldSpaceInflatedBounds);
			TArrayCollection::AddArray(&MHasBounds);
			TArrayCollection::AddArray(&MSpatialIdx);
			TArrayCollection::AddArray(&MUserData);
			TArrayCollection::AddArray(&MSyncState);
			TArrayCollection::AddArray(&MWeakParticleHandle);
			TArrayCollection::AddArray(&MParticleConstraints);

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
			TArrayCollection::AddArray(&MHasCollision);
			TArrayCollection::AddArray(&MShapesArray);
			TArrayCollection::AddArray(&ImplicitShapeMap);
			TArrayCollection::AddArray(&MLocalBounds);
			TArrayCollection::AddArray(&MWorldSpaceInflatedBounds);
			TArrayCollection::AddArray(&MHasBounds);
			TArrayCollection::AddArray(&MSpatialIdx);
			TArrayCollection::AddArray(&MUserData);
			TArrayCollection::AddArray(&MSyncState);
			TArrayCollection::AddArray(&MWeakParticleHandle);
			TArrayCollection::AddArray(&MParticleConstraints);

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

		FORCEINLINE const TRotation<T, d>& R(const int32 Index) const { return MR[Index]; }
		FORCEINLINE TRotation<T, d>& R(const int32 Index) { return MR[Index]; }

		CHAOS_API FUniqueIdx UniqueIdx(const int32 Index) const { return MUniqueIdx[Index]; }
		CHAOS_API FUniqueIdx& UniqueIdx(const int32 Index) { return MUniqueIdx[Index]; }

		CHAOS_API ESyncState& SyncState(const int32 Index) { return MSyncState[Index].State; }
		CHAOS_API ESyncState SyncState(const int32 Index) const { return MSyncState[Index].State; }

		CHAOS_API TSerializablePtr<FImplicitObject> Geometry(const int32 Index) const { return MGeometry[Index]; }

		CHAOS_API const TUniquePtr<FImplicitObject>& DynamicGeometry(const int32 Index) const { return MDynamicGeometry[Index]; }

		CHAOS_API const TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>& SharedGeometry(const int32 Index) const { return MSharedGeometry[Index]; }

		CHAOS_API bool HasCollision(const int32 Index) const { return MHasCollision[Index]; }
		CHAOS_API bool& HasCollision(const int32 Index) { return MHasCollision[Index]; }

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
			if (InGeometry)
			{
				MHasBounds[Index] = InGeometry->HasBoundingBox();
				MLocalBounds[Index] = InGeometry->BoundingBox();
				// world space inflated bounds needs to take v into account - this is done in integrate for dynamics anyway, so
				// this computation is mainly for statics
				MWorldSpaceInflatedBounds[Index] = MLocalBounds[Index].TransformedAABB(TRigidTransform<FReal, 3>(X(Index), R(Index)));
			}
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
		FORCEINLINE THandleType* Handle(int32 Index) const { return const_cast<THandleType*>(MGeometryParticleHandle[Index].Get()); }

		CHAOS_API void SetHandle(int32 Index, TGeometryParticleHandle<T, d>* Handle);
		
		CHAOS_API TGeometryParticle<T, d>* GTGeometryParticle(const int32 Index) const { return MGeometryParticle[Index]; }
		CHAOS_API TGeometryParticle<T, d>*& GTGeometryParticle(const int32 Index) { return MGeometryParticle[Index]; }

		CHAOS_API FWeakParticleHandle& WeakParticleHandle(const int32 Index)
		{
			FWeakParticleHandle& WeakHandle = MWeakParticleHandle[Index];
			if(WeakHandle.IsInitialized())
			{
				return WeakHandle;
			}

			WeakHandle = FWeakParticleHandle(Handle(Index));
			return WeakHandle;
		}

		CHAOS_API TArray<FConstraintHandle*>& ParticleConstraints(const int32 Index)
		{
			return MParticleConstraints[Index];
		}

		CHAOS_API void AddConstraintHandle(const int32& Index, FConstraintHandle* InConstraintHandle)
		{
			CHAOS_ENSURE(!MParticleConstraints[Index].Contains(InConstraintHandle));
			MParticleConstraints[Index].Add(InConstraintHandle);
		}


		CHAOS_API void RemoveConstraintHandle(const int32& Index, FConstraintHandle* InConstraintHandle)
		{
			MParticleConstraints[Index].RemoveSingleSwap(InConstraintHandle);
			CHAOS_ENSURE(!MParticleConstraints[Index].Contains(InConstraintHandle));
		}
private:
		friend THandleType;
		CHAOS_API void ResetWeakParticleHandle(const int32 Index)
		{
			FWeakParticleHandle& WeakHandle = MWeakParticleHandle[Index];
			if(WeakHandle.IsInitialized())
			{
				return WeakHandle.ResetHandle();
			}
		}
public:

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

		FORCEINLINE EParticleType ParticleType() const { return MParticleType; }

		CHAOS_API const FPerShapeData* GetImplicitShape(int32 Index, const FImplicitObject* InObject)
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


		FORCEINLINE TArray<TRotation<T, d>>& AllR() { return MR; }
		FORCEINLINE TArray<TAABB<T, d>>& AllLocalBounds() { return MLocalBounds; }
		FORCEINLINE TArray<TAABB<T, d>>& AllWorldSpaceInflatedBounds() { return MWorldSpaceInflatedBounds; }
		FORCEINLINE TArray<bool>& AllHasBounds() { return MHasBounds; }

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
		TArrayCollectionArray<bool> MHasCollision;
		TArrayCollectionArray<FShapesArray> MShapesArray;
		TArrayCollectionArray<TMap<const FImplicitObject*, int32>> ImplicitShapeMap;
		TArrayCollectionArray<TAABB<T,d>> MLocalBounds;
		TArrayCollectionArray<TAABB<T, d>> MWorldSpaceInflatedBounds;
		TArrayCollectionArray<bool> MHasBounds;
		TArrayCollectionArray<FSpatialAccelerationIdx> MSpatialIdx;
		TArrayCollectionArray<void*> MUserData;
		TArrayCollectionArray<FSyncState> MSyncState;
		TArrayCollectionArray<FWeakParticleHandle> MWeakParticleHandle;
		TArrayCollectionArray<TArray<FConstraintHandle*> > MParticleConstraints;

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
	void TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>::SetHandle(int32 Index, TGeometryParticleHandle<FReal, 3>* Handle);

	template<>
	TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>* TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>::SerializationFactory(FChaosArchive& Ar, TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>* Particles);

#if PLATFORM_MAC || PLATFORM_LINUX
	extern template class CHAOS_API TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::RigidBodySim>;
	extern template class CHAOS_API TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>;
#else
	extern template class TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::RigidBodySim>;
	extern template class TGeometryParticlesImp<FReal, 3, EGeometryParticlesSimType::Other>;
#endif

}
