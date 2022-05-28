// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeTexturePatchCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "LandscapeTexturePatchBase.h"
#include "TextureCompiler.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "LandscapeTexturePatchCustomization"

TSharedRef<IDetailCustomization> FLandscapeTexturePatchCustomization::MakeInstance()
{
	return MakeShareable(new FLandscapeTexturePatchCustomization);
}

void FLandscapeTexturePatchCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// We're going to add a button to save the internal texture. We do this in a detail customization
	// rather than in a "CallInEditor" method on the patch itself because this is an editor-only method,
	// and the patch is not in an editor-only module.

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() != 1)
	{
		// TODO: Could consider showing but disabling if multiple patches are selected.
		return;
	}

	ULandscapeTexturePatchBase* PatchObject = Cast<ULandscapeTexturePatchBase>(ObjectsBeingCustomized[0]);
	if (!ensure(PatchObject))
	{
		return;
	}

	FText ButtonLabel = LOCTEXT("SaveInternalTextureButtonLabel", "Save Internal Texture as Asset");

	DetailBuilder.EditCategory("Initialization")
		.AddCustomRow(ButtonLabel, false)
		.RowTag(FName(TEXT("InternalTextureToExternal")))
		[
			// The slate here matches what we do in FObjectDetails::AddCallInEditorMethods
			SNew(SWrapBox)
			.UseAllottedSize(true)
			+ SWrapBox::Slot()
			.Padding(0.0f, 0.0f, 5.0f, 3.0f)
			[
				SNew(SButton)
				.Text(ButtonLabel)
				.IsEnabled(TAttribute<bool>::CreateLambda([PatchObject]() {
					return IsValid(PatchObject) && (
						(PatchObject->GetSourceMode() == ELandscapeTexturePatchSourceMode::InternalTexture && PatchObject->GetInternalTexture())
						|| (PatchObject->GetSourceMode() == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget && PatchObject->GetInternalRenderTarget()));
				}))
				.OnClicked(FOnClicked::CreateWeakLambda(PatchObject, [PatchObject]() {

					if (!IsValid(PatchObject) || PatchObject->GetSourceMode() == ELandscapeTexturePatchSourceMode::TextureAsset)
					{
						return FReply::Unhandled();
					}

					if (PatchObject->GetSourceMode() == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
					{
						// Make sure the data is inside the internal texture for copying...
						PatchObject->SetSourceMode(ELandscapeTexturePatchSourceMode::InternalTexture);
					}

					if (!PatchObject->GetInternalTexture())
					{
						return FReply::Unhandled();
					}

					// For the "save as" call to work properly for us, we would like the internal texture
					// to be marked as transient and be in the transient package. We could do that with a
					// Rename(), but feels risky to break it under the patch, even though we'll be changing
					// the source mode. So we'll make a copy and use that.
					UTexture* TextureCopy = DuplicateObject(PatchObject->GetInternalTexture(), nullptr);
					TextureCopy->SetFlags(RF_Transient);
					FTextureCompilingManager::Get().FinishCompilation({ TextureCopy });

					// Bring up the popup and deal with the saving.
					TArray<UObject*> SavedObjects;
					FEditorFileUtils::SaveAssetsAs({ TextureCopy }, SavedObjects);

					if (SavedObjects.Num() > 0)
					{
						UTexture* NewTexture = Cast<UTexture>(SavedObjects[0]);
						if (ensure(NewTexture))
						{
							PatchObject->SetTextureAsset(NewTexture);
						}
						PatchObject->SetSourceMode(ELandscapeTexturePatchSourceMode::TextureAsset);
					}
				
					return FReply::Handled();
					}))
				.ToolTipText(LOCTEXT("SaveInternalTextureTooltip", 
					"Save the current internal texture as a new texture asset and set that as the source of the patch."))
			]
		];
}

#undef LOCTEXT_NAMESPACE