// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyle.h"

class FDisplayClusterConfiguratorStyle
{
public:

	static void Initialize();

	static void Shutdown();

	static void ReloadTextures();

	static const ISlateStyle& Get();

	static FName GetStyleSetName();

	static const FLinearColor& GetColor(FName PropertyName, const ANSICHAR* Specifier = nullptr);

	static const FLinearColor& GetCornerColor(uint32 Index);

	static const FMargin& GetMargin(FName PropertyName, const ANSICHAR* Specifier = nullptr);

	static const FSlateBrush* GetBrush(FName PropertyName, const ANSICHAR* Specifier = nullptr);

	template< typename WidgetStyleType >
	static const WidgetStyleType& GetWidgetStyle(FName PropertyName, const ANSICHAR* Specifier = nullptr)
	{
		return StyleInstance->GetWidgetStyle<WidgetStyleType>(PropertyName, Specifier);
	}

private:
	static TSharedRef<FSlateStyleSet> Create();

	struct FCornerColor
	{
		FCornerColor() 
		{}
		
		FCornerColor(const FName& InName, const FLinearColor& InColor)
			: Name(InName)
			, Color(InColor)
		{}

		FName Name;
		FLinearColor Color;
	};

	static TArray<FCornerColor> CornerColors;

private:
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};
