// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace ConcertUtil
{

/** Delete a directory tree via a move and delete */
CONCERT_API bool DeleteDirectoryTree(const TCHAR* InDirectoryToDelete, const TCHAR* InMoveToDirBeforeDelete = nullptr);

} // namespace ConcertUtil
