// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PbrtHairTranslator.h"

#include "GroomImportOptions.h"
#include "Misc/Paths.h"

namespace PbrtHairFormat
{
	static const float RootRadius = 0.0001f; // m
	static const float TipRadius = 0.00005f; // m
}

bool FPbrtHairTranslator::Translate(const FString& FileName, FHairDescription& HairDescription, const FGroomConversionSettings& ConversionSettings)
{
	bool bSuccess = false;

	// #ueent_todo: Pbrt loader from HairStrandsLoader

	return bSuccess;
}

bool FPbrtHairTranslator::CanTranslate(const FString& FilePath)
{
	return IsFileExtensionSupported(FPaths::GetExtension(FilePath));
}

bool FPbrtHairTranslator::IsFileExtensionSupported(const FString& FileExtension) const
{
	return GetSupportedFormat().StartsWith(FileExtension);
}

FString FPbrtHairTranslator::GetSupportedFormat() const
{
	return TEXT("pbrt;pbrt file");
}
