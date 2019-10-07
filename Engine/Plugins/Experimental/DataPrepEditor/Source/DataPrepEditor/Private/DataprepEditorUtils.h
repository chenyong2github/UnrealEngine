// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Fonts/SlateFontInfo.h"

class FDataprepEditorUtils
{
public:
	static void NotifySystemOfChangeInPipeline(UObject* SourceObject);
	static FSlateFontInfo GetGlyphFont();
};
