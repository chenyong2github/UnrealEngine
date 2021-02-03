// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	template<class T>
	class TSegment
	{
	public:
		TSegment() {}
		TSegment(const TVec3<T>& x1, const TVec3<T>& x2)
			: MPoint(x1)
			, MAxis(x2 - x1)
			, MLength(MAxis.SafeNormalize())
		{ }

		FORCEINLINE bool IsConvex() const { return true; }

		FORCEINLINE const TVec3<T> GetCenter() const { return MPoint + (.5f * MLength * MAxis); }

		FORCEINLINE const TVec3<T>& GetX1() const { return MPoint; }

		FORCEINLINE TVec3<T> GetX2() const { return MPoint + MAxis * MLength; }

		FORCEINLINE const TVec3<T>& GetAxis() const { return MAxis; }

		FORCEINLINE float GetLength() const { return MLength; }

		TVec3<T> Support(const TVec3<T>& Direction, const T Thickness) const
		{
			const T Dot = TVec3<T>::DotProduct(Direction, MAxis);
			const TVec3<T> FarthestCap = Dot >= 0 ? GetX2() : GetX1();	//orthogonal we choose either
			//We want N / ||N|| and to avoid inf
			//So we want N / ||N|| < 1 / eps => N eps < ||N||, but this is clearly true for all eps < 1 and N > 0
			T SizeSqr = Direction.SizeSquared();
			if (SizeSqr <= TNumericLimits<T>::Min())
			{
				return FarthestCap;
			}
			const TVec3<T> NormalizedDirection = Direction / sqrt(SizeSqr);
			return FarthestCap + (NormalizedDirection * Thickness);
		}

		FORCEINLINE_DEBUGGABLE TVec3<T> SupportCore(const TVec3<T>& Direction) const
		{
			const T Dot = TVec3<T>::DotProduct(Direction, MAxis);
			const TVec3<T> FarthestCap = Dot >= 0 ? GetX2() : GetX1();	//orthogonal we choose either
			return FarthestCap;
		}

		FORCEINLINE void Serialize(FArchive &Ar) 
		{
			Ar << MPoint << MAxis << MLength;
		}

		FORCEINLINE TAABB<T, 3> BoundingBox() const
		{
			TAABB<T,3> Box(MPoint,MPoint);
			Box.GrowToInclude(GetX2());
			return Box;
		}

	private:
		TVec3<T> MPoint;
		TVec3<T> MAxis;
		T MLength;
	};
}
