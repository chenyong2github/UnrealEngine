// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

using FPlayerId = FString;

inline FPlayerId ToPlayerId(FString PlayerIdString) { return FPlayerId(PlayerIdString); }

inline FPlayerId ToPlayerId(int32 PlayerIdInteger) { return FString::FromInt(PlayerIdInteger); }

inline int32 PlayerIdToInt(FPlayerId PlayerId) { return FCString::Atoi(*PlayerId); }

static const FPlayerId INVALID_PLAYER_ID = ToPlayerId(FString(TEXT("Invalid Player Id")));