// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/GeometryCollectionExample.h"
#include "GeometryCollection/GeometryCollectionExampleResponse.h"

namespace GeometryCollectionExample
{	
	
	template<class T>
	void Solver_AdvanceNoObjects();

	template<class T>
	void Solver_AdvanceDisabledObjects();

	template<class T>
	void Solver_AdvanceDisabledClusteredObjects();

	template<class T>
	void Solver_ValidateReverseMapping();

	template<class T>
	void Solver_CollisionEventFilter();

	template<class T>
	void Solver_BreakingEventFilter();

}
