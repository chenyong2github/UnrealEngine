// Copyright Epic Games, Inc. All Rights Reserved.

#include "UserInterface/PropertyEditor/PropertyEditorConstants.h"
#include "EditorStyleSet.h"
#include "Styling/StyleColors.h"

const FName PropertyEditorConstants::PropertyFontStyle( TEXT("PropertyWindow.NormalFont") );
const FName PropertyEditorConstants::CategoryFontStyle( TEXT("PropertyWindow.BoldFont") );

const FName PropertyEditorConstants::MD_Bitmask( TEXT("Bitmask") );
const FName PropertyEditorConstants::MD_BitmaskEnum( TEXT("BitmaskEnum") );
const FName PropertyEditorConstants::MD_UseEnumValuesAsMaskValuesInEditor( TEXT("UseEnumValuesAsMaskValuesInEditor") );

const FSlateBrush* PropertyEditorConstants::GetOverlayBrush( const TSharedRef< class FPropertyEditor > PropertyEditor )
{
	return FEditorStyle::GetBrush( TEXT("PropertyWindow.NoOverlayColor") );
}

FSlateColor PropertyEditorConstants::GetRowBackgroundColor(int32 IndentLevel) 
{
	if (IndentLevel == 0)
	{
		return FAppStyle::Get().GetSlateColor("Colors.Background");
	}

	int32 ColorIndex = 0;
	int32 Increment = 1;

	for (int i = 0; i < IndentLevel; ++i)
	{
		ColorIndex += Increment;

		if (ColorIndex == 0 || ColorIndex == 3)
		{
			Increment = -Increment;
		}
	}

	static const FLinearColor Colors[] =
	{
		COLOR("#2D2D2D"),
		COLOR("#373737"),
		COLOR("#434343"),
		COLOR("#515151")
	};

	return FSlateColor(Colors[ColorIndex]);
}
