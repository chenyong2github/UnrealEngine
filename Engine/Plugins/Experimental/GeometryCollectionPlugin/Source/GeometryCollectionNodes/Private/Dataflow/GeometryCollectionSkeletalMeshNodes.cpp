// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionSkeletalMeshNodes.h"

#include "Engine/SkeletalMesh.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"


namespace Dataflow
{
	void GeometryCollectionSkeletalMeshNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSkeletalMeshToCollectionDataflowNode);

	}
}

void FSkeletalMeshToCollectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		DataType InCollection = GetInput(&Collection)->GetValue<DataType>(Context, Collection);

		//
		// @todo(dataflow) : Implemention conversion from skeletal mesh to TManagedArrayCollection
		//

		Out->SetValue<DataType>(InCollection, Context);
	}
}


