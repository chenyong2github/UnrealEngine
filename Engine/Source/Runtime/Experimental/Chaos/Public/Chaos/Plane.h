// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "ChaosArchive.h"
#include "ChaosCheck.h"

namespace Chaos
{

template <typename T, int d>
class TPlaneConcrete
{
public:

	// Scale the plane and assume that any of the scale components could be zero
	static TPlaneConcrete<T, d> MakeScaledSafe(const TPlaneConcrete<T, d>& Plane, const TVector<T, d>& Scale)
	{
		const FVec3 ScaledX = Plane.MX * Scale;
		
		// If all 3 scale components are non-zero we can just inverse-scale the normal
		// If 1 scale component is zero, the normal will point in that direction of the zero scale
		// If 2 scale components are zero, the normal will be zero along the non-zero scale direction
		// If 3 scale components are zero, the normal will be unchanged
		const int32 ZeroX = FMath::IsNearlyZero(Scale.X) ? 1 : 0;
		const int32 ZeroY = FMath::IsNearlyZero(Scale.Y) ? 1 : 0;
		const int32 ZeroZ = FMath::IsNearlyZero(Scale.Z) ? 1 : 0;
		const int32 NumZeros = ZeroX + ZeroY + ZeroZ;
		FVec3 ScaledN;
		if (NumZeros == 0)
		{
			// All 3 scale components non-zero
			ScaledN = FVec3(Plane.MNormal.X / Scale.X, Plane.MNormal.Y / Scale.Y, Plane.MNormal.Z / Scale.Z);
		}
		else if (NumZeros == 1)
		{
			// Exactly one Scale component is zero
			ScaledN = FVec3(
				(ZeroX) ? 1.0f : 0.0f,
				(ZeroY) ? 1.0f : 0.0f,
				(ZeroZ) ? 1.0f : 0.0f);
		}
		else if (NumZeros == 2)
		{
			// Exactly two Scale components is zero
			ScaledN = FVec3(
				(ZeroX) ? Plane.MNormal.X : 0.0f,
				(ZeroY) ? Plane.MNormal.Y : 0.0f,
				(ZeroZ) ? Plane.MNormal.Z : 0.0f);
		}
		else // (NumZeros == 3)
		{
			// All 3 scale components are zero
			ScaledN = Plane.MNormal;
		}

		// Even after all the above, we may still get a zero normal (e.g., we scale N=(1,0,0) by S=(0,1,0))
		const FReal ScaleN2 = ScaledN.SizeSquared();
		if (ScaleN2 > SMALL_NUMBER)
		{
			ScaledN = ScaledN * FMath::InvSqrt(ScaleN2);
		}
		else
		{
			ScaledN = Plane.MNormal;
		}
		
		return TPlaneConcrete<FReal, 3>(ScaledX, ScaledN);
	}

	// Scale the plane and assume that none of the scale components are zero
	static TPlaneConcrete<T, d> MakeScaledUnsafe(const TPlaneConcrete<T, d>& Plane, const TVector<T, d>& Scale)
	{
		const FVec3 ScaledX = Plane.MX * Scale;
		FVec3 ScaledN = Plane.MNormal / Scale;

		// We don't handle zero scales, but we could still end up with a small normal
		const FReal ScaleN2 = ScaledN.SizeSquared();
		if (ScaleN2 > SMALL_NUMBER)
		{
			ScaledN =  ScaledN * FMath::InvSqrt(ScaleN2);
		}
		else
		{
			ScaledN = Plane.MNormal;
		}

		return TPlaneConcrete<FReal, 3>(ScaledX, ScaledN);
	}


	TPlaneConcrete() = default;
	TPlaneConcrete(const TVector<T, d>& InX, const TVector<T, d>& InNormal)
	    : MX(InX)
	    , MNormal(InNormal)
	{
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
	T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const
	{
		Normal = MNormal;
		return TVector<T, d>::DotProduct(x - MX, MNormal);
	}

	TVector<T, d> FindClosestPoint(const TVector<T, d>& x, const T Thickness = (T)0) const
	{
		auto Dist = TVector<T, d>::DotProduct(x - MX, MNormal) - Thickness;
		return x - TVector<T, d>(Dist * MNormal);
	}

	bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const
	{
		ensure(FMath::IsNearlyEqual(Dir.SizeSquared(),1, KINDA_SMALL_NUMBER));
		CHAOS_ENSURE(Length > 0);
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

	Pair<TVector<T, d>, bool> FindClosestIntersection(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const
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

	FORCEINLINE void Serialize(FArchive& Ar)
	{
		Ar << MX << MNormal;
	}

	uint32 GetTypeHash() const
	{
		return HashCombine(::GetTypeHash(MX), ::GetTypeHash(MNormal));
	}

  private:
	TVector<T, d> MX;
	TVector<T, d> MNormal;
};

template <typename T, int d>
FArchive& operator<<(FArchive& Ar, TPlaneConcrete<T,d>& PlaneConcrete)
{
	PlaneConcrete.Serialize(Ar);
	return Ar;
}

template<class T, int d>
class TPlane final : public FImplicitObject
{
  public:
	using FImplicitObject::GetTypeName;


	TPlane() : FImplicitObject(0, ImplicitObjectType::Plane) {}	//needed for serialization
	TPlane(const TVector<T, d>& InX, const TVector<T, d>& InNormal)
	    : FImplicitObject(0, ImplicitObjectType::Plane)
		, MPlaneConcrete(InX, InNormal)
	{
	}
	TPlane(const TPlane<T, d>& Other)
	    : FImplicitObject(0, ImplicitObjectType::Plane)
	    , MPlaneConcrete(Other.MPlaneConcrete)
	{
	}
	TPlane(TPlane<T, d>&& Other)
	    : FImplicitObject(0, ImplicitObjectType::Plane)
	    , MPlaneConcrete(MoveTemp(Other.MPlaneConcrete))
	{
	}
	virtual ~TPlane() {}

	static constexpr EImplicitObjectType StaticType()
	{
		return ImplicitObjectType::Plane;
	}

	FReal GetRadius() const
	{
		return 0.0f;
	}

	/**
	 * Phi is positive on the side of the normal, and negative otherwise.
	 */
	T SignedDistance(const TVector<T, d>& x) const
	{
		return MPlaneConcrete.SignedDistance(x);
	}

	/**
	 * Phi is positive on the side of the normal, and negative otherwise.
	 */
	virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override
	{
		return MPlaneConcrete.PhiWithNormal(x,Normal);
	}

	TVector<T, d> FindClosestPoint(const TVector<T, d>& x, const T Thickness = (T)0) const
	{
		return MPlaneConcrete.FindClosestPoint(x,Thickness);
	}

	virtual bool Raycast(const TVector<T, d>& StartPoint, const TVector<T, d>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, d>& OutPosition, TVector<T, d>& OutNormal, int32& OutFaceIndex) const override
	{
		return MPlaneConcrete.Raycast(StartPoint,Dir,Length,Thickness,OutTime,OutPosition,OutNormal,OutFaceIndex);
	}

	virtual Pair<TVector<T, d>, bool> FindClosestIntersectionImp(const TVector<T, d>& StartPoint, const TVector<T, d>& EndPoint, const T Thickness) const override
 	{
		return MPlaneConcrete.FindClosestIntersection(StartPoint,EndPoint,Thickness);
	}

	const TVector<T,d>& X() const { return MPlaneConcrete.X(); }
	const TVector<T,d>& Normal() const { return MPlaneConcrete.Normal(); }
	const TVector<T, d>& Normal(const TVector<T, d>&) const { return MPlaneConcrete.Normal(); }
	
	FORCEINLINE void SerializeImp(FArchive& Ar)
	{
		FImplicitObject::SerializeImp(Ar);
		MPlaneConcrete.Serialize(Ar);
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
		return MPlaneConcrete.GetTypeHash();
	}

	const TPlaneConcrete<T,d>& PlaneConcrete() const { return MPlaneConcrete; }

  private:
	  TPlaneConcrete<T,d> MPlaneConcrete;
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
