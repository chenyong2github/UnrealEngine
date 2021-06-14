// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Math/MatrixH.h"

#include "CADKernel/Math/MathConst.h"
#include "CADKernel/UI/Message.h"

namespace CADKernel
{

	const FMatrixH FMatrixH::Identity;

	FPoint FMatrixH::Multiply(const FPoint& Point) const
	{
		return FPoint(
			Point.X * Get(0, 0) + Point.Y * Get(0, 1) + Point.Z * Get(0, 2) + Get(0, 3),
			Point.X * Get(1, 0) + Point.Y * Get(1, 1) + Point.Z * Get(1, 2) + Get(1, 3),
			Point.X * Get(2, 0) + Point.Y * Get(2, 1) + Point.Z * Get(2, 2) + Get(2, 3)
		);
	}

	/**
	 * Apply only rotation matrix to the vector
	 */
	FPoint FMatrixH::MultiplyVector(const FPoint& Vector) const
	{
		return FPoint(
			Vector.X * Get(0, 0) + Vector.Y * Get(0, 1) + Vector.Z * Get(0, 2),
			Vector.X * Get(1, 0) + Vector.Y * Get(1, 1) + Vector.Z * Get(1, 2),
			Vector.X * Get(2, 0) + Vector.Y * Get(2, 1) + Vector.Z * Get(2, 2)
		);
	}

	void FMatrixH::BuildChangeOfCoordinateSystemMatrix(const FPoint& Xaxis, const FPoint& Yaxis, const FPoint& Zaxis, const FPoint& Origin)
	{
		Get(0, 0) = Xaxis[0];
		Get(1, 0) = Xaxis[1];
		Get(2, 0) = Xaxis[2];
		Get(3, 0) = 0.0;

		Get(0, 1) = Yaxis[0];
		Get(1, 1) = Yaxis[1];
		Get(2, 1) = Yaxis[2];
		Get(3, 1) = 0.0;

		Get(0, 2) = Zaxis[0];
		Get(1, 2) = Zaxis[1];
		Get(2, 2) = Zaxis[2];
		Get(3, 2) = 0.0;

		Get(0, 3) = Origin[0];
		Get(1, 3) = Origin[1];
		Get(2, 3) = Origin[2];
		Get(3, 3) = 1.0;
	}

	void FMatrixH::FromAxisOrigin(const FPoint& Axis, const FPoint& Origin)
	{
		FPoint Zaxis = FPoint(0, 1, 0);
		FPoint Xaxis = Zaxis ^ Axis;

		//on cherche le vecteur ox
		Xaxis.Normalize();
		if (FMath::Abs(Xaxis.Length()) < SMALL_NUMBER)
		{
			Zaxis = FPoint(1, 0, 0);
			Xaxis = Axis ^ Zaxis;
			Xaxis.Normalize();
			if (FMath::Abs(Xaxis.Length()) < SMALL_NUMBER)
			{
				Zaxis = FPoint(0, 0, 1);
				Xaxis = Axis ^ Zaxis;

				Xaxis.Normalize();
				ensureCADKernel(FMath::Abs(Xaxis.Length()) > SMALL_NUMBER);
			}
		}

		FPoint YAxis = Axis ^ Xaxis;
		Zaxis = Axis;

		YAxis.Normalize();
		Zaxis.Normalize();

		BuildChangeOfCoordinateSystemMatrix(Xaxis, YAxis, Zaxis, Origin);
	}

	FMatrixH FMatrixH::MakeRotationMatrix(double Angle, FPoint Axe)
	{
		FMatrixH Matrix;
		Matrix.SetIdentity();

		ensureCADKernel(Axe.Length() > SMALL_NUMBER);
		Axe.Normalize();

		Matrix.Get(0, 0) = Axe[0] * Axe[0] + (double)cos(Angle) * ((double)1.0 - Axe[0] * Axe[0]);
		Matrix.Get(0, 1) = (double)(1.0 - cos(Angle)) * Axe[0] * Axe[1] - (double)sin(Angle) * Axe[2];
		Matrix.Get(0, 2) = (double)(1.0 - cos(Angle)) * Axe[0] * Axe[2] + (double)sin(Angle) * Axe[1];

		Matrix.Get(1, 0) = (double)(1.0 - cos(Angle)) * Axe[1] * Axe[0] + (double)sin(Angle) * Axe[2];
		Matrix.Get(1, 1) = Axe[1] * Axe[1] + (double)cos(Angle) * ((double)1.0 - Axe[1] * Axe[1]);
		Matrix.Get(1, 2) = (double)(1.0 - cos(Angle)) * Axe[1] * Axe[2] - (double)sin(Angle) * Axe[0];

		Matrix.Get(2, 0) = (double)(1.0 - cos(Angle)) * Axe[2] * Axe[0] - (double)sin(Angle) * Axe[1];
		Matrix.Get(2, 1) = (double)(1.0 - cos(Angle)) * Axe[2] * Axe[1] + (double)sin(Angle) * Axe[0];
		Matrix.Get(2, 2) = Axe[2] * Axe[2] + (double)cos(Angle) * ((double)1.0 - Axe[2] * Axe[2]);
		return Matrix;
	}

	FMatrixH FMatrixH::MakeTranslationMatrix(const FPoint& Point)
	{
		FMatrixH Matrix;
		Matrix.SetIdentity();
		Matrix.Get(0, 3) = Point.X;
		Matrix.Get(1, 3) = Point.Y;
		Matrix.Get(2, 3) = Point.Z;
		return Matrix;
	}

	FMatrixH FMatrixH::MakeScaleMatrix(double XScale, double YScale, double ZScale)
	{
		FMatrixH Matrix;
		Matrix.SetIdentity();
		Matrix.Get(0, 0) = XScale;
		Matrix.Get(1, 1) = YScale;
		Matrix.Get(2, 2) = ZScale;
		return Matrix;
	}

	FMatrixH FMatrixH::operator*(const FMatrixH& InMatrix) const
	{
		FMatrixH Result;

		for (int32 Index = 0; Index < 4; Index++)
		{
			for (int32 Jndex = 0; Jndex < 4; Jndex++)
			{
				Result.Get(Index, Jndex) = 0;
				for (int32 Kndex = 0; Kndex < 4; Kndex++)
				{
					Result.Get(Index, Jndex) = Result.Get(Index, Jndex) + (Get(Index, Kndex) * InMatrix.Get(Kndex, Jndex));
				}
			}
		}
		return Result;
	}

	FMatrixH FMatrixH::operator+(const FMatrixH& InMatrix) const
	{
		FMatrixH Result;
		for (int32 i = 0; i < 16; i++)
		{
			Result.Matrix[i] = Matrix[i] + InMatrix.Matrix[i];
		}
		return Result;
	}

	FPoint FMatrixH::PointRotation(const FPoint& PointToRotate, const FPoint& Origin) const
	{
		FPoint Result = Origin;
		for (int32 Index = 0; Index < 3; Index++)
		{
			for (int32 Jndex = 0; Jndex < 3; Jndex++)
			{
				Result[Index] += Get(Index, Jndex) * (PointToRotate[Jndex] - Origin[Jndex]);
			}
		}
		return Result;
	}

	FVector FMatrixH::PointRotation(const FVector& PointToRotate, const FVector& Origin) const
	{
		FVector Result = Origin;
		for (int32 Index = 0; Index < 3; Index++)
		{
			for (int32 Jndex = 0; Jndex < 3; Jndex++)
			{
				Result[Index] += Get(Index, Jndex) * (PointToRotate[Jndex] - Origin[Jndex]);
			}
		}
		return Result;
	}

	FPoint2D FMatrixH::PointRotation(const FPoint2D& PointToRotate, const FPoint2D& Origin) const
	{
		FPoint2D Result = Origin;
		for (int32 Index = 0; Index < 2; Index++)
		{
			for (int32 Jndex = 0; Jndex < 2; Jndex++)
			{
				Result[Index] += Get(Index, Jndex) * (PointToRotate[Jndex] - Origin[Jndex]);
			}
		}
		return Result;
	}

	FPoint FMatrixH::Column(int32 Index) const
	{
		return FPoint(Get(0, Index), Get(1, Index), Get(2, Index));
	}

	FPoint FMatrixH::Row(int32 Index) const
	{
		return FPoint(Get(Index, 0), Get(Index, 1), Get(Index, 2));
	}

	void FMatrixH::Inverse()
	{
		InverseMatrixN(Matrix, 4);
	}

	void FMatrixH::Transpose()
	{
		FMatrixH Tmp = *this;
		for (int32 Index = 0; Index < 4; Index++)
		{
			for (int32 Jndex = 0; Jndex < 4; Jndex++)
			{
				Get(Index, Jndex) = Tmp.Get(Jndex, Index);
			}
		}
	}

	void FMatrixH::GetMatrixDouble(double* OutMatrix) const
	{
		memcpy(OutMatrix, Matrix, 16 * sizeof(double));
	}

	void FMatrixH::CrossProduct(const FPoint& vec)
	{
		SetIdentity();
		Get(1, 0) = -vec.Z;
		Get(2, 0) = vec.Y;
		Get(0, 1) = vec.Z;
		Get(2, 1) = -vec.X;
		Get(0, 2) = -vec.Y;
		Get(1, 2) = vec.X;
		Get(3, 3) = 1.0;
	}

	void FMatrixH::Print(EVerboseLevel level) const
	{
		FMessage::Printf(level, TEXT(" - Matrix\n"));
		for (int32 Row = 0; Row < 4; Row++)
		{
			FMessage::Printf(level, TEXT("	- "));
			for (int32 Column = 0; Column < 4; Column++)
			{
				FMessage::Printf(level, TEXT("%f "), Get(Row, Column));
			}
			FMessage::Printf(level, TEXT("\n"));
		}
	}

	void InverseMatrixN(double* Matrice, int32 Rank)
	{
		const double One = 1.0;
		const double Zero = 0.0;

		TArray<double> TempMatrix;
		TempMatrix.Append(Matrice, Rank * Rank);

		double Determinant = One;

		TArray<int32> ColumnToRow;
		ColumnToRow.SetNum(Rank);
		for (int32 Index = 0; Index < Rank; Index++)
		{
			ColumnToRow[Index] = Index;
		}

		for (int32 Column = 0; Column < Rank; ++Column)
		{
			double Pivot = 0;

			int32 Row = Column;
			while (Row < Rank)
			{
				Pivot = TempMatrix[Rank * Row + Column];
				if (!FMath::IsNearlyZero(Pivot))
				{
					break;
				}
				Row++;
			}

			Determinant = Determinant * Pivot;
			if (Row != Column)
			{
				Swap(ColumnToRow[Column], ColumnToRow[Row]);
				for (int32 Index = 0; Index < Rank; Index++)
				{
					Swap(TempMatrix[Row * Rank + Index], TempMatrix[Column * Rank + Index]);
				}
				Determinant = -Determinant;
			}

			double InvPivot = One / Pivot;
			TempMatrix[Column * Rank + Column] = One;

			for (int32 Index = 0; Index < Rank; Index++)
			{
				TempMatrix[Column * Rank + Index] = TempMatrix[Column * Rank + Index] * InvPivot;
			}

			for (Row = 0; Row < Rank; Row++)
			{
				if (Row == Column)
				{
					continue;
				}

				double ValueRC = TempMatrix[Row * Rank + Column];

				TempMatrix[Row * Rank + Column] = Zero;
				for (int32 Index = 0; Index < Rank; Index++)
				{
					TempMatrix[Row * Rank + Index] = TempMatrix[Row * Rank + Index] - ValueRC * TempMatrix[Column * Rank + Index];
				}
			}
		}

		for (int32 Column = 0; Column < Rank; Column++)
		{
			int32 Row = Column;
			while (Column < Rank)
			{
				if (ColumnToRow[Row] == Column)
				{
					break;
				}
				Row++;
			}

			if (Column == Row)
			{
				continue;
			}

			ColumnToRow[Row] = ColumnToRow[Column];
			for (int32 Index = 0; Index < Rank; Index++)
			{
				Swap(TempMatrix[Index * Rank + Column], TempMatrix[Index * Rank + Row]);
			}
		}

		memcpy(Matrice, TempMatrix.GetData(), Rank * Rank * sizeof(double));
	}

	void MatrixProduct(int32 ARowNum, int32 AColumnNum, int32 ResultRank, const double* MatrixA, const double* MatrixB, double* MatrixResult)
	{
		for (int32 RowA = 0; RowA < ARowNum; RowA++)
		{
			for (int32 ColumnB = 0; ColumnB < ResultRank; ColumnB++)
			{
				int32 ResultIndex = RowA * ResultRank + ColumnB;

				MatrixResult[ResultIndex] = 0.0;
				for (int32 k = 0; k < AColumnNum; k++)
				{
					MatrixResult[ResultIndex] = MatrixResult[ResultIndex] + MatrixA[RowA * AColumnNum + k] * MatrixB[k * ResultRank + ColumnB];
				}
			}
		}
	}

	void TransposeMatrix(int32 RowNum, int32 ColumnNum, const double* InMatrix, double* OutMatrix)
	{
		for (int32 Row = 0; Row < RowNum; ++Row)
		{
			for (int32 Column = 0; Column < ColumnNum; ++Column)
			{
				OutMatrix[RowNum * Column + Row] = InMatrix[ColumnNum * Row + Column];
			}
		}
	}
} // namespace CADKernel
