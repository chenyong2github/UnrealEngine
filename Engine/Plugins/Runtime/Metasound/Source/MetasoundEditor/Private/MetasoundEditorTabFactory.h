// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorStyleSet.h"
#include "Framework/Docking/TabManager.h"
#include "Internationalization/Internationalization.h"
#include "Widgets/Docking/SDockTab.h"
#include "Templates/SharedPointer.h"

#define LOCTEXT_NAMESPACE "MetasoundEditor"

namespace Metasound
{
	namespace Editor
	{
		namespace TabFactory
		{
			namespace Names
			{
				const FName GraphCanvas = "MetasoundEditor_GraphCanvas";
				const FName Palette = "MetasoundEditor_Palette";
				const FName Properties = "MetasoundEditor_Properties";
			}

			TSharedRef<SDockTab> CreateGraphCanvasTab(TSharedPtr<SGraphEditor> GraphEditor, const FSpawnTabArgs& Args)
			{
				check(Args.GetTabId() == Names::GraphCanvas);

				TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
					.Label(LOCTEXT("MetasoundGraphCanvasTitle", "Viewport"));

				if (GraphEditor.IsValid())
				{
					SpawnedTab->SetContent(GraphEditor.ToSharedRef());
				}

				return SpawnedTab;
			}

			TSharedRef<SDockTab> CreatePaletteTab(TSharedPtr<SMetasoundPalette> Palette, const FSpawnTabArgs& Args)
			{
				check(Args.GetTabId() == Names::Palette);

				return SNew(SDockTab)
					.Icon(FEditorStyle::GetBrush("Kismet.Tabs.Palette"))
					.Label(LOCTEXT("MetasoundPaletteTitle", "Palette"))
					[
						Palette.ToSharedRef()
					];
			}

			TSharedRef<SDockTab> CreatePropertiesTab(TSharedPtr<IDetailsView> PropertiesView, const FSpawnTabArgs& Args)
			{
				check(Args.GetTabId() == Names::Properties);

				return SNew(SDockTab)
					.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
					.Label(LOCTEXT("MetasoundDetailsTitle", "Details"))
					[
						PropertiesView.ToSharedRef()
					];
			}
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE // MetasoundEditor
