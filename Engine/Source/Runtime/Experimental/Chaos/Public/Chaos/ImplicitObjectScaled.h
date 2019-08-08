// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Box.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Transform.h"
#include "ChaosArchive.h"
#include <type_traits>
#include "Templates/EnableIf.h"

namespace Chaos
{
template<class T, int d, bool bInstanced = true>
class TImplicitObjectScaled final : public TImplicitObject<T, d>
{
using ObjectType = typename std::conditional<bInstanced, TSerializablePtr<TImplicitObject<T, d>>, TUniquePtr<TImplicitObject<T, d>>>::type;

public:
	IMPLICIT_OBJECT_SERIALIZER(TImplicitObjectScaled)
	TImplicitObjectScaled(ObjectType Object, const TVector<T, d>& Scale)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Scaled)
	    , MObject(MoveTemp(Object))
	{
		switch (MObject->GetType(true))
		{
		case ImplicitObjectType::Scaled:
		case ImplicitObjectType::Transformed:
		case ImplicitObjectType::Union:
			check(false);	//scale is only supported for concrete types like sphere, capsule, convex, levelset, etc... Nothing that contains other objects
		default:
			break;
		}
		this->bIsConvex = MObject->IsConvex();
		SetScale(Scale);
	}
	TImplicitObjectScaled(ObjectType Object, TUniquePtr<Chaos::TImplicitObject<T,d>> &&ObjectOwner, const TVector<T, d>& Scale)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Scaled)
	    , MObject(Object)
	{
		this->bIsConvex = Object->IsConvex();
		SetScale(Scale);
	}

	TImplicitObjectScaled(const TImplicitObjectScaled<T, d, bInstanced>& Other) = delete;
	TImplicitObjectScaled(TImplicitObjectScaled<T, d, bInstanced>&& Other)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Scaled)
	    , MObject(Other.MObject)
	    , MScale(Other.MScale)
	    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
	{
		this->bIsConvex = Other.MObject->IsConvex();
	}
	~TImplicitObjectScaled() {}

	static ImplicitObjectType GetType()
	{
		return ImplicitObjectType::Scaled;
	}

	const TImplicitObject<T, d>* GetUnscaledObject() const
	{
		return MObject.Get();
	}

	virtual T PhiWithNormal(const TVector<T, d>& X, TVector<T, d>& Normal) const override
	{
		const TVector<T, d> UnscaledX = MInvScale * X;
		TVector<T, d> UnscaledNormal;
		const T UnscaledPhi = MObject->PhiWithNormal(UnscaledX, UnscaledNormal);
		Normal = MScale * UnscaledNormal;
		const T ScaleFactor = Normal.SafeNormalize();
		const T ScaledPhi = UnscaledPhi * ScaleFactor;
		return ScaledPhi;
	}

	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
	{
		ensure(Length > 0);
		ensure(FMath::IsNearlyEqual(Dir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
		ensure(Thickness == 0 || (FMath::IsNearlyEqual(MScale[0], MScale[1]) && FMath::IsNearlyEqual(MScale[0], MScale[2])));	//non uniform turns sphere into an ellipsoid so no longer a raycast and requires a more expensive sweep

		const TVector<T, d> UnscaledStart = MInvScale * StartPoint;
		TVector<T, d> UnscaledDir = MInvScale * Dir * Length;
		const T UnscaledLength = UnscaledDir.SafeNormalize();
		
		TVector<T, d> UnscaledPosition;
		TVector<T, d> UnscaledNormal;

		if (MObject->Raycast(UnscaledStart, UnscaledDir, UnscaledLength, Thickness * MInvScale[0], OutTime, UnscaledPosition, UnscaledNormal, OutFaceIndex))
		{
			OutTime = UnscaledLength ? OutTime * (Length / UnscaledLength) : 0;	//is this needed? SafeNormalize above seems bad
			OutPosition = MScale * UnscaledPosition;
			OutNormal = (MScale * UnscaledNormal).GetSafeNormal();
			return true;
		}
		
		return false;
	}

	virtual bool Overlap(const TVector<T, d>& Point, const T Thickness) const override
	{
		const TVector<T, d> UnscaledPoint = MInvScale * Point;
		return MObject->Overlap(UnscaledPoint, Thickness);
	}

	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
	{
		const TVector<T,d> UnscaledStart = MInvScale * StartPoint;
		const TVector<T, d> UnscaledEnd = MInvScale * EndPoint;
		auto ClosestIntersection = MObject->FindClosestIntersection(UnscaledStart, UnscaledEnd, Thickness);
		if (ClosestIntersection.Second)
		{
			ClosestIntersection.First = MScale * ClosestIntersection.First;
		}
		return ClosestIntersection;
	}

	virtual TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const override
	{
		// Support_obj(dir) = pt => for all x in obj, pt \dot dir >= x \dot dir
		// We want Support_objScaled(dir) = Support_obj(dir') where dir' is some modification of dir so we can use the unscaled support function
		// If objScaled = Aobj where A is a transform, then we can say Support_objScaled(dir) = pt => for all x in obj, pt \dot dir >= Ax \dot dir
		// But this is the same as pt \dot dir >= dir^T Ax = (dir^TA) x = (A^T dir)^T x
		//So let dir' = A^T dir.
		//Since we only support scaling on the principal axes A is a diagonal (and therefore symmetric) matrix and so a simple component wise multiplication is sufficient
		const TVector<T, d> UnthickenedPt = MObject->Support(Direction * MScale, 0) * MScale;
		return Thickness > 0 ? TVector<T, d>(UnthickenedPt + Direction.GetSafeNormal() * Thickness) : UnthickenedPt;
	}

	const TVector<T, d>& GetScale() const { return MScale; }
	void SetScale(const TVector<T, d>& Scale)
	{
		constexpr T MinMagnitude = 1e-4;
		for (int Axis = 0; Axis < d; ++Axis)
		{
			if (FMath::Abs(Scale[Axis]) < MinMagnitude)
			{
				MScale[Axis] = MinMagnitude;
			}
			else
			{
				MScale[Axis] = Scale[Axis];
			}

			MInvScale[Axis] = 1 / MScale[Axis];
		}
		UpdateBounds();
	}

	virtual const TBox<T, d>& BoundingBox() const override { return MLocalBoundingBox; }

	const ObjectType Object() const { return MObject; }
	
	virtual void Serialize(FChaosArchive& Ar) override
	{
		TImplicitObject<T, d>::SerializeImp(Ar);
		Ar << MObject << MScale << MInvScale << MLocalBoundingBox;
	}

	virtual uint32 GetTypeHash() const override
	{
		return HashCombine(MObject->GetTypeHash(), ::GetTypeHash(MScale));
	}

private:
	ObjectType MObject;
	TVector<T, d> MScale;
	TVector<T, d> MInvScale;
	TBox<T, d> MLocalBoundingBox;

	//needed for serialization
	TImplicitObjectScaled() : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Scaled) {}
	friend TImplicitObject<T, d>;	//needed for serialization

	void UpdateBounds()
	{
		const TBox<T, d> UnscaledBounds = MObject->BoundingBox();
		MLocalBoundingBox = TBox<T,d>(UnscaledBounds.Min() * MScale, UnscaledBounds.Max() * MScale);
	}
};

template <typename T, int d>
using TImplicitObjectScaledNonSerializable = TImplicitObjectScaled<T, d, false>;

}