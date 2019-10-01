// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapSharedFileFunctionLibrary.h"
#include "MagicLeapSharedFilePlugin.h"

bool UMagicLeapSharedFileFunctionLibrary::SharedFileListAccessibleFiles(TArray<FString>& OutSharedFileList)
{
	return GetMagicLeapSharedFilePlugin().SharedFileListAccessibleFiles(OutSharedFileList);
}

bool UMagicLeapSharedFileFunctionLibrary::SharedFilePickAsync(const FMagicLeapFilesPickedResultDelegate& InResultDelegate)
{
	return GetMagicLeapSharedFilePlugin().SharedFilePickAsync(InResultDelegate);
}
