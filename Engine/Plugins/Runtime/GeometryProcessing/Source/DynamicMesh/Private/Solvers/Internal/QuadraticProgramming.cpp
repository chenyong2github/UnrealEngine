// Copyright Epic Games, Inc. All Rights Reserved.

#include "Solvers/Internal/QuadraticProgramming.h"
#include "MatrixSolver.h"

using namespace UE::Geometry;

FQuadraticProgramming::FQuadraticProgramming(const FSparseMatrixD* InMatrixQ, const FColumnVectorD* InVectorF)
:
MatrixQ(InMatrixQ),
VectorF(InVectorF)
{
    checkSlow(InMatrixQ);
}

FQuadraticProgramming::~FQuadraticProgramming()
{

}

bool FQuadraticProgramming::SetFixedConstraints(const TArray<int>* InFixedRowIndices, const FSparseMatrixD* InFixedValues)
{
    if (InFixedRowIndices && InFixedValues && InFixedRowIndices->Num() == InFixedValues->rows())
    {
        if (InFixedRowIndices->Num() <= MatrixQ->rows() && InFixedValues->rows() <= MatrixQ->rows())
        {
            FixedRowIndices = InFixedRowIndices;
            FixedValues = InFixedValues;
            bFixedConstraintsSet = true; 
            
			return true;
        }
    }

    return false;
}

bool FQuadraticProgramming::PreFactorize()
{   
    //
    // Setup the solver depending on which types of constraints are set
    //
    if (bFixedConstraintsSet)
    { 
        /**
         * With fixed constraints only, we can rewrite the original optimization problem 
         *
         *      trace(0.5*X^t*Q*X + X^t*f)
         *      
         * as a sum of sub-problems acting on each column x of X
         * 
         *                | Q_vv Q_vf |   |x_v|                    |f_v|
         *  |x_v x_f|^t * | Q_fv Q_ff | * |x_f|   +  |x_v x_f|^t * |f_f|
         * 
         * where: 
         *      x is a single column of X.
         *      f is a set of all indices (rows) that are fixed to known values.
         *      v is a set of the leftover variables we are optimizing for.
         * 
         * So Q_vf is a submatrix that is a result of slicing the rows of the original matrix Q by the indices in "v",
         * and the columns by the indices in "f".
         * 
         * We can rewrite our constrained optimization problem as an unconstrained optimization 
         * problem where we are optimizing for the x_v only:
         * 
         *      min (0.5*x_v^t*Q_vv*x_v  +  x_v^t * (f_v + Q_vf)) 
         *  
         * Taking the gradient with respect to x_v and setting the result to zero, we get:
         * 
         *      x_v = inverse(Q_vv) * -(f_v + Q_vf * x_f);
         */  
        const int32 NumParameters = static_cast<int32>(MatrixQ->rows()); // parameters are variable plus fixed values
        const int32 NumFixed = FixedRowIndices->Num();
        const int32 NumVariables = NumParameters - NumFixed;

        
        // Compute the row indices of the variable parameters based on the input fixed parameters
        TBitArray<FDefaultBitArrayAllocator> IsParameterVariable(true, NumParameters);
        for (const int FixedRowIdx : *FixedRowIndices)
        {
            IsParameterVariable[FixedRowIdx] = false;
        }
        
		// The variable "v" set
		VariableRowIndices.SetNumUninitialized(NumVariables);
        for (int ParameterRowIdx = 0, VariableRowIdx = 0; ParameterRowIdx < NumParameters; ++ParameterRowIdx)
        {
            if (IsParameterVariable[ParameterRowIdx])
            {
				VariableRowIndices[VariableRowIdx++] = ParameterRowIdx;
            }
        }

        // The Q_vv matrix
        FSparseMatrixD VariablesQ;
		SliceSparseMatrix(*MatrixQ, VariableRowIndices, VariableRowIndices, VariablesQ);

        // Construct a linear solver for a symmetric positive (semi-)definite matrix
        const EMatrixSolverType MatrixSolverType = EMatrixSolverType::FastestPSD;
        const bool bIsSymmetric = true;
        Solver = ContructMatrixSolver(MatrixSolverType);
        Solver->SetUp(VariablesQ, bIsSymmetric);
        if (!ensure(Solver->bSucceeded())) 
        {
            Solver = nullptr;
            return false;
        }

		return true;
    }

    return false;
}

bool FQuadraticProgramming::Solve(FDenseMatrixD& Solution)
{
    if (bFixedConstraintsSet)
    {   
        if (!ensureMsgf(Solver, TEXT("Solver was not setup. Call the SetUp() method first.")))
        {
            return false;
        }

        // The Q_vf matrix
        FSparseMatrixD VariablesFixedQ;
		SliceSparseMatrix(*MatrixQ, VariableRowIndices, *FixedRowIndices, VariablesFixedQ);

		// The f_v column vector 
		FDenseMatrixD VariablesF;
		if (VectorF)
		{ 
			SliceDenseMatrix(*VectorF, VariableRowIndices, VariablesF);
		}
		
        Solution.resize(MatrixQ->rows(), FixedValues->cols());

        // We can iterate over every column of X and solve for each column individually
        for (int ColIdx = 0; ColIdx < Solution.cols(); ++ColIdx) //TODO: Parallelize
        {
            const FColumnVectorD FixedVector = FixedValues->col(ColIdx);

			// The -(f_v + Q_vf * x_f) vector. If the linear coefficients were not specified, skip f_v.
            FColumnVectorD VectorB;
            if (VectorF) 
            {
				VectorB = -1 * (VariablesF + VariablesFixedQ * FixedVector);
            } 
            else 
            {
                VectorB = -1 * VariablesFixedQ * FixedVector;
            }

            FColumnVectorD VariablesVector;
            Solver->Solve(VectorB, VariablesVector);
            if (ensure(Solver->bSucceeded()))
            {   
                // Copy over the fixed and variable values to the solution matrix
                for (int RowIdx = 0; RowIdx < FixedRowIndices->Num(); ++RowIdx)
                {
                    Solution((*FixedRowIndices)[RowIdx], ColIdx) = FixedVector(RowIdx);
                }

                for (int RowIdx = 0; RowIdx < VariableRowIndices.Num(); ++RowIdx)
                {
                    Solution(VariableRowIndices[RowIdx], ColIdx) = VariablesVector(RowIdx);
                }
            }
			else 
			{
				return false;
			}
        }

		return true;
    }

	return false;
}

bool FQuadraticProgramming::SolveWithFixedConstraints(const FSparseMatrixD& MatrixQ, 
                                                      const FColumnVectorD& VectorF, 
                                                      const TArray<int>& FixedRowIndices, 
                                                      const FSparseMatrixD& FixedValues, 
                                                      FDenseMatrixD& Solution)
{
    FQuadraticProgramming QP(&MatrixQ, &VectorF);
    if (!QP.SetFixedConstraints(&FixedRowIndices, &FixedValues))
    {
        return false;
    }
    
    if (!QP.PreFactorize())
    {
        return false;
    }

    return QP.Solve(Solution);
}