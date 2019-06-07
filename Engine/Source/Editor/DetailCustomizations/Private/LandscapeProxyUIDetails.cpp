// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandscapeProxyUIDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "Runtime/Landscape/Classes/Landscape.h"
#include "Runtime/Landscape/Classes/LandscapeInfo.h"
#include "Settings/EditorExperimentalSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Misc/MessageDialog.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FLandscapeProxyUIDetails"

FLandscapeProxyUIDetails::FLandscapeProxyUIDetails()
{
}

TSharedRef<IDetailCustomization> FLandscapeProxyUIDetails::MakeInstance()
{
	return MakeShareable( new FLandscapeProxyUIDetails);
}

void FLandscapeProxyUIDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	TArray<TWeakObjectPtr<UObject>> EditingObjects;
	DetailBuilder.GetObjectsBeingCustomized(EditingObjects);

	auto GenerateTextWidget = [](const FText& InText, bool bInBold = false) -> TSharedRef<SWidget>
	{
		return SNew(STextBlock)
				.Font(bInBold? IDetailLayoutBuilder::GetDetailFontBold() : IDetailLayoutBuilder::GetDetailFont())
				.Text(InText);
	};

	if (EditingObjects.Num() == 1)
	{
		if (ALandscapeProxy* Proxy = Cast<ALandscapeProxy>(EditingObjects[0]))
		{
			if (ULandscapeInfo* LandscapeInfo = Proxy->GetLandscapeInfo())
			{
				IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory("Information", FText::GetEmpty(), ECategoryPriority::Important);
				
				FText RowDisplayText = LOCTEXT("LandscapeComponentResolution", "Component Resolution (Verts)");
				CategoryBuilder.AddCustomRow(RowDisplayText)
				.NameContent()
				[
					GenerateTextWidget(RowDisplayText)
				]
				.ValueContent()
				[
					GenerateTextWidget(FText::Format(LOCTEXT("LandscapeComponentResolutionValue", "{0} x {0}"), Proxy->ComponentSizeQuads+1), true) // Verts
				];

				RowDisplayText = LOCTEXT("LandscapeComponentCount", "Component Count");
				CategoryBuilder.AddCustomRow(RowDisplayText)
				.NameContent()
				[
					GenerateTextWidget(RowDisplayText)
				]
				.ValueContent()
				[
					GenerateTextWidget(FText::Format(LOCTEXT("LandscapeComponentCountValue", "{0}"), Proxy->LandscapeComponents.Num()), true)
				];

				RowDisplayText = LOCTEXT("LandscapeComponentSubsections", "Component Subsections");
				CategoryBuilder.AddCustomRow(RowDisplayText)
				.NameContent()
				[
					GenerateTextWidget(RowDisplayText)
				]
				.ValueContent()
				[
					GenerateTextWidget(FText::Format(LOCTEXT("LandscapeComponentSubSectionsValue", "{0} x {0}"), Proxy->NumSubsections), true)
				];

				FIntRect Rect = Proxy->GetBoundingRect();
				FIntPoint Size = Rect.Size();
				RowDisplayText = LOCTEXT("LandscapeResolution", "Resolution (Verts)");
				CategoryBuilder.AddCustomRow(RowDisplayText)
				.NameContent()
				[
					GenerateTextWidget(RowDisplayText)
				]
				.ValueContent()
				[
					GenerateTextWidget(FText::Format(LOCTEXT("LandscapeResolutionValue", "{0} x {1}"), Size.X+1, Size.Y+1), true)
				];

				int32 LandscapeCount = LandscapeInfo->Proxies.Num() + (LandscapeInfo->LandscapeActor.Get() ? 1 : 0);
				RowDisplayText = LOCTEXT("LandscapeCount", "Landscape Count");
				CategoryBuilder.AddCustomRow(RowDisplayText)
				.NameContent()
				[
					GenerateTextWidget(RowDisplayText)
				]
				.ValueContent()
				[
					GenerateTextWidget(FText::Format(LOCTEXT("LandscapeCountValue", "{0}"), LandscapeCount), true)
				];

				LandscapeInfo->GetLandscapeExtent(Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y);
				Size = Rect.Size();
				RowDisplayText = LOCTEXT("LandscapeOverallResolution", "Overall Resolution (Verts)");
				CategoryBuilder.AddCustomRow(RowDisplayText)
				.NameContent()
				[
					GenerateTextWidget(RowDisplayText)
				]
				.ValueContent()
				[
					GenerateTextWidget(FText::Format(LOCTEXT("LandscapeOveralResolutionValue", "{0} x {1}"), Size.X + 1, Size.Y + 1), true)
				];
			}

		}
	}
}

#undef LOCTEXT_NAMESPACE
