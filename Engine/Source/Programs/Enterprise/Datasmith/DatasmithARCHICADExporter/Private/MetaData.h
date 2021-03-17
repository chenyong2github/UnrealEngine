// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

BEGIN_NAMESPACE_UE_AC

class FMetaData
{
  public:
	FMetaData(const GS::Guid& InElementId);

	void ExportMetaData();

	const TSharedRef< IDatasmithMetaDataElement >& GetMetaData() const { return MetaData; }

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
