// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CHAOS

#include "ChaosSQTypes.h"
#include "PhysicsInterfaceDeclaresCore.h"
#include "PhysicsInterfaceTypesCore.h"
#include "PhysicsInterfaceUtilsCore.h"
#include "PhysicsInterfaceWrapperShared.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Capsule.h"

template<typename DummyT>
struct FCallbackDummy {};

template <typename T>
using FPhysicsHitCallback = FCallbackDummy<T>;

#if WITH_PHYSX
using FQueryFilterData = PxQueryFilterData;
#else
using FQueryFilterData = FPhysTypeDummy;
#endif

/** We use this struct so that if no conversion is needed in another API, we can avoid the copy (if we think that's critical) */
struct FPhysicsRaycastInputAdapater
{
	FPhysicsRaycastInputAdapater(const FVector& InStart, const FVector& InDir, const EHitFlags InFlags)
		: Start(InStart)
		, Dir(InDir)
		, OutputFlags(InFlags)
	{

	}
	FVector Start;
	FVector Dir;
	EHitFlags OutputFlags;
};

/** We use this struct so that if no conversion is needed in another API, we can avoid the copy (if we think that's critical) */
struct FPhysicsSweepInputAdapater
{
	FPhysicsSweepInputAdapater(const FTransform& InStartTM, const FVector& InDir, const EHitFlags InFlags)
		: StartTM(InStartTM)
		, Dir(InDir)
		, OutputFlags(InFlags)
	{

	}
	FTransform StartTM;
	FVector Dir;
	EHitFlags OutputFlags;
};

/** We use this struct so that if no conversion is needed in another API, we can avoid the copy (if we think that's critical) */
struct FPhysicsOverlapInputAdapater
{
	FPhysicsOverlapInputAdapater(const FTransform& InPose)
		: GeomPose(InPose)
	{

	}
	FTransform GeomPose;
};

template<typename HitType>
class FDynamicHitBuffer : public FCallbackDummy<HitType>
{
public:
	FDynamicHitBuffer()
		: bHasBlockingHit(false)
	{}

	~FDynamicHitBuffer() {}

	bool ProcessTouchBuffer(TArrayView<const HitType>& Touches)
	{
		Hits.Append(Touches);
		return true;
	}

	void FinishQuery()
	{
		if(bHasBlockingHit)
		{
			Hits.Add(CurrentBlockingHit);
		}
	}

	bool HasHit() const { return bHasBlockingHit || Hits.Num() == 0; }

	int32 GetNumHits() const
	{
		return Hits.Num();
	}

	HitType* GetHits()
	{
		return (HitType*)Hits.GetData();
	}

	HitType* GetBlock()
	{
		return &CurrentBlockingHit;
	}

	bool HasBlockingHit() const
	{
		return bHasBlockingHit;
	}

protected:
	HitType CurrentBlockingHit;
	bool bHasBlockingHit;

private:
	static const int32 BufferSize = 512;

	/** Hit buffer used to provide hits via ProcessTouchBuffer */
	TTypeCompatibleBytes<HitType> HitBuffer[BufferSize];

	/** Hits encountered. Can be larger than HIT_BUFFER_SIZE */
	TArray<TTypeCompatibleBytes<HitType>, TInlineAllocator<BufferSize>> Hits;
};


inline ECollisionShapeType GetType(const FPhysicsGeometry& InGeometry)
{
	return ECollisionShapeType(InGeometry.GetType());
}

inline ECollisionShapeType GetGeometryType(const FPhysTypeDummy& Shape)
{
	return ECollisionShapeType::None;
}

inline float GetRadius(const FPhysicsCapsuleGeometry& InCapsule)
{
	return InCapsule.GetRadius();
}

inline float GetHalfHeight(const FPhysicsCapsuleGeometry& InCapsule)
{
	return InCapsule.GetHeight()/2.;
}

inline bool HadInitialOverlap(const FPhysTypeDummy& Hit)
{
	return false;
}

inline Chaos::TImplicitObject<float, 3>* GetShape(const FPhysTypeDummy& Hit)
{
	return nullptr;
}

inline FPhysActorDummy* GetActor(const FPhysTypeDummy& Hit)
{
	return nullptr;
}

inline float GetDistance(const FPhysTypeDummy& Hit)
{
	return 0.0f;
}

inline FVector GetPosition(const FPhysTypeDummy& Hit)
{
	return FVector::ZeroVector;
}

inline FVector GetNormal(const FPhysTypeDummy& Hit)
{
	return FVector(0.0f, 0.0f, 1.0f);
}

inline FPhysTypeDummy* GetMaterialFromInternalFaceIndex(const FPhysTypeDummy& Shape, uint32 InternalFaceIndex)
{
	return nullptr;
}

inline FHitFlags GetFlags(const FPhysTypeDummy& Hit)
{
	return FHitFlags(EHitFlags::None);
}

FORCEINLINE void SetFlags(FPhysTypeDummy& Hit, FHitFlags Flags)
{
	//Hit.flags = U2PHitFlags(Flags);
}

inline uint32 GetInternalFaceIndex(const FPhysTypeDummy& Hit)
{
	return 0;
}

inline void SetInternalFaceIndex(FPhysTypeDummy& Hit, uint32 FaceIndex)
{

}

inline FCollisionFilterData GetQueryFilterData(const Chaos::TImplicitObject<float, 3>& Shape)
{
	return FCollisionFilterData();
}

inline FCollisionFilterData GetSimulationFilterData(const FPhysTypeDummy& Shape)
{
	return FCollisionFilterData();
}

inline uint32 GetInvalidPhysicsFaceIndex()
{
	return 0xffffffff;
}

inline uint32 GetTriangleMeshExternalFaceIndex(const FPhysTypeDummy& Shape, uint32 InternalFaceIndex)
{
	return GetInvalidPhysicsFaceIndex();
}

inline FTransform GetGlobalPose(const FPhysActorDummy& RigidActor)
{
	return FTransform::Identity;
}

inline uint32 GetNumShapes(const FPhysActorDummy& RigidActor)
{
	return 0;
}

inline void GetShapes(const FPhysActorDummy& RigidActor, Chaos::TImplicitObject<float, 3>** ShapesBuffer, uint32 NumShapes)
{

}

inline void SetActor(FPhysTypeDummy& Hit, FPhysActorDummy* Actor)
{

}

inline void SetShape(FPhysTypeDummy& Hit, Chaos::TImplicitObject<float, 3>* Shape)
{

}

template <typename HitType>
void SetBlock(FPhysicsHitCallback<HitType>& Callback, const HitType& Hit)
{

}

template <typename HitType>
void SetHasBlock(FPhysicsHitCallback<HitType>& Callback, bool bHasBlock)
{

}

template <typename HitType>
void ProcessTouches(FPhysicsHitCallback<HitType>& Callback, const TArray<HitType>& TouchingHits)
{

}

template <typename HitType>
void FinalizeQuery(FPhysicsHitCallback<HitType>& Callback)
{

}

template <typename HitType>
HitType* GetBlock(const FPhysicsHitCallback<HitType>& Callback)
{
	return nullptr;
}

template <typename HitType>
bool GetHasBlock(const FPhysicsHitCallback<HitType>& Callback)
{
	return false;
}

template <typename HitType>
FORCEINLINE_DEBUGGABLE bool Insert(FCallbackDummy<HitType>& Callback, const HitType& Hit, bool bBlocking)
{
	// Previous Physx implementation
	//if(!Callback.hasBlock || GetDistance(Hit) < GetDistance(Callback.block))
	//{
	//	if(bBlocking)
	//	{
	//		Callback.block = Hit;
	//		Callback.hasBlock = true;
	//	}
	//	else
	//	{
	//		if(Callback.maxNbTouches > 0)
	//		{
	//			Callback.processTouches(&Hit, 1);
	//
	//		}
	//	}
	//}

	return true;
}

template <typename HitType>
FORCEINLINE bool InsertOverlap(FCallbackDummy<HitType>& Callback, const HitType& Hit)
{
	// Previous Physx implementation
	//return Callback.processTouches(&Hit, 1);

	return true;
}

#endif