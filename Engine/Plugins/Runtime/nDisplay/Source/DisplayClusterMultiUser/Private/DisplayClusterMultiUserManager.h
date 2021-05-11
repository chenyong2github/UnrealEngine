// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertTransactionEvents.h"


/**
 * Manager for handling multi user transactions in nDisplay.
 */
class FDisplayClusterMultiUserManager
{
public:
	FDisplayClusterMultiUserManager();
	virtual ~FDisplayClusterMultiUserManager();

private:
	ETransactionFilterResult ShouldObjectBeTransacted(UObject* InObject, UPackage* InPackage);
};
