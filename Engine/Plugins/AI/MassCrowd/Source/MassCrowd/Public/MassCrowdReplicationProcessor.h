// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassReplicationProcessorBase.h"

#include "MassCrowdReplicationProcessor.generated.h"

class UMassReplicationSubsystem;
class MassLODManager;
class AMassCrowdClientBubbleInfo;
class UWorld;

/** Processor that handles replication and only runs on the server. It queries Mass entity fragments and sets those values when appropriate using the MassClientBubbleHandler. */
UCLASS()
class MASSCROWD_API UMassCrowdReplicationProcessor : public UMassReplicationProcessorBase
{
	GENERATED_BODY()
public:
	UMassCrowdReplicationProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	void ProcessClientReplication(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context);
};