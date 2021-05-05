// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"
#include "MathUtil.h"
#include "Serialization/Archive.h"
#include "Templates/UnrealTypeTraits.h"
#include <sstream>


namespace UE {
namespace Geometry {


/**
* Templated 2D Vector. Ported from g3Sharp library, with the intention of
* maintaining compatibility with existing g3Sharp code. Has an API
* similar to WildMagic, GTEngine, Eigen, etc.
*
* Convenience typedefs for FVector2f/FVector2d are defined, and
* should be preferentially used over the base template type
*
* @todo Possibly can be replaced/merged with Chaos TVector<T,N>
*/
template <typename T>
struct FVector2
{
	T X{}, Y{};

	constexpr FVector2()
		: X(0), Y(0)
	{
	}

	constexpr FVector2(T ValX, T ValY)
		: X(ValX), Y(ValY)
	{
	}

	constexpr FVector2(const T* Data)
		: X(Data[0]), Y(Data[1])
	{
	}

	constexpr FVector2(const FVector2& Vec) = default;

	constexpr FVector2(const FVector2D& Vec)
		: X((T)Vec.X), Y((T)Vec.Y)
	{
	}

	template<typename RealType2>
	explicit constexpr FVector2(const FVector2<RealType2>& Vec)
		: X((T)Vec.X), Y((T)Vec.Y)
	{
	}

	explicit constexpr operator const T*() const
	{
		return &X;
	};

	explicit constexpr operator T*()
	{
		return &X;
	}

	explicit operator FVector2D() const
	{
		return FVector2D((float)X, (float)Y);
	}

	constexpr static FVector2<T> Zero()
	{
		return FVector2<T>((T)0, (T)0);
	}
	constexpr static FVector2<T> One()
	{
		return FVector2<T>((T)1, (T)1);
	}
	constexpr static FVector2<T> UnitX()
	{
		return FVector2<T>((T)1, (T)0);
	}
	constexpr static FVector2<T> UnitY()
	{
		return FVector2<T>((T)0, (T)1);
	}

	constexpr FVector2<T>& operator=(const FVector2<T>& V2)
	{
		X = V2.X;
		Y = V2.Y;
		return *this;
	}

	constexpr T& operator[](int Idx)
	{
		return (&X)[Idx];
	}
	constexpr const T& operator[](int Idx) const
	{
		return (&X)[Idx];
	}

	constexpr T Length() const
	{
		return TMathUtil<T>::Sqrt(X * X + Y * Y);
	}
	T SquaredLength() const
	{
		return X * X + Y * Y;
	}

	constexpr T Distance(const FVector2<T>& V2) const
	{
		T dx = V2.X - X;
		T dy = V2.Y - Y;
		return TMathUtil<T>::Sqrt(dx * dx + dy * dy);
	}
	constexpr T DistanceSquared(const FVector2<T>& V2) const
	{
		T dx = V2.X - X;
		T dy = V2.Y - Y;
		return dx * dx + dy * dy;
	}

	constexpr T Dot(const FVector2<T>& V2) const
	{
		return X * V2.X + Y * V2.Y;
	}

	constexpr  FVector2 operator-() const
	{
		return FVector2(-X, -Y);
	}

	constexpr FVector2 operator+(const FVector2& V2) const
	{
		return FVector2(X + V2.X, Y + V2.Y);
	}

	constexpr FVector2 operator-(const FVector2& V2) const
	{
		return FVector2(X - V2.X, Y - V2.Y);
	}

	constexpr FVector2<T> operator+(const T& Scalar) const
	{
		return FVector2<T>(X + Scalar, Y + Scalar);
	}

	constexpr FVector2<T> operator-(const T& Scalar) const
	{
		return FVector2<T>(X - Scalar, Y - Scalar);
	}

	constexpr FVector2<T> operator*(const T& Scalar) const
	{
		return FVector2<T>(X * Scalar, Y * Scalar);
	}

	template<typename RealType2>
	constexpr FVector2<T> operator*(const RealType2& Scalar) const
	{
		return FVector2<T>(X * (T)Scalar, Y * (T)Scalar);
	}

	constexpr FVector2<T> operator*(const FVector2<T>& V2) const // component-wise
	{
		return FVector2<T>(X * V2.X, Y * V2.Y);
	}

	constexpr FVector2<T> operator/(const T& Scalar) const
	{
		return FVector2<T>(X / Scalar, Y / Scalar);
	}

	constexpr FVector2<T> operator/(const FVector2<T>& V2) const // component-wise
	{
		return FVector2<T>(X / V2.X, Y / V2.Y);
	}

	constexpr FVector2<T>& operator+=(const FVector2<T>& V2)
	{
		X += V2.X;
		Y += V2.Y;
		return *this;
	}

	constexpr FVector2<T>& operator-=(const FVector2<T>& V2)
	{
		X -= V2.X;
		Y -= V2.Y;
		return *this;
	}

	constexpr FVector2<T>& operator*=(const T& Scalar)
	{
		X *= Scalar;
		Y *= Scalar;
		return *this;
	}

	constexpr FVector2<T>& operator/=(const T& Scalar)
	{
		X /= Scalar;
		Y /= Scalar;
		return *this;
	}

	constexpr bool operator==(const FVector2<T>& Other) const
	{
		return X == Other.X && Y == Other.Y;
	}

	constexpr bool operator!=(const FVector2<T>& Other) const
	{
		return X != Other.X || Y != Other.Y;
	}

	/**
	 * Serialization operator for FVector2.
	 *
	 * @param Ar Archive to serialize with.
	 * @param Vec Vector to serialize.
	 * @returns Passing down serializing archive.
	 */
	friend FArchive& operator<<(FArchive& Ar, FVector2& Vec)
	{
		Vec.Serialize(Ar);
		return Ar;
	}

	/** Serialize FVector2 to an archive. */
	void Serialize(FArchive& Ar)
	{
		Ar << X;
		Ar << Y;
	}
};

/** @return dot product of V1 with PerpCW(V2), ie V2 rotated 90 degrees clockwise */
template <typename T>
constexpr T DotPerp(const FVector2<T>& V1, const FVector2<T>& V2)
{
	return V1.X * V2.Y - V1.Y * V2.X;
}

/** @return right-Perpendicular vector to V, ie V rotated 90 degrees clockwise */
template <typename T>
constexpr FVector2<T> PerpCW(const FVector2<T>& V)
{
	return FVector2<T>(V.Y, -V.X);
}

/** @return > 0 if C is to the left of the line from A to B, < 0 if to the right, 0 if on the line */
template<typename T>
T Orient(const FVector2<T>& A, const FVector2<T>& B, const FVector2<T>& C)
{
	return DotPerp((B - A), (C - A));
}


template <typename T>
constexpr bool IsNormalized(const FVector2<T>& Vector, const T Tolerance = TMathUtil<T>::ZeroTolerance)
{
	return TMathUtil<T>::Abs((Vector.X*Vector.X + Vector.Y*Vector.Y) - 1) < Tolerance;
}

template <typename T>
T Normalize(FVector2<T>& Vector, const T Epsilon = 0)
{
	 T length = Vector.Length();
	 if (length > Epsilon)
	 {
		 T invLength = ((T)1) / length;
		 Vector.X *= invLength;
		 Vector.Y *= invLength;
		 return length;
	 }
	 Vector.X = Vector.Y = (T)0;
	 return (T)0;
}

template <typename T>
FVector2<T> Normalized(const FVector2<T>& Vector, const T Epsilon = 0)
{
	T length = Vector.Length();
	if (length > Epsilon)
	{
		T invLength = ((T)1) / length;
		return FVector2<T>(Vector.X*invLength, Vector.Y*invLength);
	}
	return FVector2<T>((T)0, (T)0);
}


// Angle in Degrees
template <typename T>
T AngleD(const FVector2<T>& V1, const FVector2<T>& V2)
{
	T DotVal = V1.Dot(V2);
	T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
	return TMathUtil<T>::ACos(ClampedDot) * TMathUtil<T>::RadToDeg;
}

// Angle in Radians
template <typename T>
T AngleR(const FVector2<T>& V1, const FVector2<T>& V2)
{
	T DotVal = V1.Dot(V2);
	T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
	return TMathUtil<T>::ACos(ClampedDot);
}

// Angle in Radians
template <typename T>
T SignedAngleR(const FVector2<T>& V1, const FVector2<T>& V2)
{
	T DotVal = V1.Dot(V2);
	T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
	T Direction = DotPerp(V1, V2);
	if (Direction * Direction < TMathUtil<T>::ZeroTolerance)
	{
		return (DotVal < 0) ? TMathUtil<T>::Pi : (T)0;
	}
	else
	{
		T Sign = Direction < 0 ? (T)-1 : (T)1;
		return Sign * TMathUtil<T>::ACos(ClampedDot);
	}
}

template <typename T>
FVector2<T> Lerp(const FVector2<T>& A, const FVector2<T>& B, T Alpha)
{
	T OneMinusAlpha = (T)1 - Alpha;
	return FVector2<T>(OneMinusAlpha * A.X + Alpha * B.X,
		OneMinusAlpha * A.Y + Alpha * B.Y);
}


/**
 * Templated 3D Vector. Ported from g3Sharp library, with the intention of 
 * maintaining compatibility with existing g3Sharp code. Has an API
 * similar to WildMagic, GTEngine, Eigen, etc. 
 * 
 * Convenience typedefs for FVector3f/FVector3d are defined, and
 * should be preferentially used over the base template type
 * 
 * @todo Possibly can be replaced/merged with Chaos TVector<T,N>
 */
template <typename T>
struct FVector3 : public UE::Math::TVector<T>
{
	using UE::Math::TVector<T>::TVector;
	using UE::Math::TVector<T>::X;
	using UE::Math::TVector<T>::Y;
	using UE::Math::TVector<T>::Z;

	FVector3() : UE::Math::TVector<T>((T)0)
	{
	}

	FVector3(const UE::Math::TVector<T>& Vec)
	{
		X = Vec.X;
		Y = Vec.Y;
		Z = Vec.Z;
	}

	template<typename OtherType, TEMPLATE_REQUIRES(!std::is_same<T, OtherType>::value)>
	explicit FVector3(const UE::Math::TVector<OtherType>& Vec)
	{
		X = (T)Vec.X;
		Y = (T)Vec.Y;
		Z = (T)Vec.Z;
	}

	FVector3& operator=(const UE::Math::TVector<T>& Vec)
	{
		X = Vec.X;
		Y = Vec.Y;
		Z = Vec.Z;
		return *this;
	}

	explicit operator FVector3f() const
	{
		return FVector3f((float)X, (float)Y, (float)Z);
	}

	explicit operator FVector3d() const
	{
		return FVector3d((double)X, (double)Y, (double)Z);
	}


	explicit operator FLinearColor() const
	{
		return FLinearColor((float)X, (float)Y, (float)Z);
	}
	FVector3(const FLinearColor& Color)
	{
		X = (T)Color.R;
		Y = (T)Color.G;
		Z = (T)Color.B;
	}


	explicit FVector3(const T* Data)
	{
		X = Data[0];
		Y = Data[1];
		Z = Data[2];
	}


	explicit operator const T*() const
	{
		return &X;
	};
	explicit operator T*()
	{
		return &X;
	}


	static FVector3<T> Zero()
	{
		return FVector3<T>((T)0, (T)0, (T)0);
	}
	static FVector3<T> One()
	{
		return FVector3<T>((T)1, (T)1, (T)1);
	}
	static FVector3<T> UnitX()
	{
		return FVector3<T>((T)1, (T)0, (T)0);
	}
	static FVector3<T> UnitY()
	{
		return FVector3<T>((T)0, (T)1, (T)0);
	}
	static FVector3<T> UnitZ()
	{
		return FVector3<T>((T)0, (T)0, (T)1);
	}
	static FVector3<T> MaxVector()
	{
		return FVector3<T>(TNumericLimits<T>::Max(), TNumericLimits<T>::Max(), TNumericLimits<T>::Max());
	}

	T Length() const
	{
		return TMathUtil<T>::Sqrt(X * X + Y * Y + Z * Z);
	}
	T SquaredLength() const
	{
		return X * X + Y * Y + Z * Z;
	}

	T Distance(const UE::Math::TVector<T>& V2) const
	{
		T dx = V2.X - X;
		T dy = V2.Y - Y;
		T dz = V2.Z - Z;
		return TMathUtil<T>::Sqrt(dx * dx + dy * dy + dz * dz);
	}
	T DistanceSquared(const UE::Math::TVector<T>& V2) const
	{
		T dx = V2.X - X;
		T dy = V2.Y - Y;
		T dz = V2.Z - Z;
		return dx * dx + dy * dy + dz * dz;
	}

	FVector3<T> operator-() const
	{
		return FVector3<T>(-X, -Y, -Z);
	}

	FVector3<T> operator+(const UE::Math::TVector<T>& V2) const
	{
		return FVector3<T>(X + V2.X, Y + V2.Y, Z + V2.Z);
	}

	FVector3<T> operator-(const UE::Math::TVector<T>& V2) const
	{
		return FVector3<T>(X - V2.X, Y - V2.Y, Z - V2.Z);
	}

	FVector3<T> operator+(const T& Scalar) const
	{
		return FVector3<T>(X + Scalar, Y + Scalar, Z + Scalar);
	}

	FVector3<T> operator-(const T& Scalar) const
	{
		return FVector3<T>(X - Scalar, Y - Scalar, Z - Scalar);
	}

	FVector3<T> operator*(const T& Scalar) const
	{
		return FVector3<T>(X * Scalar, Y * Scalar, Z * Scalar);
	}

	FVector3<T> operator*(const UE::Math::TVector<T>& V2) const // component-wise
	{
		return FVector3<T>(X * V2.X, Y * V2.Y, Z * V2.Z);
	}

	FVector3<T> operator/(const T& Scalar) const
	{
		return FVector3<T>(X / Scalar, Y / Scalar, Z / Scalar);
	}

	FVector3<T> operator/(const UE::Math::TVector<T>& V2) const // component-wise
	{
		return FVector3<T>(X / V2.X, Y / V2.Y, Z / V2.Z);
	}

	template<typename RealType2, TEMPLATE_REQUIRES(std::is_floating_point<RealType2>::value)>
	FVector3<T> operator*(const RealType2& Scalar) const
	{
		return FVector3<T>(X * (T)Scalar, Y * (T)Scalar, Z * (T)Scalar);
	}

	FVector3<T>& operator+=(const UE::Math::TVector<T>& V2)
	{
		X += V2.X;
		Y += V2.Y;
		Z += V2.Z;
		return *this;
	}

	FVector3<T>& operator-=(const UE::Math::TVector<T>& V2)
	{
		X -= V2.X;
		Y -= V2.Y;
		Z -= V2.Z;
		return *this;
	}

	FVector3<T>& operator*=(const T& Scalar)
	{
		X *= Scalar;
		Y *= Scalar;
		Z *= Scalar;
		return *this;
	}

	FVector3<T>& operator/=(const T& Scalar)
	{
		X /= Scalar;
		Y /= Scalar;
		Z /= Scalar;
		return *this;
	}

	T Dot(const UE::Math::TVector<T>& V2) const
	{
		return X * V2.X + Y * V2.Y + Z * V2.Z;
	}

	FVector3<T> Cross(const UE::Math::TVector<T>& V2) const
	{
		return FVector3(
			Y * V2.Z - Z * V2.Y,
			Z * V2.X - X * V2.Z,
			X * V2.Y - Y * V2.X);
	}
};





/** @return unit vector along axis X=0, Y=1, Z=2 */
template <typename T>
constexpr UE::Math::TVector<T> MakeUnitVector3(int32 Axis)
{
	UE::Math::TVector<T> UnitVec((T)0, (T)0, (T)0);
	UnitVec[FMath::Clamp(Axis, 0, 2)] = (T)1;
	return UnitVec;
}


template<typename T>
T Length(const UE::Math::TVector<T>& V)
{
	return TMathUtil<T>::Sqrt(V.X*V.X + V.Y*V.Y + V.Z*V.Z);
}

template<typename T>
T SquaredLength(const UE::Math::TVector<T>& V)
{
	return V.X*V.X + V.Y*V.Y + V.Z*V.Z;
}


template <typename T>
constexpr bool IsNormalized(const UE::Math::TVector<T>& Vector, const T Tolerance = TMathUtil<T>::ZeroTolerance)
{
	return TMathUtil<T>::Abs((Vector.X*Vector.X + Vector.Y*Vector.Y + Vector.Z*Vector.Z) - 1) < Tolerance;
}


template<typename T>
T Normalize(UE::Math::TVector<T>& Vector, const T Epsilon = 0)
{
	T length = Vector.Length();
	if (length > Epsilon)
	{
		T invLength = ((T)1) / length;
		Vector.X *= invLength;
		Vector.Y *= invLength;
		Vector.Z *= invLength;
		return length;
	}
	Vector.X = Vector.Y = Vector.Z = (T)0;
	return (T)0;
}

template<typename T>
constexpr UE::Math::TVector<T> Normalized(const UE::Math::TVector<T>& Vector, const T Epsilon = 0)
{
	T length = Vector.Length();
	if (length > Epsilon)
	{
		T invLength = ((T)1) / length;
		return UE::Math::TVector<T>(Vector.X*invLength, Vector.Y*invLength, Vector.Z*invLength);
	}
	return UE::Math::TVector<T>((T)0, (T)0, (T)0);
}

template<typename T>
T Distance(const UE::Math::TVector<T>& V1, const UE::Math::TVector<T>& V2)
{
	T dx = V2.X - V1.X;
	T dy = V2.Y - V1.Y;
	T dz = V2.Z - V1.Z;
	return TMathUtil<T>::Sqrt(dx * dx + dy * dy + dz * dz);
}

template<typename T>
T DistanceSquared(const UE::Math::TVector<T>& V1, const UE::Math::TVector<T>& V2)
{
	T dx = V2.X - V1.X;
	T dy = V2.Y - V1.Y;
	T dz = V2.Z - V1.Z;
	return dx * dx + dy * dy + dz * dz;
}



template<typename T>
T Dot(const UE::Math::TVector<T>& V1, const UE::Math::TVector<T>& V2)
{
	return V1.X * V2.X + V1.Y * V2.Y + V1.Z * V2.Z;
}

template<typename T>
UE::Math::TVector<T> Cross(const UE::Math::TVector<T>& V1, const UE::Math::TVector<T>& V2)
{
	return UE::Math::TVector<T>(
		V1.Y * V2.Z - V1.Z * V2.Y,
		V1.Z * V2.X - V1.X * V2.Z,
		V1.X * V2.Y - V1.Y * V2.X);
}

template<typename T>
UE::Math::TVector<T> UnitCross(const UE::Math::TVector<T>& V1, const UE::Math::TVector<T>& V2)
{
	UE::Math::TVector<T> N = V1.Cross(V2);
	return Normalized(N);
}

/**
 * Computes the Angle between V1 and V2, assuming they are already normalized
 * @return the (positive) angle between V1 and V2 in degrees
 */
template <typename T>
T AngleD(const UE::Math::TVector<T>& V1, const UE::Math::TVector<T>& V2)
{
	T DotVal = V1.Dot(V2);
	T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
	return TMathUtil<T>::ACos(ClampedDot) * TMathUtil<T>::RadToDeg;
}

/**
 * Computes the Angle between V1 and V2, assuming they are already normalized
 * @return the (positive) angle between V1 and V2 in radians
 */
template <typename T>
T AngleR(const UE::Math::TVector<T>& V1, const UE::Math::TVector<T>& V2)
{
	T DotVal = V1.Dot(V2);
	T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
	return TMathUtil<T>::ACos(ClampedDot);
}

template <typename T>
constexpr FVector2<T> GetXY(const UE::Math::TVector<T>& V)
{
	return FVector2<T>(V.X, V.Y);
}

template <typename T>
constexpr FVector2<T> GetXZ(const UE::Math::TVector<T>& V)
{
	return FVector2<T>(V.X, V.Z);
}

template <typename T>
constexpr FVector2<T> GetYZ(const UE::Math::TVector<T>& V)
{
	return FVector2<T>(V.Y, V.Z);
}


template<typename T>
constexpr FVector3<T> Min(const UE::Math::TVector<T>& V0, const UE::Math::TVector<T>& V1)
{
	return FVector3<T>(TMathUtil<T>::Min(V0.X, V1.X),
		TMathUtil<T>::Min(V0.Y, V1.Y),
		TMathUtil<T>::Min(V0.Z, V1.Z));
}

template<typename T>
constexpr FVector3<T> Max(const UE::Math::TVector<T>& V0, const UE::Math::TVector<T>& V1)
{
	return FVector3<T>(TMathUtil<T>::Max(V0.X, V1.X),
		TMathUtil<T>::Max(V0.Y, V1.Y),
		TMathUtil<T>::Max(V0.Z, V1.Z));
}


template<typename T>
constexpr T MaxElement(const UE::Math::TVector<T>& Vector)
{
	return TMathUtil<T>::Max3(Vector.X, Vector.Y, Vector.Z);
}

/** @return 0/1/2 index of maximum element */
template<typename T>
constexpr int32 MaxElementIndex(const UE::Math::TVector<T>& Vector)
{
	return TMathUtil<T>::Max3Index(Vector.X, Vector.Y, Vector.Z);
}

template<typename T>
constexpr T MinElement(const UE::Math::TVector<T>& Vector)
{
	return TMathUtil<T>::Min3(Vector.X, Vector.Y, Vector.Z);
}

/** @return 0/1/2 index of minimum element */
template<typename T>
constexpr int32 MinElementIndex(const UE::Math::TVector<T>& Vector)
{
	return TMathUtil<T>::Min3Index(Vector.X, Vector.Y, Vector.Z);
}

template<typename T>
constexpr T MaxAbsElement(const UE::Math::TVector<T>& Vector)
{
	return TMathUtil<T>::Max3(TMathUtil<T>::Abs(Vector.X), TMathUtil<T>::Abs(Vector.Y), TMathUtil<T>::Abs(Vector.Z));
}

/** @return 0/1/2 index of maximum absolute-value element */
template<typename T>
constexpr T MaxAbsElementIndex(const UE::Math::TVector<T>& Vector)
{
	return TMathUtil<T>::Max3Index(TMathUtil<T>::Abs(Vector.X), TMathUtil<T>::Abs(Vector.Y), TMathUtil<T>::Abs(Vector.Z));
}

template<typename T>
constexpr T MinAbsElement(const UE::Math::TVector<T>& Vector)
{
	return TMathUtil<T>::Min3(TMathUtil<T>::Abs(Vector.X), TMathUtil<T>::Abs(Vector.Y), TMathUtil<T>::Abs(Vector.Z));
}

/** @return 0/1/2 index of minimum absolute-value element */
template<typename T>
constexpr T MinAbsElementIndex(const UE::Math::TVector<T>& Vector)
{
	return TMathUtil<T>::Min3Index(TMathUtil<T>::Abs(Vector.X), TMathUtil<T>::Abs(Vector.Y), TMathUtil<T>::Abs(Vector.Z));
}

template<typename T>
constexpr FColor ToFColor(const UE::Math::TVector<T>& Vector)
{
	return FColor(
		FMathf::Clamp((int)((float)Vector.X * 255.0f), 0, 255),
		FMathf::Clamp((int)((float)Vector.Y * 255.0f), 0, 255),
		FMathf::Clamp((int)((float)Vector.Z * 255.0f), 0, 255));
}

template<typename T>
constexpr FLinearColor ToLinearColor(const UE::Math::TVector<T>& Vector)
{
	return FLinearColor((float)Vector.X, (float)Vector.Y, (float)Vector.Z);
}

template<typename T>
UE::Math::TVector<T> Lerp(const UE::Math::TVector<T>& A, const UE::Math::TVector<T>& B, T Alpha)
{
	T OneMinusAlpha = (T)1 - Alpha;
	return UE::Math::TVector<T>(OneMinusAlpha * A.X + Alpha * B.X,
		OneMinusAlpha * A.Y + Alpha * B.Y,
		OneMinusAlpha * A.Z + Alpha * B.Z);
}

template<typename T>
UE::Math::TVector<T> Blend3(const UE::Math::TVector<T>& A, const UE::Math::TVector<T>& B, const UE::Math::TVector<T>& C, const T& WeightA, const T& WeightB, const T& WeightC)
{
	return UE::Math::TVector<T>(
		WeightA * A.X + WeightB * B.X + WeightC * C.X,
		WeightA * A.Y + WeightB * B.Y + WeightC * C.Y,
		WeightA * A.Z + WeightB * B.Z + WeightC * C.Z);
}

template <typename RealType>
inline FVector3<RealType> operator*(RealType Scalar, const FVector3<RealType>& V)
{
	return FVector3<RealType>(Scalar * V.X, Scalar * V.Y, Scalar * V.Z);
}

// allow float*Vector3<double> and double*Vector3<float>
template <typename RealType, typename RealType2, TEMPLATE_REQUIRES(std::is_floating_point<RealType2>::value)>
inline FVector3<RealType> operator*(RealType2 Scalar, const FVector3<RealType>& V)
{
	return FVector3<RealType>((RealType)Scalar * V.X, (RealType)Scalar * V.Y, (RealType)Scalar * V.Z);
}

template <typename RealType>
std::ostream& operator<<(std::ostream& os, const FVector3<RealType>& Vec)
{
	os << Vec.X << " " << Vec.Y << " " << Vec.Z;
	return os;
}

template <typename T>
FORCEINLINE uint32 GetTypeHash(const FVector3<T>& Vector)
{
	// (this is how FIntVector and all the other FVectors do their hash functions)
	// Note: this assumes there's no padding that could contain uncompared data.
	return FCrc::MemCrc_DEPRECATED(&Vector, sizeof(FVector3<T>));
}





template <typename T>
FORCEINLINE uint32 GetTypeHash(const FVector2<T>& Vector)
{
	// (this is how FIntVector and all the other FVectors do their hash functions)
	// Note: this assumes there's no padding that could contain uncompared data.
	return FCrc::MemCrc_DEPRECATED(&Vector, sizeof(FVector2<T>));
}

template <typename RealType>
inline FVector2<RealType> operator*(RealType Scalar, const FVector2<RealType>& V)
{
	return FVector2<RealType>(Scalar * V.X, Scalar * V.Y);
}

// allow float*Vector2<double> and double*Vector2<float>
template <typename RealType, typename RealType2>
inline FVector2<RealType> operator*(RealType2 Scalar, const FVector2<RealType>& V)
{
	return FVector2<RealType>((RealType)Scalar * V.X, (RealType)Scalar * V.Y);
}

template <typename RealType>
std::ostream& operator<<(std::ostream& os, const FVector2<RealType>& Vec)
{
	os << Vec.X << " " << Vec.Y;
	return os;
}








template <typename T>
struct TVector4
{
	T X{}, Y{}, Z{}, W{};

	constexpr TVector4()
		: X(0), Y(0), Z(0), W(0)
	{
	}

	constexpr TVector4(T ValX, T ValY, T ValZ, T ValW)
		: X(ValX), Y(ValY), Z(ValZ), W(ValW)
	{
	}

	constexpr TVector4(const T* Data)
		: X(Data[0]), Y(Data[1]), Z(Data[2]), W(Data[3])
	{
	}

	constexpr TVector4(const TVector4& Vec) = default;

	template<typename RealType2>
	explicit constexpr TVector4(const TVector4<RealType2>& Vec)
		: X((T)Vec.X), Y((T)Vec.Y), Z((T)Vec.Z), W((T)Vec.W)
	{
	}

	explicit constexpr operator const T*() const
	{
		return &X;
	};
	explicit constexpr operator T*()
	{
		return &X;
	}

	explicit constexpr operator FLinearColor() const
	{
		return FLinearColor((float)X, (float)Y, (float)Z, (float)W);
	}
	constexpr TVector4(const FLinearColor& Color)
		: X((T)Color.R), Y((T)Color.G), Z((T)Color.B), W((T)Color.A)
	{
	}

	explicit constexpr operator FColor() const
	{
		return FColor(
			FMathf::Clamp((int)((float)X*255.0f), 0, 255),
			FMathf::Clamp((int)((float)Y*255.0f), 0, 255),
			FMathf::Clamp((int)((float)Z*255.0f), 0, 255),
			FMathf::Clamp((int)((float)W*255.0f), 0, 255));
	}


	static TVector4<T> Zero()
	{
		return TVector4<T>((T)0, (T)0, (T)0, (T)0);
	}
	static TVector4<T> One()
	{
		return TVector4<T>((T)1, (T)1, (T)1, (T)1);
	}


	TVector4<T>& operator=(const TVector4<T>& V2)
	{
		X = V2.X;
		Y = V2.Y;
		Z = V2.Z;
		W = V2.W;
		return *this;
	}

	T& operator[](int Idx)
	{
		return (&X)[Idx];
	}
	const T& operator[](int Idx) const
	{
		return (&X)[Idx];
	}

	T Length() const
	{
		return TMathUtil<T>::Sqrt(X * X + Y * Y + Z * Z + W * W);
	}
	T SquaredLength() const
	{
		return X * X + Y * Y + Z * Z + W * W;
	}


	constexpr TVector4<T> operator-() const
	{
		return TVector4<T>(-X, -Y, -Z, -W);
	}

	constexpr TVector4<T> operator+(const TVector4<T>& V2) const
	{
		return TVector4<T>(X + V2.X, Y + V2.Y, Z + V2.Z, W + V2.W);
	}

	constexpr TVector4<T> operator-(const TVector4<T>& V2) const
	{
		return TVector4<T>(X - V2.X, Y - V2.Y, Z - V2.Z, W - V2.W);
	}

	constexpr TVector4<T> operator+(const T& Scalar) const
	{
		return TVector4<T>(X + Scalar, Y + Scalar, Z + Scalar, W + Scalar);
	}

	constexpr TVector4<T> operator-(const T& Scalar) const
	{
		return TVector4<T>(X - Scalar, Y - Scalar, Z - Scalar, W - Scalar);
	}

	constexpr TVector4<T> operator*(const T& Scalar) const
	{
		return TVector4<T>(X * Scalar, Y * Scalar, Z * Scalar, W * Scalar);
	}

	template<typename RealType2>
	constexpr TVector4<T> operator*(const RealType2& Scalar) const
	{
		return TVector4<T>(X * (T)Scalar, Y * (T)Scalar, Z * (T)Scalar, W * (T)Scalar);
	}

	constexpr TVector4<T> operator*(const TVector4<T>& V2) const // component-wise
	{
		return TVector4<T>(X * V2.X, Y * V2.Y, Z * V2.Z, W * V2.W);
	}

	constexpr TVector4<T> operator/(const T& Scalar) const
	{
		return TVector4<T>(X / Scalar, Y / Scalar, Z / Scalar, W / Scalar);
	}

	constexpr TVector4<T> operator/(const TVector4<T>& V2) const // component-wise
	{
		return TVector4<T>(X / V2.X, Y / V2.Y, Z / V2.Z, W / V2.W);
	}

	constexpr TVector4<T>& operator+=(const TVector4<T>& V2)
	{
		X += V2.X;
		Y += V2.Y;
		Z += V2.Z;
		W += V2.W;
		return *this;
	}

	constexpr TVector4<T>& operator-=(const TVector4<T>& V2)
	{
		X -= V2.X;
		Y -= V2.Y;
		Z -= V2.Z;
		W -= V2.W;
		return *this;
	}

	constexpr TVector4<T>& operator*=(const T& Scalar)
	{
		X *= Scalar;
		Y *= Scalar;
		Z *= Scalar;
		W *= Scalar;
		return *this;
	}

	constexpr TVector4<T>& operator/=(const T& Scalar)
	{
		X /= Scalar;
		Y /= Scalar;
		Z /= Scalar;
		W /= Scalar;
		return *this;
	}

	T Dot(const TVector4<T>& V2) const
	{
		return X * V2.X + Y * V2.Y + Z * V2.Z + W * V2.W;
	}

	constexpr bool operator==(const TVector4<T>& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z && W == Other.W;
	}

	constexpr bool operator!=(const TVector4<T>& Other) const
	{
		return X != Other.X || Y != Other.Y || Z != Other.Z || W != Other.W;
	}

	/**
	 * Serialization operator for TVector4.
	 *
	 * @param Ar Archive to serialize with.
	 * @param Vec Vector to serialize.
	 * @returns Passing down serializing archive.
	 */
	friend FArchive& operator<<(FArchive& Ar, TVector4& Vec)
	{
		Vec.Serialize(Ar);
		return Ar;
	}

	/** Serialize TVector4 to an archive. */
	void Serialize(FArchive& Ar)
	{
		Ar << X;
		Ar << Y;
		Ar << Z;
		Ar << W;
	}
};

template <typename T>
constexpr bool IsNormalized(const TVector4<T>& Vector, const T Tolerance = TMathUtil<T>::ZeroTolerance)
{
	return TMathUtil<T>::Abs((Vector.X*Vector.X + Vector.Y*Vector.Y + Vector.Z*Vector.Z + Vector.W*Vector.W) - 1) < Tolerance;
}

template<typename T>
T Normalize(TVector4<T>& Vector, const T Epsilon = 0)
{
	T length = Vector.Length();
	if (length > Epsilon)
	{
		T invLength = ((T)1) / length;
		Vector.X *= invLength;
		Vector.Y *= invLength;
		Vector.Z *= invLength;
		Vector.W *= invLength;
		return length;
	}
	Vector.X = Vector.Y = Vector.Z = Vector.W = (T)0;
	return (T)0;
}

template<typename T>
TVector4<T> Normalized(const TVector4<T>& Vector, const T Epsilon = 0)
{
	T length = Vector.Length();
	if (length > Epsilon)
	{
		T invLength = ((T)1) / length;
		return TVector4<T>(Vector.X*invLength, Vector.Y*invLength, Vector.Z*invLength, Vector.W*invLength);
	}
	return TVector4<T>::Zero();
}

template<typename T>
constexpr FVector3<T> GetXYZ(const TVector4<T>& V)
{
	return FVector3<T>(V.X, V.Y, V.Z);
}

template<typename T>
TVector4<T> Lerp(const TVector4<T>& A, const TVector4<T>& B, T Alpha)
{
	T OneMinusAlpha = (T)1 - Alpha;
	return TVector4<T>(OneMinusAlpha * A.X + Alpha * B.X,
		OneMinusAlpha * A.Y + Alpha * B.Y,
		OneMinusAlpha * A.Z + Alpha * B.Z,
		OneMinusAlpha * A.W + Alpha * B.W);
}

template<typename T>
TVector4<T> Blend3(const TVector4<T>& A, const TVector4<T>& B, const TVector4<T>& C, const T& WeightA, const T& WeightB, const T& WeightC)
{
	return TVector4<T>(
		WeightA * A.X + WeightB * B.X + WeightC * C.X,
		WeightA * A.Y + WeightB * B.Y + WeightC * C.Y,
		WeightA * A.Z + WeightB * B.Z + WeightC * C.Z,
		WeightA * A.W + WeightB * B.W + WeightC * C.W);
}

template <typename RealType>
inline TVector4<RealType> operator*(RealType Scalar, const TVector4<RealType>& V)
{
	return TVector4<RealType>(Scalar * V.X, Scalar * V.Y, Scalar * V.Z, Scalar * V.W);
}

// allow float*Vector4<double> and double*Vector4<float>
template <typename RealType, typename RealType2>
inline TVector4<RealType> operator*(RealType2 Scalar, const TVector4<RealType>& V)
{
	return TVector4<RealType>((RealType)Scalar * V.X, (RealType)Scalar * V.Y, (RealType)Scalar * V.Z, (RealType)Scalar * V.W);
}

template <typename RealType>
std::ostream& operator<<(std::ostream& os, const TVector4<RealType>& Vec)
{
	os << Vec.X << " " << Vec.Y << " " << Vec.Z << " " << Vec.W;
	return os;
}


template <typename T>
FORCEINLINE uint32 GetTypeHash(const TVector4<T>& Vector)
{
	// (this is how FIntVector and all the other FVectors do their hash functions)
	// Note: this assumes there's no padding that could contain uncompared data.
	return FCrc::MemCrc_DEPRECATED(&Vector, sizeof(TVector4<T>));
}




} // end namespace UE::Geometry
} // end namespace UE

typedef UE::Geometry::FVector2<float> FVector2f;
typedef UE::Geometry::FVector2<double> FVector2d;

//typedef UE::Geometry::FVector3<float> FVector3f;
//typedef UE::Geometry::FVector3<double> FVector3d;

typedef UE::Geometry::TVector4<float> FVector4f;
typedef UE::Geometry::TVector4<double> FVector4d;
template<> struct TCanBulkSerialize<FVector2f> { enum { Value = true }; };
template<> struct TCanBulkSerialize<FVector2d> { enum { Value = true }; };

template<> struct TCanBulkSerialize<FVector4f> { enum { Value = true }; };
template<> struct TCanBulkSerialize<FVector4d> { enum { Value = true }; };
