// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshNodesPlugin.h"

#include "CoreMinimal.h"

#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/ChaosFleshBindingsNodes.h"
#include "Dataflow/ChaosFleshCoreNodes.h"
#include "Dataflow/ChaosFleshEngineAssetNodes.h"
#include "Dataflow/ChaosFleshFiberDirectionInitializationNodes.h"
#include "Dataflow/ChaosFleshKinematicInitializationNodes.h"
#include "Dataflow/ChaosFleshRenderInitializationNodes.h"
#include "Dataflow/ChaosFleshTetrahedralNodes.h"


#define LOCTEXT_NAMESPACE "ChaosFleshNodes"


void IChaosFleshNodesPlugin::StartupModule()
{
	Dataflow::RegisterChaosFleshEngineAssetNodes();
	Dataflow::RegisterChaosFleshCoreNodes();
	Dataflow::ChaosFleshFiberDirectionInitializationNodes();
	Dataflow::ChaosFleshRenderInitializationNodes();
	Dataflow::RegisterChaosFleshKinematicInitializationNodes();
	Dataflow::ChaosFleshTetrahedralNodes();
	Dataflow::ChaosFleshBindingsNodes();
}

void IChaosFleshNodesPlugin::ShutdownModule()
{
}


IMPLEMENT_MODULE(IChaosFleshNodesPlugin, ChaosFleshNodes)


#undef LOCTEXT_NAMESPACE
