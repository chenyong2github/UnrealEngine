// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Math/Point.h"
#include "CADKernel/Math/MathConst.h"

namespace CADKernel
{
	/**
	* Should be unified with the math utilities implemented by the Geometry team.
	*/
	class CADKERNEL_API FMatrixH
	{
	private:
		double Matrix[16];

	public:

		static const FMatrixH Identity;

		FMatrixH()
		{
			SetIdentity();
		}

		FMatrixH(const double* const InMatrix16)
		{
			for (int32 Index = 0; Index < 16; Index++)
			{
				Matrix[Index] = InMatrix16[Index];
			}
		}

		FMatrixH(const double InMatrix44[][4])
		{
			for (int32 Row = 0; Row < 4; Row++)
			{
				for (int32 Column = 0; Column < 4; Column++)
				{
					Matrix[4 * Row + Column] = InMatrix44[Row][Column];
				}
			}
		}

		FMatrixH(const FPoint& Origin, const FPoint& Ox, const FPoint& Oy, const FPoint& Oz)
		{
			BuildChangeOfCoordinateSystemMatrix(Ox, Oy, Oz, Origin);
		}

		friend FArchive& operator<<(FArchive& Ar, FMatrixH& InMatrix)
		{
			Ar.Serialize(InMatrix.Matrix, 16 * sizeof(double));
			return Ar;
		}

		void SetIdentity()
		{
			for (int32 Row = 0; Row < 4; Row++)
			{
				for (int32 Column = 0; Column < 4; Column++)
				{
					Get(Row, Column) = Row == Column ? 1. : 0.;
				}
			}
		}

		void FromAxisOrigin(const FPoint& Axis, const FPoint& Origin);

		/**
		 * @param Xaxis the vector X of the old coordinate system in the new coordinate system
		 * @param Yaxis the vector Y of the old coordinate system in the new coordinate system
		 * @param Zaxis the vector Z of the old coordinate system in the new coordinate system
		 * @param Origin Origin of the old coordinate system in the new coordinate system
		 * @return the transform matrix
		 */
		void BuildChangeOfCoordinateSystemMatrix(const FPoint& Xaxis, const FPoint& Yaxis, const FPoint& Zaxis, const FPoint& Origin);

		FPoint Multiply(const FPoint& InPoint) const;

		FPoint MultiplyVector(const FPoint& InVector) const;

		static FMatrixH MakeRotationMatrix(double InAngle, const FPoint InAxe);

		static FMatrixH MakeTranslationMatrix(const FPoint& InPoint);

		static FMatrixH MakeScaleMatrix(double ScaleX, double ScaleY, double ScaleZ);

		/**
		 * Apply the rotation centered in origin to PointToRotate
		 */
		FPoint PointRotation(const FPoint& PointToRotate, const FPoint& Origin) const;
		FPoint2D PointRotation(const FPoint2D& PointToRotate, const FPoint2D& Origin) const;
		FVector PointRotation(const FVector& PointToRotate, const FVector& Origin) const;

		void Inverse();

		FMatrixH GetInverse() const
		{
			FMatrixH NewMatrix = *this;
			NewMatrix.Inverse();
			return NewMatrix;
		}

		void Transpose();

		double& Get(int32 Row, int32 Column)
		{
			return Matrix[Row * 4 + Column];
		}

		double Get(int32 Row, int32 Column) const
		{
			return Matrix[Row * 4 + Column];
		}

		double& operator()(int32 Row, int32 Column)
		{
			return Matrix[Row * 4 + Column];
		}

		double operator()(int32 Row, int32 Column) const
		{
			return Matrix[Row * 4 + Column];
		}

		double& operator[](int32 Index)
		{
			return Matrix[Index];
		}

		FMatrixH operator*(const FMatrixH& m) const;

		FPoint operator*(const FPoint& Point) const
		{
			return Multiply(Point);
		}

		FMatrixH operator+(const FMatrixH& m) const;

		void GetMatrixDouble(double* Matrix) const;

		FPoint Column(int32 Index) const;

		FPoint Row(int32 Index) const;

		void CrossProduct(const FPoint& vec);

		void Print(EVerboseLevel level) const;

		bool IsId() const
		{
			for (int32 Row = 0; Row < 4; Row++)
			{
				for (int32 Column = 0; Column < 4; Column++)
				{
					if (Row == Column)
					{
						if (!FMath::IsNearlyEqual(Get(Row, Column), 1.)) return false;
					}
					else
					{
						if (!FMath::IsNearlyZero(Get(Row, Column))) return false;
					}
				}
			}
			return true;
		}
	};

	CADKERNEL_API void InverseMatrixN(double* Matrix, int32 n);

	CADKERNEL_API void MatrixProduct(int32 ARowNum, int32 AColumnNum, int32 ResultRank, const double* InMatrixA, const double* InMatrixB, double* OutMatrix);

	CADKERNEL_API void TransposeMatrix(int32 RowNum, int32 ColumnNum, const double* InMatrix, double* OutMatrix);

} // namespace CADKernel

