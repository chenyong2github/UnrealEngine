// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

class FMLDeformerEditorStyle : public FSlateStyleSet
{
public:
	FMLDeformerEditorStyle();
	~FMLDeformerEditorStyle();
	static FMLDeformerEditorStyle& Get();
};
