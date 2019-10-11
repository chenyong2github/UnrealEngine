// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HairDescription.h"

class HAIRSTRANDSEDITOR_API IHairStrandsTranslator
{
public:
	virtual ~IHairStrandsTranslator() {}

	/** Translate a given file into a HairDescription; return true if successful */
	virtual bool Translate(const FString& FilePath, FHairDescription& OutHairDescription, const struct FGroomConversionSettings& ConversionSettings) = 0;

	/** Return true if a given file can be translated (by checking its content if necessary) */
	virtual bool CanTranslate(const FString& FilePath) = 0;

	/** Return true if a given file extension is supported by the translator */
	virtual bool IsFileExtensionSupported(const FString& FileExtension) const = 0;

	/** Return the file format supported by the translator in the form "ext;file format description" */
	virtual FString GetSupportedFormat() const = 0;
};
