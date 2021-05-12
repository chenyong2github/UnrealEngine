// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Box.h"
#include "Chaos/Convex.h"
#include "Chaos/ImplicitFwd.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Transform.h"
#include "Chaos/Utilities.h"	// For ScaleInertia - pull that into mass utils
#include "ChaosArchive.h"
#include "Templates/ChooseClass.h"
#include "Templates/EnableIf.h"
#include "Math/NumericLimits.h"
#include "ChaosCheck.h"

namespace Chaos
{

struct FMTDInfo;

class FImplicitObjectInstanced : public FImplicitObject
{
public:
	FImplicitObjectInstanced(int32 Flags, EImplicitObjectType InType)
        : FImplicitObject(Flags, InType | ImplicitObjectType::IsInstanced)
		, OuterMargin(0)
	{
	}

	virtual TSerializablePtr<FImplicitObject> GetInnerObject() const
	{
		return TSerializablePtr<FImplicitObject>();
	}

	// Returns a winding order multiplier used in the manifold clipping and required when we have negative scales
	FORCEINLINE FReal GetWindingOrder() const
	{
		return 1.0f;
	}

	
protected:
	FReal OuterMargin;
};
	
template <typename TConcrete>
class TImplicitObjectInstanced final : public FImplicitObjectInstanced
{
public:
	using T = typename TConcrete::TType;
	using TType = T;
	static constexpr int d = TConcrete::D;
	static constexpr int D = d;
	using ObjectType = TSharedPtr<TConcrete,ESPMode::ThreadSafe>;

	using FImplicitObject::GetTypeName;

	//needed for serialization
	TImplicitObjectInstanced()
		: FImplicitObjectInstanced(EImplicitObject::HasBoundingBox,StaticType())
	{
		this->OuterMargin = 0;
	}

	TImplicitObjectInstanced(const ObjectType&& Object, const FReal InMargin = 0)
		: FImplicitObjectInstanced(EImplicitObject::HasBoundingBox, Object->GetType())
		, MObject(MoveTemp(Object))
	{
		ensure(IsInstanced(MObject->GetType()) == false);	//cannot have an instance of an instance
		this->bIsConvex = MObject->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		this->OuterMargin = InMargin;
		SetMargin(OuterMargin + MObject->GetMargin());
	}

	TImplicitObjectInstanced(const ObjectType& Object, const FReal InMargin = 0)
		: FImplicitObjectInstanced(EImplicitObject::HasBoundingBox,Object->GetType())
		, MObject(Object)
	{
		ensure(IsInstanced(MObject->GetType()) == false);	//cannot have an instance of an instance
		this->bIsConvex = MObject->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		this->OuterMargin = InMargin;
		SetMargin(OuterMargin + MObject->GetMargin());
	}

	TImplicitObjectInstanced(TImplicitObjectInstanced<TConcrete>&& Other)
		: FImplicitObjectInstanced(EImplicitObject::HasBoundingBox, Other.MObject->GetType())
		, MObject(MoveTemp(Other.MObject))
	{
		ensureMsgf((IsScaled(MObject->GetType()) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType()) == false), TEXT("Scaled objects should not contain instances."));
		this->bIsConvex = Other.MObject->IsConvex();
		this->bDoCollide = Other.MObject->GetDoCollide();
		this->OuterMargin = Other.OuterMargin;
		SetMargin(Other.GetMargin());
	}

	static constexpr EImplicitObjectType StaticType()
	{
		return TConcrete::StaticType() | ImplicitObjectType::IsInstanced;
	}

	virtual TSerializablePtr<FImplicitObject> GetInnerObject() const override
	{
		return MakeSerializable(MObject);
	}
	
	const TConcrete* GetInstancedObject() const
	{
		return MObject.Get();
	}

	FReal GetRadius() const
	{
		return MObject->GetRadius();
	}

	bool GetDoCollide() const
	{
		return MObject->GetDoCollide();
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

	// The support position from the specified direction
	FORCEINLINE TVector<T, d> Support(const TVector<T, d>& Direction, const T Thickness) const
	{
		return MObject->Support(Direction, Thickness); 
	}

	// this shouldn't be called, but is required until we remove the explicit function implementations in CollisionResolution.cpp
	FORCEINLINE TVector<T, d> SupportScaled(const TVector<T, d>& Direction, const T Thickness, const FVec3& Scale) const
	{
		return MObject->SupportScaled(Direction, Thickness, Scale);
	}

	// The support position from the specified direction, if the shape is reduced by the margin
	FORCEINLINE TVector<T, d> SupportCore(const TVector<T, d>& Direction, FReal InMargin) const
	{
		return MObject->SupportCore(Direction, InMargin);
	}

	virtual const TAABB<T, d> BoundingBox() const override 
	{ 
		return MObject->BoundingBox();
	}

	const ObjectType Object() const { return MObject; }

	virtual uint32 GetTypeHash() const override
	{
		return MObject->GetTypeHash();
	}

	virtual TUniquePtr<FImplicitObject> Copy() const override
	{
		return TUniquePtr<FImplicitObject>(CopyHelper(this));
	}

	static const TImplicitObjectInstanced<TConcrete>& AsInstancedChecked(const FImplicitObject& Obj)
	{
		if(TIsSame<TConcrete,FImplicitObject>::Value)
		{
			//can cast any instanced to ImplicitObject base
			check(IsInstanced(Obj.GetType()));
		} else
		{
			check(StaticType() == Obj.GetType());
		}
		return static_cast<const TImplicitObjectInstanced<TConcrete>&>(Obj);
	}

	/** This is a low level function and assumes the internal object has a SweepGeom function. Should not be called directly. See GeometryQueries.h : SweepQuery */
	template <typename QueryGeomType>
	bool LowLevelSweepGeom(const QueryGeomType& B,const TRigidTransform<T,d>& BToATM,const TVector<T,d>& LocalDir,const T Length,T& OutTime,TVector<T,d>& LocalPosition,TVector<T,d>& LocalNormal,int32& OutFaceIndex,T Thickness = 0,bool bComputeMTD = false) const
	{
		return MObject->SweepGeom(B,BToATM,LocalDir,Length,OutTime,LocalPosition,LocalNormal,OutFaceIndex,Thickness,bComputeMTD);
	}

	/** This is a low level function and assumes the internal object has a OverlapGeom function. Should not be called directly. See GeometryQueries.h : OverlapQuery */
	template <typename QueryGeomType>
	bool LowLevelOverlapGeom(const QueryGeomType& B,const TRigidTransform<T,d>& BToATM,T Thickness = 0, FMTDInfo* OutMTD = nullptr) const
	{
		return MObject->OverlapGeom(B,BToATM,Thickness, OutMTD);
	}

	virtual uint16 GetMaterialIndex(uint32 HintIndex) const
	{
		return MObject->GetMaterialIndex(HintIndex);
	}

	// Get the index of the plane that most opposes the normal
	int32 GetMostOpposingPlane(const FVec3& Normal) const
	{
		return MObject->GetMostOpposingPlane(Normal);
	}

	// Get the nearest point on an edge of the specified face
	FVec3 GetClosestEdgePosition(int32 PlaneIndex, const FVec3& Position) const
	{
		return MObject->GetClosestEdgePosition(PlaneIndex, Position);
	}

	bool GetClosestEdgeVertices(int32 PlaneIndexHint, const FVec3& Position, int32& OutVertexIndex0, int32& OutVertexIndex1) const
	{
		return MObject->GetClosestEdgeVertices(PlaneIndexHint, Position, OutVertexIndex0, OutVertexIndex1);
	}

	// Get an array of all the plane indices that belong to a vertex (up to MaxVertexPlanes).
	// Returns the number of planes found.
	int32 FindVertexPlanes(int32 VertexIndex, int32* OutVertexPlanes, int32 MaxVertexPlanes) const
	{
		return MObject->FindVertexPlanes(VertexIndex, OutVertexPlanes, MaxVertexPlanes);
	}

	// The number of vertices that make up the corners of the specified face
	int32 NumPlaneVertices(int32 PlaneIndex) const
	{
		return MObject->NumPlaneVertices(PlaneIndex);
	}

	// Get the vertex index of one of the vertices making up the corners of the specified face
	int32 GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const
	{
		return MObject->GetPlaneVertex(PlaneIndex, PlaneVertexIndex);
	}

	int32 GetEdgeVertex(int32 EdgeIndex, int32 EdgeVertexIndex) const
	{
		return MObject->GetEdgeVertex(EdgeIndex, EdgeVertexIndex);
	}

	int32 GetEdgePlane(int32 EdgeIndex, int32 EdgePlaneIndex) const
	{
		return MObject->GetEdgePlane(EdgeIndex, EdgePlaneIndex);
	}

	int32 NumPlanes() const
	{
		return MObject->NumPlanes();
	}

	int32 NumEdges() const
	{
		return MObject->NumEdges();
	}

	int32 NumVertices() const
	{
		return MObject->NumVertices();
	}

	// Get the plane at the specified index (e.g., indices from FindVertexPlanes)
	const TPlaneConcrete<FReal, 3> GetPlane(int32 FaceIndex) const
	{
		return MObject->GetPlane(FaceIndex);
	}

	// Get the vertex at the specified index (e.g., indices from GetPlaneVertexs)
	const FVec3 GetVertex(int32 VertexIndex) const
	{
		return MObject->GetVertex(VertexIndex);
	}

	const FVec3 GetCenterOfMass() const
	{
		return MObject->GetCenterOfMass();
	}

	FRotation3 GetRotationOfMass() const
	{
		return GetRotationOfMass();
	}

	const FMatrix33 GetInertiaTensor(const FReal Mass) const
	{
		return MObject->GetInertiaTensor(Mass);
	}

protected:
	ObjectType MObject;

	static TImplicitObjectInstanced<TConcrete>* CopyHelper(const TImplicitObjectInstanced<TConcrete>* Obj)
	{
		return new TImplicitObjectInstanced<TConcrete>(Obj->MObject);
	}
};

class FImplicitObjectScaled : public FImplicitObject
{
public:
	FImplicitObjectScaled(int32 Flags, EImplicitObjectType InType)
		: FImplicitObject(Flags, InType | ImplicitObjectType::IsScaled)
		, MScale(1)
		, MInvScale(1)
		, OuterMargin(0.0f)
		, MLocalBoundingBox(FAABB3::EmptyAABB())
	{
	}

	virtual TSerializablePtr<FImplicitObject> GetInnerObject() const
	{
		return TSerializablePtr<FImplicitObject>();
	}

	// Returns a winding order multiplier used in the manifold clipping and required when we have negative scales
	FORCEINLINE FReal GetWindingOrder() const
	{
		const FVec3 SignVector = MScale.GetSignVector();
		return SignVector.X * SignVector.Y * SignVector.Z;
	}

	const FVec3& GetScale() const
	{
		return MScale;
	}

	const FVec3& GetInvScale() const
	{
		return MInvScale;
	}
	
	virtual const FAABB3 BoundingBox() const override
	{
		return MLocalBoundingBox;
	}

protected:
	FVec3 MScale;
	FVec3 MInvScale;
	FReal OuterMargin;	//Allows us to inflate the instance before the scale is applied. This is useful when sweeps need to apply a non scale on a geometry with uniform thickness
	FAABB3 MLocalBoundingBox;
};

template<typename TConcrete, bool bInstanced = true>
class TImplicitObjectScaled final : public FImplicitObjectScaled
{
public:
	using T = typename TConcrete::TType;
	using TType = T;
	static constexpr int d = TConcrete::D;
	static constexpr int D = d;

	using ObjectType = typename TChooseClass<bInstanced, TSerializablePtr<TConcrete>, TUniquePtr<TConcrete>>::Result;
	using FImplicitObject::GetTypeName;

	TImplicitObjectScaled(ObjectType Object, const TVector<T, d>& Scale, T InMargin = 0)
	    : FImplicitObjectScaled(EImplicitObject::HasBoundingBox, Object->GetType())
	    , MObject(MoveTemp(Object))
		, MSharedPtrForRefCount(nullptr)
	{
		ensureMsgf((IsScaled(MObject->GetType()) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType()) == false), TEXT("Scaled objects should not contain instances."));
		switch (MObject->GetType())
		{
		case ImplicitObjectType::Transformed:
		case ImplicitObjectType::Union:
			check(false);	//scale is only supported for concrete types like sphere, capsule, convex, levelset, etc... Nothing that contains other objects
		default:
			break;
		}
		this->bIsConvex = MObject->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		this->OuterMargin = InMargin;
		SetScale(Scale);
	}

	TImplicitObjectScaled(TSharedPtr<TConcrete, ESPMode::ThreadSafe> Object, const TVector<T, d>& Scale, T InMargin = 0)
	    : FImplicitObjectScaled(EImplicitObject::HasBoundingBox, Object->GetType())
	    , MObject(MakeSerializable<TConcrete, ESPMode::ThreadSafe>(Object))
		, MSharedPtrForRefCount(Object)
	{
		ensureMsgf((IsScaled(MObject->GetType()) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType()) == false), TEXT("Scaled objects should not contain instances."));
		switch (MObject->GetType())
		{
		case ImplicitObjectType::Transformed:
		case ImplicitObjectType::Union:
			check(false);	//scale is only supported for concrete types like sphere, capsule, convex, levelset, etc... Nothing that contains other objects
		default:
			break;
		}
		this->bIsConvex = MObject->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		this->OuterMargin = InMargin;
		SetScale(Scale);
	}

	TImplicitObjectScaled(ObjectType Object, TUniquePtr<Chaos::FImplicitObject> &&ObjectOwner, const TVector<T, d>& Scale, T InMargin = 0)
	    : FImplicitObjectScaled(EImplicitObject::HasBoundingBox, Object->GetType())
	    , MObject(Object)
		, MSharedPtrForRefCount(nullptr)
	{
		ensureMsgf((IsScaled(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType(true)) == false), TEXT("Scaled objects should not contain instances."));
		this->bIsConvex = Object->IsConvex();
		this->bDoCollide = MObject->GetDoCollide();
		this->OuterMargin = InMargin;
		SetScale(Scale);
	}

	TImplicitObjectScaled(const TImplicitObjectScaled<TConcrete, bInstanced>& Other) = delete;
	TImplicitObjectScaled(TImplicitObjectScaled<TConcrete, bInstanced>&& Other)
	    : FImplicitObjectScaled(EImplicitObject::HasBoundingBox, Other.MObject->GetType() | ImplicitObjectType::IsScaled)
	    , MObject(MoveTemp(Other.MObject))
		, MSharedPtrForRefCount(MoveTemp(Other.MSharedPtrForRefCount))
	{
		ensureMsgf((IsScaled(MObject->GetType()) == false), TEXT("Scaled objects should not contain each other."));
		ensureMsgf((IsInstanced(MObject->GetType()) == false), TEXT("Scaled objects should not contain instances."));
		this->bIsConvex = Other.MObject->IsConvex();
		this->bDoCollide = Other.MObject->GetDoCollide();
		this->OuterMargin = Other.OuterMargin;
		this->MScale = Other.MScale;
        this->MInvScale = Other.MInvScale;
        this->OuterMargin = Other.OuterMargin;
        this->MLocalBoundingBox = Other.MLocalBoundingBox;
        SetMargin(Other.GetMargin());
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

	virtual TSerializablePtr<FImplicitObject> GetInnerObject() const override
	{
		return MakeSerializable(MObject);
	}

	const TConcrete* GetUnscaledObject() const
	{
		return MObject.Get();
	}

	FReal GetRadius() const
	{
		return (MObject->GetRadius() > 0.0f) ? Margin : 0.0f;
	}
	
	virtual T PhiWithNormal(const TVector<T, d>& X, TVector<T, d>& Normal) const override
	{
		return MObject->PhiWithNormalScaled(X, MScale, Normal);
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
			T UnscaledTime;

			if (MObject->Raycast(UnscaledStart, UnscaledDir, UnscaledLength, Thickness * MInvScale[0], UnscaledTime, UnscaledPosition, UnscaledNormal, OutFaceIndex))
			{
				//We double check that NewTime < Length because of potential precision issues. When that happens we always keep the shortest hit first
				const T NewTime = LengthScaleInv * UnscaledTime;
				if (NewTime < Length && NewTime != 0) // Normal/Position output may be uninitialized with TOI 0.
				{
					OutPosition = MScale * UnscaledPosition;
					OutNormal = (MInvScale * UnscaledNormal).GetSafeNormal(TNumericLimits<T>::Min());
					OutTime = NewTime;
					return true;
				}
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
			T UnscaledTime;

			auto ScaledB = MakeScaledHelper(B, MInvScale);

			TRigidTransform<T, d> BToATMNoScale(BToATM.GetLocation() * MInvScale, BToATM.GetRotation());
			
			if (MObject->SweepGeom(ScaledB, BToATMNoScale, UnscaledDir, UnscaledLength, UnscaledTime, UnscaledPosition, UnscaledNormal, OutFaceIndex, Thickness, bComputeMTD, MScale))
			{
				const T NewTime = LengthScaleInv * UnscaledTime;
				//We double check that NewTime < Length because of potential precision issues. When that happens we always keep the shortest hit first
				if (NewTime < Length)
				{
					OutTime = NewTime;
					LocalPosition = MScale * UnscaledPosition;
					LocalNormal = (MInvScale * UnscaledNormal).GetSafeNormal();
					return true;
				}
			}
		}

		return false;
	}

	template <typename QueryGeomType>
	bool GJKContactPoint(const QueryGeomType& A, const FRigidTransform3& AToBTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& Penetration) const
	{
		TRigidTransform<T, d> AToBTMNoScale(AToBTM.GetLocation() * MInvScale, AToBTM.GetRotation());

		auto ScaledA = MakeScaledHelper(A, MInvScale);
		return MObject->GJKContactPoint(ScaledA, AToBTMNoScale, Thickness, Location, Normal, Penetration, MScale);
	}

	/** This is a low level function and assumes the internal object has a OverlapGeom function. Should not be called directly. See GeometryQueries.h : OverlapQuery */
	template <typename QueryGeomType>
	bool LowLevelOverlapGeom(const QueryGeomType& B, const TRigidTransform<T, d>& BToATM, T Thickness = 0, FMTDInfo* OutMTD = nullptr) const
	{
		ensure(Thickness == 0 || (FMath::IsNearlyEqual(MScale[0], MScale[1]) && FMath::IsNearlyEqual(MScale[0], MScale[2])));

		auto ScaledB = MakeScaledHelper(B, MInvScale);
		TRigidTransform<T, d> BToATMNoScale(BToATM.GetLocation() * MInvScale, BToATM.GetRotation());
		return MObject->OverlapGeom(ScaledB, BToATMNoScale, Thickness, OutMTD, MScale);
	}

	// Get the index of the plane that most opposes the normal
	int32 GetMostOpposingPlane(const FVec3& Normal) const
	{
		return MObject->GetMostOpposingPlaneScaled(Normal, MScale);
	}

	// Get the nearest point on an edge of the specified face
	FVec3 GetClosestEdgePosition(int32 PlaneIndex, const FVec3& Position) const
	{
		return MObject->GetClosestEdgePosition(PlaneIndex, MInvScale * Position) * MScale;
	}

	bool GetClosestEdgeVertices(int32 PlaneIndex, const FVec3& Position, int32& OutVertexIndex0, int32& OutVertexIndex1) const
	{
		return MObject->GetClosestEdgeVertices(PlaneIndex, MInvScale * Position, OutVertexIndex0, OutVertexIndex1);
	}

	// Get an array of all the plane indices that belong to a vertex (up to MaxVertexPlanes).
	// Returns the number of planes found.
	int32 FindVertexPlanes(int32 VertexIndex, int32* OutVertexPlanes, int32 MaxVertexPlanes) const
	{
		return MObject->FindVertexPlanes(VertexIndex, OutVertexPlanes, MaxVertexPlanes);
	}

	// The number of vertices that make up the corners of the specified face
	int32 NumPlaneVertices(int32 PlaneIndex) const
	{
		return MObject->NumPlaneVertices(PlaneIndex);
	}

	// Get the vertex index of one of the vertices making up the corners of the specified face
	int32 GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const
	{
		return MObject->GetPlaneVertex(PlaneIndex, PlaneVertexIndex);
	}

	int32 GetEdgeVertex(int32 EdgeIndex, int32 EdgeVertexIndex) const
	{
		return MObject->GetEdgeVertex(EdgeIndex, EdgeVertexIndex);
	}

	int32 GetEdgePlane(int32 EdgeIndex, int32 EdgePlaneIndex) const
	{
		return MObject->GetEdgePlane(EdgeIndex, EdgePlaneIndex);
	}

	int32 NumPlanes() const
	{
		return MObject->NumPlanes();
	}

	int32 NumEdges() const
	{
		return MObject->NumEdges();
	}

	int32 NumVertices() const
	{
		return MObject->NumVertices();
	}

	// Get the plane at the specified index (e.g., indices from FindVertexPlanes)
	const TPlaneConcrete<FReal, 3> GetPlane(int32 FaceIndex) const
	{
		const TPlaneConcrete<FReal, 3> InnerPlane = MObject->GetPlane(FaceIndex);
		return TPlaneConcrete<FReal, 3>::MakeScaledUnsafe(InnerPlane, MScale);	// "Unsafe" means scale has no zeros
	}

	// Get the vertex at the specified index (e.g., indices from GetPlaneVertex)
	const FVec3 GetVertex(int32 VertexIndex) const
	{
		const FVec3 InnerVertex = MObject->GetVertex(VertexIndex);
		return MScale * InnerVertex;
	}


	virtual int32 FindMostOpposingFace(const TVector<T, d>& Position, const TVector<T, d>& UnitDir, int32 HintFaceIndex, T SearchDist) const override
	{
		ensure(FMath::IsNearlyEqual(UnitDir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
		return MObject->FindMostOpposingFaceScaled(Position, UnitDir, HintFaceIndex, SearchDist, MScale);
	}

	virtual TVector<T, 3> FindGeometryOpposingNormal(const TVector<T, d>& DenormDir, int32 HintFaceIndex, const TVector<T, d>& OriginalNormal) const override
	{
		//ensure(OuterMargin == 0);	//not supported: do we care?
		ensure(FMath::IsNearlyEqual(OriginalNormal.SizeSquared(), 1, KINDA_SMALL_NUMBER));

		// Get unscaled dir and normal
		const TVector<T, 3> LocalDenormDir = DenormDir * MScale;
		const TVector<T, 3> LocalOriginalNormalDenorm = OriginalNormal * MScale;
		const T NormalLengthScale = LocalOriginalNormalDenorm.Size();
		const TVector<T, 3> LocalOriginalNormal
			= ensure(NormalLengthScale > SMALL_NUMBER)
			? LocalOriginalNormalDenorm / NormalLengthScale
			: TVector<T, d>(0, 0, 1);

		// Compute final normal
		const TVector<T, d> LocalNormal = MObject->FindGeometryOpposingNormal(LocalDenormDir, HintFaceIndex, LocalOriginalNormal);
		TVector<T, d> Normal = LocalNormal * MInvScale;
		if (CHAOS_ENSURE(Normal.SafeNormalize(TNumericLimits<T>::Min())) == 0)
		{
			Normal = TVector<T,3>(0,0,1);
		}

		return Normal;
	}

	virtual bool Overlap(const TVector<T, d>& Point, const T Thickness) const override
	{
		const TVector<T, d> UnscaledPoint = MInvScale * Point;

		// TODO: consider alternative that handles thickness scaling properly in 3D, only works for uniform scaling right now
		const T UnscaleThickness = MInvScale[0] * Thickness; 

		return MObject->Overlap(UnscaledPoint, UnscaleThickness);
	}

	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
	{
		ensure(OuterMargin == 0);	//not supported: do we care?
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
		const TVector<T, d> UnthickenedPt = MObject->Support(Direction * MScale, 0.0f) * MScale;
		return Thickness > 0 ? TVector<T, d>(UnthickenedPt + Direction.GetSafeNormal() * Thickness) : UnthickenedPt;
	}

	FORCEINLINE_DEBUGGABLE TVector<T, d> SupportCore(const TVector<T, d>& Direction, FReal InMargin) const
	{
		return MObject->SupportCoreScaled(Direction, InMargin, MScale);
	}

	void SetScale(const FVec3& Scale)
	{
		constexpr FReal MinMagnitude = 1e-6;
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			if (!CHAOS_ENSURE(FMath::Abs(Scale[Axis]) >= MinMagnitude))
			{
				MScale[Axis] = MinMagnitude;
			}
			else
			{
				MScale[Axis] = Scale[Axis];
			}

			MInvScale[Axis] = 1 / MScale[Axis];
		}
		SetMargin(OuterMargin + MScale[0] * MObject->GetMargin());
		UpdateBounds();
	}

	const FReal GetVolume() const
	{
		return MScale.X * MScale.Y * MScale.Z * MObject->GetVolume();
	}

	const FVec3 GetCenterOfMass() const
	{
		return MScale * MObject->GetCenterOfMass();
	}

	FRotation3 GetRotationOfMass() const
	{
		return MObject->GetRotationOfMass();
	}

	const FMatrix33 GetInertiaTensor(const FReal Mass) const
	{
		return Utilities::ScaleInertia(MObject->GetInertiaTensor(Mass), MScale, false);
	}

	const ObjectType Object() const { return MObject; }

	// Only should be retrieved for copy purposes. Do not modify or access.
	TSharedPtr<TConcrete, ESPMode::ThreadSafe> GetSharedObject() const { return MSharedPtrForRefCount; }
	
	virtual void Serialize(FChaosArchive& Ar) override
	{
		FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName(), false);
		FImplicitObject::SerializeImp(Ar);
		Ar << MObject << MScale << MInvScale;
		TBox<T,d>::SerializeAsAABB(Ar, MLocalBoundingBox);
		ensure(OuterMargin == 0);	//not supported: do we care?

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

	//needed for serialization
	TImplicitObjectScaled()
	: FImplicitObjectScaled(EImplicitObject::HasBoundingBox, StaticType())
	{}
	friend FImplicitObject;	//needed for serialization

	static TImplicitObjectScaled<TConcrete, true>* CopyHelper(const TImplicitObjectScaled<TConcrete, true>* Obj)
	{
		return new TImplicitObjectScaled<TConcrete, true>(Obj->MObject, Obj->MScale, Obj->OuterMargin);
	}

	static TImplicitObjectScaled<TConcrete, false>* CopyHelper(const TImplicitObjectScaled<TConcrete, false>* Obj)
	{
		return new TImplicitObjectScaled<TConcrete, false>(Obj->MObject->Copy(), Obj->MScale, Obj->OuterMargin);
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
