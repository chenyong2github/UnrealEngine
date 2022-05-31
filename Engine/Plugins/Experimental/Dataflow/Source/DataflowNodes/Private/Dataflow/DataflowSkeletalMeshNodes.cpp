// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSkeletalMeshNodes.h"
#include "Dataflow/DataflowCore.h"

namespace Dataflow
{
	void RegisterSkeletalMeshNodes()
	{
		//DATAFLOW_NODE_REGISTER_CREATION_FACTORY(SkeletalMesh);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(SkeletalMeshBone);
	}
}

