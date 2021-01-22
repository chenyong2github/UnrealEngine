// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeMaterial.h"

#include "DatasmithFacadeKeyValueProperty.h"
#include "DatasmithFacadeScene.h"
#include "DatasmithFacadeUEPbrMaterial.h"

#include "DatasmithDefinitions.h"
#include "DatasmithUtils.h"
#include "Misc/Paths.h"

FDatasmithFacadeBaseMaterial::FDatasmithFacadeBaseMaterial(
	const TSharedRef<IDatasmithBaseMaterialElement>& BaseMaterialElement
) : 
	FDatasmithFacadeElement( BaseMaterialElement )
{
}

FDatasmithFacadeBaseMaterial::EDatasmithMaterialType FDatasmithFacadeBaseMaterial::GetDatasmithMaterialType() const
{
	return GetDatasmithMaterialType(GetDatasmithBaseMaterial());
}

FDatasmithFacadeBaseMaterial::EDatasmithMaterialType FDatasmithFacadeBaseMaterial::GetDatasmithMaterialType(
	const TSharedRef<IDatasmithBaseMaterialElement>& InMaterial
)
{
	if (InMaterial->IsA( EDatasmithElementType::UEPbrMaterial ))
	{
		return EDatasmithMaterialType::UEPbrMaterial;
	}
	else if (InMaterial->IsA(EDatasmithElementType::MasterMaterial))
	{
		return EDatasmithMaterialType::MasterMaterial;
	}

	return EDatasmithMaterialType::Unsupported;
}

TSharedRef<IDatasmithBaseMaterialElement> FDatasmithFacadeBaseMaterial::GetDatasmithBaseMaterial() const
{
	return StaticCastSharedRef<IDatasmithBaseMaterialElement>( InternalDatasmithElement );
}

FDatasmithFacadeBaseMaterial* FDatasmithFacadeBaseMaterial::GetNewFacadeBaseMaterialFromSharedPtr(
	const TSharedPtr<IDatasmithBaseMaterialElement>& InMaterial
)
{
	if (InMaterial)
	{
		TSharedRef<IDatasmithBaseMaterialElement> MaterialRef = InMaterial.ToSharedRef();
		EDatasmithMaterialType MaterialType = GetDatasmithMaterialType(MaterialRef);

		switch (MaterialType)
		{
		case EDatasmithMaterialType::UEPbrMaterial:
			return new FDatasmithFacadeUEPbrMaterial(StaticCastSharedRef<IDatasmithUEPbrMaterialElement>(MaterialRef));
		case EDatasmithMaterialType::MasterMaterial:
			return new FDatasmithFacadeMasterMaterial(StaticCastSharedRef<IDatasmithMasterMaterialElement>(MaterialRef));
		case EDatasmithMaterialType::Unsupported:
		default:
			return nullptr;
		}
	}

	return nullptr;
}

FDatasmithFacadeMasterMaterial::FDatasmithFacadeMasterMaterial(const TCHAR* InElementName) 
	: FDatasmithFacadeBaseMaterial( FDatasmithSceneFactory::CreateMasterMaterial(InElementName))
{
	TSharedPtr<IDatasmithMasterMaterialElement> MasterMaterial = GetDatasmithMasterMaterial();
	MasterMaterial->SetMaterialType(EDatasmithMasterMaterialType::Opaque);
}

FDatasmithFacadeMasterMaterial::FDatasmithFacadeMasterMaterial(const TSharedRef<IDatasmithMasterMaterialElement>& InMaterialRef)
	: FDatasmithFacadeBaseMaterial(InMaterialRef)
{}

FDatasmithFacadeMasterMaterial::EMasterMaterialType FDatasmithFacadeMasterMaterial::GetMaterialType() const
{
	return static_cast<EMasterMaterialType>(GetDatasmithMasterMaterial()->GetMaterialType());
}

void FDatasmithFacadeMasterMaterial::SetMaterialType(
	EMasterMaterialType InMasterMaterialType
)
{
	GetDatasmithMasterMaterial()->SetMaterialType(static_cast<EDatasmithMasterMaterialType>(InMasterMaterialType));
}

FDatasmithFacadeMasterMaterial::EMasterMaterialQuality FDatasmithFacadeMasterMaterial::GetQuality() const
{
	return static_cast<EMasterMaterialQuality>(GetDatasmithMasterMaterial()->GetQuality());
}

void FDatasmithFacadeMasterMaterial::SetQuality(
	EMasterMaterialQuality InQuality
)
{
	GetDatasmithMasterMaterial()->SetQuality(static_cast<EDatasmithMasterMaterialQuality>(InQuality));
}

const TCHAR* FDatasmithFacadeMasterMaterial::GetCustomMaterialPathName() const
{
	return GetDatasmithMasterMaterial()->GetCustomMaterialPathName();
}

void FDatasmithFacadeMasterMaterial::SetCustomMaterialPathName(
	const TCHAR* InPathName
)
{
	GetDatasmithMasterMaterial()->SetCustomMaterialPathName(InPathName);
}

void FDatasmithFacadeMasterMaterial::AddColor(
	const TCHAR*  InPropertyName,
	unsigned char InR,
	unsigned char InG,
	unsigned char InB,
	unsigned char InA
)
{
	// Convert the sRGBA color to a Datasmith linear color.
	FLinearColor LinearColor(FColor(InR, InG, InB, InA));

	// Add the Datasmith material linear color property.
	AddColor(InPropertyName, LinearColor.R, LinearColor.G, LinearColor.B, LinearColor.A);
}

void FDatasmithFacadeMasterMaterial::AddColor(
	const TCHAR* InPropertyName,
	float        InR,
	float        InG,
	float        InB,
	float        InA
)
{
	FLinearColor LinearColor(InR, InG, InB, InA);

	// Create a new Datasmith material color property.
	TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Color);
	MaterialPropertyPtr->SetValue(*LinearColor.ToString());

	// Add the new property to the Datasmith material properties.
	GetDatasmithMasterMaterial()->AddProperty(MaterialPropertyPtr);
}

void FDatasmithFacadeMasterMaterial::AddTexture(
	const TCHAR* InPropertyName,
	const FDatasmithFacadeTexture* InTexture
)
{
	if (InTexture)
	{
		// Create a new Datasmith material texture property.
		TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
		MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Texture);
		MaterialPropertyPtr->SetValue(InTexture->GetName());

		// Add the new property to the Datasmith material properties.
		GetDatasmithMasterMaterial()->AddProperty(MaterialPropertyPtr);
	}
}

void FDatasmithFacadeMasterMaterial::AddString(
	const TCHAR* InPropertyName,
	const TCHAR* InPropertyValue
)
{
	if (!FString(InPropertyValue).IsEmpty())
	{
		// Create a new Datasmith material string property.
		TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
		MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::String);
		MaterialPropertyPtr->SetValue(InPropertyValue);

		// Add the new property to the array of Datasmith material properties.
		GetDatasmithMasterMaterial()->AddProperty(MaterialPropertyPtr);
	}
}

void FDatasmithFacadeMasterMaterial::AddFloat(
	const TCHAR* InPropertyName,
	float        InPropertyValue
)
{
	// Create a new Datasmith material float property.
	TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Float);
	MaterialPropertyPtr->SetValue(*FString::Printf(TEXT("%f"), InPropertyValue));

	// Add the new property to the Datasmith material properties.
	GetDatasmithMasterMaterial()->AddProperty(MaterialPropertyPtr);
}

void FDatasmithFacadeMasterMaterial::AddBoolean(
	const TCHAR* InPropertyName,
	bool         bInPropertyValue
)
{
	// Create a new Datasmith material boolean property.
	TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Bool);
	MaterialPropertyPtr->SetValue(bInPropertyValue ? TEXT("True") : TEXT("False"));

	// Add the new property to the Datasmith material properties.
	GetDatasmithMasterMaterial()->AddProperty(MaterialPropertyPtr);
}

int32 FDatasmithFacadeMasterMaterial::GetPropertiesCount() const
{
	return GetDatasmithMasterMaterial()->GetPropertiesCount();
}

FDatasmithFacadeKeyValueProperty* FDatasmithFacadeMasterMaterial::GetNewProperty(
	int32 PropertyIndex
) const
{
	if (const TSharedPtr<IDatasmithKeyValueProperty>& Property = GetDatasmithMasterMaterial()->GetProperty(PropertyIndex))
	{
		return new FDatasmithFacadeKeyValueProperty(Property.ToSharedRef());
	}
	else
	{
		return nullptr;
	}
}

FDatasmithFacadeKeyValueProperty* FDatasmithFacadeMasterMaterial::GetNewPropertyByName(
	const TCHAR* PropertyName
) const
{
	if (const TSharedPtr<IDatasmithKeyValueProperty>& Property = GetDatasmithMasterMaterial()->GetPropertyByName(PropertyName))
	{
		return new FDatasmithFacadeKeyValueProperty(Property.ToSharedRef());
	}
	else
	{
		return nullptr;
	}
}

TSharedRef<IDatasmithMasterMaterialElement> FDatasmithFacadeMasterMaterial::GetDatasmithMasterMaterial() const
{
	return StaticCastSharedRef<IDatasmithMasterMaterialElement>( InternalDatasmithElement );
}