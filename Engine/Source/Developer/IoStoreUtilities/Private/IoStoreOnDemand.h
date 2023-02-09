// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"

class IIoStoreWriter;

struct FOnDemandWriterSettings
{
	FString TocFilePath;
	FString OutputDirectory;
	FName ContainerName;
};

TUniquePtr<IIoStoreWriter> MakeOnDemandIoStoreWriter(const FOnDemandWriterSettings& WriterSettings);
