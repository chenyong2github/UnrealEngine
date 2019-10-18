// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapSharedFileTypes.generated.h"

USTRUCT()
struct FDummyMLSharedFileStruct
{
    GENERATED_BODY()
};

/**
 * Delegate used to convey the result of a file pick operation. 
 * @param List of file names the user picked to provide access to this app. The file names can then be used to read the file using the IMagicLeapSharedFilePlugin::SharedFileRead() function.
 */
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapFilesPickedResultDelegate, const TArray<FString>&, PickedFiles);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapFilesPickedResultDelegateMulti, const TArray<FString>&, PickedFiles);
