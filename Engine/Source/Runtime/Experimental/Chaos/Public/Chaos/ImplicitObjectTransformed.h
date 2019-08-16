// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Box.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Transform.h"
#include "ChaosArchive.h"
#include "Templates/EnableIf.h"

namespace Chaos
{

template <typename T, int d>
void TImplicitObjectTransformSerializeHelper(FChaosArchive& Ar, TSerializablePtr<TImplicitObject<T, d>>& Obj)
{
	Ar << Obj;
}

template <typename T, int d>
void TImplicitObjectTransformSerializeHelper(FChaosArchive& Ar, const TImplicitObject<T, d>* Obj)
{
	check(false);
}

template <typename T, int d>
void TImplicitObjectTransformAccumulateSerializableHelper(TArray<Pair<TSerializablePtr<TImplicitObject<T, d>>, TRigidTransform<T, d>>>& Out, TSerializablePtr<TImplicitObject<T, d>> Obj, const TRigidTransform<T, d>& NewTM)
{
	Obj->AccumulateAllSerializableImplicitObjects(Out, NewTM, Obj);
}

template <typename T, int d>
void TImplicitObjectTransformAccumulateSerializableHelper(TArray<Pair<TSerializablePtr<TImplicitObject<T, d>>, TRigidTransform<T, d>>>& Out, const TImplicitObject<T, d>* Obj, const TRigidTransform<T, d>& NewTM)
{
	check(false);
}

template<class T, int d, bool bSerializable = true>
class TImplicitObjectTransformed final : public TImplicitObject<T, d>
{
using ObjectType = typename TChooseClass<bSerializable, TSerializablePtr<TImplicitObject<T, d>>, const TImplicitObject<T, d>*>::Result;

public:
	IMPLICIT_OBJECT_SERIALIZER(TImplicitObjectTransformed)
	TImplicitObjectTransformed(ObjectType Object, const TRigidTransform<T, d>& InTransform)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Transformed)
	    , MObject(Object)
	    , MTransform(InTransform)
	    , MLocalBoundingBox(Object->BoundingBox().TransformedBox(InTransform))
	{
		this->bIsConvex = Object->IsConvex();
	}
	TImplicitObjectTransformed(ObjectType Object, TUniquePtr<Chaos::TImplicitObject<T,d>> &&ObjectOwner, const TRigidTransform<T, d>& InTransform)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Transformed)
	    , MObject(Object)
		, MObjectOwner(MoveTemp(ObjectOwner))
	    , MTransform(InTransform)
	    , MLocalBoundingBox(Object->BoundingBox().TransformedBox(InTransform))
	{
		this->bIsConvex = Object->IsConvex();
	}

	TImplicitObjectTransformed(const TImplicitObjectTransformed<T, d, bSerializable>& Other) = delete;
	TImplicitObjectTransformed(TImplicitObjectTransformed<T, d, bSerializable>&& Other)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Transformed)
	    , MObject(Other.MObject)
	    , MTransform(Other.MTransform)
	    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
	{
		this->bIsConvex = Other.MObject->IsConvex();
	}
	~TImplicitObjectTransformed() {}

	static ImplicitObjectType GetType()
	{
		return ImplicitObjectType::Transformed;
	}

	const TImplicitObject<T, d>* GetTransformedObject() const
	{
		return MObject.Get();
	}

	virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override
	{
		auto TransformedX = MTransform.InverseTransformPosition(x);
		auto Phi = MObject->PhiWithNormal(TransformedX, Normal);
		Normal = MTransform.TransformVector(Normal);
		return Phi;
	}

	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
	{
		const TVector<T, d> LocalStart = MTransform.InverseTransformPosition(StartPoint);
		const TVector<T, d> LocalDir = MTransform.InverseTransformVector(Dir);
		TVector<T, d> LocalPosition;
		TVector<T, d> LocalNormal;

		if (MObject->Raycast(LocalStart, LocalDir, Length, Thickness, OutTime, LocalPosition, LocalNormal, OutFaceIndex))
		{
			OutPosition = MTransform.TransformPosition(LocalPosition);
			OutNormal = MTransform.TransformVector(LocalNormal);
			return true;
		}
		
		return false;
	}

	virtual bool Overlap(const TVector<T, d>& Point, const T Thickness) const override
	{
		const TVector<T, d> LocalPoint = MTransform.InverseTransformPosition(Point);
		return MObject->Overlap(LocalPoint, Thickness);
	}

	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
	{
		auto TransformedStart = MTransform.InverseTransformPosition(StartPoint);
		auto TransformedEnd = MTransform.InverseTransformPosition(EndPoint);
		auto ClosestIntersection = MObject->FindClosestIntersection(TransformedStart, TransformedEnd, Thickness);
		if (ClosestIntersection.Second)
		{
			ClosestIntersection.First = MTransform.TransformPosition(ClosestIntersection.First);
		}
		return ClosestIntersection;
	}

	virtual TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const override
	{
		return MTransform.TransformPosition(MObject->Support(MTransform.InverseTransformVector(Direction), Thickness));
	}

	const TRigidTransform<T, d>& GetTransform() const { return MTransform; }
	void SetTransform(const TRigidTransform<T, d>& InTransform)
	{
		MLocalBoundingBox = MObject->BoundingBox().TransformedBox(InTransform);
		MTransform = InTransform;
	}

	virtual void AccumulateAllImplicitObjects(TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM) const
	{
		const TRigidTransform<T, d> NewTM = MTransform * ParentTM;
		MObject->AccumulateAllImplicitObjects(Out, NewTM);
	}

	virtual void AccumulateAllSerializableImplicitObjects(TArray<Pair<TSerializablePtr<TImplicitObject<T, d>>, TRigidTransform<T, d>>>& Out, const TRigidTransform<T, d>& ParentTM, TSerializablePtr<TImplicitObject<T,d>> This) const
	{
		check(bSerializable);
		const TRigidTransform<T, d> NewTM = MTransform * ParentTM;
		TImplicitObjectTransformAccumulateSerializableHelper(Out, MObject, NewTM);
	}

	virtual void FindAllIntersectingObjects(TArray < Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>>& Out, const TBox<T, d>& LocalBounds) const
	{
		const TBox<T, d> SubobjectBounds = LocalBounds.TransformedBox(MTransform.Inverse());
		int32 NumOut = Out.Num();
		MObject->FindAllIntersectingObjects(Out, SubobjectBounds);
		if (Out.Num() > NumOut)
		{
			Out[NumOut].Second = MTransform * Out[NumOut].Second;
		}
	}

	virtual const TBox<T, d>& BoundingBox() const override { return MLocalBoundingBox; }

	const ObjectType Object() const { return MObject; }
	
	virtual void Serialize(FChaosArchive& Ar) override
	{
		check(bSerializable);
		TImplicitObject<T, d>::SerializeImp(Ar);
		TImplicitObjectTransformSerializeHelper(Ar, MObject);
		Ar << MTransform << MLocalBoundingBox;
	}

	virtual uint32 GetTypeHash() const override
	{
		// Combine the hash from the inner, non transformed object with our transform
		return HashCombine(MObject->GetTypeHash(), ::GetTypeHash(MTransform));
	}

private:
	ObjectType MObject;
	TUniquePtr<Chaos::TImplicitObject<T, d>> MObjectOwner;
	TRigidTransform<T, d> MTransform;
	TBox<T, d> MLocalBoundingBox;

	//needed for serialization
	TImplicitObjectTransformed() : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Transformed) {}
	friend TImplicitObject<T, d>;	//needed for serialization
};

template <typename T, int d>
using TImplicitObjectTransformedNonSerializable = TImplicitObjectTransformed<T, d, false>;

}