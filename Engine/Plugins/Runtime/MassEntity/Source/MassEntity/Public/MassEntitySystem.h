// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySubsystem.h"
#include "MassEntitySystem.generated.h"


class UWorld;

UCLASS()
class MASSENTITY_API UPipeEntitySubsystem : public UEntitySubsystem
{
	GENERATED_BODY()
public:
	/** we never create PipeEntitySubsystem as a regular WorldSubsystem. Created on demand.
	 *  This functionality might need to be moved down to Mass plugin.
	 *  
	 *  @todo this is a bit hacky since makes UPipeEntitySubsystem unobtainable via regular World::GetSubsystem which 
	 *  might be confusing to users. Ideally we'd extract UEntitySubsystem's functionality into a separate class 
	 *  and make both UEntitySubsystem and UPipeEntitySubsystem utilize it.
	 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return false; }

	static UPipeEntitySubsystem* GetCurrent(UWorld* World) { return InstanceGetter(World); }

	/** This function is meant to be called once per project, make sure you know all code sites that call it to avoid surprises */
	static void SetGetter_Internal(TFunctionRef<UPipeEntitySubsystem*(UWorld*)> InGetter) { new (&InstanceGetter)TFunctionRef<UPipeEntitySubsystem*(UWorld*)>(InGetter); }

private:
	static TFunctionRef<UPipeEntitySubsystem*(UWorld*)> InstanceGetter;
};