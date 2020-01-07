// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "ChaosArchive.h"

namespace Chaos
{
template<class T, int d>
class TPlane final : public FImplicitObject
{
  public:
	using FImplicitObject::GetTypeName;


	TPlane() : FImplicitObject(0, ImplicitObjectType::Plane) {}	//needed for serialization
	TPlane(const TVector<T, d>& InX, const TVector<T, d>& InNormal)
	    : FImplicitObject(0, ImplicitObjectType::Plane)
	    , MX(InX)
	    , MNormal(InNormal)
	{
	}
	TPlane(const TPlane<T, d>& Other)
	    : FImplicitObject(0, ImplicitObjectType::Plane)
	    , MX(Other.MX)
	    , MNormal(Other.MNormal)
	{
	}
	TPlane(TPlane<T, d>&& Other)
	    : FImplicitObject(0, ImplicitObjectType::Plane)
	    , MX(MoveTemp(Other.MX))
	    , MNormal(MoveTemp(Other.MNormal))
	{
	}
	virtual ~TPlane() {}

	static constexpr EImplicitObjectType StaticType()
	{
		return ImplicitObjectType::Plane;
	}

	/**
	 * Phi is positive on the side of the normal, and negative otherwise.
	 */
	T SignedDistance(const TVector<T, d>& x) const
	{
		return TVector<T, d>::DotProduct(x - MX, MNormal);
	}

	/**
	 * Phi is positive on the side of the normal, and negative otherwise.
	 */
	virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override
	{
		Normal = MNormal;
		return TVector<T, d>::DotProduct(x - MX, MNormal);
	}

	TVector<T, d> FindClosestPoint(const TVector<T, d>& x, const T Thickness = (T)0) const
	{
		auto Dist = TVector<T, d>::DotProduct(x - MX, MNormal) - Thickness;
		return x - TVector<T, d>(Dist * MNormal);
	}

	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
	{
		ensure(FMath::IsNearlyEqual(Dir.SizeSquared(),1, KINDA_SMALL_NUMBER));
		ensure(Length > 0);
		OutFaceIndex = INDEX_NONE;

		const T SignedDist = TVector<T, d>::DotProduct(StartPoint - MX, MNormal);
		if (FMath::Abs(SignedDist) < Thickness)
		{
			//initial overlap so stop
			OutTime = 0;
			return true;
		}

		const TVector<T, d> DirTowardsPlane = SignedDist < 0 ? MNormal : -MNormal;
		const T RayProjectedTowardsPlane = TVector<T, d>::DotProduct(Dir, DirTowardsPlane);
		const T Epsilon = 1e-7;
		if (RayProjectedTowardsPlane < Epsilon)	//moving parallel or away
		{
			return false;
		}

		//No initial overlap so we are outside the thickness band of the plane. So translate the plane to account for thickness	
		const TVector<T, d> TranslatedPlaneX = MX - Thickness * DirTowardsPlane;
		const TVector<T, d> StartToTranslatedPlaneX = TranslatedPlaneX - StartPoint;
		const T LengthTowardsPlane = TVector<T, d>::DotProduct(StartToTranslatedPlaneX, DirTowardsPlane);
		const T LengthAlongRay = LengthTowardsPlane / RayProjectedTowardsPlane;
		
		if (LengthAlongRay > Length)
		{
			return false;	//never reach
		}

		OutTime = LengthAlongRay;
		OutPosition = StartPoint + (LengthAlongRay + Thickness) * Dir;
		OutNormal = -DirTowardsPlane;
		return true;
	}

	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
 	{
		TVector<T, d> Direction = EndPoint - StartPoint;
		T Length = Direction.Size();
		Direction = Direction.GetSafeNormal();
		TVector<T, d> XPos = MX + MNormal * Thickness;
		TVector<T, d> XNeg = MX - MNormal * Thickness;
		TVector<T, d> EffectiveX = ((XNeg - StartPoint).Size() < (XPos - StartPoint).Size()) ? XNeg : XPos;
		TVector<T, d> PlaneToStart = EffectiveX - StartPoint;
		T Denominator = TVector<T, d>::DotProduct(Direction, MNormal);
		if (Denominator == 0)
		{
			if (TVector<T, d>::DotProduct(PlaneToStart, MNormal) == 0)
			{
				return MakePair(EndPoint, true);
			}
			return MakePair(TVector<T, d>(0), false);
		}
		T Root = TVector<T, d>::DotProduct(PlaneToStart, MNormal) / Denominator;
		if (Root < 0 || Root > Length)
		{
			return MakePair(TVector<T, d>(0), false);
		}
		return MakePair(TVector<T, d>(Root * Direction + StartPoint), true);
	}

	const TVector<T, d>& X() const { return MX; }
	const TVector<T, d>& Normal() const { return MNormal; }
	const TVector<T, d>& Normal(const TVector<T, d>&) const { return MNormal; }
	
	FORCEINLINE void SerializeImp(FArchive& Ar)
	{
		FImplicitObject::SerializeImp(Ar);
		Ar << MX << MNormal;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
		SerializeImp(Ar);
	}

	virtual void Serialize(FArchive& Ar) override
	{
		SerializeImp(Ar);
	}

	virtual uint32 GetTypeHash() const override
	{
		return HashCombine(::GetTypeHash(MX), ::GetTypeHash(MNormal));
	}

  private:
	TVector<T, d> MX;
	TVector<T, d> MNormal;
};

template<typename T, int d>
TVector<T, 2> ComputeBarycentricInPlane(const TVector<T, d>& P0, const TVector<T, d>& P1, const TVector<T, d>& P2, const TVector<T, d>& P)
{
	TVector<T, 2> Bary;
	TVector<T, d> P10 = P1 - P0;
	TVector<T, d> P20 = P2 - P0;
	TVector<T, d> PP0 = P - P0;
	T Size10 = P10.SizeSquared();
	T Size20 = P20.SizeSquared();
	T ProjSides = TVector<T, d>::DotProduct(P10, P20);
	T ProjP1 = TVector<T, d>::DotProduct(PP0, P10);
	T ProjP2 = TVector<T, d>::DotProduct(PP0, P20);
	T Denom = Size10 * Size20 - ProjSides * ProjSides;
	Bary.X = (Size20 * ProjP1 - ProjSides * ProjP2) / Denom;
	Bary.Y = (Size10 * ProjP2 - ProjSides * ProjP1) / Denom;
	return Bary;
}

template<typename T, int d>
const TVector<T, d> FindClosestPointOnLineSegment(const TVector<T, d>& P0, const TVector<T, d>& P1, const TVector<T, d>& P)
{
	const TVector<T, d> P10 = P1 - P0;
	const TVector<T, d> PP0 = P - P0;
	const T Proj = TVector<T, d>::DotProduct(P10, PP0);
	if (Proj < (T)0) //first check we're not behind
	{
		return P0;
	}

	const T Denom2 = P10.SizeSquared();
	if (Denom2 < (T)1e-4)
	{
		return P0;
	}

	//do proper projection
	const T NormalProj = Proj / Denom2;
	if (NormalProj > (T)1) //too far forward
	{
		return P1;
	}

	return P0 + NormalProj * P10; //somewhere on the line
}


template<typename T, int d>
TVector<T, d> FindClosestPointOnTriangle(const TVector<T, d>& ClosestPointOnPlane, const TVector<T, d>& P0, const TVector<T, d>& P1, const TVector<T, d>& P2, const TVector<T, d>& P)
{
	const T Epsilon = 1e-4;

	const TVector<T, 2> Bary = ComputeBarycentricInPlane(P0, P1, P2, ClosestPointOnPlane);

	if (Bary[0] >= -Epsilon && Bary[0] <= 1 + Epsilon && Bary[1] >= -Epsilon && Bary[1] <= 1 + Epsilon && (Bary[0] + Bary[1]) <= (1 + Epsilon))
	{
		return ClosestPointOnPlane;
	}

	const TVector<T, d> P10Closest = FindClosestPointOnLineSegment(P0, P1, P);
	const TVector<T, d> P20Closest = FindClosestPointOnLineSegment(P0, P2, P);
	const TVector<T, d> P21Closest = FindClosestPointOnLineSegment(P1, P2, P);

	const T P10Dist2 = (P - P10Closest).SizeSquared();
	const T P20Dist2 = (P - P20Closest).SizeSquared();
	const T P21Dist2 = (P - P21Closest).SizeSquared();

	if (P10Dist2 < P20Dist2)
	{
		if (P10Dist2 < P21Dist2)
		{
			return P10Closest;
		}
		else
		{
			return P21Closest;
		}
	}
	else
	{
		if (P20Dist2 < P21Dist2)
		{
			return P20Closest;
		}
		else
		{
			return P21Closest;
		}
	}
}

template<typename T, int d>
TVector<T, d> FindClosestPointOnTriangle(const TPlane<T, d>& TrianglePlane, const TVector<T, d>& P0, const TVector<T, d>& P1, const TVector<T, d>& P2, const TVector<T, d>& P)
{
	const TVector<T, d> PointOnPlane = TrianglePlane.FindClosestPoint(P);
	return FindClosestPointOnTriangle(PointOnPlane, P0, P1, P2, P);
}


template<typename T, int d>
bool IntersectPlanes2(TVector<T,d>& I, TVector<T,d>& D, const TPlane<T,d>& P1, const TPlane<T,d>& P2)
{
	FVector LI = I, LD = D;
	FPlane LP1(P1.X(), P1.Normal()), LP2(P2.X(), P2.Normal());
	bool RetVal = FMath::IntersectPlanes2(LI,LD,LP1,LP2);
	I = LI; D = LD;
	return RetVal;
}

}
