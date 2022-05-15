// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ManagedArrayCollectionNodes.h"

#include "Dataflow/DataflowConnectionTypes.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNodeFactory.h"

#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/ManagedArrayCollection.h"


namespace Dataflow
{
	
void RegisterManagedArrayCollectionNodes()
{
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FNewManagedArrayCollectionNode);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FAddAttributeNode);
}

}

