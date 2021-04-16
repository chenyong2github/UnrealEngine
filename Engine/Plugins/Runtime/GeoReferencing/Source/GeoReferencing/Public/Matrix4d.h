// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MatrixTypes.h"

template <typename RealType>
struct TMatrix4
{
	TVector4<RealType> Row0;
	TVector4<RealType> Row1;
	TVector4<RealType> Row2;
	TVector4<RealType> Row3;

	TMatrix4()
	{
	}

	TMatrix4(RealType ConstantValue)
	{
		Row0 = TVector4<RealType>(ConstantValue, ConstantValue, ConstantValue, ConstantValue);
		Row1 = Row0;
		Row2 = Row0;
		Row3 = Row0;
	}

	TMatrix4(RealType Diag0, RealType Diag1, RealType Diag2, RealType Diag3)
	{
		Row0 = TVector4<RealType>(Diag0, 0, 0, 0);
		Row1 = TVector4<RealType>(0, Diag1, 0, 0);
		Row2 = TVector4<RealType>(0, 0, Diag2, 0);
		Row3 = TVector4<RealType>(0, 0, 0, Diag3);
	}

	/**
	 * Construct outer-product of U*transpose(V) of U and V
	 * result is that Mij = u_i * v_j
	 */
	TMatrix4<RealType>(const TVector4<RealType>& U, const TVector4<RealType>& V)
		: Row0(U.X* V.X, U.X* V.Y, U.X* V.Z, U.X* V.W),
		Row1(U.Y* V.X, U.Y* V.Y, U.Y* V.Z, U.Y* V.W),
		Row2(U.Z* V.X, U.Z* V.Y, U.Z* V.Z, U.Z* V.W),
		Row3(U.W* V.X, U.W* V.Y, U.W* V.Z, U.W* V.W)
	{
	}

	TMatrix4(RealType M00, RealType M01, RealType M02, RealType M03, RealType M10, RealType M11, RealType M12, RealType M13,  RealType M20, RealType M21, RealType M22, RealType M23, RealType M30, RealType M31, RealType M32, RealType M33)
		: Row0(M00, M01, M02, M03),
		  Row1(M10, M11, M12, M13),
		  Row2(M20, M21, M22, M23), 
		  Row3(M30, M31, M32, M33)
	{
	}

	TMatrix4(const TVector4<RealType>& V1, const TVector4<RealType>& V2, const TVector4<RealType>& V3, const TVector4<RealType>& V4, bool bRows)
	{
		if (bRows)
		{
			Row0 = V1;
			Row1 = V2;
			Row2 = V3;
			Row3 = V4;
		}
		else
		{
			Row0 = TVector4<RealType>(V1.X, V2.X, V3.X, V4.X);
			Row1 = TVector4<RealType>(V1.Y, V2.Y, V3.Y, V4.Y);
			Row2 = TVector4<RealType>(V1.Z, V2.Z, V3.Z, V4.Z);
			Row3 = TVector4<RealType>(V1.W, V2.W, V3.W, V4.W);
		}
	}

	static TMatrix4<RealType> Zero()
	{
		return TMatrix4<RealType>(0);
	}
	static TMatrix4<RealType> Identity()
	{
		return TMatrix4<RealType>(1, 1, 1, 1);
	}

	RealType operator()(int Row, int Col) const
	{
		check(Row >= 0 && Row < 4 && Col >= 0 && Col < 4);
		if (Row == 0)
		{
			return Row0[Col];
		}
		else if (Row == 1)
		{
			return Row1[Col];
		}
		else if (Row == 2)
		{
			return Row2[Col];
		}
		else
		{
			return Row3[Col];
		}
	}

	TMatrix4<RealType> operator*(RealType Scale) const
	{
		return TMatrix4<RealType>(
			Row0.X * Scale, Row0.Y * Scale, Row0.Z * Scale, Row0.W * Scale,
			Row1.X * Scale, Row1.Y * Scale, Row1.Z * Scale, Row1.W * Scale,
			Row2.X * Scale, Row2.Y * Scale, Row2.Z * Scale, Row2.W * Scale,
			Row3.X * Scale, Row3.Y * Scale, Row3.Z * Scale, Row3.W * Scale);
	}

	TVector4<RealType> operator*(const TVector4<RealType>& V) const
	{
		return TVector4<RealType>(
			Row0.X * V.X + Row0.Y * V.Y + Row0.Z * V.Z + Row0.W * V.W,
			Row1.X * V.X + Row1.Y * V.Y + Row1.Z * V.Z + Row1.W * V.W,
			Row2.X * V.X + Row2.Y * V.Y + Row2.Z * V.Z + Row2.W * V.W,
			Row3.X * V.X + Row3.Y * V.Y + Row3.Z * V.Z + Row3.W * V.W);
	}

	FVector3<RealType> operator*(const FVector3<RealType>& V) const
	{
		return FVector3<RealType>(
			Row0.X * V.X + Row0.Y * V.Y + Row0.Z * V.Z,
			Row1.X * V.X + Row1.Y * V.Y + Row1.Z * V.Z,
			Row2.X * V.X + Row2.Y * V.Y + Row2.Z * V.Z);
	}

	TMatrix4<RealType> operator*(const TMatrix4<RealType>& Mat2) const
	{
		RealType M00 = Row0.X * Mat2.Row0.X + Row0.Y * Mat2.Row1.X + Row0.Z * Mat2.Row2.X + Row0.W * Mat2.Row3.X;
		RealType M01 = Row0.X * Mat2.Row0.Y + Row0.Y * Mat2.Row1.Y + Row0.Z * Mat2.Row2.Y + Row0.W * Mat2.Row3.Y;
		RealType M02 = Row0.X * Mat2.Row0.Z + Row0.Y * Mat2.Row1.Z + Row0.Z * Mat2.Row2.Z + Row0.W * Mat2.Row3.Z;
		RealType M03 = Row0.X * Mat2.Row0.W + Row0.Y * Mat2.Row1.W + Row0.Z * Mat2.Row2.W + Row0.W * Mat2.Row3.W;

		RealType M10 = Row1.X * Mat2.Row0.X + Row1.Y * Mat2.Row1.X + Row1.Z * Mat2.Row2.X + Row1.W * Mat2.Row3.X;
		RealType M11 = Row1.X * Mat2.Row0.Y + Row1.Y * Mat2.Row1.Y + Row1.Z * Mat2.Row2.Y + Row1.W * Mat2.Row3.Y;
		RealType M12 = Row1.X * Mat2.Row0.Z + Row1.Y * Mat2.Row1.Z + Row1.Z * Mat2.Row2.Z + Row1.W * Mat2.Row3.Z;
		RealType M13 = Row1.X * Mat2.Row0.W + Row1.Y * Mat2.Row1.W + Row1.Z * Mat2.Row2.W + Row1.W * Mat2.Row3.W;

		RealType M20 = Row2.X * Mat2.Row0.X + Row2.Y * Mat2.Row1.X + Row2.Z * Mat2.Row2.X + Row2.W * Mat2.Row3.X;
		RealType M21 = Row2.X * Mat2.Row0.Y + Row2.Y * Mat2.Row1.Y + Row2.Z * Mat2.Row2.Y + Row2.W * Mat2.Row3.Y;
		RealType M22 = Row2.X * Mat2.Row0.Z + Row2.Y * Mat2.Row1.Z + Row2.Z * Mat2.Row2.Z + Row2.W * Mat2.Row3.Z;
		RealType M23 = Row2.X * Mat2.Row0.W + Row2.Y * Mat2.Row1.W + Row2.Z * Mat2.Row2.W + Row2.W * Mat2.Row3.W;

		RealType M30 = Row3.X * Mat2.Row0.X + Row3.Y * Mat2.Row1.X + Row3.Z * Mat2.Row2.X + Row3.W * Mat2.Row3.X;
		RealType M31 = Row3.X * Mat2.Row0.Y + Row3.Y * Mat2.Row1.Y + Row3.Z * Mat2.Row2.Y + Row3.W * Mat2.Row3.Y;
		RealType M32 = Row3.X * Mat2.Row0.Z + Row3.Y * Mat2.Row1.Z + Row3.Z * Mat2.Row2.Z + Row3.W * Mat2.Row3.Z;
		RealType M33 = Row3.X * Mat2.Row0.W + Row3.Y * Mat2.Row1.W + Row3.Z * Mat2.Row2.W + Row3.W * Mat2.Row3.W;


		return TMatrix4<RealType>(M00, M01, M02, M03, M10, M11, M12, M13, M20, M21, M22, M23, M30, M31, M32, M33);
	}

	TMatrix4<RealType> operator+(const TMatrix4<RealType>& Mat2) const
	{
		return TMatrix4<RealType>(Row0 + Mat2.Row0, Row1 + Mat2.Row1, Row2 + Mat2.Row2, Row3 + Mat2.Row3, true);
	}

	TMatrix4<RealType> operator-(const TMatrix4<RealType>& Mat2) const
	{
		return TMatrix4<RealType>(Row0 - Mat2.Row0, Row1 - Mat2.Row1, Row2 - Mat2.Row2, Row3 - Mat2.Row3, true);
	}

	inline TMatrix4<RealType>& operator*=(const RealType& Scalar)
	{
		Row0 *= Scalar;
		Row1 *= Scalar;
		Row2 *= Scalar;
		Row3 *= Scalar;
		return *this;
	}

	inline TMatrix4<RealType>& operator+=(const TMatrix4<RealType>& Mat2)
	{
		Row0 += Mat2.Row0;
		Row1 += Mat2.Row1;
		Row2 += Mat2.Row2;
		Row3 += Mat2.Row3;
		return *this;
	}

	RealType InnerProduct(const TMatrix4<RealType>& Mat2) const
	{
		return Row0.Dot(Mat2.Row0) + Row1.Dot(Mat2.Row1) + Row2.Dot(Mat2.Row2) + Row3.Dot(Mat2.Row3);
	}

	RealType Trace() const
	{
		return Row0.X + Row1.Y + Row2.Z + Row3.W;
	}

	RealType Determinant() const
	{
		RealType a11 = Row0.X, a12 = Row0.Y, a13 = Row0.Z, a14 = Row0.W,
				 a21 = Row1.X, a22 = Row1.Y, a23 = Row1.Z, a24 = Row1.W,
				 a31 = Row2.X, a32 = Row2.Y, a33 = Row2.Z, a34 = Row2.W,
				 a41 = Row3.X, a42 = Row3.Y, a43 = Row3.Z, a44 = Row3.W;

		RealType i00 = 
			a22 * (a33 * a44 - a34 * a43) -
			a32 * (a23 * a44 - a24 * a43) +
			a42 * (a23 * a34 - a24 * a33);

		RealType i01 = 
			a12 * (a33 * a44 - a34 * a43) -
			a32 * (a13 * a44 - a14 * a43) +
			a42 * (a13 * a34 - a14 * a33);

		RealType i02 =
			a12 * (a23 * a44 - a24 * a43) -
			a22 * (a13 * a44 - a14 * a43) +
			a42 * (a13 * a24 - a14 * a23);

		RealType i03 =
			a12 * (a23 * a34 - a24 * a33) -
			a22 * (a13 * a34 - a14 * a33) +
			a32 * (a13 * a24 - a14 * a23);

		return a11 * i00 - a21 * i01 + a31 * i02 - a41 * i03;
	}

	TMatrix4<RealType> Inverse() const
	{
		RealType a11 = Row0.X, a12 = Row0.Y, a13 = Row0.Z, a14 = Row0.W,
			a21 = Row1.X, a22 = Row1.Y, a23 = Row1.Z, a24 = Row1.W,
			a31 = Row2.X, a32 = Row2.Y, a33 = Row2.Z, a34 = Row2.W,
			a41 = Row3.X, a42 = Row3.Y, a43 = Row3.Z, a44 = Row3.W;

		RealType Tmp11 = a33 * a44 - a34 * a43;
		RealType Tmp12 = a23 * a44 - a24 * a43;
		RealType Tmp13 = a23 * a34 - a24 * a33;

		RealType Tmp21 = a33 * a44 - a34 * a43;
		RealType Tmp22 = a13 * a44 - a14 * a43;
		RealType Tmp23 = a13 * a34 - a14 * a33;

		RealType Tmp31 = a23 * a44 - a24 * a43;
		RealType Tmp32 = a13 * a44 - a14 * a43;
		RealType Tmp33 = a13 * a24 - a14 * a23;

		RealType Tmp41 = a23 * a34 - a24 * a33;
		RealType Tmp42 = a13 * a34 - a14 * a33;
		RealType Tmp43 = a13 * a24 - a14 * a23;

		RealType i00 = a22 * Tmp11 - a32 * Tmp12 + a42 * Tmp13;
		RealType i01 = a12 * Tmp21 - a32 * Tmp22 + a42 * Tmp23;
		RealType i02 = a12 * Tmp31 - a22 * Tmp32 + a42 * Tmp33;
		RealType i03 = a12 * Tmp41 - a22 * Tmp42 + a32 * Tmp43;

		RealType Determinant = a11 * i00 - a21 * i01 + a31 * i02 - a41 * i03;

		ensure(TMathUtil<RealType>::Abs(Determinant) >= TMathUtil<RealType>::Epsilon);

		const RealType    RDet = 1.0 / Determinant;

		RealType Result11 = RDet * i00;
		RealType Result12 = -RDet * i01;
		RealType Result13 = RDet * i02;
		RealType Result14 = -RDet * i03;
		RealType Result21 = -RDet * (a21 * Tmp11 - a31 * Tmp12 + a41 * Tmp13);
		RealType Result22 = RDet * (a11 * Tmp21 - a31 * Tmp22 + a41 * Tmp23);
		RealType Result23 = -RDet * (a11 * Tmp31 - a21 * Tmp32 + a41 * Tmp33);
		RealType Result24 = RDet * (a11 * Tmp41 - a21 * Tmp42 + a31 * Tmp43);
		RealType Result31 = RDet * (
			a21 * (a32 * a44 - a34 * a42) -
			a31 * (a22 * a44 - a24 * a42) +
			a41 * (a22 * a34 - a24 * a32)
			);
		RealType Result32 = -RDet * (
			a11 * (a32 * a44 - a34 * a42) -
			a31 * (a12 * a44 - a14 * a42) +
			a41 * (a12 * a34 - a14 * a32)
			);
		RealType Result33 = RDet * (
			a11 * (a22 * a44 - a24 * a42) -
			a21 * (a12 * a44 - a14 * a42) +
			a41 * (a12 * a24 - a14 * a22)
			);
		RealType Result34 = -RDet * (
			a11 * (a22 * a34 - a24 * a32) -
			a21 * (a12 * a34 - a14 * a32) +
			a31 * (a12 * a24 - a14 * a22)
			);
		RealType Result41 = -RDet * (
			a21 * (a32 * a43 - a33 * a42) -
			a31 * (a22 * a43 - a23 * a42) +
			a41 * (a22 * a33 - a23 * a32)
			);
		RealType Result42 = RDet * (
			a11 * (a32 * a43 - a33 * a42) -
			a31 * (a12 * a43 - a13 * a42) +
			a41 * (a12 * a33 - a13 * a32)
			);
		RealType Result43 = -RDet * (
			a11 * (a22 * a43 - a23 * a42) -
			a21 * (a12 * a43 - a13 * a42) +
			a41 * (a12 * a23 - a13 * a22)
			);
		RealType Result44 = RDet * (
			a11 * (a22 * a33 - a23 * a32) -
			a21 * (a12 * a33 - a13 * a32) +
			a31 * (a12 * a23 - a13 * a22)
			);
		
		return TMatrix4<RealType>(
			Result11, Result12, Result13, Result14,
			Result21, Result22, Result23, Result24,
			Result31, Result32, Result33, Result34,
			Result41, Result42, Result43, Result44);
	}

	TMatrix4<RealType> Transpose() const
	{
		return TMatrix4<RealType>(
			Row0.X, Row1.X, Row2.X, Row3.X,
			Row0.Y, Row1.Y, Row2.Y, Row3.Y,
			Row0.Z, Row1.Z, Row2.Z, Row3.Z,
			Row0.W, Row1.W, Row2.W, Row3.W);
	}

	bool EpsilonEqual(const TMatrix4<RealType>& Mat2, RealType Epsilon) const
	{
		return VectorUtil::EpsilonEqual(Row0, Mat2.Row0, Epsilon) &&
			VectorUtil::EpsilonEqual(Row1, Mat2.Row1, Epsilon) &&
			VectorUtil::EpsilonEqual(Row2, Mat2.Row2, Epsilon) &&
			VectorUtil::EpsilonEqual(Row3, Mat2.Row3, Epsilon);
	}
};

template <typename RealType>
inline TMatrix4<RealType> operator*(RealType Scale, const TMatrix4<RealType>& Mat)
{
	return TMatrix4<RealType>(
		Mat.Row0.X * Scale, Mat.Row0.Y * Scale, Mat.Row0.Z * Scale, Mat.Row0.W,
		Mat.Row1.X * Scale, Mat.Row1.Y * Scale, Mat.Row1.Z * Scale, Mat.Row1.W,
		Mat.Row2.X * Scale, Mat.Row2.Y * Scale, Mat.Row2.Z * Scale, Mat.Row2.W,
		Mat.Row3.X * Scale, Mat.Row3.Y * Scale, Mat.Row3.Z * Scale, Mat.Row3.W
		);
}

typedef TMatrix4<float> FMatrix4f;
typedef TMatrix4<double> FMatrix4d;