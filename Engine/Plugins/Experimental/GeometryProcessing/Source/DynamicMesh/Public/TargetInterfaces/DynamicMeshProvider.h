// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "GeometryBase.h"

#include "DynamicMeshProvider.generated.h"

PREDECLARE_GEOMETRY(class FDynamicMesh3);

UINTERFACE()
class DYNAMICMESH_API UDynamicMeshProvider : public UInterface
{
	GENERATED_BODY()
};

class DYNAMICMESH_API IDynamicMeshProvider
{
	GENERATED_BODY()

public:
	/**
	 * Gives back an editable dynamic mesh. Changes may or may not affect the target, but
	 * target guarantees that they are safe to perform without crashing. In particular,
	 * note that it's unspecified whether the next GetMesh() call after an edit will give
	 * back the edited mesh or a copy of the original.
	 * 
	 * To guarantee changes having an effect, target must also be a IDynamicMeshCommitter 
	 * and the mesh must be committed.
	 */
	virtual TSharedPtr<UE::Geometry::FDynamicMesh3> GetDynamicMesh() = 0;
};
