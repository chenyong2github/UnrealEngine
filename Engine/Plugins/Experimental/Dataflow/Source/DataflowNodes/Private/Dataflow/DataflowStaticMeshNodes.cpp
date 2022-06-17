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

void FGetStaticMeshDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const UStaticMesh> DataType;
	if (Out->IsA<DataType>(&StaticMesh))
	{
		GetOutput(&StaticMesh)->SetValue<DataType>(StaticMesh, Context); // prime to avoid ensure

		if (StaticMesh)
		{
			GetOutput(&StaticMesh)->SetValue<DataType>(StaticMesh, Context);
		}
		else if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
		{
			if (const UStaticMesh* StaticMeshFromOwner = Dataflow::Reflection::FindObjectPtrProperty<UStaticMesh>(
				EngineContext->Owner, PropertyName))
			{
				GetOutput(&StaticMesh)->SetValue<DataType>(DataType(StaticMeshFromOwner), Context);
			}
		}
	}
}


