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
struct HDataflowNode : public HActor
{
	DECLARE_HIT_PROXY(DATAFLOWENGINEPLUGIN_API)
	int32 GeometryIndex = INDEX_NONE;
	FString NodeName = FString("");

	HDataflowNode(AActor* InActor, const UPrimitiveComponent* InPrimitiveComponent, FString InNodeName, int32 InGeometryIndex)
		: HActor(InActor, InPrimitiveComponent)
	{
		NodeName = InNodeName;
		GeometryIndex = InGeometryIndex;
		SectionIndex = GeometryIndex;
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Default;
	}
};