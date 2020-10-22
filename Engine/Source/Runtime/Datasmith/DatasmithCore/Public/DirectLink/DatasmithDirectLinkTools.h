// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "IDatasmithSceneElements.h"
class IDatasmithElement;


DATASMITHCORE_API const TCHAR* GetElementTypeName(const IDatasmithElement* Element);

DATASMITHCORE_API void DumpDatasmithScene(const TSharedRef<IDatasmithScene>& Scene, const TCHAR* BaseName);
