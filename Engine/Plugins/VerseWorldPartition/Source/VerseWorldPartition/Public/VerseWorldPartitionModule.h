// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class IVerseWorldPartitionModule : public IModuleInterface
{
public:
	static VERSEWORLDPARTITION_API IVerseWorldPartitionModule& Get();
};
