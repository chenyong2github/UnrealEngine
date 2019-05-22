// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/** Style data for Insights tools */
class FInsightsStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	static const FLinearColor& GetColor(FName PropertyName, const ANSICHAR* Specifier = NULL)
	{
		return StyleInstance->GetColor(PropertyName, Specifier);
	}

	static const FSlateBrush* GetBrush(FName PropertyName, const ANSICHAR* Specifier = NULL)
	{
		return StyleInstance->GetBrush(PropertyName, Specifier);
	}

private:
	static TSharedRef<class FSlateStyleSet> Create();

private:
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};
