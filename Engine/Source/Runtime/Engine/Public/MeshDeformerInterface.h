// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "MeshDeformerInterface.generated.h"

class UMeshDeformerInstance;

UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class ENGINE_API UMeshDeformerInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

/* This is interface class for getting/setting deformer instance
 *
 * Implemented by SkeletalMeshComponent, GroomComponent
 */

class ENGINE_API IMeshDeformerInterface
{
	GENERATED_IINTERFACE_BODY()

	/** Get the currently active MeshDeformer Instance. */
	virtual UMeshDeformerInstance const* GetMeshDeformerInstance() const = 0;
};

