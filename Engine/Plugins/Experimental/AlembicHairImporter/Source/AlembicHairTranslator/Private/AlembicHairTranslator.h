// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HairStrandsTranslator.h"

class FAlembicHairTranslator : public IHairStrandsTranslator
{
public:
	virtual bool Translate(const FString& FilePath, FHairDescription& OutHairDescription, const FGroomConversionSettings& ConversionSettings) override;
	virtual bool CanTranslate(const FString& FilePath) override;
	virtual bool IsFileExtensionSupported(const FString& FileExtension) const override;
	virtual FString GetSupportedFormat() const override;
};
