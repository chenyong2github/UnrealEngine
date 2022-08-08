// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesCollection.h"
#include "IRenderPageCollectionEditor.h"
#include "RenderPage/RenderPageCollection.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SlateOptMacros.h"

#define LOCTEXT_NAMESPACE "SRenderPagesCollection"


void UE::RenderPages::Private::SRenderPagesCollection::Tick(const FGeometry&, const double, const float)
{
	if (DetailsView.IsValid())
	{
		if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
		{
			if (URenderPageCollection* Collection = BlueprintEditor->GetInstance())
			{
				if (!IsValid(Collection) || BlueprintEditor->IsBatchRendering())
				{
					Collection = nullptr;
				}
				if (DetailsViewRenderPageCollectionWeakPtr != Collection)
				{
					DetailsViewRenderPageCollectionWeakPtr = Collection;
					DetailsView->SetObject(Collection);
				}
			}
		}
	}
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesCollection::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;
	DetailsViewRenderPageCollectionWeakPtr = InBlueprintEditor->GetInstance();

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;
	DetailsViewArgs.NotifyHook = this;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(DetailsViewRenderPageCollectionWeakPtr.Get());

	ChildSlot
	[
		DetailsView->AsShared()
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesCollection::NotifyPreChange(FEditPropertyChain* PropertyAboutToChange) {}

void UE::RenderPages::Private::SRenderPagesCollection::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged)
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		BlueprintEditor->MarkAsModified();
		BlueprintEditor->OnRenderPagesChanged().Broadcast();
	}
}


#undef LOCTEXT_NAMESPACE
