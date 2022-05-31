// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowInputOutput.h"


namespace Dataflow
{
	//typedef TSharedPtr<FManagedArrayCollection> FManagedArrayCollectionSharedPtr;
	DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWNODES_API, FString, String)
	DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWNODES_API, FVector3f, Vector3f)
}
