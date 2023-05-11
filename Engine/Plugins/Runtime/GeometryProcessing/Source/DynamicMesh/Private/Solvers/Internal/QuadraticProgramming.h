// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "SparseMatrix.h"
#include "DenseMatrix.h"
#include "MatrixBase.h"

namespace UE
{
namespace Geometry
{
/**
 * This class provides functionality to solve quadratic programming problems.
 * 
 * The goal is to minimize the function:
 *
 *      trace(0.5*X^t*Q*X + X^t*f)
 *
 *      where:
 *          Q \in R^(n x n) is a symmetric positive (semi-)definite matrix of quadratic coefficients.
 *          X \in R^(n x m) is a matrix of parameters we optimize for.
 *          f \in R^(n x 1) is a column vector of linear coefficients.
 *
 *      subject to:
 *
 *          Fixed Constraints: 
 *              X[FixedRowIndices, :] = FixedValues
 *                  where FixedRowIndices \in Z+^(k x 1), FixedValues \in R^(k x m) and k is the number of fixed rows.
 *
 *          TODO:
 *          Linear Equality Constraints:
 *          Linear Inequality Constraints:
 * 
 * Example usage:
 *      - The solver is initialized with the quadratic Q and linear coefficients f:
 *          FSparseMatrixD Q = ...
 *          FColumnVectorD f = ...
 *          FQuadraticProgramming QP(&Q, &f);
 * 
 *      - Then constraints are set by calling Set[ConstraintType]Constraints methods (only the fixed constraints are supported at the moment).
 *          TArray<int> FixedIndices = ...
 *          FSparseMatrix FixedValues = ...
 *          QP.SetFixedConstraints(&FixedIndices, &FixedValues);
 * 
 *      - Then PreFactorize() method needs to be called to pre-factorize the matrices and to set up the internal linear solver. 
 *           QP.PreFactorize(Solution);
 * 
 *      - Finally call Solve():
 *          FDenseMatrixD Solution;
 *          QP.Solve(Solution);
 * 
 *       Calling the PreFactorize() method can be skipped if: 
 *          Fixed constraints:
 *              If only the values of the fixed constraints changed from the last time the PreFactorize() was called.
 *          
 *          TODO:
 *          Linear Equality Constraints:
 *          Linear Inequality Constraints:
 */

class DYNAMICMESH_API FQuadraticProgramming
{
public:

    FQuadraticProgramming(const FSparseMatrixD* InMatrixQ, const FColumnVectorD* InVectorF = nullptr);

    bool SetFixedConstraints(const TArray<int>* InFixedRowIndices, const FSparseMatrixD* InFixedValues);

    /**  Pre-factorizes the matrices and sets up the solver. */
    bool PreFactorize();

    //TODO: Allow to solve for sparse matrices with a passed float threshold value that determines if the value is close enough to 0 
    bool Solve(FDenseMatrixD& Solution);


    // 
    // Helper "one-function call" methods for setting up and solving the QP problems
    //
    static bool SolveWithFixedConstraints(const FSparseMatrixD& MatrixQ, const FColumnVectorD& VectorF, const TArray<int>& FixedRowIndices, const FSparseMatrixD& FixedValues, FDenseMatrixD& Solution);

protected:

    const FSparseMatrixD* MatrixQ = nullptr; // Matrix of quadratic coefficient
    const FColumnVectorD* VectorF = nullptr; // Column vector of linear coefficients

    // Fixed constraints
    bool bFixedConstraintsSet = false;  // set to true if SetFixedConstraints is called and successful
    const TArray<int>* FixedRowIndices = nullptr;  // row indices of the fixed parameters, set by the user when calling SetFixedConstraints
    const FSparseMatrixD* FixedValues = nullptr;   // matrix of fixed values, set by the user when calling SetFixedConstraints
    TArray<int> VariableRowIndices;  // row indices of the variable parameters, computed internally by the call to PreFactorize()

    TUniquePtr<IMatrixSolverBase> Solver = nullptr; // PreFactorize() method pre-factorizes the matrices and setups the solver
};

}
}
