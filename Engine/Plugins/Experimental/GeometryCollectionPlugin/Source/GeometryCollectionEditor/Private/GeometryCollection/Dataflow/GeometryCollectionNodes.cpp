// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollectionNodes.h"

namespace Dataflow
{
	void GeometryCollectionEngineAssetNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(GetCollectionAssetNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(ExampleCollectionEditNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(SetCollectionAssetNode);
	}
}

