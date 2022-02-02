// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeProxyUIDetails.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Misc/MessageDialog.h"
#include "PropertyHandle.h"
#include "Runtime/Landscape/Classes/Landscape.h"
#include "Runtime/Landscape/Classes/LandscapeInfo.h"
#include "RuntimeVirtualTextureSetBounds.h"
#include "ScopedTransaction.h"
#include "Settings/EditorExperimentalSettings.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

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
		LandscapeProxy = Cast<ALandscapeProxy>(EditingObjects[0]);
		if (LandscapeProxy != nullptr)
		{
			if (ULandscapeInfo* LandscapeInfo = LandscapeProxy->GetLandscapeInfo())
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
					GenerateTextWidget(FText::Format(LOCTEXT("LandscapeComponentResolutionValue", "{0} x {0}"), LandscapeProxy->ComponentSizeQuads+1), true) // Verts
				];

				RowDisplayText = LOCTEXT("LandscapeComponentCount", "Component Count");
				CategoryBuilder.AddCustomRow(RowDisplayText)
				.NameContent()
				[
					GenerateTextWidget(RowDisplayText)
				]
				.ValueContent()
				[
					GenerateTextWidget(FText::Format(LOCTEXT("LandscapeComponentCountValue", "{0}"), LandscapeProxy->LandscapeComponents.Num()), true)
				];

				RowDisplayText = LOCTEXT("LandscapeComponentSubsections", "Component Subsections");
				CategoryBuilder.AddCustomRow(RowDisplayText)
				.NameContent()
				[
					GenerateTextWidget(RowDisplayText)
				]
				.ValueContent()
				[
					GenerateTextWidget(FText::Format(LOCTEXT("LandscapeComponentSubSectionsValue", "{0} x {0}"), LandscapeProxy->NumSubsections), true)
				];

				FIntRect Rect = LandscapeProxy->GetBoundingRect();
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

				int32 TotalComponentCount = LandscapeInfo->XYtoComponentMap.Num();
				RowDisplayText = LOCTEXT("TotalLandscapeComponentCount", "Total Component Count");
				CategoryBuilder.AddCustomRow(RowDisplayText)
				.NameContent()
				[
					GenerateTextWidget(RowDisplayText)
				]
				.ValueContent()
				[
					GenerateTextWidget(FText::Format(LOCTEXT("TotalLandscapeComponentCountValue", "{0}"), TotalComponentCount), true)
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


			// Apply custom widget for CreateVolume.
			TSharedRef<IPropertyHandle> CreateVolumePropertyHandle = DetailBuilder.GetProperty(TEXT("bSetCreateRuntimeVirtualTextureVolumes"));
			DetailBuilder.EditDefaultProperty(CreateVolumePropertyHandle)->CustomWidget()
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("Button_CreateVolumes", "Create Volumes"))
				.ToolTipText(LOCTEXT("Button_CreateVolumes_Tooltip", "Create volumes for the selected Runtime Virtual Textures."))
			]
			.ValueContent()
			.MinDesiredWidth(125.f)
			[
				SNew(SButton)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.ContentPadding(2)
				.Text(LOCTEXT("Button_CreateVolumes", "Create Volumes"))
				.OnClicked(this, &FLandscapeProxyUIDetails::CreateRuntimeVirtualTextureVolume)
				.IsEnabled(this, &FLandscapeProxyUIDetails::IsCreateRuntimeVirtualTextureVolumeEnabled)
			];

		}
	}
}

static void GetMissingRuntimeVirtualTextureVolumes(ALandscapeProxy* LandscapeProxy, TArray<URuntimeVirtualTexture*>& OutVirtualTextures)
{
	UWorld* World = LandscapeProxy != nullptr ? LandscapeProxy->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	TArray<URuntimeVirtualTexture*> FoundVolumes;
	for (TObjectIterator<URuntimeVirtualTextureComponent> It(RF_ClassDefaultObject, false, EInternalObjectFlags::Garbage); It; ++It)
	{
		if (It->GetWorld() == World)
		{
			if (URuntimeVirtualTexture* VirtualTexture = It->GetVirtualTexture())
			{
				FoundVolumes.Add(VirtualTexture);
			}
		}
	}

	for (URuntimeVirtualTexture* VirtualTexture : LandscapeProxy->RuntimeVirtualTextures)
	{
		if (VirtualTexture != nullptr && FoundVolumes.Find(VirtualTexture) == INDEX_NONE)
		{
			OutVirtualTextures.Add(VirtualTexture);
		}
	}
}

bool FLandscapeProxyUIDetails::IsCreateRuntimeVirtualTextureVolumeEnabled() const
{
	TArray<URuntimeVirtualTexture*> VirtualTextureVolumesToCreate;
	GetMissingRuntimeVirtualTextureVolumes(LandscapeProxy, VirtualTextureVolumesToCreate);
	return VirtualTextureVolumesToCreate.Num() > 0;
}

FReply FLandscapeProxyUIDetails::CreateRuntimeVirtualTextureVolume()
{
	TArray<URuntimeVirtualTexture*> VirtualTextureVolumesToCreate;
	GetMissingRuntimeVirtualTextureVolumes(LandscapeProxy, VirtualTextureVolumesToCreate);
	if (VirtualTextureVolumesToCreate.Num() == 0)
	{
		return FReply::Unhandled();
	}

	const FScopedTransaction Transaction(LOCTEXT("Transaction_CreateVolumes", "Create Runtime Virtual Texture Volumes"));
	
	for (URuntimeVirtualTexture* VirtualTexture : VirtualTextureVolumesToCreate)
	{
		ARuntimeVirtualTextureVolume* NewVolume = LandscapeProxy->GetWorld()->SpawnActor<ARuntimeVirtualTextureVolume>();
		NewVolume->VirtualTextureComponent->SetVirtualTexture(VirtualTexture);
		NewVolume->VirtualTextureComponent->SetBoundsAlignActor(LandscapeProxy);
		RuntimeVirtualTexture::SetBounds(NewVolume->VirtualTextureComponent);
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
