// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Containers/ContainersFwd.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "Templates/UniquePtr.h"

struct FGeometryAddParams;

namespace ChaosInterface
{
	/**
	 * Create the Chaos Geometry based on the geometry parameters.
	 */
	void CreateGeometry(const FGeometryAddParams& InParams, TArray<TUniquePtr<Chaos::TImplicitObject<float, 3>>>& OutGeoms, TArray<TUniquePtr<Chaos::TPerShapeData<float, 3>>, TInlineAllocator<1>>& OutShapes);
}
