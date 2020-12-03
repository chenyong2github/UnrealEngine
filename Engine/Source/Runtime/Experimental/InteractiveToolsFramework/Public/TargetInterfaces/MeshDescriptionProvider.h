// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "MeshDescriptionProvider.generated.h"

struct FMeshDescription;

UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UMeshDescriptionProvider : public UInterface
{
	GENERATED_BODY()
};

class INTERACTIVETOOLSFRAMEWORK_API IMeshDescriptionProvider
{
	GENERATED_BODY()

public:
	virtual FMeshDescription* GetMeshDescription() = 0;

	//TODO: Should this be providing a shared pointer instead? That seems like generally
	// the better idea, but some refactoring may be necessary to avoid having to copy
	// when getting from a static mesh.
};