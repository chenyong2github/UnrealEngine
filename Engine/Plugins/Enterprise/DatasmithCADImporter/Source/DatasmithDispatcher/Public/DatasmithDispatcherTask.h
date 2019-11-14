// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CoreTechFileParser.h"

namespace DatasmithDispatcher
{
using ETaskState = CADLibrary::ECoretechParsingResult;

struct FTask
{
	FTask() = default;

	FTask(const FString& InFile)
	{
		FileName = InFile;
		State = ETaskState::UnTreated;
	}

	FString FileName;
	int32 Index = -1;
	ETaskState State = ETaskState::Unknown;
};

} // NS DatasmithDispatcher
