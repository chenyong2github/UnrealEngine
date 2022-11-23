// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "Components/PrimitiveComponent.h"

#include "EngineUtils.h"

class UDataflowComponent;
class UPrimitiveComponent;

/** HitProxy with for dataflow actor.
 */
struct HDataflowActor : public HActor
{
	DECLARE_HIT_PROXY(DATAFLOWENGINEPLUGIN_API)

		HDataflowActor(AActor* InActor, const UPrimitiveComponent* InPrimitiveComponent, int32 InSectionIndex, int32 InMaterialIndex)
		: HActor(InActor, InPrimitiveComponent)
	{
		SectionIndex= InSectionIndex;
		MaterialIndex = InMaterialIndex;
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::GrabHandClosed;
	}
};