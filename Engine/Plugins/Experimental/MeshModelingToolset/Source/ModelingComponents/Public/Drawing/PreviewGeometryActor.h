// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolObjects.h"

#include "PreviewGeometryActor.generated.h"

/**
 * An actor suitable for attaching components used to draw preview elements, such as LineSetComponent and TriangleSetComponent.
 */
UCLASS()
class MODELINGCOMPONENTS_API APreviewGeometryActor : public AInternalToolFrameworkActor
{
	GENERATED_BODY()
private:
	APreviewGeometryActor()
	{
#if WITH_EDITORONLY_DATA
		// hide this actor in the scene outliner
		bListedInSceneOutliner = false;
#endif
	}

public:
};