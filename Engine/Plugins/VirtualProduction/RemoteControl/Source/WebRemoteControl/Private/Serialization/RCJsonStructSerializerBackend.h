// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Backends/JsonStructSerializerBackend.h"

/**
 * Implements a custom writer for UStruct serialization using Json.
 * Tries to serialize enum values with their display name.
 */
class FRCJsonStructSerializerBackend
	: public FJsonStructSerializerBackend
{
public:

	/**
	 * Creates and initializes a new instance with the given flags.
	 *
	 * @param InArchive The archive to serialize into.
	 * @param InFlags The flags that control the serialization behavior (typically EStructSerializerBackendFlags::Default).
	 */
	FRCJsonStructSerializerBackend(FArchive& InArchive, const EStructSerializerBackendFlags InFlags)
		: FJsonStructSerializerBackend(InArchive, InFlags)
	{ }

	// IStructSerializerBackend interface
	virtual void WriteProperty(const FStructSerializerState& State, int32 ArrayIndex = 0) override;
};
