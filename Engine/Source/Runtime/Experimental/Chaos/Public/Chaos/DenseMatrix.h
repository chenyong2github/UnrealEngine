// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"
#include "Chaos/Vector.h"
#include "Chaos/Matrix.h"

namespace Chaos
{
	/**
	 * A matrix with run-time variable dimensions, up to an element limit defined at compile-time.
	 *
	 * Elements are stored in row-major order (i.e., elements in a row are adjacent in memory). Note
	 * that FMatrix stores elements in column-major order so that we can access the columns quickly
	 * which is handy when you have rotation matrices and want the spatial axes. We don't care about
	 * that so we use the more conventional row-major indexing and matching storage.
	 */
	template<int32 T_MAXELEMENTS>
	class TDenseMatrix
	{
	public:
		static const int32 MaxElements = T_MAXELEMENTS;

		TDenseMatrix()
			: NRows(0)
			, NCols(0)
		{
		}

		TDenseMatrix(const int32 InNRows, const int32 InNCols)
			: NRows(InNRows)
			, NCols(InNCols)
		{
		}

		TDenseMatrix(const TDenseMatrix<MaxElements>& A)
		{
			*this = A;
		}

		TDenseMatrix<MaxElements>& operator=(const TDenseMatrix<MaxElements>& A)
		{
			SetDimensions(A.NumRows(), A.NumColumns());
			for (int32 Index = 0; Index < NumElements(); ++Index)
			{
				M[Index] = A.M[Index];
			}
			return *this;
		}

		int32 NumRows() const
		{
			return NRows;
		}

		int32 NumColumns() const
		{
			return NCols;
		}

		int32 NumElements() const
		{
			return NRows * NCols;
		}

		/**
		 * Set the dimensions of the matrix. This does not rearrange or set any matrix elements so the matrix should not be used until its values have been set.
		 */
		void SetDimensions(const int32 InNumRows, const int32 InNumColumns)
		{
			check(InNumRows * InNumColumns <= MaxElements);
			NRows = InNumRows;
			NCols = InNumColumns;
		}

		FORCEINLINE int32 ElementIndex(const int32 RowIndex, const int32 ColumnIndex) const
		{
			checkSlow(RowIndex < NumRows());
			checkSlow(ColumnIndex < NumColumns());
			return RowIndex * NCols + ColumnIndex;
		}

		/**
		 * Return a writable reference to the element at the specified row and column.
		 */
		FReal& At(const int32 RowIndex, const int32 ColumnIndex)
		{
			return M[ElementIndex(RowIndex, ColumnIndex)];
		}

		/**
		 * Return a read-only reference to the element at the specified row and column.
		 */
		const FReal& At(const int32 RowIndex, const int32 ColumnIndex) const
		{
			return M[ElementIndex(RowIndex, ColumnIndex)];
		}

		/**
		 * Set all elements to 'V'.
		 */
		void SetAll(FReal V)
		{
			for (int32 II = 0; II < NumElements(); ++II)
			{
				M[II] = V;
			}
		}

		/**
		 * Set the diagonal elements to 'V'. Does not set off-diagonal elements.
		 * /see MakeDiagonal
		 */
		void SetDiagonal(FReal V)
		{
			int32 Num = FMath::Min(NRows, NCols);
			for (int32 II = 0; II < Num; ++II)
			{
				M[ElementIndex(II, II)] = V;
			}
		}


		//
		// Factory methods
		//

		static TDenseMatrix<MaxElements> Make(const int32 InNumRows, const int32 InNumCols)
		{
			return TDenseMatrix<MaxElements>(InNumRows, InNumCols);
		}


		static TDenseMatrix<MaxElements> Make(const int32 InNumRows, const int32 InNumCols, const FReal* V, const int32 VLen)
		{
			return TDenseMatrix<MaxElements>(InNumRows, InNumCols, V, VLen);
		}

		static TDenseMatrix<MaxElements> Make(const int32 InNumRows, const int32 InNumCols, std::initializer_list<FReal> InitList)
		{
			return TDenseMatrix<MaxElements>(InNumRows, InNumCols, InitList);
		}

		static TDenseMatrix<MaxElements> Make(const FMatrix33& InM)
		{
			// NOTE: UE matrices are Column-major (columns are sequential in memory), but DenseMatrix is Row-major (rows are sequential in memory)
			TDenseMatrix<MaxElements> M(3, 3);
			M.At(0, 0) = InM.M[0][0];
			M.At(0, 1) = InM.M[1][0];
			M.At(0, 2) = InM.M[2][0];
			M.At(1, 0) = InM.M[0][1];
			M.At(1, 1) = InM.M[1][1];
			M.At(1, 2) = InM.M[2][1];
			M.At(2, 0) = InM.M[0][2];
			M.At(2, 1) = InM.M[1][2];
			M.At(2, 2) = InM.M[2][2];
			return M;
		}

		static TDenseMatrix<MaxElements> MakeDiagonal(const int32 InNumRows, const int32 InNumCols, const FReal D)
		{
			TDenseMatrix<MaxElements> M(InNumRows, InNumCols);
			for (int32 I = 0; I < InNumRows; ++I)
			{
				for (int32 J = 0; J < InNumCols; ++J)
				{
					M.At(I, J) = (I == J) ? D : 0;
				}
			}
			return M;
		}

		static TDenseMatrix<MaxElements> MakeIdentity(const int32 InDim)
		{
			return MakeDiagonal(InDim, InDim, (FReal)1);
		}

		//
		// Math operations
		//

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> Negative(const TDenseMatrix<T_EA>& A)
		{
			// @todo(ccaulfield): optimize
			TDenseMatrix<T_MAXELEMENTS> Result(A.NumRows(), A.NumColumns());
			for (int32 IRow = 0; IRow < Result.NumRows(); ++IRow)
			{
				for (int32 ICol = 0; ICol < Result.NumColumns(); ++ICol)
				{
					Result.At(IRow, ICol) = -A.At(IRow, ICol);
				}
			}
			return Result;
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> Add(const TDenseMatrix<T_EA>& A, const TDenseMatrix<T_EB>& B)
		{
			// @todo(ccaulfield): optimize
			check(A.NumColumns() == B.NumColumns());
			check(A.NumRows() == B.NumRows());
			TDenseMatrix<T_MAXELEMENTS> Result(A.NumRows(), A.NumColumns());
			for (int32 IRow = 0; IRow < Result.NumRows(); ++IRow)
			{
				for (int32 ICol = 0; ICol < Result.NumColumns(); ++ICol)
				{
					Result.At(IRow, ICol) = A.At(IRow, ICol) + B.At(IRow, ICol);
				}
			}
			return Result;
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> Subtract(const TDenseMatrix<T_EA>& A, const TDenseMatrix<T_EB>& B)
		{
			// @todo(ccaulfield): optimize
			check(A.NumColumns() == B.NumColumns());
			check(A.NumRows() == B.NumRows());
			TDenseMatrix<T_MAXELEMENTS> Result(A.NumRows(), A.NumColumns());
			for (int32 IRow = 0; IRow < Result.NumRows(); ++IRow)
			{
				for (int32 ICol = 0; ICol < Result.NumColumns(); ++ICol)
				{
					Result.At(IRow, ICol) = A.At(IRow, ICol) - B.At(IRow, ICol);
				}
			}
			return Result;
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> MultiplyAB(const TDenseMatrix<T_EA>& A, const TDenseMatrix<T_EB>& B)
		{
			// @todo(ccaulfield): optimize
			check(A.NumColumns() == B.NumRows());
			TDenseMatrix<T_MAXELEMENTS> Result(A.NumRows(), B.NumColumns());
			for (int32 IRow = 0; IRow < Result.NumRows(); ++IRow)
			{
				for (int32 ICol = 0; ICol < Result.NumColumns(); ++ICol)
				{
					FReal V = 0;
					for (int32 II = 0; II < A.NumColumns(); ++II)
					{
						V += A.At(IRow, II) * B.At(II, ICol);
					}
					Result.At(IRow, ICol) = V;
				}
			}
			return Result;
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> MultiplyAtB(const TDenseMatrix<T_EA>& A, const TDenseMatrix<T_EB>& B)
		{
			// @todo(ccaulfield): optimize
			check(A.NumRows() == B.NumRows());
			TDenseMatrix<T_MAXELEMENTS> Result(A.NumColumns(), B.NumColumns());
			for (int32 IRow = 0; IRow < Result.NumRows(); ++IRow)
			{
				for (int32 ICol = 0; ICol < Result.NumColumns(); ++ICol)
				{
					FReal V = 0;
					for (int32 II = 0; II < A.NumRows(); ++II)
					{
						V += A.At(II, IRow) * B.At(II, ICol);
					}
					Result.At(IRow, ICol) = V;
				}
			}
			return Result;
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> MultiplyABt(const TDenseMatrix<T_EA>& A, const TDenseMatrix<T_EB>& B)
		{
			// @todo(ccaulfield): optimize
			check(A.NumColumns() == B.NumColumns());
			TDenseMatrix<T_MAXELEMENTS> Result(A.NumRows(), B.NumRows());
			for (int32 IRow = 0; IRow < Result.NumRows(); ++IRow)
			{
				for (int32 ICol = 0; ICol < Result.NumColumns(); ++ICol)
				{
					FReal V = 0;
					for (int32 II = 0; II < A.NumColumns(); ++II)
					{
						V += A.At(IRow, II) * B.At(ICol, II);
					}
					Result.At(IRow, ICol) = V;
				}
			}
			return Result;
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> MultiplyAtBt(const TDenseMatrix<T_EA>& A, const TDenseMatrix<T_EB>& B)
		{
			// @todo(ccaulfield): optimize
			check(A.NumRows() == B.NumColumns());
			TDenseMatrix<T_MAXELEMENTS> Result(A.NumColumns(), B.NumRows());
			for (int32 IRow = 0; IRow < Result.NumRows(); ++IRow)
			{
				for (int32 ICol = 0; ICol < Result.NumColumns(); ++ICol)
				{
					FReal V = 0;
					for (int32 II = 0; II < A.NumRows(); ++II)
					{
						V += A.At(II, IRow) * B.At(ICol, II);
					}
					Result.At(IRow, ICol) = V;
				}
			}
			return Result;
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> Multiply(const TDenseMatrix<T_EA>& A, const FReal V)
		{
			// @todo(ccaulfield): optimize
			TDenseMatrix<T_MAXELEMENTS> Result(A.NumRows(), A.NumColumns());
			for (int32 IRow = 0; IRow < Result.NumRows(); ++IRow)
			{
				for (int32 ICol = 0; ICol < Result.NumColumns(); ++ICol)
				{
					Result.At(IRow, ICol) = Result.At(IRow, ICol) / V;
				}
			}
			return Result;
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> Multiply(const FReal V, const TDenseMatrix<T_EA>& A)
		{
			return Multiply(A, V);
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> Divide(const TDenseMatrix<T_EA>& A, const FReal V)
		{
			// @todo(ccaulfield): optimize
			TDenseMatrix<T_MAXELEMENTS> Result(A.NumRows(), A.NumColumns());
			for (int32 IRow = 0; IRow < Result.NumRows(); ++IRow)
			{
				for (int32 ICol = 0; ICol < Result.NumColumns(); ++ICol)
				{
					Result.At(IRow, ICol) = Result.At(IRow, ICol) / V;
				}
			}
			return Result;
		}

		template<int32 T_EA, int32 T_EB>
		static TDenseMatrix<MaxElements> DotProduct(const TDenseMatrix<T_EA>& A, const TDenseMatrix<T_EA>& B)
		{
			return MultiplyAtB(A, B);
		}

	private:
		TDenseMatrix(const int32 InNRows, const int32 InNCols, const FReal* V, const int32 N)
			: NRows(InNRows)
			, NCols(InNCols)
		{
			int32 NLimited = FMath::Min<int32>(NumElements(), N);
			for (int32 I = 0; I < NLimited; ++I)
			{
				M[I] = V[I];
			}
		}

		FReal M[MaxElements];
		int32 NRows;
		int32 NCols;
	};

	/**
	 * Methods to solves sets of Linear equations stored as
	 *	AX = B
	 * where A is an NxN matrix, and X.B are Nx1 column vectors.
	 */
	class FDenseMatrixSolver
	{
	public:

		/**
		 * Overwrite A with its Cholesky Factor (A must be Positive Definite).
		 * See "Matrix Computations, 4th Edition" Section 4.2, Golub & Van Loan.
		 *
		 * The Cholesky Factor of A is G (Gt its transpose), where A = GGt. G is lower triangular.
		 */
		template<int32 T_E>
		static bool CholeskyFactorize(TDenseMatrix<T_E>& A)
		{
			check(A.NumRows() == A.NumColumns());
			const int32 N = A.NumRows();
			for (int32 I = 0; I < N; ++I)
			{
				for (int32 J = I; J < N; ++J)
				{
					FReal Sum = A.At(I, J);
					for (int32 K = I - 1; K >= 0; --K)
					{
						Sum -= A.At(I, K) * A.At(J, K);
					}
					if (I == J)
					{
						if (Sum <= 0)
						{
							// Not positive definite (rounding?)
							return false;
						}
						A.At(I, J) = FMath::Sqrt(Sum);
					}
					else
					{
						A.At(J, I) = Sum / A.At(I, I);
					}
				}
			}

			for (int32 I = 0; I < N; ++I)
			{
				for (int32 J = 0; J < I; ++J)
				{
					A.At(J, I) = 0;
				}
			}

			return true;
		}

		/**
		 * This solves AX = B, where A is positive definite and has been Cholesky Factorized to produce G, 
		 * where A = GGt, G is lower triangular.
		 *
		 * This is a helper method for SolvePositiveDefinite, or useful if you need to reuse the 
		 * Cholesky Factor and therefore calculated it yourself.
		 *
		 * \see SolvePositiveDefinite
		 */
		template<int32 T_EA, int32 T_EB, int32 T_EX>
		static void SolveCholeskyFactorized(const TDenseMatrix<T_EA>& G, const TDenseMatrix<T_EB>& B, TDenseMatrix<T_EX>& X)
		{
			check(B.NumColumns() == 1);
			check(G.NumRows() == B.NumRows());

			const int32 N = G.NumRows();
			X.SetDimensions(N, 1);

			// Solve LY = B (G is lower-triangular)
			for (int32 I = 0; I < N; ++I)
			{
				FReal Sum = B.At(I, 0);
				for (int32 K = I - 1; K >= 0; --K)
				{
					Sum -= G.At(I, K) * X.At(K, 0);
				}
				X.At(I, 0) = Sum / G.At(I, I);
			}

			// Solve LtX = Y (Lt is upper-triangular)
			for (int32 I = N - 1; I >= 0; --I)
			{
				FReal Sum = X.At(I, 0);
				for (int32 K = I + 1; K < N; ++K)
				{
					Sum -= G.At(K, I) * X.At(K, 0);
				}
				X.At(I, 0) = Sum / G.At(I, I);
			}
		}

		/**
		 * Solve AX = B, for positive-definite NxN matrix A, and Nx1 column vectors B and X.
		 *
		 * For positive definite A, A = GGt, where G is the Cholesky factor and lower trangular.
		 * We can solve GGtX = B by first solving GY = B, and then GtX = Y.
		 *
		 * E.g., this can be used to solve constraint equations of the form
		 *		J.I.Jt.X = B
		 * where J is a Jacobian (Jt its transpose), I is an Inverse mas matrix, and B the residual.
		 * In this case, I is positive definite, and therefore so is JIJt.
		 *
		 */
		template<int32 T_EA, int32 T_EB, int32 T_EX>
		static bool SolvePositiveDefinite(const TDenseMatrix<T_EA>& A, const TDenseMatrix<T_EB>& B, TDenseMatrix<T_EX>& X)
		{
			check(B.NumColumns() == 1);
			check(A.NumRows() == B.NumRows());

			TDenseMatrix<T_EA> G = A;
			if (!CholeskyFactorize(G))
			{
				// Not positive definite
				return false;
			}

			SolveCholeskyFactorized(G, B, X);
			return true;
		}
	};

}
