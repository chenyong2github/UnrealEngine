// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith facade.
#include "DatasmithFacadeElement.h"
#include "DatasmithFacadeTexture.h"

class DATASMITHFACADE_API FDatasmithFacadeBaseMaterial :
	public FDatasmithFacadeElement
{
public:
	enum class EFacadeMaterialType
	{
		MasterMaterial,
		UEPbrMaterial,
	};

	virtual EFacadeMaterialType GetMaterialType() = 0;

#ifdef SWIG_FACADE
protected:
#endif

	// Build a Datasmith material element and add it to the Datasmith scene.
	virtual void BuildScene( FDatasmithFacadeScene& SceneRef ) override;

	virtual TSharedRef<IDatasmithBaseMaterialElement> GetDatasmithBaseMaterial();

	virtual TSharedRef<const IDatasmithBaseMaterialElement> GetDatasmithBaseMaterial() const;

protected:

	FDatasmithFacadeBaseMaterial(
		const TSharedRef<IDatasmithBaseMaterialElement>& BaseMaterialElement 
	);

	void AddTextureReference( const FString& InTextureName, const FString& InTextureFilePath, FDatasmithFacadeTexture::ETextureMode InTextureMode );

private:

	struct TextureInfo
	{
		FString TextureName;
		FString TextureFilePath;
		FDatasmithFacadeTexture::ETextureMode TextureMode;

		TextureInfo( const FString& InTextureName, const FString& InTextureFilePath, FDatasmithFacadeTexture::ETextureMode InTextureMode )
			: TextureName( InTextureName )
			, TextureFilePath( InTextureFilePath )
			, TextureMode( InTextureMode )
		{}
	};

	// Add a the given texture to the DatasmithScene if there is no other texture with the same name.
	void AddTextureElementToScene(
		const TextureInfo& TextureAssetInfo,
		const TSharedPtr<IDatasmithScene>& DatasmithScene,
		const TSharedPtr<TSet<FString>>& ExportedTextureSet
	);

	// Array of info on this material's textures.
	TArray<TextureInfo> ReferencedTextureAssets;

	// A weak reference to the TSet holding all the texture names added to the current DatasmithScene.
	// Used to make sure we don't add the same texture twice to the scene.
	TWeakPtr<TSet<FString>> ExportedTextureSetRef;
	
	// A weak reference to the current DatasmithScene. Used to add new texture elements to the scene when adding a texture to the material.
	TWeakPtr<IDatasmithScene> DatasmithSceneRef;
};

class DATASMITHFACADE_API FDatasmithFacadeMasterMaterial :
	public FDatasmithFacadeBaseMaterial
{
	friend class FDatasmithFacadeScene;

public:

	// Possible Datasmith master material types.
	enum class EMasterMaterialType
	{
		Opaque,
		Transparent,
		CutOut
	};

public:

	FDatasmithFacadeMasterMaterial(
		const TCHAR* InElementName // Datasmith element name
	);

	virtual ~FDatasmithFacadeMasterMaterial() {}

	virtual EFacadeMaterialType GetMaterialType() override { return EFacadeMaterialType::MasterMaterial; }

	// Set the Datasmith master material type.
	void SetMasterMaterialType(
		EMasterMaterialType InMasterMaterialType // master material type
	);

	// Add a Datasmith material sRGBA color property.
	void AddColor(
		const TCHAR*  InPropertyName, // color property name
		unsigned char InR,            // red
		unsigned char InG,            // green
		unsigned char InB,            // blue
		unsigned char InA             // alpha
	);

	// Add a Datasmith material linear color property.
	void AddColor(
		const TCHAR* InPropertyName, // color property name
		float        InR,            // red
		float        InG,            // green
		float        InB,            // blue
		float        InA             // alpha
	);

	// Add a Datasmith material texture property.
	void AddTexture(
		const TCHAR* InPropertyName,                       // texture property name
		const TCHAR* InTextureFilePath,                    // texture file path
		FDatasmithFacadeTexture::ETextureMode InTextureMode = FDatasmithFacadeTexture::ETextureMode::Diffuse // texture mode
	);

	// Add a Datasmith material string property.
	void AddString(
		const TCHAR* InPropertyName, // property name
		const TCHAR* InPropertyValue // property value
	);

	// Add a Datasmith material float property.
	void AddFloat(
		const TCHAR* InPropertyName, // property name
		float        InPropertyValue // property value
	);

	// Add a Datasmith material boolean property.
	void AddBoolean(
		const TCHAR* InPropertyName,  // property name
		bool         bInPropertyValue // property value
	);

#ifdef SWIG_FACADE
protected:
#endif

	TSharedRef<IDatasmithMasterMaterialElement> GetDatasmithMasterMaterial() const;
};
