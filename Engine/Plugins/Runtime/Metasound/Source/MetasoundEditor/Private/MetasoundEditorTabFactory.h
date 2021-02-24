// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorStyleSet.h"
#include "Framework/Docking/TabManager.h"
#include "Internationalization/Internationalization.h"
#include "Widgets/Docking/SDockTab.h"
#include "SAudioMeter.h"
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
				const FName Analyzers = "MetasoundEditor_Analyzers";
				const FName Inspector = "MetasoundEditor_Inspector";
				const FName GraphCanvas = "MetasoundEditor_GraphCanvas";
				const FName Metasound = "MetasoundEditor_Metasound";
				const FName Palette = "MetasoundEditor_Palette";
			}

			TSharedRef<SDockTab> CreateAnalyzersTab(TSharedPtr<SAudioMeter> InAudioMeter, const FSpawnTabArgs& Args)
			{
				check(Args.GetTabId() == Names::Analyzers);

				return SNew(SDockTab)
					.Icon(FEditorStyle::GetBrush("Kismet.Tabs.Palette"))
					.Label(LOCTEXT("MetasoundAnalyzersTitle", "Analyzers"))
					[
						InAudioMeter.ToSharedRef()
					];
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

			TSharedRef<SDockTab> CreateInspectorTab(TSharedPtr<IDetailsView> DetailsView, const FSpawnTabArgs& Args)
			{
				check(Args.GetTabId() == Names::Inspector);

				return SNew(SDockTab)
					.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
					.Label(LOCTEXT("MetasoundInspectorTitle", "Inspector"))
					[
						DetailsView.ToSharedRef()
					];
			}

			TSharedRef<SDockTab> CreateMetasoundTab(TSharedPtr<IDetailsView> DetailsView, const FSpawnTabArgs& Args)
			{
				check(Args.GetTabId() == Names::Metasound);

				return SNew(SDockTab)
					.Icon(FAppStyle::Get().GetBrush("Icons.Settings"))
					.Label(LOCTEXT("MetasoundGeneralTitle", "Metasound"))
					[
						DetailsView.ToSharedRef()
					];
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
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE // MetasoundEditor
