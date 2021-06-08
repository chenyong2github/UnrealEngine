// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StructOnScope.h"

struct FRemoteControlProtocolEntity;

/**
 * Interface class for RCProtocolBindingList widget
 */
class IRCProtocolBindingList
{
public:
	using FRemoteControlProtocolEntitySet = TSet<TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>>>;

	/** Virtual destructor */
	virtual ~IRCProtocolBindingList() = default;

	/** Get the set of entities which is awaiting state and waiting for binding */
	virtual FRemoteControlProtocolEntitySet GetAwaitingProtocolEntities() const = 0;
};
