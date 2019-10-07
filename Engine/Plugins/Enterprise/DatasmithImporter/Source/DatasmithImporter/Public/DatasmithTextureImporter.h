// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"
#include "UObject/StrongObjectPtr.h"

struct FDatasmithImportContext;
class IDatasmithTextureElement;
class UTexture;
class UTextureFactory;

class FDatasmithTextureImporter : private FNoncopyable
{
public:
	FDatasmithTextureImporter(FDatasmithImportContext& InImportContext);
	~FDatasmithTextureImporter();

	UTexture* CreateTexture(const TSharedPtr<IDatasmithTextureElement>& TextureElement);

private:
	bool ResizeTextureElement(const TSharedPtr<IDatasmithTextureElement>& TextureElement, FString& ResizedFilename);

private:
	FDatasmithImportContext& ImportContext;
	TStrongObjectPtr< UTextureFactory > TextureFact;
	FString TempDir;
};