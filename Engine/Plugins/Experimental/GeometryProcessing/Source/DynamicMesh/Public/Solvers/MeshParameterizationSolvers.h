// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "Solvers/ConstrainedMeshSolver.h"
#include "DynamicMesh3.h"

namespace UE
{
	namespace MeshDeformation
	{
		/**
		 * Create solver for Free-Boundary UV Parameterization for this mesh.
		 * @warning Assumption is that mesh is a single connected component
		 */
		TUniquePtr<UE::Solvers::IConstrainedMeshUVSolver> DYNAMICMESH_API ConstructNaturalConformalParamSolver(const FDynamicMesh3& DynamicMesh);
	}
}

