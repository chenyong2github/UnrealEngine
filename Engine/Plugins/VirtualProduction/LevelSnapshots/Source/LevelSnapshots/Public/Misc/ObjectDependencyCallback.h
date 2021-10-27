// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Callback used to traverse object dependencies while serializing objects.
 * @param OriginalObjectDependency Index to FWorldSnapshotData::SerializedReferences
 */
using FProcessObjectDependency = TFunctionRef<void(int32 OriginalObjectIndex)>;
