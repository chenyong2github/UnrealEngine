// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"
#include "MathUtil.h"
#include <sstream>





/**
* Templated 2D Vector. Ported from g3Sharp library, with the intention of
* maintaining compatibility with existing g3Sharp code. Has an API
* similar to WildMagic, GTEngine, Eigen, etc.
*
* Convenience typedefs for FVector2f/FVector2d/FVector2i are defined, and
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

	// Note: DotPerp and FVector2's version of Cross are the same function
	constexpr T DotPerp(const FVector2<T>& V2) const
	{
		return X * V2.Y - Y * V2.X;
	}
	constexpr T Cross(const FVector2<T>& V2) const
	{
		return X * V2.Y - Y * V2.X;
	}

	constexpr FVector2<T> Perp() const
	{
		return FVector2<T>(Y, -X);
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

	constexpr bool IsNormalized()
	{
		return TMathUtil<T>::Abs((X * X + Y * Y) - 1) < TMathUtil<T>::ZeroTolerance;
	}

	// Angle in Degrees
	T AngleD(const FVector2<T>& V2) const
	{
		T DotVal = Dot(V2);
		T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
		return (T)(acos(ClampedDot) * (T)(180.0 / 3.14159265358979323846));
	}

	// Angle in Radians
	T AngleR(const FVector2<T>& V2) const
	{
		T DotVal = Dot(V2);
		T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
		return (T)acos(ClampedDot);
	}

	// Angle in Radians
	T SignedAngleR(const FVector2<T>& V2) const
	{
		T DotVal = Dot(V2);
		T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
		T Direction = Cross(V2);
		if (Direction*Direction < TMathUtil<T>::ZeroTolerance)
		{
			return (DotVal < 0) ? TMathUtil<T>::Pi : (T)0;
		}
		else
		{
			T Sign = Direction < 0 ? (T)-1 : (T)1;
			return Sign * TMathUtil<T>::ACos(ClampedDot);
		}
	}

	T Normalize(const T Epsilon = 0)
	{
		T length = Length();
		if (length > Epsilon)
		{
			T invLength = ((T)1) / length;
			X *= invLength;
			Y *= invLength;
			return length;
		}
		X = Y = (T)0;
		return 0;
	}

	constexpr FVector2<T> Normalized(const T Epsilon = 0) const
	{
		T length = Length();
		if (length > Epsilon)
		{
			T invLength = ((T)1) / length;
			return FVector2<T>(X * invLength, Y * invLength);
		}
		return FVector2<T>((T)0, (T)0);
	}



	constexpr bool operator==(const FVector2<T>& Other) const
	{
		return X == Other.X && Y == Other.Y;
	}

	constexpr bool operator!=(const FVector2<T>& Other) const
	{
		return X != Other.X || Y != Other.Y;
	}


	static FVector2<T> Lerp(const FVector2<T>& A, const FVector2<T>& B, T Alpha)
	{
		T OneMinusAlpha = (T)1 - Alpha;
		return FVector2<T>(OneMinusAlpha * A.X + Alpha * B.X,
			OneMinusAlpha * A.Y + Alpha * B.Y);
	}

	// Note: This was called "IsLeft" in the Geometry3Sharp code (where it also went through Math.Sign)
	// Returns >0 if C is to the left of the A->B Line, <0 if to the right, 0 if on the line
	static T Orient(const FVector2<T>& A, const FVector2<T>& B, const FVector2<T>& C)
	{
		return (B - A).DotPerp(C - A);
	}
};


/**
 * Templated 3D Vector. Ported from g3Sharp library, with the intention of 
 * maintaining compatibility with existing g3Sharp code. Has an API
 * similar to WildMagic, GTEngine, Eigen, etc. 
 * 
 * Convenience typedefs for FVector3f/FVector3d/FVector3i are defined, and
 * should be preferentially used over the base template type
 * 
 * @todo Possibly can be replaced/merged with Chaos TVector<T,N>
 */
template <typename T>
struct FVector3
{
	T X{}, Y{}, Z{};

	constexpr FVector3()
		: X(0), Y(0), Z(0)
	{
	}

	constexpr FVector3(T ValX, T ValY, T ValZ)
		: X(ValX), Y(ValY), Z(ValZ)
	{
	}

	constexpr FVector3(const T* Data)
		: X(Data[0]), Y(Data[1]), Z(Data[2])
	{
	}

	constexpr FVector3(const FVector2<T>& Data)
		: X(Data.X), Y(Data.Y), Z((T)0)
	{
	}

	constexpr FVector3(const FVector3& Vec) = default;

	template<typename RealType2>
	explicit constexpr FVector3(const FVector3<RealType2>& Vec)
		: X((T)Vec.X), Y((T)Vec.Y), Z((T)Vec.Z)
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

	explicit operator FVector() const
	{
		return FVector((float)X, (float)Y, (float)Z);
	}

	FVector3(const FVector& Vec)
		: X((T)Vec.X), Y((T)Vec.Y), Z((T)Vec.Z)
	{
	}


	explicit constexpr operator FLinearColor() const
	{
		return FLinearColor((float)X, (float)Y, (float)Z);
	}
	constexpr FVector3(const FLinearColor& Color)
		: X((T)Color.R), Y((T)Color.G), Z((T)Color.B)
	{
	}

	explicit constexpr operator FColor() const
	{
		return FColor(
			FMathf::Clamp((int)((float)X*255.0f), 0, 255),
			FMathf::Clamp((int)((float)Y*255.0f), 0, 255),
			FMathf::Clamp((int)((float)Z*255.0f), 0, 255));
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
	
	/** @return unit vector along axis X=0, Y=1, Z=2 */
	constexpr static FVector3<T> MakeUnit(int32 Axis)
	{
		FVector3<T> UnitVec((T)0, (T)0, (T)0);
		UnitVec[FMath::Clamp(Axis, 0, 2)] = (T)1;
		return UnitVec;
	}

	FVector3<T>& operator=(const FVector3<T>& V2)
	{
		X = V2.X;
		Y = V2.Y;
		Z = V2.Z;
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
		return TMathUtil<T>::Sqrt(X * X + Y * Y + Z * Z);
	}
	T SquaredLength() const
	{
		return X * X + Y * Y + Z * Z;
	}

	constexpr T Distance(const FVector3<T>& V2) const
	{
		T dx = V2.X - X;
		T dy = V2.Y - Y;
		T dz = V2.Z - Z;
		return TMathUtil<T>::Sqrt(dx * dx + dy * dy + dz * dz);
	}
	constexpr T DistanceSquared(const FVector3<T>& V2) const
	{
		T dx = V2.X - X;
		T dy = V2.Y - Y;
		T dz = V2.Z - Z;
		return dx * dx + dy * dy + dz * dz;
	}

	constexpr FVector3<T> operator-() const
	{
		return FVector3<T>(-X, -Y, -Z);
	}

	constexpr FVector3<T> operator+(const FVector3<T>& V2) const
	{
		return FVector3<T>(X + V2.X, Y + V2.Y, Z + V2.Z);
	}

	constexpr FVector3<T> operator-(const FVector3<T>& V2) const
	{
		return FVector3<T>(X - V2.X, Y - V2.Y, Z - V2.Z);
	}

	constexpr FVector3<T> operator+(const T& Scalar) const
	{
		return FVector3<T>(X + Scalar, Y + Scalar, Z + Scalar);
	}

	constexpr FVector3<T> operator-(const T& Scalar) const
	{
		return FVector3<T>(X - Scalar, Y - Scalar, Z - Scalar);
	}

	constexpr FVector3<T> operator*(const T& Scalar) const
	{
		return FVector3<T>(X * Scalar, Y * Scalar, Z * Scalar);
	}

	template<typename RealType2>
	constexpr FVector3<T> operator*(const RealType2& Scalar) const
	{
		return FVector3<T>(X * (T)Scalar, Y * (T)Scalar, Z * (T)Scalar);
	}

	constexpr FVector3<T> operator*(const FVector3<T>& V2) const // component-wise
	{
		return FVector3<T>(X * V2.X, Y * V2.Y, Z * V2.Z);
	}

	constexpr FVector3<T> operator/(const T& Scalar) const
	{
		return FVector3<T>(X / Scalar, Y / Scalar, Z / Scalar);
	}

	constexpr FVector3<T> operator/(const FVector3<T>& V2) const // component-wise
	{
		return FVector3<T>(X / V2.X, Y / V2.Y, Z / V2.Z);
	}

	constexpr FVector3<T>& operator+=(const FVector3<T>& V2)
	{
		X += V2.X;
		Y += V2.Y;
		Z += V2.Z;
		return *this;
	}

	constexpr FVector3<T>& operator-=(const FVector3<T>& V2)
	{
		X -= V2.X;
		Y -= V2.Y;
		Z -= V2.Z;
		return *this;
	}

	constexpr FVector3<T>& operator*=(const T& Scalar)
	{
		X *= Scalar;
		Y *= Scalar;
		Z *= Scalar;
		return *this;
	}

	constexpr FVector3<T>& operator/=(const T& Scalar)
	{
		X /= Scalar;
		Y /= Scalar;
		Z /= Scalar;
		return *this;
	}

	T Dot(const FVector3<T>& V2) const
	{
		return X * V2.X + Y * V2.Y + Z * V2.Z;
	}

	FVector3<T> Cross(const FVector3<T>& V2) const
	{
		return FVector3(
			Y * V2.Z - Z * V2.Y,
			Z * V2.X - X * V2.Z,
			X * V2.Y - Y * V2.X);
	}

	FVector3<T> UnitCross(const FVector3<T>& V2) const
	{
		FVector3<T> N = Cross(V2);
		return N.Normalized();
	}

	/**
	 * Assumes this vector and V2 are both already normalized
	 * @return the angle to vector V2 in degrees
	 */
	T AngleD(const FVector3<T>& V2) const
	{
		T DotVal = Dot(V2);
		T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
		return (T)(acos(ClampedDot) * (T)(180.0 / 3.14159265358979323846));
	}

	/**
	 * Assumes this vector and V2 are both already normalized
	 * @return the angle to vector V2 in radians
	 */
	T AngleR(const FVector3<T>& V2) const
	{
		T DotVal = Dot(V2);
		T ClampedDot = (DotVal < (T)-1) ? (T)-1 : ((DotVal > (T)1) ? (T)1 : DotVal);
		return (T)acos(ClampedDot);
	}

	constexpr bool IsNormalized()
	{
		return TMathUtil<T>::Abs((X * X + Y * Y + Z * Z) - 1) < TMathUtil<T>::ZeroTolerance;
	}

	T Normalize(const T Epsilon = 0)
	{
		T length = Length();
		if (length > Epsilon)
		{
			T invLength = ((T)1) / length;
			X *= invLength;
			Y *= invLength;
			Z *= invLength;
			return length;
		}
		X = Y = Z = (T)0;
		return 0;
	}

	constexpr FVector3<T> Normalized(const T Epsilon = 0) const
	{
		T length = Length();
		if (length > Epsilon)
		{
			T invLength = ((T)1) / length;
			return FVector3<T>(X * invLength, Y * invLength, Z * invLength);
		}
		return FVector3<T>((T)0, (T)0, (T)0);
	}

	constexpr T MaxElement() const
	{
		return TMathUtil<T>::Max3(X,Y,Z);
	}

	/** @return 0/1/2 index of maximum element */
	constexpr int32 MaxElementIndex() const
	{
		return TMathUtil<T>::Max3Index(X, Y, Z);
	}

	constexpr T MinElement() const
	{
		return TMathUtil<T>::Min3(X,Y,Z);
	}

	/** @return 0/1/2 index of minimum element */
	constexpr int32 MinElementIndex() const
	{
		return TMathUtil<T>::Min3Index(X, Y, Z);
	}

	constexpr friend FVector3 Min( const FVector3& V0, const FVector3& V1 )
	{
		return FVector3(TMathUtil<T>::Min(V0.X, V1.X),
						TMathUtil<T>::Min(V0.Y, V1.Y),
						TMathUtil<T>::Min(V0.Z, V1.Z));
	}

	constexpr friend FVector3 Max( const FVector3& V0, const FVector3& V1 )
	{
		return FVector3(TMathUtil<T>::Max(V0.X, V1.X),
						TMathUtil<T>::Max(V0.Y, V1.Y),
						TMathUtil<T>::Max(V0.Z, V1.Z));
	}

	constexpr T MaxAbsElement() const
	{
		return TMathUtil<T>::Max3(TMathUtil<T>::Abs(X), TMathUtil<T>::Abs(Y), TMathUtil<T>::Abs(Z));
	}

	/** @return 0/1/2 index of maximum absolute-value element */
	constexpr T MaxAbsElementIndex() const
	{
		return TMathUtil<T>::Max3Index(TMathUtil<T>::Abs(X), TMathUtil<T>::Abs(Y), TMathUtil<T>::Abs(Z));
	}

	constexpr T MinAbsElement() const
	{
		return TMathUtil<T>::Min3(TMathUtil<T>::Abs(X), TMathUtil<T>::Abs(Y), TMathUtil<T>::Abs(Z));
	}

	/** @return 0/1/2 index of minimum absolute-value element */
	constexpr T MinAbsElementIndex() const
	{
		return TMathUtil<T>::Min3Index(TMathUtil<T>::Abs(X), TMathUtil<T>::Abs(Y), TMathUtil<T>::Abs(Z));
	}

	constexpr FVector2<T> XY() const
	{
		return FVector2<T>(X, Y);
	}
	constexpr FVector2<T> XZ() const
	{
		return FVector2<T>(X, Z);
	}
	constexpr FVector2<T> YZ() const
	{
		return FVector2<T>(Y, Z);
	}

	static FVector3<T> Lerp(const FVector3<T>& A, const FVector3<T>& B, T Alpha)
	{
		T OneMinusAlpha = (T)1 - Alpha;
		return FVector3<T>(OneMinusAlpha * A.X + Alpha * B.X,
						   OneMinusAlpha * A.Y + Alpha * B.Y,
						   OneMinusAlpha * A.Z + Alpha * B.Z);
	}

	static FVector3<T> Blend3(const FVector3<T>& A, const FVector3<T>& B, const FVector3<T>& C, const T& WeightA, const T& WeightB, const T& WeightC)
	{
		return FVector3<T>(
			WeightA*A.X + WeightB*B.X + WeightC*C.X,
			WeightA*A.Y + WeightB*B.Y + WeightC*C.Y,
			WeightA*A.Z + WeightB*B.Z + WeightC*C.Z);
	}


	constexpr bool operator==(const FVector3<T>& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z;
	}

	constexpr bool operator!=(const FVector3<T>& Other) const
	{
		return X != Other.X || Y != Other.Y || Z != Other.Z;
	}
};

template <typename RealType>
inline FVector3<RealType> operator*(RealType Scalar, const FVector3<RealType>& V)
{
	return FVector3<RealType>(Scalar * V.X, Scalar * V.Y, Scalar * V.Z);
}

// allow float*Vector3<double> and double*Vector3<float>
template <typename RealType, typename RealType2>
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

typedef FVector3<float> FVector3f;
typedef FVector3<double> FVector3d;
typedef FVector3<int> FVector3i;

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

typedef FVector2<float> FVector2f;
typedef FVector2<double> FVector2d;
typedef FVector2<int> FVector2i;










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
		return FVector3<T>((T)1, (T)1, (T)1, (T)1);
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


	constexpr bool IsNormalized()
	{
		return TMathUtil<T>::Abs((X * X + Y * Y + Z * Z + W * W) - 1) < TMathUtil<T>::ZeroTolerance;
	}

	T Normalize(const T Epsilon = 0)
	{
		T length = Length();
		if (length > Epsilon)
		{
			T invLength = ((T)1) / length;
			X *= invLength;
			Y *= invLength;
			Z *= invLength;
			W *= invLength;
			return length;
		}
		X = Y = Z = (T)0;
		return 0;
	}

	constexpr TVector4<T> Normalized(const T Epsilon = 0) const
	{
		T length = Length();
		if (length > Epsilon)
		{
			T invLength = ((T)1) / length;
			return TVector4<T>(X * invLength, Y * invLength, Z * invLength, W * invLength);
		}
		return TVector4<T>::Zero();
	}


	constexpr FVector3<T> XYZ() const
	{
		return FVector3<T>(X, Y, Z);
	}

	static TVector4<T> Lerp(const TVector4<T>& A, const TVector4<T>& B, T Alpha)
	{
		T OneMinusAlpha = (T)1 - Alpha;
		return TVector4<T>(OneMinusAlpha * A.X + Alpha * B.X,
						   OneMinusAlpha * A.Y + Alpha * B.Y,
						   OneMinusAlpha * A.Z + Alpha * B.Z,
						   OneMinusAlpha * A.W + Alpha * B.W );
	}

	static TVector4<T> Blend3(const TVector4<T>& A, const TVector4<T>& B, const TVector4<T>& C, const T& WeightA, const T& WeightB, const T& WeightC)
	{
		return TVector4<T>(
			WeightA*A.X + WeightB*B.X + WeightC*C.X,
			WeightA*A.Y + WeightB*B.Y + WeightC*C.Y,
			WeightA*A.Z + WeightB*B.Z + WeightC*C.Z,
			WeightA*A.W + WeightB*B.W + WeightC*C.W);
	}


	constexpr bool operator==(const TVector4<T>& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z && W == Other.W;
	}

	constexpr bool operator!=(const TVector4<T>& Other) const
	{
		return X != Other.X || Y != Other.Y || Z != Other.Z || Z == Other.Z;
	}
};

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

typedef TVector4<float> FVector4f;
typedef TVector4<double> FVector4d;
typedef TVector4<int> FVector4i;

template <typename T>
FORCEINLINE uint32 GetTypeHash(const TVector4<T>& Vector)
{
	// (this is how FIntVector and all the other FVectors do their hash functions)
	// Note: this assumes there's no padding that could contain uncompared data.
	return FCrc::MemCrc_DEPRECATED(&Vector, sizeof(TVector4<T>));
}