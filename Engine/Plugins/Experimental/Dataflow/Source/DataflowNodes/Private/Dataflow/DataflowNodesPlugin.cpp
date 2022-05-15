// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNodesPlugin.h"

#include "CoreMinimal.h"

#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/ManagedArrayCollectionNodes.h"


#define LOCTEXT_NAMESPACE "DataflowNodes"


void IDataflowNodesPlugin::StartupModule()
{
	Dataflow::RegisterManagedArrayCollectionNodes();
}

void IDataflowNodesPlugin::ShutdownModule()
{
	if (UObjectInitialized())
	{
		//Dataflow::UnregisterManagedArrayCollectionNodes(); ?
	}
}


IMPLEMENT_MODULE(IDataflowNodesPlugin, DataflowNodes)


#undef LOCTEXT_NAMESPACE
