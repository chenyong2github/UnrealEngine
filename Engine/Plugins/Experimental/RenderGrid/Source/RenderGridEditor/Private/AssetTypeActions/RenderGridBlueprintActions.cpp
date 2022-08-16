// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGridBlueprintActions.h"
#include "Styling/AppStyle.h"
#include "Blueprints/RenderGridBlueprint.h"
#include "Factories/RenderGridFactory.h"
#include "RenderGridEditorModule.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"


FText UE::RenderGrid::Private::FRenderGridBlueprintActions::GetName() const
{
	return LOCTEXT("AssetTypeActions_RenderGrid", "Render Grid");
}

FColor UE::RenderGrid::Private::FRenderGridBlueprintActions::GetTypeColor() const
{
	return FColor(255, 64, 64);
}

UClass* UE::RenderGrid::Private::FRenderGridBlueprintActions::GetSupportedClass() const
{
	return URenderGridBlueprint::StaticClass();
}

void UE::RenderGrid::Private::FRenderGridBlueprintActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (URenderGridBlueprint* RenderGridBlueprint = Cast<URenderGridBlueprint>(*ObjIt))
		{
			constexpr bool bBringToFrontIfOpen = true;
			if (IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(RenderGridBlueprint, bBringToFrontIfOpen))
			{
				EditorInstance->FocusWindow(RenderGridBlueprint);
			}
			else
			{
				const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
				IRenderGridEditorModule::Get().CreateRenderGridEditor(Mode, EditWithinLevelEditor, RenderGridBlueprint);
			}
		}
	}
}

uint32 UE::RenderGrid::Private::FRenderGridBlueprintActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

TSharedPtr<SWidget> UE::RenderGrid::Private::FRenderGridBlueprintActions::GetThumbnailOverlay(const FAssetData& AssetData) const
{
	const FSlateBrush* Icon = FSlateIconFinder::FindIconBrushForClass(URenderGridBlueprint::StaticClass());

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

UFactory* UE::RenderGrid::Private::FRenderGridBlueprintActions::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	URenderGridBlueprintFactory* RenderGridBlueprintFactory = NewObject<URenderGridBlueprintFactory>();
	RenderGridBlueprintFactory->ParentClass = TSubclassOf<URenderGrid>(*InBlueprint->GeneratedClass);
	return RenderGridBlueprintFactory;
}


#undef LOCTEXT_NAMESPACE
