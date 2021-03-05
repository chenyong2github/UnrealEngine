// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CoreTechFileParser.h"
#include "CADData.h"

namespace DatasmithDispatcher
{
using ETaskState = CADLibrary::ECoreTechParsingResult;

struct FTask
{
	FTask() = default;

	FTask(const CADLibrary::FFileDescription& InFile)
	{
		FileDescription = InFile;
		State = ETaskState::UnTreated;
	}

	CADLibrary::FFileDescription FileDescription;
	int32 Index = -1;
	ETaskState State = ETaskState::Unknown;
};

} // NS DatasmithDispatcher
