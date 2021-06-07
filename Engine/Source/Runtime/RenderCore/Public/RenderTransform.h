// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/MemoryLayout.h"

// TODO: Further compress data size with tighter encoding
// LWC_TODO: Rebasing support (no 64bit types in here)
// TODO: Optimization (avoid full 4x4 math)
struct FRenderTransform
{
	FVector3f TransformRows[3];
	FVector3f Origin;

public:
	FORCEINLINE FRenderTransform()
	{
	}

	FORCEINLINE FRenderTransform(const FMatrix44f& M)
	{
		TransformRows[0]	= FVector3f(M.M[0][0], M.M[0][1], M.M[0][2]);
		TransformRows[1]	= FVector3f(M.M[1][0], M.M[1][1], M.M[1][2]);
		TransformRows[2]	= FVector3f(M.M[2][0], M.M[2][1], M.M[2][2]);
		Origin				= FVector3f(M.M[3][0], M.M[3][1], M.M[3][2]);
	}

	FORCEINLINE FRenderTransform& operator=(const FRenderTransform& From)
	{
		TransformRows[0] = From.TransformRows[0];
		TransformRows[1] = From.TransformRows[1];
		TransformRows[2] = From.TransformRows[2];
		Origin = From.Origin;
		return *this;
	}

	FORCEINLINE FRenderTransform& operator=(const FMatrix44f& From)
	{
		TransformRows[0]	= FVector3f(From.M[0][0], From.M[0][1], From.M[0][2]);
		TransformRows[1]	= FVector3f(From.M[1][0], From.M[1][1], From.M[1][2]);
		TransformRows[2]	= FVector3f(From.M[2][0], From.M[2][1], From.M[2][2]);
		Origin				= FVector3f(From.M[3][0], From.M[3][1], From.M[3][2]);
		return *this;
	}

	FORCEINLINE bool Equals(const FRenderTransform& Other, float Tolerance = KINDA_SMALL_NUMBER) const
	{
		return
			TransformRows[0].Equals(Other.TransformRows[0], Tolerance) &&
			TransformRows[1].Equals(Other.TransformRows[1], Tolerance) &&
			TransformRows[2].Equals(Other.TransformRows[2], Tolerance) &&
			Origin.Equals(Other.Origin, Tolerance);
	}

	FORCEINLINE FMatrix44f ToMatrix44f() const
	{
		FMatrix44f Matrix;
		Matrix.M[0][0] = TransformRows[0].X;
		Matrix.M[0][1] = TransformRows[0].Y;
		Matrix.M[0][2] = TransformRows[0].Z;
		Matrix.M[0][3] = 0.0f;
		Matrix.M[1][0] = TransformRows[1].X;
		Matrix.M[1][1] = TransformRows[1].Y;
		Matrix.M[1][2] = TransformRows[1].Z;
		Matrix.M[1][3] = 0.0f;
		Matrix.M[2][0] = TransformRows[2].X;
		Matrix.M[2][1] = TransformRows[2].Y;
		Matrix.M[2][2] = TransformRows[2].Z;
		Matrix.M[2][3] = 0.0f;
		Matrix.M[3][0] = Origin.X;
		Matrix.M[3][1] = Origin.Y;
		Matrix.M[3][2] = Origin.Z;
		Matrix.M[3][3] = 1.0f;
		return Matrix;
	}

	FORCEINLINE FMatrix ToMatrix() const
	{
		return (FMatrix)ToMatrix44f();
	}

	FORCEINLINE void To3x4MatrixTranspose(float* Result) const
	{
		float* RESTRICT Dest = Result;

		Dest[ 0] = TransformRows[0].X;	// [0][0]
		Dest[ 1] = TransformRows[1].X;	// [1][0]
		Dest[ 2] = TransformRows[2].X;	// [2][0]
		Dest[ 3] = Origin.X;			// [3][0]

		Dest[ 4] = TransformRows[0].Y;	// [0][1]
		Dest[ 5] = TransformRows[1].Y;	// [1][1]
		Dest[ 6] = TransformRows[2].Y;	// [2][1]
		Dest[ 7] = Origin.Y;			// [3][1]

		Dest[ 8] = TransformRows[0].Z;	// [0][2]
		Dest[ 9] = TransformRows[1].Z;	// [1][2]
		Dest[10] = TransformRows[2].Z;	// [2][2]
		Dest[11] = Origin.Z;			// [3][2]
	}

	FORCEINLINE FRenderTransform operator* (const FRenderTransform& Other) const
	{
		// Use vectorized 4x4 implementation
		const FMatrix44f LHS = ToMatrix44f();
		const FMatrix44f RHS = Other.ToMatrix44f();
		return (LHS * RHS);
	}

	FORCEINLINE FRenderTransform operator* (const FMatrix44f& Other) const
	{
		// Use vectorized 4x4 implementation
		const FMatrix44f LHS = ToMatrix44f();
		return (LHS * Other);
	}

	FORCEINLINE float RotDeterminant() const
	{
		return
			TransformRows[0].X * (TransformRows[1].Y * TransformRows[2].Z - TransformRows[1].Z * TransformRows[2].Y) -
			TransformRows[1].X * (TransformRows[0].Y * TransformRows[2].Z - TransformRows[0].Z * TransformRows[2].Y) +
			TransformRows[2].X * (TransformRows[0].Y * TransformRows[1].Z - TransformRows[0].Z * TransformRows[1].Y);
	}

	FORCEINLINE FRenderTransform Inverse() const
	{
		// Use vectorized 4x4 implementation
		return ToMatrix44f().Inverse();
	}

	FORCEINLINE FRenderTransform InverseFast() const
	{
		// Use vectorized 4x4 implementation
		return ToMatrix44f().InverseFast();
	}

	FORCEINLINE FVector3f Orthonormalize()
	{
		FVector3f X = TransformRows[0];
		FVector3f Y = TransformRows[1];
		FVector3f Z = TransformRows[2];

		// Modified Gram-Schmidt orthogonalization
		Y -= (Y | X) / (X | X) * X;
		Z -= (Z | X) / (X | X) * X;
		Z -= (Z | Y) / (Y | Y) * Y;

		TransformRows[0] = X;
		TransformRows[1] = Y;
		TransformRows[2] = Z;

		// Extract per axis scales
		FVector3f Scale;
		Scale.X = X.Size();
		Scale.Y = Y.Size();
		Scale.Z = Z.Size();

		return Scale;
	}

	FORCEINLINE void SetIdentity()
	{
		TransformRows[0] = FVector3f(1.0f, 0.0f, 0.0f);
		TransformRows[1] = FVector3f(0.0f, 1.0f, 0.0f);
		TransformRows[2] = FVector3f(0.0f, 0.0f, 1.0f);
		Origin = FVector3f::ZeroVector;
	}

	/**
	 * Serializes the render transform.
	 *
	 * @param Ar Reference to the serialization archive.
	 * @param T Reference to the render transform being serialized.
	 * @return Reference to the Archive after serialization.
	 */
	FORCEINLINE friend FArchive& operator<< (FArchive& Ar, FRenderTransform& T)
	{
		Ar << T.TransformRows[0].X << T.TransformRows[0].Y << T.TransformRows[0].Z;
		Ar << T.TransformRows[1].X << T.TransformRows[1].Y << T.TransformRows[1].Z;
		Ar << T.TransformRows[2].X << T.TransformRows[2].Y << T.TransformRows[2].Z;
		Ar << T.Origin.X << T.Origin.Y << T.Origin.Z;
		return Ar;
	}

	RENDERCORE_API static FRenderTransform Identity;
};

struct FRenderBounds
{
	FVector3f Min;
	FVector3f Max;

public:
	FORCEINLINE FRenderBounds()
	: Min( MAX_flt,  MAX_flt,  MAX_flt)
	, Max(-MAX_flt, -MAX_flt, -MAX_flt)
	{
	}

	FORCEINLINE FRenderBounds(const FVector3f& InMin, const FVector3f& InMax)
	: Min(InMin)
	, Max(InMax)
	{
	}

	FORCEINLINE FRenderBounds(const FBox& Box)
	{
		Min = Box.Min;
		Max = Box.Max;
	}

	FORCEINLINE FRenderBounds(const FBoxSphereBounds& Bounds)
	{
		Min = Bounds.Origin - Bounds.BoxExtent;
		Max = Bounds.Origin + Bounds.BoxExtent;
	}

	FORCEINLINE FBox ToBox() const
	{
		return FBox(Min, Max);
	}

	FORCEINLINE FBoxSphereBounds ToBoxSphereBounds() const
	{
		return FBoxSphereBounds(ToBox());
	}

	FORCEINLINE FRenderBounds& operator = (const FVector3f& Other)
	{
		Min = Other;
		Max = Other;
		return *this;
	}

	FORCEINLINE FRenderBounds& operator += (const FVector3f& Other)
	{
		const VectorRegister VecOther = VectorLoadFloat3(&Other.X);
		VectorStoreFloat3(VectorMin(VectorLoadFloat3(&Min.Y), VecOther), &Min);
		VectorStoreFloat3(VectorMax(VectorLoadFloat3(&Max.X), VecOther), &Max);
		return *this;
	}

	FORCEINLINE FRenderBounds& operator += (const FRenderBounds& Other)
	{
		VectorStoreFloat3(VectorMin(VectorLoadFloat3(&Min), VectorLoadFloat3(&Other.Min)), &Min);
		VectorStoreFloat3(VectorMax(VectorLoadFloat3(&Max), VectorLoadFloat3(&Other.Max)), &Max);
		return *this;
	}

	FORCEINLINE FRenderBounds operator+ (const FRenderBounds& Other) const
	{
		return FRenderBounds(*this) += Other;
	}

	FORCEINLINE const FVector3f& GetMin() const
	{
		return Min;
	}

	FORCEINLINE const FVector3f& GetMax() const
	{
		return Max;
	}

	FORCEINLINE FVector3f GetCenter() const
	{
		return (Max + Min) * 0.5f;
	}

	FORCEINLINE FVector3f GetExtent() const
	{
		return (Max - Min) * 0.5f;
	}

	FORCEINLINE float GetSurfaceArea() const
	{
		FVector3f Size = Max - Min;
		return 0.5f * (Size.X * Size.Y + Size.X * Size.Z + Size.Y * Size.Z);
	}

	/**
	 * Gets a bounding volume transformed by a matrix.
	 *
	 * @param M The matrix.
	 * @return The transformed volume.
	 */
	RENDERCORE_API FRenderBounds TransformBy(const FMatrix44f& M) const;

	/**
	 * Gets a bounding volume transformed by a render transform.
	 *
	 * @param T The render transform.
	 * @return The transformed volume.
	 */
	RENDERCORE_API FRenderBounds TransformBy(const FRenderTransform& T) const;

	/**
	 * Serializes the render bounds.
	 *
	 * @param Ar Reference to the serialization archive.
	 * @param B Reference to the render bounds being serialized.
	 * @return Reference to the Archive after serialization.
	 */
	FORCEINLINE friend FArchive& operator<< (FArchive& Ar, FRenderBounds& B)
	{
		Ar << B.Min;
		Ar << B.Max;
		return Ar;
	}
};
