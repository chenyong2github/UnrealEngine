// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/SharedPointer.h"


class IDatasmithElement;
class IDatasmithScene;

namespace DirectLink
{
class FSceneSnapshot;

DATASMITHCORE_API const TCHAR* GetElementTypeName(const IDatasmithElement* Element);

DATASMITHCORE_API void DumpDatasmithScene(const TSharedRef<IDatasmithScene>& Scene, const TCHAR* BaseName);

void DumpSceneSnapshot(FSceneSnapshot& SceneSnapshot, const FString& BaseFileName);

} // namespace DirectLink
