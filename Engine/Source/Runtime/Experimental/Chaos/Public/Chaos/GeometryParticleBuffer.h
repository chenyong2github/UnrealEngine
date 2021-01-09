// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Framework/PhysicsProxyBase.h"

namespace Chaos
{

class FPhysicsSolverBase;

class FGeometryParticleBuffer
{
public:
	FGeometryParticleBuffer(const TGeometryParticleParameters<FReal, 3>& StaticParams = TGeometryParticleParameters<FReal, 3>())
	{
		Type = EParticleType::Static;
		MUserData = nullptr;
		Proxy = nullptr;
		GeometryParticleDefaultConstruct<FReal, 3>(*this, StaticParams);
	}

	virtual ~FGeometryParticleBuffer() = default;

	FGeometryParticleBuffer(const FGeometryParticleBuffer&) = delete;

	FGeometryParticleBuffer& operator=(const FGeometryParticleBuffer&) = delete;

	virtual bool IsParticleValid() const
	{
		auto& Geometry = MNonFrequentData.Read().Geometry();
		return Geometry && Geometry->IsValidGeometry();	//todo: if we want support for sample particles without geometry we need to adjust this
	}

	const FVec3& X() const { return MXR.Read().X(); }
	void SetX(const FVec3& InX, bool bInvalidate = true);

	FUniqueIdx UniqueIdx() const { return MNonFrequentData.Read().UniqueIdx(); }
	void SetUniqueIdx(const FUniqueIdx UniqueIdx, bool bInvalidate = true)
	{
		MNonFrequentData.Modify(bInvalidate, MDirtyFlags, Proxy, [UniqueIdx](auto& Data) { Data.SetUniqueIdx(UniqueIdx); });
	}

	const FRotation3& R() const { return MXR.Read().R(); }
	void SetR(const FRotation3& InR, bool bInvalidate = true);

	void SetXR(const FParticlePositionRotation& InXR, bool bInvalidate = true)
	{
		MXR.Write(InXR, bInvalidate, MDirtyFlags, Proxy);
	}

	//todo: geometry should not be owned by particle
	void SetGeometry(TUniquePtr<FImplicitObject>&& UniqueGeometry)
	{
		// Take ownership of the geometry, putting it into a shared ptr.
		// This is necessary because we cannot be sure whether the particle
		// will be destroyed on the game thread or physics thread first,
		// but geometry data is shared between them.
		FImplicitObject* RawGeometry = UniqueGeometry.Release();
		SetGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(RawGeometry));
	}

	// TODO: Right now this method exists so we can do things like FPhysTestSerializer::CreateChaosData.
	//       We should replace this with a method for supporting SetGeometry(RawGeometry).
	void SetGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> SharedGeometry)
	{
		MNonFrequentData.Modify(true, MDirtyFlags, Proxy, [&SharedGeometry](auto& Data) { Data.SetGeometry(SharedGeometry); });
		UpdateShapesArray();
	}

	void SetGeometry(TSerializablePtr<FImplicitObject> RawGeometry)
	{
		// Ultimately this method should replace SetGeometry(SharedPtr).
		// We don't really want people making shared ptrs to geometry everywhere.
		check(false);
	}

	const TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>& SharedGeometryLowLevel() const { return MNonFrequentData.Read().Geometry(); }

	void* UserData() const { return MUserData; }
	void SetUserData(void* InUserData)
	{
		MUserData = InUserData;
	}

	void UpdateShapeBounds()
	{
		UpdateShapeBounds(FRigidTransform3(X(), R()));
	}

	void UpdateShapeBounds(const FTransform& Transform)
	{
		if (MNonFrequentData.Read().Geometry()->HasBoundingBox())
		{
			for (auto& Shape : MShapesArray)
			{
				Shape->UpdateShapeBounds(Transform);
			}
		}
	}

	void SetShapeSimCollisionEnabled(int32 InShapeIndex, bool bInEnabled)
	{
		const bool bCurrent = MShapesArray[InShapeIndex]->GetSimEnabled();
		if (bCurrent != bInEnabled)
		{
			MShapesArray[InShapeIndex]->SetSimEnabled(bInEnabled);
		}
	}

	void SetShapeQueryCollisionEnabled(int32 InShapeIndex, bool bInEnabled)
	{
		const bool bCurrent = MShapesArray[InShapeIndex]->GetQueryEnabled();
		if (bCurrent != bInEnabled)
		{
			MShapesArray[InShapeIndex]->SetQueryEnabled(bInEnabled);
		}
	}

	void SetShapeCollisionTraceType(int32 InShapeIndex, EChaosCollisionTraceFlag TraceType)
	{
		const EChaosCollisionTraceFlag Current = MShapesArray[InShapeIndex]->GetCollisionTraceType();
		if (Current != TraceType)
		{
			MShapesArray[InShapeIndex]->SetCollisionTraceType(TraceType);
		}
	}

	void SetShapeSimData(int32 InShapeIndex, const FCollisionFilterData& SimData)
	{
		const FCollisionFilterData& Current = MShapesArray[InShapeIndex]->GetSimData();
		if (Current != SimData)
		{
			MShapesArray[InShapeIndex]->SetSimData(SimData);
		}
	}

#if CHAOS_CHECKED
	const FName DebugName() const { return MNonFrequentData.Read().DebugName(); }
	void SetDebugName(const FName& InDebugName)
	{
		MNonFrequentData.Modify(true, MDirtyFlags, Proxy, [&InDebugName](auto& Data) { Data.SetDebugName(InDebugName); });
	}
#endif

	//Note: this must be called after setting geometry. This API seems bad. Should probably be part of setting geometry
	void SetShapesArray(FShapesArray&& InShapesArray)
	{
		ensure(InShapesArray.Num() == MShapesArray.Num());
		MShapesArray = MoveTemp(InShapesArray);
		MapImplicitShapes();
	}

	void SetIgnoreAnalyticCollisionsImp(FImplicitObject* Implicit, bool bIgnoreAnalyticCollisions);
	void SetIgnoreAnalyticCollisions(bool bIgnoreAnalyticCollisions)
	{
		if (MNonFrequentData.Read().Geometry())
		{
			SetIgnoreAnalyticCollisionsImp(MNonFrequentData.Read().Geometry().Get(), bIgnoreAnalyticCollisions);
		}
	}

	TSerializablePtr<FImplicitObject> Geometry() const { return MakeSerializable(MNonFrequentData.Read().Geometry()); }

	const FShapesArray& ShapesArray() const { return MShapesArray; }

	EObjectStateType ObjectState() const;
	void SetObjectState(const EObjectStateType InState, bool bAllowEvents = false, bool bInvalidate = true);

	EParticleType ObjectType() const
	{
		return Type;
	}

	FSpatialAccelerationIdx SpatialIdx() const { return MNonFrequentData.Read().SpatialIdx(); }
	void SetSpatialIdx(FSpatialAccelerationIdx Idx)
	{
		MNonFrequentData.Modify(true, MDirtyFlags, Proxy, [Idx](auto& Data) { Data.SetSpatialIdx(Idx); });
	}

	void SetNonFrequentData(const FParticleNonFrequentData& InData)
	{
		MNonFrequentData.Write(InData, true, MDirtyFlags, Proxy);
	}

	bool IsDirty() const
	{
		return MDirtyFlags.IsDirty();
	}

	bool IsClean() const
	{
		return MDirtyFlags.IsClean();
	}

	bool IsDirty(const EParticleFlags CheckBits) const
	{
		return MDirtyFlags.IsDirty(CheckBits);
	}

	const FParticleDirtyFlags& DirtyFlags() const
	{
		return MDirtyFlags;
	}

	void ClearDirtyFlags()
	{
		MDirtyFlags.Clear();
	}

	const FPerShapeData* GetImplicitShape(const FImplicitObject* InImplicit) const
	{
		const int32* ShapeIndex = ImplicitShapeMap.Find(InImplicit);
		if (ShapeIndex)
		{
			return MShapesArray[*ShapeIndex].Get();
		}

		return nullptr;
	}

	void SyncRemoteData(FDirtyPropertiesManager& Manager, int32 DataIdx, FParticleDirtyData& RemoteData, const TArray<int32>& ShapeDataIndices, FShapeDirtyData* ShapesRemoteData) const
	{
		RemoteData.SetFlags(MDirtyFlags);
		SyncRemoteDataImp(Manager, DataIdx, RemoteData);

		for (const int32 ShapeDataIdx : ShapeDataIndices)
		{
			FShapeDirtyData& ShapeRemoteData = ShapesRemoteData[ShapeDataIdx];
			const int32 ShapeIdx = ShapeRemoteData.GetShapeIdx();
			MShapesArray[ShapeIdx]->SyncRemoteData(Manager, ShapeDataIdx, ShapeRemoteData);
		}
	}

	void SetProxy(IPhysicsProxyBase* InProxy)
	{
		Proxy = InProxy;
		if (Proxy)
		{
			if (MDirtyFlags.IsDirty())
			{
				if (FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
				{
					PhysicsSolverBase->AddDirtyProxy(Proxy);
				}
			}
		}

		for (auto& Shape : MShapesArray)
		{
			Shape->SetProxy(Proxy);
		}
	}

private:

	TParticleProperty<FParticlePositionRotation, EParticleProperty::XR> MXR;
	TParticleProperty<FParticleNonFrequentData, EParticleProperty::NonFrequentData> MNonFrequentData;
	void* MUserData;	//todo: move into proxy

	FShapesArray MShapesArray;
	TMap<const FImplicitObject*, int32> ImplicitShapeMap;


public:
	// Ryan: FGeometryCollectionPhysicsProxy needs access to GeometrySharedLowLevel(), 
	// as it needs access for the same reason as ParticleData.  For some reason
	// the friend declaration isn't working.  Exposing this function until this 
	// can be straightened out.
	//friend class FGeometryCollectionPhysicsProxy;
	// This is only for use by ParticleData. This should be called only in one place,
	// when the geometry is being copied from GT to PT.
	TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> GeometrySharedLowLevel() const
	{
		return MNonFrequentData.Read().Geometry();
	}

protected:

	EParticleType Type;
	FParticleDirtyFlags MDirtyFlags;

	void MarkDirty(const EParticleFlags DirtyBits, bool bInvalidate = true);

	void UpdateShapesArray()
	{
		UpdateShapesArrayFromGeometry(MShapesArray, MakeSerializable(MNonFrequentData.Read().Geometry()), FRigidTransform3(X(), R()), Proxy);
		MapImplicitShapes();
	}

	virtual void SyncRemoteDataImp(FDirtyPropertiesManager& Manager, int32 DataIdx, const FParticleDirtyData& RemoteData) const
	{
		MXR.SyncRemote(Manager, DataIdx, RemoteData);
		MNonFrequentData.SyncRemote(Manager, DataIdx, RemoteData);
	}

	void MapImplicitShapes();

	IPhysicsProxyBase* Proxy;
};
}