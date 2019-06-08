// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/GeometryCollectionExample.h"
#include "GeometryCollection/GeometryCollectionExampleResponse.h"

namespace GeometryCollectionExample
{	
	
	template<class T>
	bool Solver_AdvanceNoObjects(ExampleResponse&& R);

	template<class T>
	bool Solver_AdvanceDisabledObjects(ExampleResponse&& R);

	template<class T>
	bool Solver_AdvanceDisabledClusteredObjects(ExampleResponse&& R);

	template<class T>
	bool Solver_ValidateReverseMapping(ExampleResponse&& R);

	template<class T>
	bool Solver_CollisionEventFilter(ExampleResponse&& R);

	template<class T>
	bool Solver_BreakingEventFilter(ExampleResponse&& R);

}
