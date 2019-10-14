// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith facade.
#include "DatasmithFacadeElement.h"


class DATASMITHFACADE_API FDatasmithFacadeMaterial :
	public FDatasmithFacadeElement
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

	// Possible Datasmith texture modes.
	// Copy of EDatasmithTextureMode from DatasmithCore DatasmithDefinitions.h.
	enum class ETextureMode : uint8
	{
		Diffuse,
		Specular,
		Normal,
		NormalGreenInv,
		Displace,
		Other,
		Bump
	};

public:

	FDatasmithFacadeMaterial(
		const TCHAR* InElementName, // Datasmith element name
		const TCHAR* InElementLabel // Datasmith element label
	);

	virtual ~FDatasmithFacadeMaterial() {}

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
		ETextureMode InTextureMode = ETextureMode::Diffuse // texture mode
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

	// Clear the set of built Datasmith texture names.
	static void ClearBuiltTextureSet();

	// Build a Datasmith material element and add it to the Datasmith scene.
	virtual void BuildScene(
		TSharedRef<IDatasmithScene> IOSceneRef // Datasmith scene
	) override;

private:

	// Set of the built Datasmith texture names.
	static TSet<FString> BuiltTextureSet;

	// Datasmith master material type.
	EMasterMaterialType MasterMaterialType;

	// Array of Datasmith material properties.
	TArray<TSharedPtr<IDatasmithKeyValueProperty>> MaterialPropertyArray;
};
