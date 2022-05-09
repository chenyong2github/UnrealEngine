// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Net/Core/Misc/NetConditionGroupManager.h"

#include "NetworkSubsystem.generated.h"


UCLASS()
class ENGINE_API UNetworkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:

	/** Access the NetConditionGroupManager */
	UE::Net::FNetConditionGroupManager& GetNetConditionGroupManager() { return GroupsManager;  }

private:

	virtual void Serialize(FArchive& Ar) override;

	/** Manage the of the subobjects and their relationships to different groups */
	UE::Net::FNetConditionGroupManager GroupsManager;
};
