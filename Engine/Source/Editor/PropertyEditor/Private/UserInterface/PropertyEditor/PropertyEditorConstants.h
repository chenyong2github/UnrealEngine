// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Styling/SlateColor.h"
#include "UObject/NameTypes.h"
#include "Templates/SharedPointer.h"

struct FSlateBrush;

class PropertyEditorConstants
{
public:

	static const FName PropertyFontStyle;
	static const FName CategoryFontStyle;

	static const FName MD_Bitmask;
	static const FName MD_BitmaskEnum;
	static const FName MD_UseEnumValuesAsMaskValuesInEditor;

	static const FSlateBrush* GetOverlayBrush( const TSharedRef< class FPropertyEditor > PropertyEditor );

	static FSlateColor GetRowBackgroundColor(int32 IndentLevel);
};
