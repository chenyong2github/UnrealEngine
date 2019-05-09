// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MaterialExpressionTextureBaseCustomization.h"
#include "PropertyCustomizationHelpers.h"
#include "DetailLayoutBuilder.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "MaterialExpressionTextureBaseCustomization"

TSharedRef<IDetailCustomization> FMaterialExpressionTextureBaseCustomization::MakeInstance()
{
	return MakeShareable(new FMaterialExpressionTextureBaseCustomization);
}

void FMaterialExpressionTextureBaseCustomization::CustomizeDetails( IDetailLayoutBuilder& DetailLayout )
{
	TSharedRef<IPropertyHandle> TextureProperty = DetailLayout.GetProperty("Texture");
	TextureProperty->MarkHiddenByCustomization();

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailLayout.GetObjectsBeingCustomized(Objects);

	if (Objects.Num())
	{
		Expression = Cast<UMaterialExpressionTextureBase>(Objects[0]);
	}

	DetailLayout.AddCustomRowToCategory(TextureProperty, TextureProperty->GetPropertyDisplayName(), false)
	.NameContent()
	[
		TextureProperty->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MaxDesiredWidth(TOptional<float>())
	[
		SNew(SObjectPropertyEntryBox)
		.PropertyHandle(TextureProperty)
		.AllowedClass(UTexture::StaticClass())
		.OnShouldFilterAsset(this, &FMaterialExpressionTextureBaseCustomization::OnShouldFilterTexture)
		.ThumbnailPool(DetailLayout.GetThumbnailPool())
	];
}

bool FMaterialExpressionTextureBaseCustomization::OnShouldFilterTexture(const FAssetData& AssetData)
{
	if (Expression.Get())
	{
		bool VirtualTextured = false;
		AssetData.GetTagValue<bool>("VirtualTextureStreaming", VirtualTextured);
		bool VirtualTexturedExpression = IsVirtualSamplerType(Expression->SamplerType);

		return VirtualTextured != VirtualTexturedExpression;
	}

	return false;
}


#undef LOCTEXT_NAMESPACE
