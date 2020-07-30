// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "riglogic/RigLogic.h"

/** A simple implementation of in-memory stream for interfacing with RigLogic API,
  so RigLogic can consume DNA as a Stream either from file or memory */

class FRigLogicMemoryStream: public rl4::BoundedIOStream
{
public:
	/** The buffer is not copied, the pointer to it is stored inside this object**/
	FRigLogicMemoryStream(TArray<uint8>* Buffer);

	void seek(size_t Position) override;
	size_t tell() override;
	void open() override;
	void close() override {}
	void read(char* ReadToBuffer, size_t Size) override;
	void write(const char* WriteFromBuffer, size_t Size) override;
	size_t size() override;

private:
	TArray<uint8>* BitStreamBuffer; //doesn't contain the array, only points to the array given to it
	size_t PositionInBuffer = 0;
};
