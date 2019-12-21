// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Box.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Transform.h"
#include "ChaosArchive.h"
#include "Templates/ChooseClass.h"
#include "Templates/EnableIf.h"
#include "Math/NumericLimits.h"
#include "ChaosCheck.h"

namespace Chaos
{

template <typename TConcrete, bool bInstanced = true>
class TImplicitObjectInstanced final : public FImplicitObject
{
public:
	using T = typename TConcrete::TType;
	using TType = T;
	static constexpr int d = TConcrete::D;
	static constexpr int D = d;

	using ObjectType = typename TChooseClass<bInstanced, TSerializablePtr<TConcrete>, TUniquePtr<TConcrete>>::Result;
	using FImplicitObject::GetTypeName;

	TImplicitObjectInstanced(ObjectType Object)
		: FImplicitObject(EImplicitObject::HasBoundingBox, Object->GetType() | ImplicitObjectType::IsInstanced)
		, MObject(MoveTemp(Object))
	{
		ensure(IsInstanced(MObject->GetType(true)) == false);	//cannot have an instance of an instance
		this->bIsConvex = MObject->IsConvex();
	}

	static constexpr EImplicitObjectType StaticType()
	{
		return TConcrete::StaticType() | ImplicitObjectType::IsInstanced;
	}

	const TConcrete* GetInstancedObject() const
	{
		return MObject.Get();
	}

	virtual T PhiWithNormal(const TVector<T, d>& X, TVector<T, d>& Normal) const override
	{
		return MObject->PhiWithNormal(X, Normal);
	}

	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
	{
		return MObject->Raycast(StartPoint, Dir, Length, Thickness, OutTime, OutPosition, OutNormal, OutFaceIndex);
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
		FImplicitObject::SerializeImp(Ar);
		Ar << MObject;
	}

	/** This is a low level function and assumes the internal object has a SweepGeom function. Should not be called directly. See GeometryQueries.h : SweepQuery */
	template <typename TGeometry>
	static bool LowLevelSweepGeom(const TImplicitObjectInstanced<TConcrete, bInstanced>& OwningInstanced, const FImplicitObject& B, const TRigidTransform<T, d>& BToATM, const TVector<T, d>& LocalDir, const T Length, T& OutTime, TVector<T, d>& LocalPosition, TVector<T, d>& LocalNormal, int32& OutFaceIndex, T Thickness = 0)
	{
		ensure(false);
		return false;
	}

	/** This is a low level function and assumes the internal object has a OverlapGeom function. Should not be called directly. See GeometryQueries.h : OverlapQuery */
	bool LowLevelOverlapGeom(const TImplicitObjectInstanced<TConcrete, bInstanced>& OwningInstanced, const FImplicitObject& B, const TRigidTransform<T, d>& BToATM, T Thickness = 0) const
	{
		ensure(false);
		return false;
	}

	virtual int32 FindMostOpposingFace(const TVector<T, d>& Position, const TVector<T, d>& UnitDir, int32 HintFaceIndex, T SearchDist) const override
	{
		return MObject->FindMostOpposingFace(Position, UnitDir, HintFaceIndex, SearchDist);
	}

	virtual TVector<T, 3> FindGeometryOpposingNormal(const TVector<T, d>& DenormDir, int32 HintFaceIndex, const TVector<T, d>& OriginalNormal) const override
	{
		return MObject->FindGeometryOpposingNormal(DenormDir, HintFaceIndex, OriginalNormal);
	}

	virtual bool Overlap(const TVector<T, d>& Point, const T Thickness) const override
	{
		return MObject->Overlap(Point, Thickness);
	}

	FORCEINLINE T GetMargin() const { return MObject->GetMargin(); }

	FORCEINLINE TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const { return MObject->Support(Direction, Thickness); }
	FORCEINLINE TVector<T, d> Support2(const TVector<T, d>& Direction, const T Thickness) const { return MObject->Support2(Direction); }

	virtual const TAABB<T, d>& BoundingBox() const override { return MObject->BoundingBox(); }

	const ObjectType Object() const { return MObject; }

	virtual uint32 GetTypeHash() const override
	{
		return MObject->GetTypeHash();
	}

	virtual TUniquePtr<FImplicitObject> Copy() const override
	{
		return TUniquePtr<FImplicitObject>(CopyHelper(this));
	}

protected:
	ObjectType MObject;

	static TImplicitObjectInstanced<TConcrete, true>* CopyHelper(const TImplicitObjectInstanced<TConcrete, true>* Obj)
	{
		return new TImplicitObjectInstanced<TConcrete, true>(Obj->MObject);
	}

	static TImplicitObjectInstanced<TConcrete, false>* CopyHelper(const TImplicitObjectInstanced<TConcrete, false>* Obj)
	{
		return new TImplicitObjectInstanced<TConcrete, false>(Obj->MObject->Copy());
	}
};


template<typename TConcrete, bool bInstanced = true>
class TImplicitObjectScaled final : public FImplicitObject
{
public:
	using T = typename TConcrete::TType;
	using TType = T;
	static constexpr int d = TConcrete::D;
	static constexpr int D = d;

	using ObjectType = typename TChooseClass<bInstanced, TSerializablePtr<TConcrete>, TUniquePtr<TConcrete>>::Result;
	using FImplicitObject::GetTypeName;

	TImplicitObjectScaled(ObjectType Object, const TVector<T, d>& Scale, T Thickness = 0)
	    : FImplicitObject(EImplicitObject::HasBoundingBox, Object->GetType() | ImplicitObjectType::IsScaled)
	    , MObject(MoveTemp(Object))
		, MSharedPtrForRefCount(nullptr)
		, MInternalThickness(Thickness)
	{
		ensureMsgf((IsScaled(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain instances."));
		switch (MObject->GetType(true))
		{
		case ImplicitObjectType::Transformed:
		case ImplicitObjectType::Union:
			check(false);	//scale is only supported for concrete types like sphere, capsule, convex, levelset, etc... Nothing that contains other objects
		default:
			break;
		}
		this->bIsConvex = MObject->IsConvex();
		SetScale(Scale);
	}

	TImplicitObjectScaled(TSharedPtr<TConcrete, ESPMode::ThreadSafe> Object, const TVector<T, d>& Scale, T Thickness = 0)
	    : FImplicitObject(EImplicitObject::HasBoundingBox, Object->GetType() | ImplicitObjectType::IsScaled)
	    , MObject(MakeSerializable<TConcrete, ESPMode::ThreadSafe>(Object))
		, MSharedPtrForRefCount(Object)
		, MInternalThickness(Thickness)
	{
		ensureMsgf((IsScaled(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain instances."));
		switch (MObject->GetType(true))
		{
		case ImplicitObjectType::Transformed:
		case ImplicitObjectType::Union:
			check(false);	//scale is only supported for concrete types like sphere, capsule, convex, levelset, etc... Nothing that contains other objects
		default:
			break;
		}
		this->bIsConvex = MObject->IsConvex();
		SetScale(Scale);
	}

	TImplicitObjectScaled(ObjectType Object, TUniquePtr<Chaos::FImplicitObject> &&ObjectOwner, const TVector<T, d>& Scale, T Thickness = 0)
	    : FImplicitObject(EImplicitObject::HasBoundingBox, Object->GetType() | ImplicitObjectType::IsScaled)
	    , MObject(Object)
		, MSharedPtrForRefCount(nullptr)
		, MInternalThickness(Thickness)
	{
		ensureMsgf((IsScaled(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain instances."));
		this->bIsConvex = Object->IsConvex();
		SetScale(Scale);
	}

	TImplicitObjectScaled(const TImplicitObjectScaled<TConcrete, bInstanced>& Other) = delete;
	TImplicitObjectScaled(TImplicitObjectScaled<TConcrete, bInstanced>&& Other)
	    : FImplicitObject(EImplicitObject::HasBoundingBox, Other.MObject->GetType() | ImplicitObjectType::IsScaled)
	    , MObject(MoveTemp(Other.MObject))
		, MSharedPtrForRefCount(MoveTemp(Other.MSharedPtrForRefCount))
	    , MScale(Other.MScale)
		, MInvScale(Other.MInvScale)
		, MInternalThickness(Other.MInternalThickness)
	    , MLocalBoundingBox(MoveTemp(Other.MLocalBoundingBox))
	{
		ensureMsgf((IsScaled(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain instances."));
		this->bIsConvex = Other.MObject->IsConvex();
	}
	~TImplicitObjectScaled() {}

	static constexpr EImplicitObjectType StaticType()
	{
		return TConcrete::StaticType() | ImplicitObjectType::IsScaled;
	}

	static const TImplicitObjectScaled<TConcrete>& AsScaledChecked(const FImplicitObject& Obj)
	{
		if (TIsSame<TConcrete, FImplicitObject>::Value)
		{
			//can cast any scaled to ImplicitObject base
			check(IsScaled(Obj.GetType()));
		}
		else
		{
			check(StaticType() == Obj.GetType());
		}
		return static_cast<const TImplicitObjectScaled<TConcrete>&>(Obj);
	}

	static TImplicitObjectScaled<TConcrete>& AsScaledChecked(FImplicitObject& Obj)
	{
		if (TIsSame<TConcrete, FImplicitObject>::Value)
		{
			//can cast any scaled to ImplicitObject base
			check(IsScaled(Obj.GetType()));
		}
		else
		{
			check(StaticType() == Obj.GetType());
		}
		return static_cast<TImplicitObjectScaled<TConcrete>&>(Obj);
	}

	static const TImplicitObjectScaled<TConcrete>* AsScaled(const FImplicitObject& Obj)
	{
		if (TIsSame<TConcrete, FImplicitObject>::Value)
		{
			//can cast any scaled to ImplicitObject base
			return IsScaled(Obj.GetType()) ? static_cast<const TImplicitObjectScaled<TConcrete>*>(&Obj) : nullptr;
		}
		else
		{
			return StaticType() == Obj.GetType() ? static_cast<const TImplicitObjectScaled<TConcrete>*>(&Obj) : nullptr;
		}
	}

	static TImplicitObjectScaled<TConcrete>* AsScaled(FImplicitObject& Obj)
	{
		if (TIsSame<TConcrete, FImplicitObject>::Value)
		{
			//can cast any scaled to ImplicitObject base
			return IsScaled(Obj.GetType()) ? static_cast<TImplicitObjectScaled<TConcrete>*>(&Obj) : nullptr;
		}
		else
		{
			return StaticType() == Obj.GetType() ? static_cast<TImplicitObjectScaled<TConcrete>*>(&Obj) : nullptr;
		}
	}

	const TConcrete* GetUnscaledObject() const
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
				if (OutTime != 0) // Normal/Position output may be uninitialized with TOI 0.
				{
					OutPosition = MScale * UnscaledPosition;
					OutNormal = (MInvScale * UnscaledNormal).GetSafeNormal();
				}
				CHAOS_ENSURE(OutTime <= Length);
				return true;
			}
		}
			
		return false;
	}

	/** This is a low level function and assumes the internal object has a SweepGeom function. Should not be called directly. See GeometryQueries.h : SweepQuery */
	template <typename QueryGeomType>
	bool LowLevelSweepGeom(const QueryGeomType& B, const TRigidTransform<T, d>& BToATM, const TVector<T, d>& LocalDir, const T Length, T& OutTime, TVector<T, d>& LocalPosition, TVector<T, d>& LocalNormal, int32& OutFaceIndex, T Thickness = 0, bool bComputeMTD = false) const
	{
		ensure(Length > 0);
		ensure(FMath::IsNearlyEqual(LocalDir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
		ensure(Thickness == 0 || (FMath::IsNearlyEqual(MScale[0], MScale[1]) && FMath::IsNearlyEqual(MScale[0], MScale[2])));

		const TVector<T, d> UnscaledDirDenorm = MInvScale * LocalDir;
		const T LengthScale = UnscaledDirDenorm.Size();
		if (ensure(LengthScale > TNumericLimits<T>::Min()))
		{
			const T LengthScaleInv = 1.f / LengthScale;
			const T UnscaledLength = Length * LengthScale;
			const TVector<T, d> UnscaledDir = UnscaledDirDenorm * LengthScaleInv;

			TVector<T, d> UnscaledPosition;
			TVector<T, d> UnscaledNormal;
			float UnscaledTime;

			auto ScaledB = MakeScaledHelper(B, MInvScale);

			TRigidTransform<T, d> BToATMNoScale(BToATM.GetLocation() * MInvScale, BToATM.GetRotation());
			
			if (MObject->SweepGeom(ScaledB, BToATMNoScale, UnscaledDir, UnscaledLength, UnscaledTime, UnscaledPosition, UnscaledNormal, OutFaceIndex, MInternalThickness + Thickness, bComputeMTD))
			{
				OutTime = LengthScaleInv * UnscaledTime;
				LocalPosition = MScale * UnscaledPosition;
				LocalNormal = (MInvScale * UnscaledNormal).GetSafeNormal();
				ensure(OutTime <= Length);
				return true;
			}
		}

		return false;
	}

	/** This is a low level function and assumes the internal object has a OverlapGeom function. Should not be called directly. See GeometryQueries.h : OverlapQuery */
	template <typename QueryGeomType>
	bool LowLevelOverlapGeom(const QueryGeomType& B, const TRigidTransform<T, d>& BToATM, T Thickness = 0) const
	{
		ensure(Thickness == 0 || (FMath::IsNearlyEqual(MScale[0], MScale[1]) && FMath::IsNearlyEqual(MScale[0], MScale[2])));

		auto ScaledB = MakeScaledHelper(B, MInvScale);
		TRigidTransform<T, d> BToATMNoScale(BToATM.GetLocation() * MInvScale, BToATM.GetRotation());
		return MObject->OverlapGeom(ScaledB, BToATMNoScale, MInternalThickness + Thickness);
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
		if (CHAOS_ENSURE(Normal.SafeNormalize()) == 0)
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

	virtual int32 FindClosestFaceAndVertices(const FVec3& Position, TArray<FVec3>& FaceVertices, FReal SearchDist = 0.01) const override
	{
		const FVec3 UnscaledPoint = MInvScale * Position;
		const FReal UnscaledSearchDist = SearchDist * MInvScale.Max();	//this is not quite right since it's no longer a sphere, but the whole thing is fuzzy anyway
		int32 FaceIndex =  MObject->FindClosestFaceAndVertices(UnscaledPoint, FaceVertices, UnscaledSearchDist);
		if (FaceIndex != INDEX_NONE)
		{
			for (FVec3& Vec : FaceVertices)
			{
				Vec = Vec * MScale;
			}
		}
		return FaceIndex;
	}


	FORCEINLINE_DEBUGGABLE TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const
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

	FORCEINLINE_DEBUGGABLE TVector<T, d> Support2(const TVector<T, d>& Direction) const
	{
		return MObject->Support2(Direction * MScale) * MScale;
	}

	FORCEINLINE T GetMargin() const
	{
		if (T UnscaledMargin = MObject->GetMargin())
		{
			ensure(MScale[0] == MScale[1] && MScale[1] == MScale[2]);
			return UnscaledMargin * FMath::Abs(MScale[0]);
		}

		return 0;
	}

	const TVector<T, d>& GetScale() const { return MScale; }
	const TVector<T, d>& GetInvScale() const { return MInvScale; }
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

	virtual const TAABB<T, d>& BoundingBox() const override { return MLocalBoundingBox; }

	const FReal GetVolume() const
	{
		// TODO: More precise volume!
		return BoundingBox().GetVolume();
	}

	const FMatrix33 GetInertiaTensor(const FReal Mass) const
	{
		// TODO: More precise inertia!
		return BoundingBox().GetInertiaTensor(Mass);
	}

	const FVec3 GetCenterOfMass() const
	{
		// TODO: I'm not sure this is correct in all cases
		return MScale * MObject->GetCenterOfMass();
	}

	const ObjectType Object() const { return MObject; }
	
	virtual void Serialize(FChaosArchive& Ar) override
	{
		FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
		FImplicitObject::SerializeImp(Ar);
		Ar << MObject << MScale << MInvScale;
		TBox<T,d>::SerializeAsAABB(Ar, MLocalBoundingBox);
		ensure(MInternalThickness == 0);	//not supported: do we care?

		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::ScaledGeometryIsConcrete)
		{
			this->Type = MObject->GetType() | ImplicitObjectType::IsScaled;	//update type so downcasts work
		}
	}

	virtual uint32 GetTypeHash() const override
	{
		return HashCombine(MObject->GetTypeHash(), ::GetTypeHash(MScale));
	}

	virtual uint16 GetMaterialIndex(uint32 HintIndex) const override
	{
		return MObject->GetMaterialIndex(HintIndex);
	}

#if 0
	virtual TUniquePtr<FImplicitObject> Copy() const override
	{
		return TUniquePtr<FImplicitObject>(CopyHelper(this));
	}
#endif
private:
	ObjectType MObject;
	TSharedPtr<TConcrete, ESPMode::ThreadSafe> MSharedPtrForRefCount; // Temporary solution to force ref counting on trianglemesh from body setup.
	TVector<T, d> MScale;
	TVector<T, d> MInvScale;
	T MInternalThickness;	//Allows us to inflate the instance before the scale is applied. This is useful when sweeps need to apply a non scale on a geometry with uniform thickness
	TAABB<T, d> MLocalBoundingBox;

	//needed for serialization
	TImplicitObjectScaled()
	: FImplicitObject(EImplicitObject::HasBoundingBox, StaticType())
	, MInternalThickness(0)
	{}
	friend FImplicitObject;	//needed for serialization

	friend class FImplicitObjectScaled;

	static TImplicitObjectScaled<TConcrete, true>* CopyHelper(const TImplicitObjectScaled<TConcrete, true>* Obj)
	{
		return new TImplicitObjectScaled<TConcrete, true>(Obj->MObject, Obj->MScale, Obj->MInternalThickness);
	}

	static TImplicitObjectScaled<TConcrete, false>* CopyHelper(const TImplicitObjectScaled<TConcrete, false>* Obj)
	{
		return new TImplicitObjectScaled<TConcrete, false>(Obj->MObject->Copy(), Obj->MScale, Obj->MInternalThickness);
	}

	void UpdateBounds()
	{
		const TAABB<T, d> UnscaledBounds = MObject->BoundingBox();
		const TVector<T, d> Vector1 = UnscaledBounds.Min() * MScale;
		MLocalBoundingBox = TAABB<T, d>(Vector1, Vector1);	//need to grow it out one vector at a time in case scale is negative
		const TVector<T, d> Vector2 = UnscaledBounds.Max() *MScale;
		MLocalBoundingBox.GrowToInclude(Vector2);
	}

	template <typename QueryGeomType>
	static auto MakeScaledHelper(const QueryGeomType& B, const TVector<T,d>& InvScale )
	{
		TUniquePtr<QueryGeomType> HackBPtr(const_cast<QueryGeomType*>(&B));	//todo: hack, need scaled object to accept raw ptr similar to transformed implicit
		TImplicitObjectScaled<QueryGeomType> ScaledB(MakeSerializable(HackBPtr), InvScale);
		HackBPtr.Release();
		return ScaledB;
	}

	template <typename QueryGeomType>
	static auto MakeScaledHelper(const TImplicitObjectScaled<QueryGeomType>& B, const TVector<T,d>& InvScale)
	{
		//if scaled of scaled just collapse into one scaled
		TImplicitObjectScaled<QueryGeomType> ScaledB(B.Object(), InvScale * B.GetScale());
		return ScaledB;
	}

};

template <typename TConcrete>
using TImplicitObjectScaledNonSerializable = TImplicitObjectScaled<TConcrete, false>;

template <typename T, int d>
using TImplicitObjectScaledGeneric = TImplicitObjectScaled<FImplicitObject>;

}
