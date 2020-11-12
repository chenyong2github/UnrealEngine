// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith facade.
#include "DatasmithFacadeElement.h"
#include "DatasmithFacadeTexture.h"

class FDatasmithFacadeKeyValueProperty;

class DATASMITHFACADE_API FDatasmithFacadeBaseMaterial :
	public FDatasmithFacadeElement
{
public:
	enum class EDatasmithMaterialType
	{
		MasterMaterial,
		UEPbrMaterial,
		Unsupported,
	};

	EDatasmithMaterialType GetDatasmithMaterialType() const;

#ifdef SWIG_FACADE
protected:
#endif
	FDatasmithFacadeBaseMaterial(
		const TSharedRef<IDatasmithBaseMaterialElement>& BaseMaterialElement 
	);

	static EDatasmithMaterialType GetDatasmithMaterialType(
		const TSharedRef<IDatasmithBaseMaterialElement>& InMaterial
	);

	TSharedRef<IDatasmithBaseMaterialElement> GetDatasmithBaseMaterial() const;

	static FDatasmithFacadeBaseMaterial* GetNewFacadeBaseMaterialFromSharedPtr(
		const TSharedPtr<IDatasmithBaseMaterialElement>& InMaterial
	);
};

class DATASMITHFACADE_API FDatasmithFacadeMasterMaterial :
	public FDatasmithFacadeBaseMaterial
{
	friend class FDatasmithFacadeScene;

public:

	// Possible Datasmith master material types, from EDatasmithMasterMaterialType in DatasmithDefinitions.h
	enum class EMasterMaterialType
	{
		Auto,
		Opaque,
		Transparent,
		ClearCoat,
		/** Instantiate a master material from a specified one */
		Custom,
		/** Material has a transparent cutout map */
		CutOut,
		/** Dummy element to count the number of types */
		Count
	};

	enum class EMasterMaterialQuality : uint8
	{
		High,
		Low,
		/** Dummy element to count the number of qualities */
		Count
	};

public:

	FDatasmithFacadeMasterMaterial(
		const TCHAR* InElementName // Datasmith element name
	);

	virtual ~FDatasmithFacadeMasterMaterial() {}

	/** Get the Datasmith master material type. */
	EMasterMaterialType GetMaterialType() const;

	/** Set the Datasmith master material type. */
	void SetMaterialType(
		EMasterMaterialType InMasterMaterialType // master material type
	);

	/** Get the Datasmith master material quality. */
	EMasterMaterialQuality GetQuality() const;

	/** Set the Datasmith master material quality. */
	void SetQuality(
		EMasterMaterialQuality InQuality
	);

	/** Get the material path name used when master material type is set to Custom */
	const TCHAR* GetCustomMaterialPathName() const;
	
	/** Set the material path name used when master material type is set to Custom */
	void SetCustomMaterialPathName(
		const TCHAR* InPathName
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
		const TCHAR* InPropertyName,             // texture property name
		const FDatasmithFacadeTexture* InTexture // texture file path
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

	int32 GetPropertiesCount() const;

	/** Returns a new FDatasmithFacadeKeyValueProperty pointing to the KeyValueProperty at the given index, the returned value must be deleted after used, can be nullptr. */
	FDatasmithFacadeKeyValueProperty* GetNewProperty(
		int32 PropertyIndex
	) const;

	/** Returns a new FDatasmithFacadeKeyValueProperty pointing to the KeyValueProperty with the given name, the returned value must be deleted after used, can be nullptr. */
	FDatasmithFacadeKeyValueProperty* GetNewPropertyByName(
		const TCHAR* PropertyName
	) const;

#ifdef SWIG_FACADE
protected:
#endif

	FDatasmithFacadeMasterMaterial(
		const TSharedRef<IDatasmithMasterMaterialElement>& InMaterialRef // Datasmith master material element
	);

	TSharedRef<IDatasmithMasterMaterialElement> GetDatasmithMasterMaterial() const;
};
