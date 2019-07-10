// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"



enum class ELaplacianWeightScheme
{
	Uniform,
	Umbrella,
	Valence,
	MeanValue,
	Cotangent,
	ClampedCotangent
};

namespace MeshSmoothingOperators
{
	/**
	*
	* Note: for discussion of implicit / explicit integration of diffusion and biharmonic equations 
	*       see "Implicit Fairing of Irregular Meshes using Diffusion and Curvature Flow" - M Desbrun 99.
	*       although the following suggests an additional source term could be included in the implicit solve for better accuracy.
	*       or "Generalized Surface Flows for Mesh Processing" Eckstein et al. 2007
	*/

	/**
	* This is equivalent to taking a single backward Euler time step of bi-harmonic diffusion
	* where L is the Laplacian (Del^2) , and L^T L is an approximation of the Del^4.
	*
	* dp/dt = - k*k L^T L[p]
	* with
	* weight = 1 / (k * Sqrt[dt] )
	*
	* p^{n+1} + dt * k * k L^TL [p^{n+1}] = p^{n}
	*
	* re-write as
	* L^TL[p^{n+1}] + weight * weight p^{n+1} = weight * weight p^{n}
	
	*
	* The result is returned in the PositionArray
	*/
	void MESHSOLVERUTILITIES_API ComputeSmoothing_BiHarmonic(const ELaplacianWeightScheme WeightingScheme, const FDynamicMesh3& OriginalMesh,
		const double Speed, const double Weight, const int32 NumIterations, TArray<FVector3d>& PositionArray);

	void  MESHSOLVERUTILITIES_API ComputeSmoothing_ImplicitBiHarmonicPCG(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& OriginalMesh,
		const double Speed, const double Weight, const int32 MaxIterations, TArray<FVector3d>& PositionArray);

	/**
	* This is equivalent to forward or backward Euler time steps of the diffusion equation
	*
	* dp/dt = L[p]
	*
	* p^{n+1} = p^{n} + dt L[p^{n}]
	*
	* with dt = Speed / Max(|w_ii|)
	*
	* here w_ii are the diagonal values of L
	*/
	void  MESHSOLVERUTILITIES_API ComputeSmoothing_Diffusion(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& OriginalMesh, bool bForwardEuler,
		const double Speed, double Weight, const int32 NumIterations, TArray<FVector3d>& PositionArray);

}

namespace MeshDeformingOperators
{
	class MESHSOLVERUTILITIES_API IConstrainedMeshOperator
	{
	public:

		virtual ~IConstrainedMeshOperator() {};

		// Add or update a constraint associated with VtxId
		virtual void AddConstraint(const int32 VtxId, const double Weight, const FVector3d& Position, const bool bPostFix) = 0;
		
		// Update or create a constraint position associated with @param VtxId
		// @return true if a constraint weight is associated with @param VtxId
		virtual bool UpdateConstraintPosition(const int32 VtxId, const FVector3d& Position, const bool bPostFix) = 0;
		
		// Update or create a constraint weight associated with @param VtxId
		// @return true if a constraint position is associated with @param VtxId
		virtual bool UpdateConstraintWeight(const int32 VtxId, const double Weight) = 0;
		
		// Clear all constraints (Positions and Weights)
		virtual void ClearConstraints() = 0;
		
		// Clear all Constraint Weights
		virtual void ClearConstraintWeights() = 0;
		
		// Clear all Constraint Positions
		virtual void ClearConstraintPositions() = 0;


		// Test if a non-zero weighted constraint is associated with VtxId 
		virtual bool IsConstrained(const int32 VtxId) const = 0;
		
		// Returns the vertex locations of the deformed mesh. 
		// Note the array may have empty elements as the index matches the Mesh based VtxId.  PositionBuffer[VtxId] = Pos.
		virtual bool Deform(TArray<FVector3d>& PositionBuffer) = 0;
	};

	/**
	*  Solves the linear system for p_vec 
	* 
	*         ( Transpose(L) * L   + (0  0      )  ) p_vec = source_vec + ( 0              )
	*		  (                      (0 lambda^2)  )                      ( lambda^2 c_vec )
	*
	*   where:  L := laplacian for the mesh,
	*           source_vec := Transpose(L)*L mesh_vertex_positions
	*           lambda := weights
	*           c_vec := constrained positions
	*
	* Expected Use:
	*
	*   // Create Deformation Solver from Mesh
	*   TUniquePtr<IConstrainedMeshOperator>  MeshDeformer = ConstructConstrainedMeshDeformer(ELaplacianWeightScheme::ClampedCotangent, DynamicMesh);
	*
	*   // Add constraints.
	*   for..
	*   {
	*   	int32 VtxId = ..; double Weight = ..; FVector3d TargetPos = ..;  bool bPostFix = ...;
	*   	MeshDeformer->AddConstraint(VtxId, Weight, TargetPos, bPostFix);
	*   }
	*
	*   // Solve for new mesh vertex locations
	*   TArray<FVector3d> PositionBuffer;
	*   MeshDeformer->Deform(PositionBuffer);
	*
	*   // Update Mesh? for (int32 VtxId : DynamicMesh.VertexIndices()) DynamicMesh.SetVertex(VtxId, PositionBuffer[VtxId]);
	*   ...
	* 
	*   // Update constraint positions.
	*   for ..
	*   {
	*   	int32 VtxId = ..;  FVector3d TargetPos = ..; bool bPostFix = ...;
	*	    MeshDeformer->UpdateConstraintPosition(VtxId, TargetPos, bPostFix);
	*   }
	*
	*   // Solve for new vertex locations.
	*   MeshDeformer->Deform(PositionBuffer);
	*   // Update Mesh?
	*/
	TUniquePtr<IConstrainedMeshOperator> MESHSOLVERUTILITIES_API ConstructConstrainedMeshDeformer(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& DynamicMesh);

	/**
	*  Solves the linear system for p_vec
	*
	*         ( Transpose(L) * L   + (0  0      )  ) p_vec = ( 0              )
	*		  (                      (0 lambda^2)  )         ( lambda^2 c_vec )
	*
	*   where:  L := laplacian for the mesh,
	*           lambda := weights
	*           c_vec := constrained positions
	*
	* Expected Use: same as the ConstrainedMeshDeformer above.
	*
	*/
	TUniquePtr<IConstrainedMeshOperator> MESHSOLVERUTILITIES_API ConstructConstrainedMeshSmoother(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& DynamicMesh);
}