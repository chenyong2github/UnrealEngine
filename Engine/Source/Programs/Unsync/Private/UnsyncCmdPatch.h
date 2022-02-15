// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnsyncCommon.h"
#include "UnsyncCore.h"

namespace unsync {

struct FCmdPatchOptions
{
	fs::path Base;
	fs::path Patch;
	fs::path Output;
};

int32 CmdPatch(const FCmdPatchOptions& Options);

}  // namespace unsync
