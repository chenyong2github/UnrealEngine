// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Models/TextureEditorCommands.h"

#define LOCTEXT_NAMESPACE "TextureEditorViewOptionsMenu"

/**
 * Static helper class for populating the "View Options" menu in the texture editor's view port.
 */
class FTextureEditorViewOptionsMenu
{
public:

	/**
	 * Creates the menu.
	 *
	 * @param MenuBuilder The builder for the menu that owns this menu.
	 */
	static void MakeMenu(FMenuBuilder& MenuBuilder, bool bIsVolumeTexture, bool bIsCubemapTexture)
	{
		// view port options
		MenuBuilder.BeginSection("ViewportSection", LOCTEXT("ViewportSectionHeader", "Viewport Options"));
		{
			MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().Desaturation);

			MenuBuilder.AddSubMenu(
				LOCTEXT("Background", "Background"),
				LOCTEXT("BackgroundTooltip", "Set the viewport's background"),
				FNewMenuDelegate::CreateStatic(&FTextureEditorViewOptionsMenu::GenerateBackgroundMenuContent)
			);

			if (bIsVolumeTexture || bIsCubemapTexture)
			{
				MenuBuilder.AddSubMenu(
					LOCTEXT("ViewMode", "View Mode"),
					LOCTEXT("ViewModeTooltip", "Set the view mode"),
					FNewMenuDelegate::CreateStatic(bIsVolumeTexture ? &FTextureEditorViewOptionsMenu::GenerateVolumeViewModeMenuContent : &FTextureEditorViewOptionsMenu::GenerateCubemapViewModeMenuContent)
				);
			}

			MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().TextureBorder);
		}
		MenuBuilder.EndSection();

		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().Settings);
	}

protected:

	/**
	 * Creates the 'Background' sub-menu.
	 *
	 * @param MenuBuilder The builder for the menu that owns this menu.
	 */
	static void GenerateBackgroundMenuContent( FMenuBuilder& MenuBuilder )
	{
		MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().CheckeredBackground);
		MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().CheckeredBackgroundFill);
		MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().SolidBackground);
	}

	/**
	 * Creates the 'View Mode' sub-menu for volume textures.
	 *
	 * @param MenuBuilder The builder for the menu that owns this menu.
	 */
	static void GenerateVolumeViewModeMenuContent( FMenuBuilder& MenuBuilder )
	{
		MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().DepthSlices);
		MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().TraceIntoVolume);
	}

	/**
	 * Creates the 'View Mode' sub-menu for cubemap textures.
	 *
	 * @param MenuBuilder The builder for the menu that owns this menu.
	 */
	static void GenerateCubemapViewModeMenuContent(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().Cubemap2DView);
		MenuBuilder.AddMenuEntry(FTextureEditorCommands::Get().Cubemap3DView);
	}
};


#undef LOCTEXT_NAMESPACE
