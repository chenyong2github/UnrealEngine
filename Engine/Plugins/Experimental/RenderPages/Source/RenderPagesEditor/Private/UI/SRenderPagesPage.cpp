// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesPage.h"
#include "RenderPage/RenderPageCollection.h"
#include "IRenderPageCollectionEditor.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "SlateOptMacros.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPage"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPage::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.NotifyHook = this;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(nullptr);

	HandlePagesSelectionChanged();
	InBlueprintEditor->OnRenderPagesSelectionChanged().AddSP(this, &SRenderPagesPage::HandlePagesSelectionChanged);

	ChildSlot
	[
		DetailsView->AsShared()
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPage::NotifyPreChange(FEditPropertyChain* PropertyAboutToChange) {}

void UE::RenderPages::Private::SRenderPagesPage::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged)
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		BlueprintEditor->MarkAsModified();
		BlueprintEditor->OnRenderPagesChanged().Broadcast();
	}
}

void UE::RenderPages::Private::SRenderPagesPage::HandlePagesSelectionChanged()
{
	if (DetailsView.IsValid())
	{
		if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
		{
			TArray<TWeakObjectPtr<UObject>> WeakSelectedPages;
			if (const TArray<URenderPage*> SelectedPages = BlueprintEditor->GetSelectedRenderPages(); (SelectedPages.Num() == 1))
			{
				WeakSelectedPages.Add(SelectedPages[0]);
			}
			DetailsView->SetObjects(WeakSelectedPages);
		}
	}
}


#undef LOCTEXT_NAMESPACE
