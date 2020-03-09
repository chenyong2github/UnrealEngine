// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "PhysicsInterfaceDeclaresCore.h"

#include "Chaos/CollisionFilterData.h"

struct FBodyInstance;

struct FActorCreationParams
{
	FActorCreationParams()
		: Scene(nullptr)
		, BodyInstance(nullptr)
		, InitialTM(FTransform::Identity)
		, bStatic(false)
		, bQueryOnly(false)
		, bEnableGravity(false)
		, DebugName(nullptr)
	{}

	FPhysScene* Scene;
	FBodyInstance* BodyInstance;
	FTransform InitialTM;
	bool bStatic;
	bool bQueryOnly;
	bool bEnableGravity;
	char* DebugName;
};

/**
* Type of query for object type or trace type
* Trace queries correspond to trace functions with TravelChannel/ResponseParams
* Object queries correspond to trace functions with Object types
*/
enum class ECollisionQuery : uint8
{
	ObjectQuery = 0,
	TraceQuery = 1
};

enum class ECollisionShapeType : uint8
{
	Sphere,
	Plane,
	Box,
	Capsule,
	Convex,
	Trimesh,
	Heightfield,
	None
};

/** Helper struct holding physics body filter data during initialisation */
struct FBodyCollisionFilterData
{
	FCollisionFilterData SimFilter;
	FCollisionFilterData QuerySimpleFilter;
	FCollisionFilterData QueryComplexFilter;
};

struct FBodyCollisionFlags
{
	FBodyCollisionFlags()
		: bEnableSimCollisionSimple(false)
		, bEnableSimCollisionComplex(false)
		, bEnableQueryCollision(false)
	{
	}

	bool bEnableSimCollisionSimple;
	bool bEnableSimCollisionComplex;
	bool bEnableQueryCollision;
};


/** Helper object to hold initialisation data for shapes */
struct FBodyCollisionData
{
	FBodyCollisionFilterData CollisionFilterData;
	FBodyCollisionFlags CollisionFlags;
};

static void SetupNonUniformHelper(FVector InScale3D, float& OutMinScale, float& OutMinScaleAbs, FVector& OutScale3DAbs)
{
	// if almost zero, set min scale
	// @todo fixme
	if (InScale3D.IsNearlyZero())
	{
		// set min scale
		InScale3D = FVector(0.1f);
	}

	OutScale3DAbs = InScale3D.GetAbs();
	OutMinScaleAbs = OutScale3DAbs.GetMin();

	OutMinScale = FMath::Max3(InScale3D.X, InScale3D.Y, InScale3D.Z) < 0.f ? -OutMinScaleAbs : OutMinScaleAbs;	//if all three values are negative make minScale negative

	if (FMath::IsNearlyZero(OutMinScale))
	{
		// only one of them can be 0, we make sure they have mini set up correctly
		OutMinScale = 0.1f;
		OutMinScaleAbs = 0.1f;
	}
}

/** Util to determine whether to use NegX version of mesh, and what transform (rotation) to apply. */
static bool CalcMeshNegScaleCompensation(const FVector& InScale3D, FTransform& OutTransform)
{
	OutTransform = FTransform::Identity;

	if (InScale3D.Y > 0.f)
	{
		if (InScale3D.Z > 0.f)
		{
			// no rotation needed
		}
		else
		{
			// y pos, z neg
			OutTransform.SetRotation(FQuat(FVector(0.0f, 1.0f, 0.0f), PI));
			//OutTransform.q = PxQuat(PxPi, PxVec3(0,1,0));
		}
	}
	else
	{
		if (InScale3D.Z > 0.f)
		{
			// y neg, z pos
			//OutTransform.q = PxQuat(PxPi, PxVec3(0,0,1));
			OutTransform.SetRotation(FQuat(FVector(0.0f, 0.0f, 1.0f), PI));
		}
		else
		{
			// y neg, z neg
			//OutTransform.q = PxQuat(PxPi, PxVec3(1,0,0));
			OutTransform.SetRotation(FQuat(FVector(1.0f, 0.0f, 0.0f), PI));
		}
	}

	// Use inverted mesh if determinant is negative
	return (InScale3D.X * InScale3D.Y * InScale3D.Z) < 0.f;
}


//TODO: Reimplement types in chaos
#if !PHYSICS_INTERFACE_PHYSX && WITH_CHAOS

const uint32 AggregateMaxSize = 128;


class UPhysicalMaterial;
class UPrimitiveComponent;
struct FBodyInstance;
struct FConstraintInstance;
struct FKShapeElem;

/** Forward declarations */
struct FKShapeElem;
struct FCustomPhysXPayload;

/** PhysX user data type*/
namespace EPhysxUserDataType
{
	enum Type
	{
		Invalid,
		BodyInstance,
		PhysicalMaterial,
		PhysScene,
		ConstraintInstance,
		PrimitiveComponent,
		AggShape,
		CustomPayload,	//This is intended for plugins
	};
};

/** PhysX user data */
struct FPhysxUserData
{
protected:
	EPhysxUserDataType::Type	Type;
	void*						Payload;

public:
	FPhysxUserData()									:Type(EPhysxUserDataType::Invalid), Payload(nullptr) {}
	FPhysxUserData(FBodyInstance* InPayload)			:Type(EPhysxUserDataType::BodyInstance), Payload(InPayload) {}
	FPhysxUserData(UPhysicalMaterial* InPayload)		:Type(EPhysxUserDataType::PhysicalMaterial), Payload(InPayload) {}
	FPhysxUserData(FPhysScene* InPayload)			    :Type(EPhysxUserDataType::PhysScene), Payload(InPayload) {}
	FPhysxUserData(FConstraintInstance* InPayload)		:Type(EPhysxUserDataType::ConstraintInstance), Payload(InPayload) {}
	FPhysxUserData(UPrimitiveComponent* InPayload)		:Type(EPhysxUserDataType::PrimitiveComponent), Payload(InPayload) {}
	FPhysxUserData(FKShapeElem* InPayload)				:Type(EPhysxUserDataType::AggShape), Payload(InPayload) {}
	FPhysxUserData(FCustomPhysXPayload* InPayload)		:Type(EPhysxUserDataType::CustomPayload), Payload(InPayload) {}
	
	template <class T> static T* Get(void* UserData);
	template <class T> static void Set(void* UserData, T* Payload);

	//helper function to determine if userData is garbage (maybe dangling pointer)
	static bool IsGarbage(void* UserData){ return ((FPhysxUserData*)UserData)->Type < EPhysxUserDataType::Invalid || ((FPhysxUserData*)UserData)->Type > EPhysxUserDataType::CustomPayload; }
};

template <> FORCEINLINE FBodyInstance* FPhysxUserData::Get(void* UserData)			{ if (!UserData || ((FPhysxUserData*)UserData)->Type != EPhysxUserDataType::BodyInstance) { return nullptr; } return (FBodyInstance*)((FPhysxUserData*)UserData)->Payload; }
template <> FORCEINLINE UPhysicalMaterial* FPhysxUserData::Get(void* UserData)		{ if (!UserData || ((FPhysxUserData*)UserData)->Type != EPhysxUserDataType::PhysicalMaterial) { return nullptr; } return (UPhysicalMaterial*)((FPhysxUserData*)UserData)->Payload; }
template <> FORCEINLINE FPhysScene* FPhysxUserData::Get(void* UserData)				{ if (!UserData || ((FPhysxUserData*)UserData)->Type != EPhysxUserDataType::PhysScene) { return nullptr; }return (FPhysScene*)((FPhysxUserData*)UserData)->Payload; }
template <> FORCEINLINE FConstraintInstance* FPhysxUserData::Get(void* UserData)	{ if (!UserData || ((FPhysxUserData*)UserData)->Type != EPhysxUserDataType::ConstraintInstance) { return nullptr; } return (FConstraintInstance*)((FPhysxUserData*)UserData)->Payload; }
template <> FORCEINLINE UPrimitiveComponent* FPhysxUserData::Get(void* UserData)	{ if (!UserData || ((FPhysxUserData*)UserData)->Type != EPhysxUserDataType::PrimitiveComponent) { return nullptr; } return (UPrimitiveComponent*)((FPhysxUserData*)UserData)->Payload; }
template <> FORCEINLINE FKShapeElem* FPhysxUserData::Get(void* UserData)	{ if (!UserData || ((FPhysxUserData*)UserData)->Type != EPhysxUserDataType::AggShape) { return nullptr; } return (FKShapeElem*)((FPhysxUserData*)UserData)->Payload; }
template <> FORCEINLINE FCustomPhysXPayload* FPhysxUserData::Get(void* UserData) { if (!UserData || ((FPhysxUserData*)UserData)->Type != EPhysxUserDataType::CustomPayload) { return nullptr; } return (FCustomPhysXPayload*)((FPhysxUserData*)UserData)->Payload; }

template <> FORCEINLINE void FPhysxUserData::Set(void* UserData, FBodyInstance* Payload)			{ check(UserData); ((FPhysxUserData*)UserData)->Type = EPhysxUserDataType::BodyInstance; ((FPhysxUserData*)UserData)->Payload = Payload; }
template <> FORCEINLINE void FPhysxUserData::Set(void* UserData, UPhysicalMaterial* Payload)		{ check(UserData); ((FPhysxUserData*)UserData)->Type = EPhysxUserDataType::PhysicalMaterial; ((FPhysxUserData*)UserData)->Payload = Payload; }
template <> FORCEINLINE void FPhysxUserData::Set(void* UserData, FPhysScene* Payload)				{ check(UserData); ((FPhysxUserData*)UserData)->Type = EPhysxUserDataType::PhysScene; ((FPhysxUserData*)UserData)->Payload = Payload; }
template <> FORCEINLINE void FPhysxUserData::Set(void* UserData, FConstraintInstance* Payload)		{ check(UserData); ((FPhysxUserData*)UserData)->Type = EPhysxUserDataType::ConstraintInstance; ((FPhysxUserData*)UserData)->Payload = Payload; }
template <> FORCEINLINE void FPhysxUserData::Set(void* UserData, UPrimitiveComponent* Payload)		{ check(UserData); ((FPhysxUserData*)UserData)->Type = EPhysxUserDataType::PrimitiveComponent; ((FPhysxUserData*)UserData)->Payload = Payload; }
template <> FORCEINLINE void FPhysxUserData::Set(void* UserData, FKShapeElem* Payload)	{ check(UserData); ((FPhysxUserData*)UserData)->Type = EPhysxUserDataType::AggShape; ((FPhysxUserData*)UserData)->Payload = Payload; }
template <> FORCEINLINE void FPhysxUserData::Set(void* UserData, FCustomPhysXPayload* Payload) { check(UserData); ((FPhysxUserData*)UserData)->Type = EPhysxUserDataType::CustomPayload; ((FPhysxUserData*)UserData)->Payload = Payload; }

/** enum for empty constructor tag*/
enum PxEMPTY
{
	PxEmpty
};

#define PX_INLINE inline

typedef int64_t PxI64;
typedef uint64_t PxU64;
typedef int32_t PxI32;
typedef uint32_t PxU32;
typedef int16_t PxI16;
typedef uint16_t PxU16;
typedef int8_t PxI8;
typedef uint8_t PxU8;
typedef float PxF32;
typedef double PxF64;
typedef float PxReal;

struct PxFilterData
{
	//= ATTENTION! =====================================================================================
	// Changing the data layout of this class breaks the binary serialization format.  See comments for 
	// PX_BINARY_SERIAL_VERSION.  If a modification is required, please adjust the getBinaryMetaData 
	// function.  If the modification is made on a custom branch, please change PX_BINARY_SERIAL_VERSION
	// accordingly.
	//==================================================================================================

	PX_INLINE PxFilterData(const PxEMPTY)
	{
	}

	/**
	\brief Default constructor.
	*/
	PX_INLINE PxFilterData()
	{
		word0 = word1 = word2 = word3 = 0;
	}

	/**
	\brief Constructor to set filter data initially.
	*/
	PX_INLINE PxFilterData(PxU32 w0, PxU32 w1, PxU32 w2, PxU32 w3) : word0(w0), word1(w1), word2(w2), word3(w3) {}

	/**
	\brief (re)sets the structure to the default.
	*/
	PX_INLINE void setToDefault()
	{
		*this = PxFilterData();
	}

	/**
	\brief Comparison operator to allow use in Array.
	*/
	PX_INLINE bool operator == (const PxFilterData& a) const
	{
		return a.word0 == word0 && a.word1 == word1 && a.word2 == word2 && a.word3 == word3;
	}

	/**
	\brief Comparison operator to allow use in Array.
	*/
	PX_INLINE bool operator != (const PxFilterData& a) const
	{
		return !(a == *this);
	}

	PxU32 word0;
	PxU32 word1;
	PxU32 word2;
	PxU32 word3;
};

struct PxQueryFlag
{
	enum Enum
	{
		eSTATIC = (1 << 0),	//!< Traverse static shapes

		eDYNAMIC = (1 << 1),	//!< Traverse dynamic shapes

		ePREFILTER = (1 << 2),	//!< Run the pre-intersection-test filter (see #PxQueryFilterCallback::preFilter())

		ePOSTFILTER = (1 << 3),	//!< Run the post-intersection-test filter (see #PxQueryFilterCallback::postFilter())

		eANY_HIT = (1 << 4),	//!< Abort traversal as soon as any hit is found and return it via callback.block.
								//!< Helps query performance. Both eTOUCH and eBLOCK hitTypes are considered hits with this flag.

		eNO_BLOCK = (1 << 5),	//!< All hits are reported as touching. Overrides eBLOCK returned from user filters with eTOUCH.
								//!< This is also an optimization hint that may improve query performance.

		eRESERVED = (1 << 15)	//!< Reserved for internal use
	};
};

typedef PxU8 PxClientID;
static const PxClientID PX_DEFAULT_CLIENT = 0;

#define PX_CUDA_CALLABLE

template <typename enumtype, typename storagetype = uint32_t>
class PxFlags
{
public:
	typedef storagetype InternalType;

	PX_CUDA_CALLABLE PX_INLINE explicit PxFlags(const PxEMPTY)
	{
	}
	PX_CUDA_CALLABLE PX_INLINE PxFlags(void);
	PX_CUDA_CALLABLE PX_INLINE PxFlags(enumtype e);
	PX_CUDA_CALLABLE PX_INLINE PxFlags(const PxFlags<enumtype, storagetype>& f);
	PX_CUDA_CALLABLE PX_INLINE explicit PxFlags(storagetype b);

	PX_CUDA_CALLABLE PX_INLINE bool isSet(enumtype e) const;
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype>& set(enumtype e);
	PX_CUDA_CALLABLE PX_INLINE bool operator==(enumtype e) const;
	PX_CUDA_CALLABLE PX_INLINE bool operator==(const PxFlags<enumtype, storagetype>& f) const;
	PX_CUDA_CALLABLE PX_INLINE bool operator==(bool b) const;
	PX_CUDA_CALLABLE PX_INLINE bool operator!=(enumtype e) const;
	PX_CUDA_CALLABLE PX_INLINE bool operator!=(const PxFlags<enumtype, storagetype>& f) const;

	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype>& operator=(const PxFlags<enumtype, storagetype>& f);
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype>& operator=(enumtype e);

	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype>& operator|=(enumtype e);
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype>& operator|=(const PxFlags<enumtype, storagetype>& f);
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype> operator|(enumtype e) const;
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype> operator|(const PxFlags<enumtype, storagetype>& f) const;

	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype>& operator&=(enumtype e);
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype>& operator&=(const PxFlags<enumtype, storagetype>& f);
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype> operator&(enumtype e) const;
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype> operator&(const PxFlags<enumtype, storagetype>& f) const;

	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype>& operator^=(enumtype e);
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype>& operator^=(const PxFlags<enumtype, storagetype>& f);
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype> operator^(enumtype e) const;
	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype> operator^(const PxFlags<enumtype, storagetype>& f) const;

	PX_CUDA_CALLABLE PX_INLINE PxFlags<enumtype, storagetype> operator~(void) const;

	PX_CUDA_CALLABLE PX_INLINE operator bool(void) const;
	PX_CUDA_CALLABLE PX_INLINE operator uint8_t(void) const;
	PX_CUDA_CALLABLE PX_INLINE operator uint16_t(void) const;
	PX_CUDA_CALLABLE PX_INLINE operator uint32_t(void) const;

	PX_CUDA_CALLABLE PX_INLINE void clear(enumtype e);

public:
	friend PX_INLINE PxFlags<enumtype, storagetype> operator&(enumtype a, PxFlags<enumtype, storagetype>& b)
	{
		PxFlags<enumtype, storagetype> out;
		out.mBits = a & b.mBits;
		return out;
	}

private:
	storagetype mBits;
};

typedef PxFlags<PxQueryFlag::Enum, PxU16> PxQueryFlags;

inline PxQueryFlags U2PQueryFlags(FQueryFlags Flags)
{
	uint32 Result = 0;
	if (Flags & EQueryFlags::PreFilter)
	{
		Result |= PxQueryFlag::ePREFILTER;
	}

	if (Flags & EQueryFlags::PostFilter)
	{
		Result |= PxQueryFlag::ePOSTFILTER;
	}

	if (Flags & EQueryFlags::AnyHit)
	{
		Result |= PxQueryFlag::eANY_HIT;
	}

	return (PxQueryFlags)Result;
}

struct PxQueryFilterData
{
	/** \brief default constructor */
	explicit PX_INLINE PxQueryFilterData() : flags(PxQueryFlag::eDYNAMIC | PxQueryFlag::eSTATIC), clientId(PX_DEFAULT_CLIENT) {}

	/** \brief constructor to set both filter data and filter flags */
	explicit PX_INLINE PxQueryFilterData(const PxFilterData& fd, PxQueryFlags f) : data(fd), flags(f), clientId(PX_DEFAULT_CLIENT) {}

	/** \brief constructor to set filter flags only */
	explicit PX_INLINE PxQueryFilterData(PxQueryFlags f) : flags(f), clientId(PX_DEFAULT_CLIENT) {}

	PxFilterData	data;		//!< Filter data associated with the scene query
	PxQueryFlags	flags;		//!< Filter flags (see #PxQueryFlags)
	PxClientID		clientId;	//!< ID of the client doing the query (see #PxScene.createClient())
};

#define PX_FLAGS_OPERATORS(enumtype, storagetype)                                                                      \
	PX_INLINE PxFlags<enumtype, storagetype> operator|(enumtype a, enumtype b)                                         \
	{                                                                                                                  \
		PxFlags<enumtype, storagetype> r(a);                                                                           \
		r |= b;                                                                                                        \
		return r;                                                                                                      \
	}                                                                                                                  \
	PX_INLINE PxFlags<enumtype, storagetype> operator&(enumtype a, enumtype b)                                         \
	{                                                                                                                  \
		PxFlags<enumtype, storagetype> r(a);                                                                           \
		r &= b;                                                                                                        \
		return r;                                                                                                      \
	}                                                                                                                  \
	PX_INLINE PxFlags<enumtype, storagetype> operator~(enumtype a)                                                     \
	{                                                                                                                  \
		return ~PxFlags<enumtype, storagetype>(a);                                                                     \
	}

#define PX_FLAGS_TYPEDEF(x, y)                                                                                         \
	typedef PxFlags<x::Enum, y> x##s;                                                                                  \
	PX_FLAGS_OPERATORS(x::Enum, y)

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>::PxFlags(void)
{
	mBits = 0;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>::PxFlags(enumtype e)
{
	mBits = static_cast<storagetype>(e);
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>::PxFlags(const PxFlags<enumtype, storagetype>& f)
{
	mBits = f.mBits;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>::PxFlags(storagetype b)
{
	mBits = b;
}

template <typename enumtype, typename storagetype>
PX_INLINE bool PxFlags<enumtype, storagetype>::isSet(enumtype e) const
{
	return (mBits & static_cast<storagetype>(e)) == static_cast<storagetype>(e);
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>& PxFlags<enumtype, storagetype>::set(enumtype e)
{
	mBits = static_cast<storagetype>(e);
	return *this;
}

template <typename enumtype, typename storagetype>
PX_INLINE bool PxFlags<enumtype, storagetype>::operator==(enumtype e) const
{
	return mBits == static_cast<storagetype>(e);
}

template <typename enumtype, typename storagetype>
PX_INLINE bool PxFlags<enumtype, storagetype>::operator==(const PxFlags<enumtype, storagetype>& f) const
{
	return mBits == f.mBits;
}

template <typename enumtype, typename storagetype>
PX_INLINE bool PxFlags<enumtype, storagetype>::operator==(bool b) const
{
	return bool(*this) == b;
}

template <typename enumtype, typename storagetype>
PX_INLINE bool PxFlags<enumtype, storagetype>::operator!=(enumtype e) const
{
	return mBits != static_cast<storagetype>(e);
}

template <typename enumtype, typename storagetype>
PX_INLINE bool PxFlags<enumtype, storagetype>::operator!=(const PxFlags<enumtype, storagetype>& f) const
{
	return mBits != f.mBits;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>& PxFlags<enumtype, storagetype>::operator=(enumtype e)
{
	mBits = static_cast<storagetype>(e);
	return *this;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>& PxFlags<enumtype, storagetype>::operator=(const PxFlags<enumtype, storagetype>& f)
{
	mBits = f.mBits;
	return *this;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>& PxFlags<enumtype, storagetype>::operator|=(enumtype e)
{
	mBits |= static_cast<storagetype>(e);
	return *this;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>& PxFlags<enumtype, storagetype>::
operator|=(const PxFlags<enumtype, storagetype>& f)
{
	mBits |= f.mBits;
	return *this;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype> PxFlags<enumtype, storagetype>::operator|(enumtype e) const
{
	PxFlags<enumtype, storagetype> out(*this);
	out |= e;
	return out;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype> PxFlags<enumtype, storagetype>::
operator|(const PxFlags<enumtype, storagetype>& f) const
{
	PxFlags<enumtype, storagetype> out(*this);
	out |= f;
	return out;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>& PxFlags<enumtype, storagetype>::operator&=(enumtype e)
{
	mBits &= static_cast<storagetype>(e);
	return *this;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>& PxFlags<enumtype, storagetype>::
operator&=(const PxFlags<enumtype, storagetype>& f)
{
	mBits &= f.mBits;
	return *this;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype> PxFlags<enumtype, storagetype>::operator&(enumtype e) const
{
	PxFlags<enumtype, storagetype> out = *this;
	out.mBits &= static_cast<storagetype>(e);
	return out;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype> PxFlags<enumtype, storagetype>::
operator&(const PxFlags<enumtype, storagetype>& f) const
{
	PxFlags<enumtype, storagetype> out = *this;
	out.mBits &= f.mBits;
	return out;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>& PxFlags<enumtype, storagetype>::operator^=(enumtype e)
{
	mBits ^= static_cast<storagetype>(e);
	return *this;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>& PxFlags<enumtype, storagetype>::
operator^=(const PxFlags<enumtype, storagetype>& f)
{
	mBits ^= f.mBits;
	return *this;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype> PxFlags<enumtype, storagetype>::operator^(enumtype e) const
{
	PxFlags<enumtype, storagetype> out = *this;
	out.mBits ^= static_cast<storagetype>(e);
	return out;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype> PxFlags<enumtype, storagetype>::
operator^(const PxFlags<enumtype, storagetype>& f) const
{
	PxFlags<enumtype, storagetype> out = *this;
	out.mBits ^= f.mBits;
	return out;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype> PxFlags<enumtype, storagetype>::operator~(void) const
{
	PxFlags<enumtype, storagetype> out;
	out.mBits = storagetype(~mBits);
	return out;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>::operator bool(void) const
{
	return mBits ? true : false;
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>::operator uint8_t(void) const
{
	return static_cast<uint8_t>(mBits);
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>::operator uint16_t(void) const
{
	return static_cast<uint16_t>(mBits);
}

template <typename enumtype, typename storagetype>
PX_INLINE PxFlags<enumtype, storagetype>::operator uint32_t(void) const
{
	return static_cast<uint32_t>(mBits);
}

template <typename enumtype, typename storagetype>
PX_INLINE void PxFlags<enumtype, storagetype>::clear(enumtype e)
{
	mBits &= ~static_cast<storagetype>(e);
}

#endif // !WITH_PHYSX
