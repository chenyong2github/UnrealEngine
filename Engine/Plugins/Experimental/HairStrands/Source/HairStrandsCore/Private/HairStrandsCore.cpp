// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsCore.h"
#include "HairStrandsInterface.h"
#include "GroomManager.h"

IMPLEMENT_MODULE(FHairStrandsCore, HairStrandsCore);

void ProcessHairStrandsBookmark(
	FRDGBuilder& GraphBuilder,
	EHairStrandsBookmark Bookmark,
	FHairStrandsBookmarkParameters& Parameters);

void ProcessHairStrandsParameters(FHairStrandsBookmarkParameters& Parameters);

void FHairStrandsCore::StartupModule()
{
	RegisterBookmarkFunction(ProcessHairStrandsBookmark, ProcessHairStrandsParameters);
}

void FHairStrandsCore::ShutdownModule()
{
}

