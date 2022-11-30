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
struct HDataflowDefault : public HActor
{
	DECLARE_HIT_PROXY(DATAFLOWENGINEPLUGIN_API)
	HDataflowDefault(AActor* InActor, const UPrimitiveComponent* InPrimitiveComponent)
		: HActor(InActor, InPrimitiveComponent){}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Default;
	}
};


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


/** HitProxy with for dataflow actor.
 */
struct HDataflowVertex : public HActor
{
	DECLARE_HIT_PROXY(DATAFLOWENGINEPLUGIN_API)

	HDataflowVertex(AActor* InActor, const UPrimitiveComponent* InPrimitiveComponent, int32 InVertexIndex)
		: HActor(InActor, InPrimitiveComponent)
	{
		SectionIndex = InVertexIndex;
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}
};