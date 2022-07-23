// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesProps.h"
#include "UI/SRenderPagesPropsBase.h"
#include "RenderPage/RenderPageCollection.h"
#include "IRenderPageCollectionEditor.h"
#include "IRenderPagesEditorModule.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "SRenderPagesProps"


void UE::RenderPages::Private::SRenderPagesProps::Tick(const FGeometry&, const double, const float)
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (URenderPageCollection* Collection = BlueprintEditor->GetInstance(); IsValid(Collection))
		{
			if (WidgetPropsSourceWeakPtr != Collection->GetPropsSource())
			{
				Refresh();
			}
		}
	}
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesProps::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;

	SAssignNew(WidgetContainer, SBorder)
		.Padding(8.0f)
		.BorderImage(new FSlateNoResource());

	Refresh();
	InBlueprintEditor->OnRenderPagesChanged().AddSP(this, &SRenderPagesProps::Refresh);
	InBlueprintEditor->OnRenderPagesBatchRenderingStarted().AddSP(this, &SRenderPagesProps::OnBatchRenderingStarted);
	InBlueprintEditor->OnRenderPagesBatchRenderingFinished().AddSP(this, &SRenderPagesProps::OnBatchRenderingFinished);

	ChildSlot
	[
		WidgetContainer.ToSharedRef()
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesProps::Refresh()
{
	if (!WidgetContainer.IsValid())
	{
		return;
	}
	WidgetContainer->ClearContent();

	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (!BlueprintEditor->IsBatchRendering())
		{
			if (URenderPageCollection* Collection = BlueprintEditor->GetInstance(); IsValid(Collection))
			{
				WidgetPropsSourceWeakPtr = Collection->GetPropsSource();
				if (URenderPagePropsSourceBase* WidgetPropsSource = WidgetPropsSourceWeakPtr.Get(); IsValid(WidgetPropsSource))
				{
					if (const TSharedPtr<SRenderPagesPropsBase> Widget = IRenderPagesEditorModule::Get().CreatePropsSourceWidget(WidgetPropsSource, BlueprintEditor))
					{
						WidgetContainer->SetContent(Widget.ToSharedRef());
					}
				}
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
