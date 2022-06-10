// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderPagesBlueprintActions.h"
#include "Styling/AppStyle.h"
#include "Blueprints/RenderPagesBlueprint.h"
#include "Factories/RenderPageCollectionFactory.h"
#include "RenderPagesEditorModule.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"


FText UE::RenderPages::Private::FRenderPagesBlueprintActions::GetName() const
{
	return LOCTEXT("AssetTypeActions_RenderPageCollection", "Render Page Collection");
}

FColor UE::RenderPages::Private::FRenderPagesBlueprintActions::GetTypeColor() const
{
	return FColor(255, 64, 64);
}

UClass* UE::RenderPages::Private::FRenderPagesBlueprintActions::GetSupportedClass() const
{
	return URenderPagesBlueprint::StaticClass();
}

void UE::RenderPages::Private::FRenderPagesBlueprintActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (URenderPagesBlueprint* RenderPagesBlueprint = Cast<URenderPagesBlueprint>(*ObjIt))
		{
			constexpr bool bBringToFrontIfOpen = true;
			if (IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(RenderPagesBlueprint, bBringToFrontIfOpen))
			{
				EditorInstance->FocusWindow(RenderPagesBlueprint);
			}
			else
			{
				const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
				IRenderPagesEditorModule::Get().CreateRenderPageCollectionEditor(Mode, EditWithinLevelEditor, RenderPagesBlueprint);
			}
		}
	}
}

uint32 UE::RenderPages::Private::FRenderPagesBlueprintActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

TSharedPtr<SWidget> UE::RenderPages::Private::FRenderPagesBlueprintActions::GetThumbnailOverlay(const FAssetData& AssetData) const
{
	const FSlateBrush* Icon = FSlateIconFinder::FindIconBrushForClass(URenderPagesBlueprint::StaticClass());

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetNoBrush())
		.Visibility(EVisibility::HitTestInvisible)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		[
			SNew(SImage)
			.Image(Icon)
		];
}

UFactory* UE::RenderPages::Private::FRenderPagesBlueprintActions::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	URenderPagesBlueprintFactory* RenderPagesBlueprintFactory = NewObject<URenderPagesBlueprintFactory>();
	RenderPagesBlueprintFactory->ParentClass = TSubclassOf<URenderPageCollection>(*InBlueprint->GeneratedClass);
	return RenderPagesBlueprintFactory;
}


#undef LOCTEXT_NAMESPACE
