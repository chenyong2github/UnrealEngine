// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractiveToolObjects.generated.h"


/**
 * AInternalToolFrameworkActor is a base class for internal Actors that the
 * ToolsFramework may spawn (for gizmos, mesh previews, etc). These Actors
 * may need special-case handling, for example to prevent the user from
 * selecting and deleting them. 
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API AInternalToolFrameworkActor : public AActor
{
	GENERATED_BODY()
private:

public:

};


