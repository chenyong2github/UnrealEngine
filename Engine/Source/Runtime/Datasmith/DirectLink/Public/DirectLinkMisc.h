// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/SecureHash.h"
#include "Templates/SharedPointer.h"


class IDatasmithElement;
class IDatasmithScene;

namespace DirectLink
{
class FSceneSnapshot;

DIRECTLINK_API const FString& GetDumpPath();

DIRECTLINK_API void DumpSceneSnapshot(FSceneSnapshot& SceneSnapshot, const FString& BaseFileName);

DIRECTLINK_API FMD5Hash GenerateSceneSnapshotHash(const FSceneSnapshot& SceneSnapshot);

} // namespace DirectLink
