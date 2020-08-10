// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith facade.
#include "DatasmithFacadeElement.h"

class FDatasmithFacadeScene;
class IDatasmithMetaDataElement;

class DATASMITHFACADE_API FDatasmithFacadeMetaData : public FDatasmithFacadeElement
{
public:	
	enum class EPropertyType
	{
		String,
		Color,
		Float,
		Bool,
		Texture,
		Vector
	};

	FDatasmithFacadeMetaData(
		const TCHAR* InElementName
	);

	// Add a property boolean property to the Datasmith actor.
	void AddPropertyBoolean(
		const TCHAR* InPropertyName,
		bool bInPropertyValue
	);

	// Add a property sRGBA color property to the Datasmith actor.
	void AddPropertyColor(
		const TCHAR*  InPropertyName,
		uint8 InR,
		uint8 InG,
		uint8 InB,
		uint8 InA
	);

	// Add a property float property to the Datasmith actor.
	void AddPropertyFloat(
		const TCHAR* InPropertyName,
		float InPropertyValue
	);

	// Add a property string property to the Datasmith actor.
	void AddPropertyString(
		const TCHAR* InPropertyName,
		const TCHAR* InPropertyValue
	);

	// Add a property texture property to the Datasmith actor.
	void AddPropertyTexture(
		const TCHAR* InPropertyName,
		const TCHAR* InTextureFilePath
	);

	// Add a property vector property to the Datasmith actor.
	void AddPropertyVector(
		const TCHAR* InPropertyName,
		const TCHAR* InPropertyValue
	);

	void AddCustomProperty(
		const TCHAR* InPropertyName,
		const TCHAR* InPropertyValue,
		EPropertyType InPropertyType
	);

	// Get the total amount of properties in this meta data.
	int32 GetPropertiesCount() const;

	// Get the name of the property at the specified Index.
	const TCHAR* GetPropertyName(
		int32 PropertyIndex
	) const;
	
	// Get the value of the property at the specified Index.
	const TCHAR* GetPropertyValue(
		int32 PropertyIndex
	) const;
	
	// Get the property type of the property at the specified Index using the OutPropertyType parameter, returns true if the operation was successful, false otherwise.
	bool GetPropertyType(
		int32 PropertyIndex,
		EPropertyType& OutPropertyType
	) const;
	
	/** Sets the Datasmith element that is associated with this meta data, if any */
	void SetAssociatedElement(
		FDatasmithFacadeElement& Element
	);

#ifdef SWIG_FACADE
protected:
#endif

	FDatasmithFacadeMetaData(
		const TSharedRef<IDatasmithMetaDataElement>& InMetaDataElement
	);

	// Build a Datasmith material element and add it to the Datasmith scene.
	virtual void BuildScene(
		FDatasmithFacadeScene& SceneRef
	) override;

	TSharedRef<IDatasmithMetaDataElement> GetDatasmithMetaDataElement() const;
};