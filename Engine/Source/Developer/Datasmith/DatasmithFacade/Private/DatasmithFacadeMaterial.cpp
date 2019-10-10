// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeMaterial.h"

#include "Misc/Paths.h"

TSet<FString> FDatasmithFacadeMaterial::BuiltTextureSet;

FDatasmithFacadeMaterial::FDatasmithFacadeMaterial(
	const TCHAR* InElementName,
	const TCHAR* InElementLabel
) :
	FDatasmithFacadeElement(InElementName, InElementLabel),
	MasterMaterialType(EMasterMaterialType::Opaque)
{
}

void FDatasmithFacadeMaterial::SetMasterMaterialType(
	EMasterMaterialType InMasterMaterialType
)
{
	MasterMaterialType = InMasterMaterialType;
}

void FDatasmithFacadeMaterial::AddColor(
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

void FDatasmithFacadeMaterial::AddColor(
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

	// Add the new property to the array of Datasmith material properties.
	MaterialPropertyArray.Add(MaterialPropertyPtr);
}

void FDatasmithFacadeMaterial::AddTexture(
	const TCHAR* InPropertyName,
	const TCHAR* InTextureFilePath,
	ETextureMode InTextureMode
)
{
	if (!FString(InTextureFilePath).IsEmpty())
	{
		// Make a unique Datasmith texture name.
		FString TextureName = FString::Printf(TEXT("%ls_%d"), *FMD5::HashAnsiString(InTextureFilePath), int(InTextureMode));

		// Create a new Datasmith material texture property.
		TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
		MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Texture);
		MaterialPropertyPtr->SetValue(*TextureName);

		// Add the new property to the array of Datasmith material properties.
		MaterialPropertyArray.Add(MaterialPropertyPtr);

		// Create a transient Datasmith material property to store the texture element data.
		FString TextureElementData = FString::Printf(TEXT("%ls;%d;%ls"), *TextureName, int(InTextureMode), InTextureFilePath);
		MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(TEXT("TextureElementData"));
		MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::String);
		MaterialPropertyPtr->SetValue(*TextureElementData);

		// Add the transient property to the array of Datasmith material properties.
		MaterialPropertyArray.Add(MaterialPropertyPtr);
	}
}

void FDatasmithFacadeMaterial::AddString(
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
		MaterialPropertyArray.Add(MaterialPropertyPtr);
	}
}

void FDatasmithFacadeMaterial::AddFloat(
	const TCHAR* InPropertyName,
	float        InPropertyValue
)
{
	// Create a new Datasmith material float property.
	TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Float);
	MaterialPropertyPtr->SetValue(*FString::Printf(TEXT("%f"), InPropertyValue));

	// Add the new property to the array of Datasmith material properties.
	MaterialPropertyArray.Add(MaterialPropertyPtr);
}

void FDatasmithFacadeMaterial::AddBoolean(
	const TCHAR* InPropertyName,
	bool         bInPropertyValue
)
{
	// Create a new Datasmith material boolean property.
	TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Bool);
	MaterialPropertyPtr->SetValue(bInPropertyValue ? TEXT("True") : TEXT("False"));

	// Add the new property to the array of Datasmith material properties.
	MaterialPropertyArray.Add(MaterialPropertyPtr);
}

void FDatasmithFacadeMaterial::ClearBuiltTextureSet()
{
	// Remove all elements from the set of built Datasmith texture names.
	BuiltTextureSet.Empty();
}

void FDatasmithFacadeMaterial::BuildScene(
	TSharedRef<IDatasmithScene> IOSceneRef
)
{
	// Create a Datasmith master material element.
	TSharedPtr<IDatasmithMasterMaterialElement> MaterialPtr = FDatasmithSceneFactory::CreateMasterMaterial(*ElementName);

	// Set the master material label used in the Unreal UI.
	MaterialPtr->SetLabel(*ElementLabel);

	// Set the Datasmith master material type.
	switch (MasterMaterialType)
	{
		case EMasterMaterialType::Opaque:
		{
			MaterialPtr->SetMaterialType(EDatasmithMasterMaterialType::Opaque);
			break;
		}
		case EMasterMaterialType::Transparent:
		{
			MaterialPtr->SetMaterialType(EDatasmithMasterMaterialType::Transparent);
			break;
		}
		case EMasterMaterialType::CutOut:
		{
			MaterialPtr->SetMaterialType(EDatasmithMasterMaterialType::CutOut);
			break;
		}
	}

	for (TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr : MaterialPropertyArray)
	{
		FString MaterialPropertyName = MaterialPropertyPtr->GetName();

		if (MaterialPropertyName == TEXT("TextureElementData"))
		{
			FString MaterialPropertyValue = MaterialPropertyPtr->GetValue();

			// Retrieve the texture element data from the transient Datasmith material property.
			TArray<FString> TextureElementData;
			MaterialPropertyValue.ParseIntoArray(TextureElementData, TEXT(";"), false);

			FString               TextureName     = TextureElementData[0];
			EDatasmithTextureMode TextureMode     = EDatasmithTextureMode(FCString::Atoi(*TextureElementData[1]));
			FString               TextureFilePath = TextureElementData[2];

			if (!BuiltTextureSet.Contains(TextureName))
			{
				// Create a Datasmith texture element.
				TSharedPtr<IDatasmithTextureElement> TexturePtr = FDatasmithSceneFactory::CreateTexture(*TextureName);

				// Set the texture label used in the Unreal UI.
				TexturePtr->SetLabel(*FPaths::GetBaseFilename(TextureFilePath));

				// Set the Datasmith texture mode.
				TexturePtr->SetTextureMode(TextureMode);

				// Set the Datasmith texture file path.
				TexturePtr->SetFile(*TextureFilePath);

				// Add the texture to the Datasmith scene.
				IOSceneRef->AddTexture(TexturePtr);

				// Keep track of the built Datasmith texture.
				BuiltTextureSet.Add(TextureName);
			}
		}
		else
		{
			// Add the material property to the Datasmith master material.
			MaterialPtr->AddProperty(MaterialPropertyPtr);
		}
	}

	// Add the master material to the Datasmith scene.
	IOSceneRef->AddMaterial(MaterialPtr);
}
