// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dataflow/DataflowInputOutput.h"

#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/ManagedArrayCollection.h"

namespace Dataflow
{
	typedef TSharedPtr<FManagedArrayCollection> FManagedArrayCollectionSharedPtr;

	DATAFLOW_CONNECTION_TYPE(DATAFLOWNODES_API, FManagedArrayCollectionSharedPtr, ManagedArrayCollectionSharedPtr)
}
