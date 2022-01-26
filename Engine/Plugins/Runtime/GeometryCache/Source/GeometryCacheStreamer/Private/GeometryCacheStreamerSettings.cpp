// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheStreamerSettings.h"
	 
UGeometryCacheStreamerSettings::UGeometryCacheStreamerSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Geometry Cache");

	LookAheadBuffer = 4.0f;
	MaxMemoryAllowed = 4096.0f;
}
