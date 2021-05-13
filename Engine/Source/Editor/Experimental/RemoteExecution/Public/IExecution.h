// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Messages.h"


class IExecution
{
public:
	/** Virtual destructor */
	virtual ~IExecution() {}

	virtual bool Execute(const FExecuteRequest& Request, FExecuteResponse& Response) = 0;
};
