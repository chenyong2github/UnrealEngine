// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithRevitLiveMaterialSelector.h"

#include "DatasmithSceneFactory.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/Casts.h"

FDatasmithRevitLiveMaterialSelector::FDatasmithRevitLiveMaterialSelector()
{
	// Master
	OpaqueMaterial.FromSoftObjectPath( FSoftObjectPath("/DatasmithRuntime/Materials/M_Opaque.M_Opaque") );
	TransparentMaterial.FromSoftObjectPath( FSoftObjectPath("/DatasmithRuntime/Materials/M_Transparent.M_Transparent") );
	CutoutMaterial.FromSoftObjectPath( FSoftObjectPath("/DatasmithRuntime/Materials/M_Cutout.M_Cutout") );
}

bool FDatasmithRevitLiveMaterialSelector::IsValid() const
{
	return OpaqueMaterial.IsValid();
}

const FDatasmithMasterMaterial& FDatasmithRevitLiveMaterialSelector::GetMasterMaterial( const TSharedPtr< IDatasmithMasterMaterialElement >& InDatasmithMaterial ) const
{
	TSharedPtr< IDatasmithMasterMaterialElement > MaterialElement = ConstCastSharedPtr< IDatasmithMasterMaterialElement >(InDatasmithMaterial);

	TFunction<void(const TCHAR*, const TCHAR*)> ConvertProperty;
	ConvertProperty = [this, &MaterialElement](const TCHAR* BoolPropertyName, const TCHAR* FloatPropertyName)
	{
		TSharedPtr< IDatasmithKeyValueProperty > BoolProperty = MaterialElement->GetPropertyByName(BoolPropertyName);
		if (BoolProperty.IsValid())
		{
			bool Value;
			this->GetBool(BoolProperty, Value);
			if (Value)
			{
				TSharedPtr<IDatasmithKeyValueProperty> NewProperty = MaterialElement->GetPropertyByName(FloatPropertyName);
				if (!NewProperty.IsValid())
				{
					NewProperty = FDatasmithSceneFactory::CreateKeyValueProperty(FloatPropertyName);
					MaterialElement->AddProperty(NewProperty);
				}
				NewProperty->SetPropertyType(EDatasmithKeyValuePropertyType::Float);
				NewProperty->SetValue(TEXT("1.0"));
			}
		}
	};

	{
		TSharedPtr< IDatasmithKeyValueProperty > Property = MaterialElement->GetPropertyByName(TEXT("Glossiness"));
		if (Property.IsValid())
		{
			TSharedPtr<IDatasmithKeyValueProperty> Roughness = MaterialElement->GetPropertyByName(TEXT("Roughness"));
			if (!Roughness.IsValid())
			{
				Roughness = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("Roughness"));

				float Value;
				GetFloat(Property, Value);
				FString NewValue = FString::Printf(TEXT("%f"), 1.f - Value);
				Roughness->SetValue(*NewValue);

				MaterialElement->AddProperty(Roughness);
			}
		}
	}

	ConvertProperty(TEXT("RoughnessMapEnable"), TEXT("RoughnessMapFading"));
	ConvertProperty(TEXT("IsMetal"), TEXT("Metallic"));
	ConvertProperty(TEXT("TintEnabled"), TEXT("TintColorFading"));
	ConvertProperty(TEXT("SelfIlluminationMapEnable"), TEXT("SelfIlluminationMapFading"));
	ConvertProperty(TEXT("IsPbr"), TEXT("UseNormalMap"));

	// Set blend mode to translucent if material requires transparency
	if (InDatasmithMaterial->GetMaterialType() == EDatasmithMasterMaterialType::Transparent)
	{
		return TransparentMaterial;
	}
	// Set blend mode to masked if material has cutouts
	else if (InDatasmithMaterial->GetMaterialType() == EDatasmithMasterMaterialType::CutOut)
	{
		return  CutoutMaterial;
	}

	return OpaqueMaterial;
}

void FDatasmithRevitLiveMaterialSelector::FinalizeMaterialInstance(const TSharedPtr<IDatasmithMasterMaterialElement>& InDatasmithMaterial, UMaterialInstanceConstant * MaterialInstance) const
{
}
