// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

namespace UE::Chaos::ClothAsset
{
	/**
	 * Editor style setting up the cloth preset icons in editor.
	 */
	class FClothPresetEditorStyle final : public FSlateStyleSet
	{
	public:
		FClothPresetEditorStyle();
		virtual ~FClothPresetEditorStyle() override;

	public:
		static FClothPresetEditorStyle& Get()
		{
			if (!Singleton.IsSet())
			{
				Singleton.Emplace();
			}
			return Singleton.GetValue();
		}

		static void Destroy()
		{
			Singleton.Reset();
		}

	private:
		static TOptional<FClothPresetEditorStyle> Singleton;
	};
}  // End namespace UE::Chaos::ClothAsset
