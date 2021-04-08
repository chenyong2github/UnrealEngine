// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

BEGIN_NAMESPACE_UE_AC

class FMetaData
{
  public:
	FMetaData(const GS::Guid& InElementId);

	FMetaData(const GS::Guid& InElementId, const TSharedPtr< IDatasmithActorElement >& InActorElement);

	void SetAssociatedElement(const GS::Guid& /* InElementId */,
							  const TSharedPtr< IDatasmithActorElement >& InActorElement)
	{
		MetaData->SetAssociatedElement(InActorElement);
	}

	void SetOrUpdate(TSharedPtr< IDatasmithMetaDataElement >* IOPtr, IDatasmithScene* IOScene);

	void ExportMetaData();

	const TSharedRef< IDatasmithMetaDataElement >& GetMetaData() const { return MetaData; }

	void AddProperty(const TCHAR* InPropKey, EDatasmithKeyValuePropertyType InPropertyValueType, const TCHAR* InValue);

	void AddProperty(const TCHAR* InPropKey, EDatasmithKeyValuePropertyType InPropertyValueType,
					 const GS::UniString& InValue)
	{
		AddProperty(InPropKey, InPropertyValueType, GSStringToUE(InValue));
	}

	void AddStringProperty(const TCHAR* InPropKey, const TCHAR* InValue)
	{
		AddProperty(InPropKey, EDatasmithKeyValuePropertyType::String, InValue);
	}

	void AddStringProperty(const TCHAR* InPropKey, const GS::UniString& InValue)
	{
		AddProperty(InPropKey, EDatasmithKeyValuePropertyType::String, InValue);
	}

  private:
	void AddMetaDataProperty(API_VariantType variantType, const GS::UniString& PropertyKey,
							 const GS::UniString& PropertyValue);

	void ExportElementIDProperty();

	void ExportClassifications();

	void ExportCategories();

	void ExportIFCProperties();

	void ExportIFCAttributes();

	void ExportProperties();

	GS::UniString GetPropertyValueString(const API_IFCPropertyValue& InIFCPropertyValue);

	GS::UniString GetPropertyValueString(const API_Variant& InVariant);

	const GS::Guid							ElementId;
	TSharedRef< IDatasmithMetaDataElement > MetaData;
};

END_NAMESPACE_UE_AC
