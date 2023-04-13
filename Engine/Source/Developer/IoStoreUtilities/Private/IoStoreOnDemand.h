// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "IO/IoDispatcher.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"

class IIoStoreWriter;

class IIoStoreOnDemandWriter
{
public:
	virtual ~IIoStoreOnDemandWriter() = default;
	virtual TSharedPtr<IIoStoreWriter> CreateContainer(const FString& ContainerName, const FIoContainerSettings& ContainerSettings) = 0;
	virtual void Flush() = 0;
};

TUniquePtr<IIoStoreOnDemandWriter> MakeIoStoreOnDemandWriter(
	const FIoStoreWriterSettings& WriterSettings,
	const FString& OutputDirectory,
	uint32 MaxConcurrentWrites = 64);
