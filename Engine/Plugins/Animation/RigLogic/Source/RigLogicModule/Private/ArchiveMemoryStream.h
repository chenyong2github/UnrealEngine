// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "riglogic/RigLogic.h"

/** Adapter that allows using an FArchive instance as a rl4::BoundedIOStream */
class FArchiveMemoryStream: public rl4::BoundedIOStream
{
public:
	explicit FArchiveMemoryStream(FArchive* Archive);

	void seek(size_t Position) override;
	size_t tell() override;
	void open() override;
	void close() override;
	void read(char* ReadToBuffer, size_t Size) override;
	void write(const char* WriteFromBuffer, size_t Size) override;
	size_t size() override;

private:
	FArchive* Archive;
	int64 Origin;

};
