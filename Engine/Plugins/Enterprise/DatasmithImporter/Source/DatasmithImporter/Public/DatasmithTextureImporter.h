// Copyright Epic Games, Inc. All Rights Reserved.
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

	UTexture* CreateTexture(const TSharedPtr<IDatasmithTextureElement>& TextureElement, const TArray<uint8>& TextureData, const FString& Extension);
	bool GetTextureData(const TSharedPtr<IDatasmithTextureElement>& TextureElement, TArray<uint8>& TextureData, FString& Extension);
	UTexture* CreateIESTexture(const TSharedPtr<IDatasmithTextureElement>& TextureElement);

private:
	bool ResizeTextureElement(const TSharedPtr<IDatasmithTextureElement>& TextureElement, FString& ResizedFilename);

private:
	FDatasmithImportContext& ImportContext;
	TStrongObjectPtr< UTextureFactory > TextureFact;
	FString TempDir;
};