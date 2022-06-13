// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowStaticMeshNodes.h"
#include "Engine/StaticMesh.h"

namespace Dataflow
{
	void RegisterStaticMeshNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetStaticMeshDataflowNode);
	}
}

void FGetStaticMeshDataflowNode::Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const
{
	StaticMeshOut.SetValue(nullptr, Context);
	if (StaticMesh)
	{
		StaticMeshOut.SetValue(StaticMesh, Context);
	}
	else if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
	{
		if (const UStaticMesh* StaticMeshFromOwner = Dataflow::Reflection::FindObjectPtrProperty<UStaticMesh>(
			EngineContext->Owner, PropertyName))
		{
			StaticMeshOut.SetValue(StaticMeshFromOwner, Context);
		}
	}
}

