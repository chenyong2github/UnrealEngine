// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Commandlets/IoStoreCommandlet.h"
#include "IoStoreUtilities.h"

UIoStoreCommandlet::UIoStoreCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UIoStoreCommandlet::Main(const FString& Params)
{
	return CreateIoStoreContainerFiles(*Params);
}
