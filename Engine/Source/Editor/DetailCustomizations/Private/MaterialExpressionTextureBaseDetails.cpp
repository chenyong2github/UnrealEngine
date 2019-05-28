// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MaterialExpressionTextureBaseDetails.h"
#include "PropertyCustomizationHelpers.h"
#include "DetailLayoutBuilder.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "MaterialExpressionTextureBaseDetails"

TSharedRef<IDetailCustomization> FMaterialExpressionTextureBaseDetails::MakeInstance()
{
	return MakeShareable(new FMaterialExpressionTextureBaseDetails);
}

void FMaterialExpressionTextureBaseDetails::CustomizeDetails( IDetailLayoutBuilder& DetailLayout )
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailLayout.GetObjectsBeingCustomized(Objects);

	if (Objects.Num() > 0)
	{
		Expression = CastChecked<UMaterialExpressionTextureBase>(Objects[0]);
	}

	EnumRestriction = MakeShareable(new FPropertyRestriction(LOCTEXT("VirtualTextureSamplerMatch", "Sampler type must match VirtualTexture usage")));
	TSharedPtr<IPropertyHandle> SamplerTypeHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureBase, SamplerType));
	SamplerTypeHandle->AddRestriction(EnumRestriction.ToSharedRef());

	TSharedPtr<IPropertyHandle> TextureHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureBase, Texture));
	TextureHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FMaterialExpressionTextureBaseDetails::OnTextureChanged));

	OnTextureChanged();
}

void FMaterialExpressionTextureBaseDetails::OnTextureChanged()
{
	bool bAllowVirtualTexture = true;
	bool bAllowNonVirtualTexture = true;
	if (Expression.IsValid() && Expression->Texture)
	{
		bAllowVirtualTexture = Expression->Texture->VirtualTextureStreaming;
		bAllowNonVirtualTexture = !bAllowVirtualTexture;
	}

	EnumRestriction->RemoveAll();

	const UEnum* MaterialSamplerTypeEnum = StaticEnum<EMaterialSamplerType>();
	for (int SamplerTypeIndex = 0; SamplerTypeIndex < SAMPLERTYPE_MAX; ++SamplerTypeIndex)
	{
		const bool bIsVirtualTexture = IsVirtualSamplerType((EMaterialSamplerType)SamplerTypeIndex);
		if ((bIsVirtualTexture && !bAllowVirtualTexture) || (!bIsVirtualTexture && !bAllowNonVirtualTexture))
		{
			EnumRestriction->AddHiddenValue(MaterialSamplerTypeEnum->GetNameStringByValue(SamplerTypeIndex));
		}
	}
}

#undef LOCTEXT_NAMESPACE
