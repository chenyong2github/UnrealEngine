// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Mesh/Structure/Mesh.h"

namespace CADKernel
{
	class FModelMesh;

	class CADKERNEL_API FVertexMesh : public FMesh
	{
	protected:

	public:
		FVertexMesh(TSharedRef<FModelMesh> Model, TSharedRef<FTopologicalEntity> TopologicalEntity)
			: FMesh(Model, TopologicalEntity)
		{
		}

		int32 GetMesh() const
		{
			return StartNodeId;
		}
	};
}

