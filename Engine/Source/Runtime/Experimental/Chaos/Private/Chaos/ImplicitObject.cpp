// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/ImplicitObject.h"
#include "Chaos/BVHParticles.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/HeightField.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Levelset.h"
#include "Chaos/Plane.h"
#include "Chaos/Sphere.h"
#include "Chaos/TaperedCylinder.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "HAL/IConsoleManager.h"
#include "UObject/DestructionObjectVersion.h"

using namespace Chaos;

template<class T, int d>
TImplicitObject<T, d>::TImplicitObject(int32 Flags, ImplicitObjectType InType)
    : Type(InType)
    , bIsConvex(!!(Flags & EImplicitObject::IsConvex))
    , bIgnoreAnalyticCollisions(!!(Flags & EImplicitObject::IgnoreAnalyticCollisions))
    , bHasBoundingBox(!!(Flags & EImplicitObject::HasBoundingBox))
{
}

template<class T, int d>
TImplicitObject<T, d>::~TImplicitObject()
{
}

template<class T, int d>
ImplicitObjectType TImplicitObject<T, d>::GetType(bool bGetTrueType) const
{
	if (bIgnoreAnalyticCollisions && !bGetTrueType)
	{
		return ImplicitObjectType::Unknown;
	}
	return Type;
}

template<class T, int d>
bool TImplicitObject<T, d>::IsValidGeometry() const
{
	return true;
}

template<class T, int d>
TUniquePtr<TImplicitObject<T, d>> TImplicitObject<T, d>::Copy() const
{
	check(false);
	return nullptr;
}

template<class T, int d>
bool TImplicitObject<T, d>::IsUnderlyingUnion() const
{
	return Type == ImplicitObjectType::Union;
}

template<class T, int d>
T TImplicitObject<T, d>::SignedDistance(const TVector<T, d>& x) const
{
	TVector<T, d> Normal;
	return PhiWithNormal(x, Normal);
}

template<class T, int d>
TVector<T, d> TImplicitObject<T, d>::Normal(const TVector<T, d>& x) const
{
	TVector<T, d> Normal;
	PhiWithNormal(x, Normal);
	return Normal;
}

template<class T, int d>
TVector<T, d> TImplicitObject<T, d>::Support(const TVector<T, d>& Direction, const T Thickness) const
{
	check(false); //not a good implementation, don't use this
	return TVector<T, d>(0);
#if 0
	check(bHasBoundingBox);
	const TBox<T, d> Box = BoundingBox();
	TVector<T, d> EndPoint = Box.Center();
	TVector<T, d> StartPoint = EndPoint + Direction.GetSafeNormal() * (Box.Extents().Max() + Thickness);
	checkSlow(SignedDistance(StartPoint) > 0);
	checkSlow(SignedDistance(EndPoint) < 0);
	// @todo(mlentine): The termination condition is slightly different here so we can probably optimize by reimplementing for this function.
	const auto& Intersection = FindClosestIntersection(StartPoint, EndPoint, Thickness);
	check(Intersection.Second);
	return Intersection.First;
#endif
}

template<typename T, int d>
const TBox<T, d>& TImplicitObject<T, d>::BoundingBox() const
{
	check(false);
	static const TBox<T, d> Unbounded(TVector<T, d>(-FLT_MAX), TVector<T, d>(FLT_MAX));
	return Unbounded;
}

// @todo(mlentine): This is a lot of duplication from the collisions code that should be reduced
template<class T, int d>
Pair<TVector<T, d>, bool> TImplicitObject<T, d>::FindDeepestIntersection(const TImplicitObject<T, d>* Other, const TBVHParticles<float, d>* Particles, const PMatrix<T, d, d>& OtherToLocalTransform, const T Thickness) const
{
	// Do analytics
	// @todo(mlentine): Should we do a convex pass here?
	if (!Particles)
	{
		return MakePair(TVector<T, d>(0), false);
	}
	TVector<T, d> Point;
	T Phi = Thickness;
	if (HasBoundingBox())
	{
		TBox<T, d> ImplicitBox = BoundingBox().TransformedBox(OtherToLocalTransform.Inverse());
		ImplicitBox.Thicken(Thickness);
		TArray<int32> PotentialParticles = Particles->FindAllIntersections(ImplicitBox);
		for (int32 i : PotentialParticles)
		{
			TVector<T, d> LocalPoint = OtherToLocalTransform.TransformPosition(Particles->X(i));
			T LocalPhi = SignedDistance(LocalPoint);
			if (LocalPhi < Phi)
			{
				Phi = LocalPhi;
				Point = Particles->X(i);
			}
		}
	}
	else
	{
		return FindDeepestIntersection(Other, static_cast<const TParticles<float, d>*>(Particles), OtherToLocalTransform, Thickness);
	}
	return MakePair(Point, Phi < Thickness);
}

template<class T, int d>
Pair<TVector<T, d>, bool> TImplicitObject<T, d>::FindDeepestIntersection(const TImplicitObject<T, d>* Other, const TParticles<float, d>* Particles, const PMatrix<T, d, d>& OtherToLocalTransform, const T Thickness) const
{
	// Do analytics
	// @todo(mlentine): Should we do a convex pass here?
	if (!Particles)
	{
		return MakePair(TVector<T, d>(0), false);
	}
	TVector<T, d> Point;
	T Phi = Thickness;
	int32 NumParticles = Particles->Size();
	for (int32 i = 0; i < NumParticles; ++i)
	{
		TVector<T, d> LocalPoint = OtherToLocalTransform.TransformPosition(Particles->X(i));
		T LocalPhi = SignedDistance(LocalPoint);
		if (LocalPhi < Phi)
		{
			Phi = LocalPhi;
			Point = Particles->X(i);
		}
	}
	return MakePair(Point, Phi < Thickness);
}

template<class T, int d>
Pair<TVector<T, d>, bool> TImplicitObject<T, d>::FindClosestIntersection(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const
{
	constexpr T Epsilon = (T)1e-4;
	constexpr T EpsilonSquared = Epsilon * Epsilon;

	//Consider 0 thickness with Start sitting on abs(Phi) < Epsilon. This is a common case; for example a particle sitting perfectly on a floor. In this case intersection could return false.
	//If start is in this fuzzy region we simply return that spot snapped onto the surface. This is valid because low precision means we don't really know where we are, so let's take the cheapest option
	//If end is in this fuzzy region it is also a valid hit. However, there could be multiple hits between start and end and since we want the first one, we can't simply return this point.
	//As such we move end away from start (and out of the fuzzy region) so that we always get a valid intersection if no earlier ones exist
	//When Thickness > 0 the same idea applies, but we must consider Phi = (Thickness - Epsilon, Thickness + Epsilon)
	TVector<T, d> Normal;
	const T Phi = PhiWithNormal(StartPoint, Normal);
	if (FMath::IsNearlyEqual(Phi, Thickness, Epsilon))
	{
		return MakePair(TVector<T, d>(StartPoint - Normal * Phi), true); //snap to surface
	}

	TVector<T, d> ModifiedEnd = EndPoint;
	{
		const TVector<T, d> OriginalStartToEnd = (EndPoint - StartPoint);
		const T OriginalLength2 = OriginalStartToEnd.SizeSquared();
		if (OriginalLength2 < EpsilonSquared)
		{
			return MakePair(TVector<T, d>(0), false); //start was not close to surface, and end is very close to start so no hit
		}

		TVector<T, d> EndNormal;
		const T EndPhi = PhiWithNormal(EndPoint, EndNormal);
		if (FMath::IsNearlyEqual(EndPhi, Thickness, Epsilon))
		{
			//We want to push End out of the fuzzy region. Moving along the normal direction is best since direction could be nearly parallel with fuzzy band
			//To ensure an intersection, we must go along the normal, but in the same general direction as the ray.
			const TVector<T, d> OriginalDir = OriginalStartToEnd / FMath::Sqrt(OriginalLength2);
			const T Dot = TVector<T, d>::DotProduct(OriginalDir, EndNormal);
			if (FMath::IsNearlyZero(Dot, Epsilon))
			{
				//End is in the fuzzy region, and the direction from start to end is nearly parallel with this fuzzy band, so we should just return End since no other hits will occur
				return MakePair(TVector<T, d>(EndPoint - Normal * Phi), true); //snap to surface
			}
			else
			{
				ModifiedEnd = EndPoint + 2.f * Epsilon * FMath::Sign(Dot) * EndNormal; //get out of fuzzy region
			}
		}
	}

	return FindClosestIntersectionImp(StartPoint, ModifiedEnd, Thickness);
}

float ClosestIntersectionStepSizeMultiplier = 0.5f;
FAutoConsoleVariableRef CVarClosestIntersectionStepSizeMultiplier(TEXT("p.ClosestIntersectionStepSizeMultiplier"), ClosestIntersectionStepSizeMultiplier, TEXT("When raycasting we use this multiplier to substep the travel distance along the ray. Smaller number gives better accuracy at higher cost"));

template<class T, int d>
Pair<TVector<T, d>, bool> TImplicitObject<T, d>::FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const
{
	T Epsilon = (T)1e-4;

	TVector<T, d> Ray = EndPoint - StartPoint;
	T Length = Ray.Size();
	TVector<T, d> Direction = Ray.GetUnsafeNormal(); //this is safe because StartPoint and EndPoint were already tested to be far enough away. In the case where ModifiedEnd is pushed, we push it along the direction so it can only get farther
	TVector<T, d> EndNormal;
	const T EndPhi = PhiWithNormal(EndPoint, EndNormal);
	TVector<T, d> ClosestPoint = StartPoint;

	TVector<T, d> Normal;
	T Phi = PhiWithNormal(ClosestPoint, Normal);

	while (Phi > Thickness + Epsilon)
	{
		ClosestPoint += Direction * (Phi - Thickness) * (T)ClosestIntersectionStepSizeMultiplier;
		if ((ClosestPoint - StartPoint).Size() > Length)
		{
			if (EndPhi < Thickness + Epsilon)
			{
				return MakePair(TVector<T, d>(EndPoint + EndNormal * (-EndPhi + Thickness)), true);
			}
			return MakePair(TVector<T, d>(0), false);
		}
		// If the Change is too small we want to nudge it forward. This makes it possible to miss intersections very close to the surface but is more efficient and shouldn't matter much.
		if ((Phi - Thickness) < (T)1e-2)
		{
			ClosestPoint += Direction * (T)1e-2;
			if ((ClosestPoint - StartPoint).Size() > Length)
			{
				if (EndPhi < Thickness + Epsilon)
				{
					return MakePair(TVector<T, d>(EndPoint + EndNormal * (-EndPhi + Thickness)), true);
				}
				else
				{
					return MakePair(TVector<T, d>(0), false);
				}
			}
		}
		T NewPhi = PhiWithNormal(ClosestPoint, Normal);
		if (NewPhi >= Phi)
		{
			if (EndPhi < Thickness + Epsilon)
			{
				return MakePair(TVector<T, d>(EndPoint + EndNormal * (-EndPhi + Thickness)), true);
			}
			return MakePair(TVector<T, d>(0), false);
		}
		Phi = NewPhi;
	}
	if (Phi < Thickness + Epsilon)
	{
		ClosestPoint += Normal * (-Phi + Thickness);
	}
	return MakePair(ClosestPoint, true);
}

template<typename T, int d>
void TImplicitObject<T, d>::FindAllIntersectingObjects(TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>>& Out, const TBox<T, d>& LocalBounds) const
{
	if (!HasBoundingBox() || LocalBounds.Intersects(BoundingBox()))
	{
		Out.Add(MakePair(this, TRigidTransform<T, d>(TVector<T, d>(0), TRotation<T, d>::FromElements(TVector<T, d>(0), (T)1))));
	}
}

template<typename T, int d>
FArchive& TImplicitObject<T, d>::SerializeLegacyHelper(FArchive& Ar, TUniquePtr<TImplicitObject<T, d>>& Value)
{
	bool bExists = Value.Get() != nullptr;
	Ar << bExists;
	if (bExists)
	{
		if (Ar.IsLoading())
		{
			int8 ObjectType;
			Ar << ObjectType;
			switch ((ImplicitObjectType)ObjectType)
			{
			case ImplicitObjectType::Sphere: { Value = TUniquePtr<TSphere<T, d>>(new TSphere<T, d>()); break; }
			case ImplicitObjectType::Box: { Value = TUniquePtr<TBox<T, d>>(new TBox<T, d>()); break; }
			case ImplicitObjectType::Plane: { Value = TUniquePtr<TPlane<T, d>>(new TPlane<T, d>()); break; }
			case ImplicitObjectType::LevelSet: { Value = TUniquePtr<TLevelSet<T, d>>(new TLevelSet<T, d>()); break; }
			default: check(false);
			}
		}
		else
		{
			if (Value->Type == ImplicitObjectType::Sphere || Value->Type == ImplicitObjectType::Box || Value->Type == ImplicitObjectType::Plane || Value->Type == ImplicitObjectType::LevelSet)
			{
				Ar << Value->Type;
			}
			else
			{
				check(false); //should not be serializing this out
			}
		}
		Ar << *Value;
	}
	return Ar;
}

template<typename T, int d>
void TImplicitObject<T, d>::SerializeImp(FArchive& Ar)
{
	Ar.UsingCustomVersion(FDestructionObjectVersion::GUID);
	if (Ar.CustomVer(FDestructionObjectVersion::GUID) >= FDestructionObjectVersion::ChaosArchiveAdded)
	{
		Ar << bIsConvex << bIgnoreAnalyticCollisions;
	}
}

template<typename T, int d>
void TImplicitObject<T, d>::Serialize(FChaosArchive& Ar)
{
	SerializeImp(Ar);
}

template<typename T, int d>
const FName TImplicitObject<T, d>::GetTypeName(const ImplicitObjectType InType)
{
	static const FName SphereName = TEXT("Sphere");
	static const FName BoxName = TEXT("Box");
	static const FName PlaneName = TEXT("Plane");
	static const FName CapsuleName = TEXT("Capsule");
	static const FName TransformedName = TEXT("Transformed");
	static const FName UnionName = TEXT("Union");
	static const FName LevelSetName = TEXT("LevelSet");
	static const FName UnknownName = TEXT("Unknown");
	static const FName ConvexName = TEXT("Convex");
	static const FName TaperedCylinderName = TEXT("TaperedCylinder");
	static const FName CylinderName = TEXT("Cylinder");
	static const FName TriangleMeshName = TEXT("TriangleMesh");
	static const FName HeightFieldName = TEXT("HeightField");
	static const FName ScaledName = TEXT("Scaled");

	switch (InType)
	{
		case ImplicitObjectType::Sphere: return SphereName;
		case ImplicitObjectType::Box: return BoxName;
		case ImplicitObjectType::Plane: return PlaneName;
		case ImplicitObjectType::Capsule: return CapsuleName;
		case ImplicitObjectType::Transformed: return TransformedName;
		case ImplicitObjectType::Union: return UnionName;
		case ImplicitObjectType::LevelSet: return LevelSetName;
		case ImplicitObjectType::Unknown: return UnknownName;
		case ImplicitObjectType::Convex: return ConvexName;
		case ImplicitObjectType::TaperedCylinder: return TaperedCylinderName;
		case ImplicitObjectType::Cylinder: return CylinderName;
		case ImplicitObjectType::TriangleMesh: return TriangleMeshName;
		case ImplicitObjectType::HeightField: return HeightFieldName;
		case ImplicitObjectType::Scaled: return ScaledName;
	}
	return NAME_None;
}

template<typename T, int d>
TImplicitObject<T, d>* TImplicitObject<T, d>::SerializationFactory(FChaosArchive& Ar, TImplicitObject<T, d>* Obj)
{
	int8 ObjectType = Ar.IsLoading() ? 0 : (int8)Obj->Type;
	Ar << ObjectType;
	switch ((ImplicitObjectType)ObjectType)
	{
	case ImplicitObjectType::Sphere: if (Ar.IsLoading()) { return new TSphere<T, d>(); } break;
	case ImplicitObjectType::Box: if (Ar.IsLoading()) { return new TBox<T, d>(); } break;
	case ImplicitObjectType::Plane: if (Ar.IsLoading()) { return new TPlane<T, d>(); } break;
	case ImplicitObjectType::Capsule: if (Ar.IsLoading()) { return new TCapsule<T>(); } break;
	case ImplicitObjectType::Transformed: if (Ar.IsLoading()) { return new TImplicitObjectTransformed<T, d>(); } break;
	case ImplicitObjectType::Union: if (Ar.IsLoading()) { return new TImplicitObjectUnion<T, d>(); } break;
	case ImplicitObjectType::LevelSet: if (Ar.IsLoading()) { return new TLevelSet<T, d>(); } break;
	case ImplicitObjectType::Convex: if (Ar.IsLoading()) { return new TConvex<T, d>(); } break;
	case ImplicitObjectType::TaperedCylinder: if (Ar.IsLoading()) { return new TTaperedCylinder<T>(); } break;
	case ImplicitObjectType::TriangleMesh: if (Ar.IsLoading()) { return new TTriangleMeshImplicitObject<T>(); } break;
	case ImplicitObjectType::Scaled: if (Ar.IsLoading()) { return new TImplicitObjectScaled<T,d>(); } break;
	case ImplicitObjectType::HeightField: if (Ar.IsLoading()) { return new THeightField<T>(); } break;
	case ImplicitObjectType::Cylinder: if (Ar.IsLoading()) { return new TCylinder<T>(); } break;
	default:
		check(false);
	}
	return nullptr;
}

//template class Chaos::TImplicitObject<float, 2>; // Missing 2D Rotation class
template class Chaos::TImplicitObject<float, 3>;
