// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowInputOutput.h"

class UStaticMesh;
class USkeletalMesh;

namespace Dataflow
{
	typedef const UStaticMesh* UStaticMeshPtr;
	typedef const USkeletalMesh* USkeletalMeshPtr;
	//typedef TSharedPtr<FManagedArrayCollection> FManagedArrayCollectionSharedPtr;
	DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWNODES_API, UStaticMeshPtr, StaticMeshPtr);
	DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWNODES_API, USkeletalMeshPtr, SkeletalMeshPtr);
	DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWNODES_API, FString, String);
	DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWNODES_API, FVector3f, Vector3f);
}
