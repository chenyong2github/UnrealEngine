// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlInterceptionCommands.h"
#include "IRemoteControlModule.h"


// Internal helper for ERCIAccess->ERCAccess conversion
constexpr ERCAccess ToInternal(ERCIAccess Value)
{
	switch (Value)
	{
	case ERCIAccess::NO_ACCESS:
		return ERCAccess::NO_ACCESS;

	case ERCIAccess::READ_ACCESS:
		return ERCAccess::READ_ACCESS;

	case ERCIAccess::WRITE_ACCESS:
		return ERCAccess::WRITE_ACCESS;

	case ERCIAccess::WRITE_TRANSACTION_ACCESS:
		return ERCAccess::WRITE_TRANSACTION_ACCESS;
	}

	return ERCAccess::NO_ACCESS;
}

// Internal helper for ERCIPayloadType->ERCPayloadType conversion
constexpr ERCPayloadType ToInternal(ERCIPayloadType Value)
{
	switch (Value)
	{
	case ERCIPayloadType::Cbor:
		return ERCPayloadType::Cbor;

	case ERCIPayloadType::Json:
		return ERCPayloadType::Json;
	}

	return ERCPayloadType::Cbor;
}

// Internal helper for ERCAccess->ERCIAccess conversion
constexpr ERCIAccess ToExternal(ERCAccess Value)
{
	switch (Value)
	{
	case ERCAccess::NO_ACCESS:
		return ERCIAccess::NO_ACCESS;

	case ERCAccess::READ_ACCESS:
		return ERCIAccess::READ_ACCESS;

	case ERCAccess::WRITE_ACCESS:
		return ERCIAccess::WRITE_ACCESS;

	case ERCAccess::WRITE_TRANSACTION_ACCESS:
		return ERCIAccess::WRITE_TRANSACTION_ACCESS;
	}

	return ERCIAccess::NO_ACCESS;
}

// Internal helper for ERCIPayloadType->ERCPayloadType conversion
constexpr ERCIPayloadType ToExternal(ERCPayloadType Value)
{
	switch (Value)
	{
	case ERCPayloadType::Cbor:
		return ERCIPayloadType::Cbor;

	case ERCPayloadType::Json:
		return ERCIPayloadType::Json;
	}

	return ERCIPayloadType::Cbor;
}
