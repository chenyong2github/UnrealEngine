// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FConcertClientInfo;

DECLARE_DELEGATE_RetVal_OneParam(TOptional<FConcertClientInfo>, FGetClientInfo, const FGuid& /*EndpointId*/);