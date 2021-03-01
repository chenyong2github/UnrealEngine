// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/ISlateStyle.h"
#include "Styling/AppStyle.h"

struct FSlateDynamicImageBrush;

class FStyle;

namespace CoreStyleConstants
{
	// Note, these sizes are in Slate Units.
	// Slate Units do NOT have to map to pixels.
	const FVector2D Icon5x16(5.0f, 16.0f);
	const FVector2D Icon8x4(8.0f, 4.0f);
	const FVector2D Icon16x4(16.0f, 4.0f);
	const FVector2D Icon8x8(8.0f, 8.0f);
	const FVector2D Icon10x10(10.0f, 10.0f);
	const FVector2D Icon12x12(12.0f, 12.0f);
	const FVector2D Icon12x16(12.0f, 16.0f);
	const FVector2D Icon14x14(14.0f, 14.0f);
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon18x18(18.0f, 18.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon22x22(22.0f, 22.0f);
	const FVector2D Icon24x24(24.0f, 24.0f);
	const FVector2D Icon25x25(25.0f, 25.0f);
	const FVector2D Icon32x32(32.0f, 32.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);
	const FVector2D Icon64x64(64.0f, 64.0f);
	const FVector2D Icon36x24(36.0f, 24.0f);
	const FVector2D Icon128x128(128.0f, 128.0f);

	// Common Margins
	const FMargin DefaultMargins(8.f, 4.f);
	// Buttons already have a built in (4., 2.) padding - adding to that a little
	const FMargin ButtonMargins(20.f, 2.f, 20.f, 3.f);

	const float InputFocusRadius = 4.f;
	const float InputFocusThickness = 1.0f;

}

/**
 * Core slate style
 */
class SLATECORE_API FStarshipCoreStyle
{
public:

	static TSharedRef<class ISlateStyle> Create();

	/** 
	* @return the Application Style 
	*
	* NOTE: Until the Editor can be fully updated, calling FStarshipCoreStyle::Get() will
	* return the AppStyle instead of the style defined in this class.  
	*
	* Using the AppStyle is preferred in most cases as it allows the style to be changed 
	* and restyled more easily.
	*
	* In cases requiring explicit use of the CoreStyle where a Slate Widget should not take on
	* the appearance of the rest of the application, use FStarshipCoreStyle::GetCoreStyle().
	*
	*/
	static const ISlateStyle& Get( )
	{
		return FAppStyle::Get();
	}

	/** @return the singleton instance of the style created in . */
	static const ISlateStyle& GetCoreStyle()
	{
		return *(Instance.Get());
	}

	/** Get the default font for Slate */
	static TSharedRef<const FCompositeFont> GetDefaultFont();

	/** Get a font style using the default for for Slate */
	static FSlateFontInfo GetDefaultFontStyle(const FName InTypefaceFontName, const int32 InSize, const FFontOutlineSettings& InOutlineSettings = FFontOutlineSettings());

	static void ResetToDefault( );

	/** Used to override the default selection colors */
	static void SetSelectorColor( const FLinearColor& NewColor );
	static void SetSelectionColor( const FLinearColor& NewColor );
	static void SetInactiveSelectionColor( const FLinearColor& NewColor );
	static void SetPressedSelectionColor( const FLinearColor& NewColor );
	static void SetFocusBrush(FSlateBrush* NewBrush);

	// todo: jdale - These are only here because of UTouchInterface::Activate and the fact that GetDynamicImageBrush is non-const
	static const TSharedPtr<FSlateDynamicImageBrush> GetDynamicImageBrush( FName BrushTemplate, FName TextureName, const ANSICHAR* Specifier = nullptr );
	static const TSharedPtr<FSlateDynamicImageBrush> GetDynamicImageBrush( FName BrushTemplate, const ANSICHAR* Specifier, class UTexture2D* TextureResource, FName TextureName );
	static const TSharedPtr<FSlateDynamicImageBrush> GetDynamicImageBrush( FName BrushTemplate, class UTexture2D* TextureResource, FName TextureName );

	static const int32 RegularTextSize = 10;
	static const int32 SmallTextSize = 8;

	static bool IsInitialized() { return Instance.IsValid(); }
private:

	static void SetStyle(const TSharedRef<class ISlateStyle>& NewStyle);
	
	static void SetupColors(TSharedRef<FStyle>& Style);
	static void SetupTextStyles(TSharedRef<FStyle>& Style);
	static void SetupButtonStyles(TSharedRef<FStyle>& Style);
	static void SetupComboButtonStyles(TSharedRef<FStyle>& Style);
	static void SetupCheckboxStyles(TSharedRef<FStyle>& Style);
	static void SetupDockingStyles(TSharedRef<FStyle>& Style);
	static void SetupColorPickerStyles(TSharedRef<FStyle>& Style);
	static void SetupTableViewStyles(TSharedRef<FStyle>& Style);
	static void SetupMultiboxStyles(TSharedRef<FStyle>& Style);

private:

	/** Singleton instances of this style. */
	static TSharedPtr< class ISlateStyle > Instance;

};

struct SLATECORE_API FStyleFonts
{

  public:
	static const FStyleFonts& Get()
	{
		if (Instance == nullptr)
		{
			Instance = MakeUnique<FStyleFonts>(FStyleFonts());
		}
		return *(Instance.Get());
	}

	const FSlateFontInfo Normal;
	const FSlateFontInfo NormalBold;
	const FSlateFontInfo Small;
	const FSlateFontInfo SmallBold;

	const FSlateFontInfo HeadingMedium;
	const FSlateFontInfo HeadingSmall;
	const FSlateFontInfo HeadingExtraSmall;

  private: 
  	FStyleFonts();
  	static TUniquePtr<struct FStyleFonts> Instance;
};

