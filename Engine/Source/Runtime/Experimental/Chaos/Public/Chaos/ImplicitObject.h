// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Matrix.h"
#include "Chaos/Pair.h"
#include "Chaos/Serializable.h"
#include "Chaos/Vector.h"

#include <functional>
#include "Transform.h"

namespace Chaos
{
template<class T, int d>
class TBox;
template<class T>
class TCylinder;
template<class T, int d>
class TSphere;
template<class T, int d>
class TPlane;
template<class T, int d>
class TParticles;
template<class T, int d>
class TBVHParticles;

enum class ImplicitObjectType : int8
{
	//Note: add entries at the bottom for serialization
	Sphere,
	Box,
	Plane,
	Capsule,
	Transformed,
	Union,
	LevelSet,
	Unknown,
	Convex,
	TaperedCylinder,
	Cylinder,
	TriangleMesh,
	HeightField,
	Scaled
};

namespace EImplicitObject
{
	enum Flags
	{
		IsConvex = 1,
		HasBoundingBox = 1 << 1,
		IgnoreAnalyticCollisions = 1 << 2,
	};

	const int32 FiniteConvex = IsConvex | HasBoundingBox;
}

template<class T, int d>
class CHAOS_API TImplicitObject
{
public:
	static constexpr bool IsSerializablePtr = true;

	static void StaticSerialize(FChaosArchive& Ar, TSerializablePtr<TImplicitObject<T, d>>& Obj);

	TImplicitObject(int32 Flags, ImplicitObjectType InType = ImplicitObjectType::Unknown);
	TImplicitObject(const TImplicitObject<T, d>&) = delete;
	TImplicitObject(TImplicitObject<T, d>&&) = delete;
	virtual ~TImplicitObject();

	template<class T_DERIVED>
	T_DERIVED* GetObject()
	{
		if (T_DERIVED::GetType() == Type)
		{
			return static_cast<T_DERIVED*>(this);
		}
		return nullptr;
	}

	template<class T_DERIVED>
	const T_DERIVED* GetObject() const
	{
		if (T_DERIVED::GetType() == Type)
		{
			return static_cast<const T_DERIVED*>(this);
		}
		return nullptr;
	}

	template<class T_DERIVED>
	const T_DERIVED& GetObjectChecked() const
	{
		check(T_DERIVED::GetType() == Type);
		return static_cast<const T_DERIVED&>(*this);
	}

	template<class T_DERIVED>
	T_DERIVED& GetObjectChecked()
	{
		check(T_DERIVED::GetType() == Type);
		return static_cast<const T_DERIVED&>(*this);
	}

	ImplicitObjectType GetType(bool bGetTrueType = false) const
	{
		if (bIgnoreAnalyticCollisions && !bGetTrueType)
		{
			return ImplicitObjectType::Unknown;
		}
		return Type;
	}

	//This is strictly used for optimization purposes
	bool IsUnderlyingUnion() const
	{
		return Type == ImplicitObjectType::Union;
	}

	/*virtual*/ T SignedDistance(const TVector<T, d>& x) const
	{
		TVector<T, d> Normal;
		return PhiWithNormal(x, Normal);
	}
	TVector<T, d> Normal(const TVector<T, d>& x) const
	{
		TVector<T, d> Normal;
		PhiWithNormal(x, Normal);
		return Normal;
	}
	virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const = 0;
	virtual const class TBox<T, d>& BoundingBox() const;
	virtual TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const;
	bool HasBoundingBox() const { return bHasBoundingBox; }
	bool IsConvex() const { return bIsConvex; }
	void IgnoreAnalyticCollisions(const bool Ignore = true) { bIgnoreAnalyticCollisions = Ignore; }
	bool GetIgnoreAnalyticCollisions() const { return bIgnoreAnalyticCollisions; }
	void SetConvex(const bool Convex = true) { bIsConvex = Convex; }

	Pair<TVector<T, d>, bool> FindDeepestIntersection(const TImplicitObject<T, d>* Other, const TBVHParticles<float, d>* Particles, const PMatrix<T, d, d>& OtherToLocalTransform, const T Thickness) const;
	Pair<TVector<T, d>, bool> FindDeepestIntersection(const TImplicitObject<T, d>* Other, const TParticles<float, d>* Particles, const PMatrix<T, d, d>& OtherToLocalTransform, const T Thickness) const;
	Pair<TVector<T, d>, bool> FindClosestIntersection(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const;

	//This gives derived types a way to avoid calling PhiWithNormal todo: this api is confusing
	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T,d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T,d>& OutPosition, TVector<T,d>& OutNormal, int32& OutFaceIndex) const
	{
		OutFaceIndex = INDEX_NONE;
		const TVector<T, d> EndPoint = StartPoint + Dir * Length;
		Pair<TVector<T,d>, bool> Result = FindClosestIntersection(StartPoint, EndPoint, Thickness);
		if (Result.Second)
		{
			OutPosition = Result.First;
			OutNormal = Normal(Result.First);
			OutTime = Length > 0 ? (OutPosition - StartPoint).Size() : 0.f;
			return true;
		}
		return false;
	}

	/** Returns the most opposing face.
		@param Position - local position to search around (for example an edge of a convex hull)
		@param UnitDir - the direction we want to oppose (for example a ray moving into the edge of a convex hull would get the face with the most negative dot(FaceNormal, UnitDir)
		@param HintFaceIndex - for certain geometry we can use this to accelerate the search.
	*/
	virtual int32 FindMostOpposingFace(const TVector<T, d>& Position, const TVector<T, d>& UnitDir, int32 HintFaceIndex) const
	{
		//Many objects have no concept of a face
		return INDEX_NONE;
	}

	//This gives derived types a way to do an overlap check without calling PhiWithNormal todo: this api is confusing
	virtual bool Overlap(const TVector<T, d>& Point, const T Thickness) const
	{
		return SignedDistance(Point) <= Thickness;
	}

	virtual void AccumulateAllImplicitObjects(TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM) const
	{
		Out.Add(MakePair(this, ParentTM));
	}

	virtual void AccumulateAllSerializableImplicitObjects(TArray<Pair<TSerializablePtr<TImplicitObject<T, d>>, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM, TSerializablePtr<TImplicitObject<T,d>> This) const
	{
		Out.Add(MakePair(This, ParentTM));
	}

	virtual void FindAllIntersectingObjects(TArray < Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>>& Out, const TBox<T, d>& LocalBounds) const;

	virtual FString ToString() const
	{
		return FString::Printf(TEXT("ImplicitObject bIsConvex:%d, bIgnoreAnalyticCollision:%d, bHasBoundingBox:%d"), bIsConvex, bIgnoreAnalyticCollisions, bHasBoundingBox);
	}

	void SerializeImp(FArchive& Ar);
	
	virtual void Serialize(FArchive& Ar)
	{
		check(false);	//Aggregate implicits require FChaosArchive - check false by default
	}

	virtual void Serialize(FChaosArchive& Ar);
	
	static FArchive& SerializeLegacyHelper(FArchive& Ar, TUniquePtr<TImplicitObject<T, d>>& Value);

	virtual uint32 GetTypeHash() const = 0;

protected:
	ImplicitObjectType Type;
	bool bIsConvex;
	bool bIgnoreAnalyticCollisions;
	bool bHasBoundingBox;

private:
	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const;
};

template <typename T, int d>
FORCEINLINE FChaosArchive& operator<<(FChaosArchive& Ar, TImplicitObject<T, d>& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

template <typename T, int d>
FORCEINLINE FArchive& operator<<(FArchive& Ar, TImplicitObject<T, d>& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

#define IMPLICIT_OBJECT_SERIALIZER(Type)\
static void StaticSerialize(FChaosArchive& Ar, TSerializablePtr<Type>& Obj)\
{\
	TImplicitObject<T, d>::StaticSerialize(Ar, (TSerializablePtr<TImplicitObject<T, d>>&)Obj);\
}

#define IMPLICIT_OBJECT_SERIALIZER_DIM(Type, Dim)\
static void StaticSerialize(FChaosArchive& Ar, TSerializablePtr<Type>& Obj)\
{\
	TImplicitObject<T, Dim>::StaticSerialize(Ar, (TSerializablePtr<TImplicitObject<T, Dim>>&)Obj);\
}

typedef TImplicitObject<float, 3> FImplicitObject3;
}