// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Box.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Transform.h"
#include "ChaosArchive.h"
#include "Templates/ChooseClass.h"
#include "Templates/EnableIf.h"
#include "Math/NumericLimits.h"

namespace Chaos
{
template<class T, int d, bool bInstanced = true>
class TImplicitObjectScaled final : public TImplicitObject<T, d>
{
using ObjectType = typename TChooseClass<bInstanced, TSerializablePtr<TImplicitObject<T, d>>, TUniquePtr<TImplicitObject<T, d>>>::Result;
public:

	using TImplicitObject<T, d>::GetTypeName;

	TImplicitObjectScaled(ObjectType Object, const TVector<T, d>& Scale, T Thickness = 0)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Scaled)
	    , MObject(MoveTemp(Object))
		, MInternalThickness(Thickness)
	{
		ensureMsgf(MObject->GetType(true) != ImplicitObjectType::Scaled, TEXT("Scaled objects should not contain each other."));
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
	TImplicitObjectScaled(ObjectType Object, TUniquePtr<Chaos::TImplicitObject<T,d>> &&ObjectOwner, const TVector<T, d>& Scale, T Thickness = 0)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Scaled)
	    , MObject(Object)
		, MInternalThickness(Thickness)
	{
		ensureMsgf(MObject->GetType(true) != ImplicitObjectType::Scaled, TEXT("Scaled objects should not contain each other."));
		this->bIsConvex = Object->IsConvex();
		SetScale(Scale);
	}

	TImplicitObjectScaled(const TImplicitObjectScaled<T, d, bInstanced>& Other) = delete;
	TImplicitObjectScaled(TImplicitObjectScaled<T, d, bInstanced>&& Other)
	    : TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Scaled)
	    , MObject(Other.MObject)
	    , MScale(Other.MScale)
		, MInvScale(Other.MInvScale)
		, MInternalThickness(Other.MInternalThickness)
	    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
	{
		ensureMsgf(MObject->GetType(true) != ImplicitObjectType::Scaled, TEXT("Scaled objects should not contain each other."));
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
		const T UnscaledPhi = MObject->PhiWithNormal(UnscaledX, UnscaledNormal) - MInternalThickness;
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
		const TVector<T, d> UnscaledDirDenorm = MInvScale * Dir;
		const T LengthScale = UnscaledDirDenorm.Size();
		if (ensure(LengthScale > TNumericLimits<T>::Min()))
		{
			const T LengthScaleInv = 1.f / LengthScale;
			const T UnscaledLength = Length * LengthScale;
			const TVector<T, d> UnscaledDir = UnscaledDirDenorm * LengthScaleInv;
			
			TVector<T, d> UnscaledPosition;
			TVector<T, d> UnscaledNormal;
			float UnscaledTime;

			if (MObject->Raycast(UnscaledStart, UnscaledDir, UnscaledLength, MInternalThickness + Thickness * MInvScale[0], UnscaledTime, UnscaledPosition, UnscaledNormal, OutFaceIndex))
			{
				OutTime = LengthScaleInv * UnscaledTime;
				OutPosition = MScale * UnscaledPosition;
				OutNormal = (MInvScale * UnscaledNormal).GetSafeNormal();
				ensure(OutTime <= Length);
				return true;
			}
		}
			
		return false;
	}

	/** This is a low level function and assumes the internal object has a SweepGeom function. Should not be called directly. See GeometryQueries.h : SweepQuery */
	template <typename TGeometry>
	static bool LowLevelSweepGeom(const TImplicitObjectScaled<T, d>& OwningScaled, const TGeometry& InternalGeom, const TImplicitObject<T, d>& B, const TRigidTransform<T, d>& BToATM, const TVector<T, d>& LocalDir, const T Length, T& OutTime, TVector<T, d>& LocalPosition, TVector<T, d>& LocalNormal, int32& OutFaceIndex, T Thickness = 0)
	{
		ensure(Length > 0);
		ensure(FMath::IsNearlyEqual(LocalDir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
		ensure(Thickness == 0 || (FMath::IsNearlyEqual(OwningScaled.MScale[0], OwningScaled.MScale[1]) && FMath::IsNearlyEqual(OwningScaled.MScale[0], OwningScaled.MScale[2])));

		const TVector<T, d> UnscaledDirDenorm = OwningScaled.MInvScale * LocalDir;
		const T LengthScale = UnscaledDirDenorm.Size();
		if (ensure(LengthScale > TNumericLimits<T>::Min()))
		{
			const T LengthScaleInv = 1.f / LengthScale;
			const T UnscaledLength = Length * LengthScale;
			const TVector<T, d> UnscaledDir = UnscaledDirDenorm * LengthScaleInv;

			TVector<T, d> UnscaledPosition;
			TVector<T, d> UnscaledNormal;
			float UnscaledTime;

			TUniquePtr<TImplicitObject<T, d>> HackBPtr(const_cast<TImplicitObject<T,d>*>(&B));	//todo: hack, need scaled object to except raw ptr similar to transformed implicit
			TImplicitObjectScaled<T, d> ScaledB(MakeSerializable(HackBPtr), OwningScaled.MInvScale);
			HackBPtr.Release();

			TRigidTransform<T, d> BToATMNoScale(BToATM.GetLocation() * OwningScaled.MInvScale, BToATM.GetRotation());
			
			if (InternalGeom.SweepGeom(ScaledB, BToATMNoScale, UnscaledDir, UnscaledLength, UnscaledTime, UnscaledPosition, UnscaledNormal, OutFaceIndex, OwningScaled.MInternalThickness + Thickness))
			{
				OutTime = LengthScaleInv * UnscaledTime;
				LocalPosition = OwningScaled.MScale * UnscaledPosition;
				LocalNormal = (OwningScaled.MInvScale * UnscaledNormal).GetSafeNormal();
				ensure(OutTime <= Length);
				return true;
			}
		}

		return false;
	}

	/** This is a low level function and assumes the internal object has a OverlapGeom function. Should not be called directly. See GeometryQueries.h : OverlapQuery */
	template <typename TGeometry>
	static bool LowLevelOverlapGeom(const TImplicitObjectScaled<T, d>& OwningScaled, const TGeometry& InternalGeom, const TImplicitObject<T, d>& B, const TRigidTransform<T, d>& BToATM, T Thickness = 0)
	{
		ensure(Thickness == 0 || (FMath::IsNearlyEqual(OwningScaled.MScale[0], OwningScaled.MScale[1]) && FMath::IsNearlyEqual(OwningScaled.MScale[0], OwningScaled.MScale[2])));

		TUniquePtr<TImplicitObject<T, d>> HackBPtr(const_cast<TImplicitObject<T, d>*>(&B));	//todo: hack, need scaled object to except raw ptr similar to transformed implicit
		TImplicitObjectScaled<T, d> ScaledB(MakeSerializable(HackBPtr), OwningScaled.MInvScale);
		HackBPtr.Release();

		TRigidTransform<T, d> BToATMNoScale(BToATM.GetLocation() * OwningScaled.MInvScale, BToATM.GetRotation());

		return InternalGeom.OverlapGeom(ScaledB, BToATMNoScale, OwningScaled.MInternalThickness + Thickness);
	}

	virtual int32 FindMostOpposingFace(const TVector<T, d>& Position, const TVector<T, d>& UnitDir, int32 HintFaceIndex, T SearchDist) const override
	{
		ensure(MInternalThickness == 0);	//not supported: do we care?
		ensure(FMath::IsNearlyEqual(UnitDir.SizeSquared(), 1, KINDA_SMALL_NUMBER));

		const TVector<T, d> UnscaledPosition = MInvScale * Position;
		const TVector<T, d> UnscaledDirDenorm = MScale * UnitDir;
		const float LengthScale = UnscaledDirDenorm.Size();
		const TVector<T, d> UnscaledDir
			= ensure(LengthScale > TNumericLimits<T>::Min())
			? UnscaledDirDenorm / LengthScale
			: TVector<T, d>(0.f, 0.f, 1.f);
		const T UnscaledSearchDist = SearchDist * MScale.Max();	//this is not quite right since it's no longer a sphere, but the whole thing is fuzzy anyway
		return MObject->FindMostOpposingFace(UnscaledPosition, UnscaledDir, HintFaceIndex, UnscaledSearchDist);
	}

	virtual TVector<T, 3> FindGeometryOpposingNormal(const TVector<T, d>& DenormDir, int32 HintFaceIndex, const TVector<T, d>& OriginalNormal) const override
	{
		ensure(MInternalThickness == 0);	//not supported: do we care?
		ensure(FMath::IsNearlyEqual(OriginalNormal.SizeSquared(), 1, KINDA_SMALL_NUMBER));

		// Get unscaled dir and normal
		const TVector<T, 3> LocalDenormDir = DenormDir * MScale;
		const TVector<T, 3> LocalOriginalNormalDenorm = OriginalNormal * MScale;
		const float NormalLengthScale = LocalOriginalNormalDenorm.Size();
		const TVector<T, 3> LocalOriginalNormal
			= ensure(NormalLengthScale > SMALL_NUMBER)
			? LocalOriginalNormalDenorm / NormalLengthScale
			: TVector<T, d>(0, 0, 1);

		// Compute final normal
		const TVector<T, d> LocalNormal = MObject->FindGeometryOpposingNormal(LocalDenormDir, HintFaceIndex, LocalOriginalNormal);
		TVector<T, d> Normal = LocalNormal * MInvScale;
		if (ensure(Normal.SafeNormalize()) == 0)
		{
			Normal = TVector<T,3>(0,0,1);
		}

		return Normal;
	}

	virtual bool Overlap(const TVector<T, d>& Point, const T Thickness) const override
	{
		const TVector<T, d> UnscaledPoint = MInvScale * Point;
		return MObject->Overlap(UnscaledPoint, MInternalThickness + Thickness);
	}

	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
	{
		ensure(MInternalThickness == 0);	//not supported: do we care?
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
		const TVector<T, d> UnthickenedPt = MObject->Support(Direction * MScale, MInternalThickness) * MScale;
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
		FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
		TImplicitObject<T, d>::SerializeImp(Ar);
		Ar << MObject << MScale << MInvScale << MLocalBoundingBox;
		ensure(MInternalThickness == 0);	//not supported: do we care?
	}

	virtual uint32 GetTypeHash() const override
	{
		return HashCombine(MObject->GetTypeHash(), ::GetTypeHash(MScale));
	}

	virtual TUniquePtr<TImplicitObject<T, d>> Copy() const override
	{
		return TUniquePtr<TImplicitObject<T, d>>(CopyHelper(this));
	}

private:
	ObjectType MObject;
	TVector<T, d> MScale;
	TVector<T, d> MInvScale;
	T MInternalThickness;	//Allows us to inflate the instance before the scale is applied. This is useful when sweeps need to apply a non scale on a geometry with uniform thickness
	TBox<T, d> MLocalBoundingBox;

	//needed for serialization
	TImplicitObjectScaled()
	: TImplicitObject<T, d>(EImplicitObject::HasBoundingBox, ImplicitObjectType::Scaled)
	, MInternalThickness(0)
	{}
	friend TImplicitObject<T, d>;	//needed for serialization

	template <typename T2, int d2, bool bInstanced2>
	friend class TImplicitObjectScaled;

	static TImplicitObjectScaled<T, d, true>* CopyHelper(const TImplicitObjectScaled<T, d, true>* Obj)
	{
		return new TImplicitObjectScaled<T, d, true>(Obj->MObject, Obj->MScale, Obj->MInternalThickness);
	}

	static TImplicitObjectScaled<T, d, false>* CopyHelper(const TImplicitObjectScaled<T, d, false>* Obj)
	{
		return new TImplicitObjectScaled<T, d, false>(Obj->MObject->Copy(), Obj->MScale, Obj->MInternalThickness);
	}

	void UpdateBounds()
	{
		const TBox<T, d> UnscaledBounds = MObject->BoundingBox();
		const TVector<T, d> Vector1 = UnscaledBounds.Min() * MScale;
		MLocalBoundingBox = TBox<T, d>(Vector1, Vector1);	//need to grow it out one vector at a time in case scale is negative
		const TVector<T, d> Vector2 = UnscaledBounds.Max() *MScale;
		MLocalBoundingBox.GrowToInclude(Vector2);
	}
};

template <typename T, int d>
using TImplicitObjectScaledNonSerializable = TImplicitObjectScaled<T, d, false>;

}
