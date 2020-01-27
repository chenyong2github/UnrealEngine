// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepEditorStyle.h"

#include "DataprepEditorModule.h"

#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FDataprepEditorStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )

#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

TSharedPtr<FSlateStyleSet> FDataprepEditorStyle::StyleSet;

void FDataprepEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());

	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	const FVector2D Icon20x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);

	StyleSet->Set("DataprepEditor.Producer", new IMAGE_PLUGIN_BRUSH("Icons/Producer24", Icon20x20));
	StyleSet->Set("DataprepEditor.Producer.Selected", new IMAGE_PLUGIN_BRUSH("Icons/Producer24", Icon20x20));

	StyleSet->Set("DataprepEditor.SaveScene", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon40x40));
	StyleSet->Set("DataprepEditor.SaveScene.Small", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon20x20));
	StyleSet->Set("DataprepEditor.SaveScene.Selected", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon40x40));
	StyleSet->Set("DataprepEditor.SaveScene.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon20x20));

	StyleSet->Set("DataprepEditor.ShowDataprepSettings", new IMAGE_PLUGIN_BRUSH("Icons/IconOptions", Icon40x40));
	StyleSet->Set("DataprepEditor.ShowDatasmithSceneSettings", new IMAGE_PLUGIN_BRUSH("Icons/IconOptions", Icon40x40));

	StyleSet->Set("DataprepEditor.BuildWorld", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon40x40));
	StyleSet->Set("DataprepEditor.BuildWorld.Small", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon20x20));
	StyleSet->Set("DataprepEditor.BuildWorld.Selected", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon40x40));
	StyleSet->Set("DataprepEditor.BuildWorld.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon20x20));

	StyleSet->Set("DataprepEditor.CommitWorld", new IMAGE_PLUGIN_BRUSH("Icons/CommitWorld", Icon40x40));
	StyleSet->Set("DataprepEditor.CommitWorld.Small", new IMAGE_PLUGIN_BRUSH("Icons/CommitWorld", Icon20x20));
	StyleSet->Set("DataprepEditor.CommitWorld.Selected", new IMAGE_PLUGIN_BRUSH("Icons/CommitWorld", Icon40x40));
	StyleSet->Set("DataprepEditor.CommitWorld.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/CommitWorld", Icon20x20));

	StyleSet->Set("DataprepEditor.ExecutePipeline", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon40x40));
	StyleSet->Set("DataprepEditor.ExecutePipeline.Small", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon20x20));
	StyleSet->Set("DataprepEditor.ExecutePipeline.Selected", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon40x40));
	StyleSet->Set("DataprepEditor.ExecutePipeline.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon20x20));

	StyleSet->Set( "DataprepEditor.SectionFont", DEFAULT_FONT( "Bold", 10 ) );

	// Dataprep action UI
	{
		StyleSet->Set("DataprepAction.Padding", 2.f);

		StyleSet->Set("DataprepAction.OutlineColor", FLinearColor(FColor(62, 62, 62)));

		StyleSet->Set("DataprepActionStep.BackgroundColor", FLinearColor(FColor(62, 62, 62)) );
		StyleSet->Set("DataprepActionStep.DragAndDrop", FLinearColor(FColor(212, 212, 59)) );
		StyleSet->Set("DataprepActionStep.Selected", FLinearColor(FColor(1, 202, 252)) );
		StyleSet->Set("DataprepActionStep.Filter.OutlineColor", FLinearColor(FColor(67, 105, 124)));
		StyleSet->Set("DataprepActionStep.Operation.OutlineColor", FLinearColor(FColor(87, 107, 61)) );

		StyleSet->Set("DataprepActionBlock.TitleBackgroundColor", FLinearColor(0.065307f, 0.065307f, 0.065307f));
		StyleSet->Set("DataprepActionBlock.ContentBackgroundColor", FLinearColor(0.11f, 0.11f, 0.11f));
		FTextBlockStyle TilteTextBlockStyle = FEditorStyle::GetWidgetStyle< FTextBlockStyle >("NormalText");
		TilteTextBlockStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
		StyleSet->Set("DataprepActionBlock.TitleTextBlockStyle", TilteTextBlockStyle);
		StyleSet->Set("DataprepActionSteps.BackgroundColor", FLinearColor( 0.1033, 0.1033, 0.1033));
		StyleSet->Set("DataprepActionStep.Padding", 10.f);
	}

	// DataprepGraphEditor
	{
		StyleSet->Set("Graph.TrackEnds.BackgroundColor", FLinearColor(0.05f, 0.05f, 0.05f, 0.2f));
		StyleSet->Set("Graph.TrackInner.BackgroundColor", FLinearColor(FColor(50, 50, 50, 200)));
		
		StyleSet->Set("Graph.ActionNode.BackgroundColor", FLinearColor(0.115861f, 0.115861f, 0.115861f));
		{
			FTextBlockStyle GraphActionNodeTitle = FTextBlockStyle(/*NormalText*/)
			.SetColorAndOpacity( FLinearColor(230.0f/255.0f,230.0f/255.0f,230.0f/255.0f) )
			.SetFont( FCoreStyle::GetDefaultFontStyle("Bold", 14) );
			StyleSet->Set( "Graph.ActionNode.Title", GraphActionNodeTitle );

			FEditableTextBoxStyle GraphActionNodeTitleEditableText = FEditableTextBoxStyle()
			.SetFont(GraphActionNodeTitle.Font);
			StyleSet->Set( "Graph.ActionNode.NodeTitleEditableText", GraphActionNodeTitleEditableText );

			StyleSet->Set( "Graph.ActionNode.TitleInlineEditableText", FInlineEditableTextBlockStyle()
				.SetTextStyle(GraphActionNodeTitle)
				.SetEditableTextBoxStyle(GraphActionNodeTitleEditableText)
			);
		}

		StyleSet->Set("Graph.ActionNode.Margin", FMargin(2.f, 0.0f, 2.f, 0.0f));
		StyleSet->Set("Graph.ActionNode.DesiredSize", FVector2D(300.f, 300.f));
	}

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FDataprepEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

FName FDataprepEditorStyle::GetStyleSetName()
{
	static FName StyleName("DataprepEditorStyle");
	return StyleName;
}

FString FDataprepEditorStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString BaseDir = IPluginManager::Get().FindPlugin(DATAPREPEDITOR_MODULE_NAME)->GetBaseDir() + TEXT("/Resources");
	return (BaseDir / RelativePath) + Extension;
}
