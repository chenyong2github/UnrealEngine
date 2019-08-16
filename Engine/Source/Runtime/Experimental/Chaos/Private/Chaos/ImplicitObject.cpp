// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/ImplicitObject.h"
#include "Chaos/BVHParticles.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
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
TVector<T, d> TImplicitObject<T, d>::Support(const TVector<T, d>& Direction, const T Thickness) const
{
	check(false);	//not a good implementation, don't use this
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
	T Epsilon = (T)1e-4;
	//Consider 0 thickness with Start sitting on abs(Phi) < Epsilon. This is a common case; for example a particle sitting perfectly on a floor. In this case intersection could return false.
	//If start is in this fuzzy region we simply return that spot snapped onto the surface. This is valid because low precision means we don't really know where we are, so let's take the cheapest option
	//If end is in this fuzzy region it is also a valid hit. However, there could be multiple hits between start and end and since we want the first one, we can't simply return this point.
	//As such we move end away from start (and out of the fuzzy region) so that we always get a valid intersection if no earlier ones exist
	//When Thickness > 0 the same idea applies, but we must consider Phi = (Thickness - Epsilon, Thickness + Epsilon)
	TVector<T, d> Normal;
	T Phi = PhiWithNormal(StartPoint, Normal);

	if (FMath::IsNearlyEqual(Phi, Thickness, Epsilon))
	{
		return MakePair(TVector<T,d>(StartPoint - Normal * Phi), true); //snap to surface
	}

	TVector<T, d> ModifiedEnd = EndPoint;
	{
		const TVector<T, d> OriginalStartToEnd = (EndPoint - StartPoint);
		const T OriginalLength = OriginalStartToEnd.Size();
		if (OriginalLength < Epsilon)
		{
			return MakePair(TVector<T, d>(0), false); //start was not close to surface, and end is very close to start so no hit
		}
		const TVector<T, d> OriginalDir = OriginalStartToEnd / OriginalLength;

		TVector<T, d> EndNormal;
		T EndPhi = PhiWithNormal(EndPoint, EndNormal);
		if (FMath::IsNearlyEqual(EndPhi, Thickness, Epsilon))
		{
			//We want to push End out of the fuzzy region. Moving along the normal direction is best since direction could be nearly parallel with fuzzy band
			//To ensure an intersection, we must go along the normal, but in the same general direction as the ray.
			const T Dot = TVector<T, d>::DotProduct(OriginalDir, EndNormal);
			if (FMath::IsNearlyZero(Dot, Epsilon))
			{
				//End is in the fuzzy region, and the direction from start to end is nearly parallel with this fuzzy band, so we should just return End since no other hits will occur
				return MakePair(TVector<T,d>(EndPoint - Normal * Phi), true); //snap to surface
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
		Out.Add(MakePair(this, TRigidTransform<T, d>(TVector<T, d>(0), TRotation<T, d>(TVector<T, d>(0), (T)1))));
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
void TImplicitObject<T, d>::StaticSerialize(FChaosArchive& Ar, TSerializablePtr<TImplicitObject<T, d>>& Serializable)
{
	TImplicitObject<T, d>* ImplicitObject = const_cast<TImplicitObject<T, d>*>(Serializable.Get());
	int8 ObjectType = Ar.IsLoading() ? 0 : (int8)ImplicitObject->Type;
	Ar << ObjectType;

	if (Ar.IsLoading())
	{
		switch ((ImplicitObjectType)ObjectType)
		{
		case ImplicitObjectType::Sphere: { ImplicitObject = new TSphere<T, d>(); break; }
		case ImplicitObjectType::Box: { ImplicitObject = new TBox<T, d>(); break; }
		case ImplicitObjectType::Plane: { ImplicitObject = new TPlane<T, d>(); break; }
		case ImplicitObjectType::Capsule: { ImplicitObject = new TCapsule<T>(); break; }
		case ImplicitObjectType::Transformed: { ImplicitObject = new TImplicitObjectTransformed<T, d>(); break; }
		case ImplicitObjectType::Union: { ImplicitObject = new TImplicitObjectUnion<T, d>(); break; }
		case ImplicitObjectType::LevelSet: { ImplicitObject = new TLevelSet<T, d>(); break; }
		case ImplicitObjectType::Convex: { ImplicitObject = new TConvex<T, d>(); break; }
		case ImplicitObjectType::TaperedCylinder: { ImplicitObject = new TTaperedCylinder<T>(); break; }
		case ImplicitObjectType::TriangleMesh: { ImplicitObject = new TTriangleMeshImplicitObject<T>(); break; }
		default:
			check(false);
		}

		Serializable.SetFromRawLowLevel(ImplicitObject);
	}
	else
	{
		switch ((ImplicitObjectType)ObjectType)
		{
		case ImplicitObjectType::Sphere:
		case ImplicitObjectType::Box:
		case ImplicitObjectType::Plane:
		case ImplicitObjectType::Capsule:
		case ImplicitObjectType::Transformed:
		case ImplicitObjectType::Union:
		case ImplicitObjectType::LevelSet:
		case ImplicitObjectType::Convex:
		case ImplicitObjectType::TaperedCylinder:
		case ImplicitObjectType::TriangleMesh:
			break;
		default:
			check(false);
		}
	}
	ImplicitObject->Serialize(Ar);
}

//template class Chaos::TImplicitObject<float, 2>; // Missing 2D Rotation class
template class Chaos::TImplicitObject<float, 3>;