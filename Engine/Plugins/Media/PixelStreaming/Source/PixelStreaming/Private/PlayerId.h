// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

using FPlayerId = FString;
inline FPlayerId ToPlayerId(FString PlayerIdString){ return FPlayerId(PlayerIdString); }
inline FPlayerId ToPlayerId(uint32 PlayerIdUnsignedInteger){ return FString::FromInt(PlayerIdUnsignedInteger); }
inline int32 PlayerIdToInt(FPlayerId PlayerId) { return FCString::Atoi(*PlayerId); }