// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	template<class T>
	class TSegment
	{
	public:
		TSegment() {}
		TSegment(const TVector<T, 3>& x1, const TVector<T, 3>& x2)
			: MPoint(x1)
			, MAxis(x2 - x1)
			, MLength(MAxis.SafeNormalize())
		{ }

		FORCEINLINE bool IsConvex() const { return true; }

		FORCEINLINE const TVector<T, 3> GetCenter() const { return MPoint + (.5f * MLength * MAxis); }

		FORCEINLINE const TVector<T, 3>& GetX1() const { return MPoint; }

		FORCEINLINE TVector<T, 3> GetX2() const { return MPoint + MAxis * MLength; }

		FORCEINLINE const TVector<T, 3>& GetAxis() const { return MAxis; }

		FORCEINLINE float GetLength() const { return MLength; }

		TVector<T,3> Support(const TVector<T, 3>& Direction, const T Thickness) const
		{
			const T Dot = TVector<T, 3>::DotProduct(Direction, MAxis);
			const TVector<T, 3> FarthestCap = Dot >= 0 ? GetX2() : GetX1();	//orthogonal we choose either
			//We want N / ||N|| and to avoid inf
			//So we want N / ||N|| < 1 / eps => N eps < ||N||, but this is clearly true for all eps < 1 and N > 0
			T SizeSqr = Direction.SizeSquared();
			if (SizeSqr <= TNumericLimits<T>::Min())
			{
				return FarthestCap;
			}
			const TVector<T, 3> NormalizedDirection = Direction / sqrt(SizeSqr);
			return FarthestCap + (NormalizedDirection * Thickness);
		}

		FORCEINLINE_DEBUGGABLE TVector<T, 3> Support2(const TVector<T, 3>& Direction) const
		{
			const T Dot = TVector<T, 3>::DotProduct(Direction, MAxis);
			const TVector<T, 3> FarthestCap = Dot >= 0 ? GetX2() : GetX1();	//orthogonal we choose either
			return FarthestCap;
		}

		FORCEINLINE void Serialize(FArchive &Ar) 
		{
			Ar << MPoint << MAxis << MLength;
		}

	private:
		TVector<T, 3> MPoint;
		TVector<T, 3> MAxis;
		T MLength;
	};
}
