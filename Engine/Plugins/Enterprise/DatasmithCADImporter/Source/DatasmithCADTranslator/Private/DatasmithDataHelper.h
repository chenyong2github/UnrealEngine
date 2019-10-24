// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "MeshTypes.h"

struct FColor;
class IDatasmithUEPbrMaterialElement;
class IDatasmithScene;

TSharedPtr<IDatasmithUEPbrMaterialElement> CreateDefaultUEPbrMaterial();
TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromColor(FColor& Color);
TSharedPtr<IDatasmithUEPbrMaterialElement> CreateUEPbrMaterialFromMaterial(CADLibrary::FCADMaterial& Material, TSharedRef<IDatasmithScene> Scene);
