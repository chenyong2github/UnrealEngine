// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeMaterial.h"

#include "DatasmithFacadeScene.h"

#include "DatasmithDefinitions.h"
#include "DatasmithUtils.h"
#include "Misc/Paths.h"

FDatasmithFacadeBaseMaterial::FDatasmithFacadeBaseMaterial(
	const TSharedRef<IDatasmithBaseMaterialElement>& BaseMaterialElement
) : 
	FDatasmithFacadeElement( BaseMaterialElement )
{
}

void FDatasmithFacadeBaseMaterial::BuildScene(
	FDatasmithFacadeScene& SceneRef
)
{
	DatasmithSceneRef = SceneRef.GetScene();
	ExportedTextureSetRef = SceneRef.GetExportedTextures();

	for ( const TextureInfo& TextureAssetInfo : ReferencedTextureAssets )
	{
		// Add the referenced texture assets to the scene.
		AddTextureElementToScene( TextureAssetInfo, SceneRef.GetScene(), SceneRef.GetExportedTextures() );
	}

	// Add the master material to the Datasmith scene.
	SceneRef.GetScene()->AddMaterial( GetDatasmithBaseMaterial() );
}

TSharedRef<IDatasmithBaseMaterialElement> FDatasmithFacadeBaseMaterial::GetDatasmithBaseMaterial()
{
	return StaticCastSharedRef<IDatasmithBaseMaterialElement>( InternalDatasmithElement );
}

TSharedRef<const IDatasmithBaseMaterialElement> FDatasmithFacadeBaseMaterial::GetDatasmithBaseMaterial() const
{
	return StaticCastSharedRef<IDatasmithBaseMaterialElement>( InternalDatasmithElement );
}

void FDatasmithFacadeBaseMaterial::AddTextureReference(
	const FString& InTextureName,
	const FString& InTextureFilePath,
	FDatasmithFacadeTexture::ETextureMode InTextureMode
)
{
	const int TextureInfoIndex = ReferencedTextureAssets.Emplace( InTextureName, InTextureFilePath, InTextureMode );

	TSharedPtr<TSet<FString>> UniqueTextureNames = ExportedTextureSetRef.Pin();
	TSharedPtr<IDatasmithScene> DatasmithScene = DatasmithSceneRef.Pin();

	//If the material was already added to the scene, add the new texture reference as well.
	if ( UniqueTextureNames.IsValid() && DatasmithScene.IsValid() )
	{
		AddTextureElementToScene( ReferencedTextureAssets[TextureInfoIndex], DatasmithScene, UniqueTextureNames );
	}
}

void FDatasmithFacadeBaseMaterial::AddTextureElementToScene(
	const TextureInfo& TextureAssetInfo,
	const TSharedPtr<IDatasmithScene>& DatasmithScene,
	const TSharedPtr<TSet<FString>>& ExportedTextureSet
)
{
	if ( ExportedTextureSet->Contains( TextureAssetInfo.TextureName ) )
	{
		return;
	}

	// Create a Datasmith texture element.
	TSharedPtr<IDatasmithTextureElement> TexturePtr = FDatasmithSceneFactory::CreateTexture( *TextureAssetInfo.TextureName );

	// Set the texture label used in the Unreal UI.
	TexturePtr->SetLabel( *FPaths::GetBaseFilename( TextureAssetInfo.TextureFilePath ) );

	// Set the Datasmith texture mode.
	TexturePtr->SetTextureMode( static_cast<EDatasmithTextureMode>(TextureAssetInfo.TextureMode) );

	// Set the Datasmith texture file path.
	TexturePtr->SetFile( *TextureAssetInfo.TextureFilePath );

	// Add the texture to the Datasmith scene.
	DatasmithScene->AddTexture( TexturePtr );

	// Keep track of the built Datasmith texture.
	ExportedTextureSet->Add( TextureAssetInfo.TextureName );
}

FDatasmithFacadeMasterMaterial::FDatasmithFacadeMasterMaterial(	const TCHAR* InElementName) 
	:	FDatasmithFacadeBaseMaterial( FDatasmithSceneFactory::CreateMasterMaterial( InElementName ) )
{
	TSharedPtr<IDatasmithMasterMaterialElement> MasterMaterial = GetDatasmithMasterMaterial();
	MasterMaterial->SetMaterialType( EDatasmithMasterMaterialType::Opaque );
}

void FDatasmithFacadeMasterMaterial::SetMasterMaterialType(
	EMasterMaterialType InMasterMaterialType
)
{
	switch ( InMasterMaterialType )
	{
	case EMasterMaterialType::Opaque:
		GetDatasmithMasterMaterial()->SetMaterialType( EDatasmithMasterMaterialType::Opaque );
		break;
	case EMasterMaterialType::Transparent:
		GetDatasmithMasterMaterial()->SetMaterialType( EDatasmithMasterMaterialType::Transparent );
		break;
	case EMasterMaterialType::CutOut:
		GetDatasmithMasterMaterial()->SetMaterialType( EDatasmithMasterMaterialType::CutOut );
		break;
	}
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
	const TCHAR* InTextureFilePath,
	FDatasmithFacadeTexture::ETextureMode InTextureMode
)
{
	if (!FString(InTextureFilePath).IsEmpty())
	{
		// Make Datasmith texture name from file name.
		FString FileName = FDatasmithUtils::SanitizeObjectName(FPaths::GetBaseFilename(InTextureFilePath));
		FString TextureName = FString::Printf(TEXT("%ls_%d"), *FileName, int(InTextureMode));

		// Create a new Datasmith material texture property.
		TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
		MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Texture);
		MaterialPropertyPtr->SetValue(*TextureName);

		// Add the new property to the Datasmith material properties.
		GetDatasmithMasterMaterial()->AddProperty(MaterialPropertyPtr);

		AddTextureReference(TextureName, InTextureFilePath, InTextureMode);
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

TSharedRef<IDatasmithMasterMaterialElement> FDatasmithFacadeMasterMaterial::GetDatasmithMasterMaterial() const
{
	return StaticCastSharedRef<IDatasmithMasterMaterialElement>( InternalDatasmithElement );
}