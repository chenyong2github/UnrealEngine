// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/GeometryCollectionExample.h"
#include "GeometryCollection/GeometryCollectionExampleResponse.h"

namespace GeometryCollectionExample
{	
	template<class T>
	bool RigidBodies_Streaming_StartSolverEmpty(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Streaming_BulkInitialization(ExampleResponse&& R);
	
	template<class T>
	bool RigidBodies_Streaming_DeferedClusteringInitialization(ExampleResponse&& R);

}
