// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
struct FWorldPartitionFileLogger
{
public:
	FWorldPartitionFileLogger(const FString& InLogFilename);
	~FWorldPartitionFileLogger();
	void WriteLine(const FString& InLine, bool bIndent = false);
	void DecrementIndentation();

private:
	int32 IndentationCount;
	class FArchive* LogFile;
};
#endif