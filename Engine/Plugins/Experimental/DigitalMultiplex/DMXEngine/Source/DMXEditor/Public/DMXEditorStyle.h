// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**  DMX editor style, for loading and creation of UI resources */
class DMXEDITOR_API FDMXEditorStyle
{
public:

	/** Initializes this style's singleton and register the Slate Style */
	static void Initialize();

	/** Unregister the Slate Style and release the Singleton */
	static void Shutdown();

	/**  Reloads textures used by slate renderer */
	static void ReloadTextures();

	/**  Returns the singleton ISlateStyle instance for DMX editor style */
	static const ISlateStyle& Get();

	/**  Returns the DMX editor style name */
	static FName GetStyleSetName();

private:
	/** Instantiate and returns a Slate Style Set object */
	static TSharedRef< class FSlateStyleSet > Create();

private:
	/** Singleton style instance */
	static TSharedPtr< class FSlateStyleSet > StyleInstance;
};