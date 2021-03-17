// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaData.h"
#include "ElementTools.h"

BEGIN_NAMESPACE_UE_AC

static const GS::UniString StringTrue("True");
static const GS::UniString StringFalse("False");
static const GS::UniString StringUndefined("Undefined");

FMetaData::FMetaData(const GS::Guid& InElementId)
	: ElementId(InElementId)
	, MetaData(FDatasmithSceneFactory::CreateMetaData(TEXT("")))
{
	MetaData->SetName(*FString::Printf(TEXT("MetaData_%s"), GSStringToUE(ElementId.ToUniString())));
}

void FMetaData::ExportMetaData()
{
	ExportElementIDProperty();
	ExportClassifications();
	ExportCategories();
	ExportIFCProperties();
	ExportIFCAttributes();
}

void FMetaData::AddMetaDataProperty(API_VariantType variantType, const GS::UniString& PropertyKey,
									const GS::UniString& PropertyValue)
{
	static const GS::UniString StringUndefinedLocalized(GetGSName(kName_Undefined));

	if (PropertyValue.IsEmpty() || PropertyValue.IsEqual(StringUndefined, GS::UniString::CaseInsensitive) ||
		PropertyValue.IsEqual(StringUndefinedLocalized, GS::UniString::CaseInsensitive))
		return;

	EDatasmithKeyValuePropertyType PropertyValueType; // = EDatasmithKeyValuePropertyType::String;
	switch (variantType)
	{
		case API_PropertyIntegerValueType:
			PropertyValueType = EDatasmithKeyValuePropertyType::Float;
			break;
		case API_PropertyRealValueType:
			PropertyValueType = EDatasmithKeyValuePropertyType::Float;
			break;
		case API_PropertyStringValueType:
			PropertyValueType = EDatasmithKeyValuePropertyType::String;
			break;
		case API_PropertyBooleanValueType:
			PropertyValueType = EDatasmithKeyValuePropertyType::Bool;
			break;
		case API_PropertyGuidValueType:
			PropertyValueType = EDatasmithKeyValuePropertyType::String;
			break;
		default:
			PropertyValueType = EDatasmithKeyValuePropertyType::String;
			break;
	}

	TSharedRef< IDatasmithKeyValueProperty > metaDataProperty =
		FDatasmithSceneFactory::CreateKeyValueProperty(GSStringToUE(PropertyKey));
	metaDataProperty->SetValue(GSStringToUE(PropertyValue));
	metaDataProperty->SetPropertyType(PropertyValueType);

	MetaData->AddProperty(metaDataProperty);
}

void FMetaData::ExportElementIDProperty()
{
	API_ElementMemo Memo;
	Zap(&Memo);
	GSErrCode GSErr = ACAPI_Element_GetMemo(GSGuid2APIGuid(ElementId), &Memo, APIMemoMask_All);
	if (GSErr == NoError && Memo.elemInfoString != nullptr)
	{
		AddMetaDataProperty(API_PropertyStringValueType, "ID", *Memo.elemInfoString);
	}
}

void FMetaData::ExportClassifications()
{
	GS::Array< GS::Pair< API_ClassificationSystem, API_ClassificationItem > > ApiClassifications;
	GSErrCode GSErr = FElementTools::GetElementClassifications(ApiClassifications, GSGuid2APIGuid(ElementId));

	if (GSErr == NoError)
	{
		for (const GS::Pair< API_ClassificationSystem, API_ClassificationItem >& Classification : ApiClassifications)
		{
			AddMetaDataProperty(API_PropertyStringValueType, Classification.first.name + "_ID",
								Classification.second.id);
			AddMetaDataProperty(API_PropertyStringValueType, Classification.first.name + "_Name",
								Classification.second.name);
			AddMetaDataProperty(API_PropertyStringValueType, Classification.first.name + "_Description",
								Classification.second.description);
		}
	}
	else
	{
		UE_AC_DebugF("FMetaData::ExportClassifications - FElementTools::GetElementClassifications returned error %d\n",
					 GSErr);
	}
}

void FMetaData::ExportCategories()
{
	static const GS::UniString StringCategory("CAT_");

	GS::Array< API_ElemCategory > CategoryList;
	GSErrCode					  GSErr = ACAPI_Database(APIDb_GetElementCategoriesID, &CategoryList);
	if (GSErr == NoError)
	{
		for (const API_ElemCategory& Category : CategoryList)
		{
			API_ElemCategoryValue ElemCategoryValue;
			GSErr = ACAPI_Element_GetCategoryValue(GSGuid2APIGuid(ElementId), Category, &ElemCategoryValue);
			if (GSErr == NoError)
			{
				AddMetaDataProperty(API_PropertyStringValueType,
									StringCategory + GS::UniString(ElemCategoryValue.category.name),
									GS::UniString(ElemCategoryValue.name));
			}
			else
			{
				if (GSErr != APIERR_BADPARS)
				{
					UE_AC_DebugF("FMetaData::ExportCategories - ACAPI_Element_GetCategoryValue returned error %d\n",
								 GSErr);
				}
			}
		}
	}
	else
	{
		UE_AC_DebugF("FMetaData::ExportCategories - APIDb_GetElementCategoriesID returned error %d\n", GSErr);
	}
}

void FMetaData::ExportIFCProperties()
{
	static const GS::UniString StringIFC("IFC_");
	static const GS::UniString StringLower("_lower");
	static const GS::UniString StringUpper("_upper");

	GS::Array< API_IFCProperty > IFCProperties;
	GSErrCode GSErr = ACAPI_Element_GetIFCProperties(GSGuid2APIGuid(ElementId), false, &IFCProperties);
	if (GSErr == NoError)
	{
		for (const API_IFCProperty& IFCProperty : IFCProperties)
		{
			GS::UniString KeyName = StringIFC + IFCProperty.head.propertyName;
			GS::UniString ValueName;
			switch (IFCProperty.head.propertyType)
			{
				case API_IFCPropertySingleValueType:
					ValueName = GetPropertyValueString(IFCProperty.singleValue.nominalValue);
					AddMetaDataProperty(API_PropertyStringValueType, KeyName, ValueName);
					break;
				case API_IFCPropertyListValueType:
					{
						Int32 i = 0;
						for (API_IFCPropertyValue value : IFCProperty.listValue.listValues)
						{
							ValueName = GetPropertyValueString(value);
							AddMetaDataProperty(API_PropertyStringValueType, KeyName + '_' + GS::ValueToUniString(++i),
												ValueName);
						}
						break;
					}
				case API_IFCPropertyBoundedValueType:
					ValueName = GetPropertyValueString(IFCProperty.boundedValue.lowerBoundValue);
					AddMetaDataProperty(API_PropertyStringValueType, KeyName + "_" + StringLower, ValueName);
					ValueName = GetPropertyValueString(IFCProperty.boundedValue.upperBoundValue);
					AddMetaDataProperty(API_PropertyStringValueType, KeyName + "_" + StringUpper, ValueName);
					break;
				case API_IFCPropertyEnumeratedValueType:
					{
						Int32 i = 0;
						for (API_IFCPropertyValue value : IFCProperty.enumeratedValue.enumerationValues)
						{
							ValueName = GetPropertyValueString(value);
							AddMetaDataProperty(API_PropertyStringValueType, KeyName + '_' + GS::ValueToUniString(++i),
												ValueName);
						}
						break;
					}
				case API_IFCPropertyTableValueType:
					{
						Int32 i = 0;
						for (API_IFCPropertyValue DefiningValue : IFCProperty.tableValue.definingValues)
						{
							ValueName = GetPropertyValueString(IFCProperty.tableValue.definedValues[i++]);
							AddMetaDataProperty(API_PropertyStringValueType,
												KeyName + '_' + GetPropertyValueString(DefiningValue), ValueName);
						}
						break;
					}
				default:
					break;
			}
		}
	}
	else
	{
		if (GSErr != APIERR_BADPARS)
		{
			UE_AC_DebugF("FMetaData::ExportIFCProperties - ACAPI_Element_GetIFCProperties returned error %d\n", GSErr);
		}
	}
}

void FMetaData::ExportIFCAttributes()
{
	static const GS::UniString StringIFCAttribute("IFC_Attribute_");

	GS::Array< API_IFCAttribute > IFCAttributes;
	GSErrCode GSErr = ACAPI_Element_GetIFCAttributes(GSGuid2APIGuid(ElementId), false, &IFCAttributes);
	if (GSErr == NoError)
	{
		for (const API_IFCAttribute& IfcAttribute : IFCAttributes)
		{
			AddMetaDataProperty(API_PropertyStringValueType, StringIFCAttribute + IfcAttribute.attributeName,
								IfcAttribute.attributeValue);
		}
	}
	else
	{
		if (GSErr != APIERR_BADPARS)
		{
			UE_AC_DebugF("FMetaData::ExportIFCAttributes - ACAPI_Element_GetIFCAttributes returned error %d\n", GSErr);
		}
	}
}

void FMetaData::ExportProperties()
{
	static const GS::UniString StringProperty("PROP_");

	GS::Array< API_Property > Properties;

	GSErrCode GSErr = FElementTools::GetElementProperties(Properties, GSGuid2APIGuid(ElementId));
	if (GSErr == NoError)
	{
		for (const API_Property& Property : Properties)
		{
			GS::UniString PropertyKey = StringProperty + Property.definition.name;

			GS::UniString PropertyValue;

			switch (Property.definition.collectionType)
			{
				case API_PropertySingleCollectionType:
					PropertyValue = GetPropertyValueString(Property.value.singleVariant.variant);
					AddMetaDataProperty(Property.definition.valueType, PropertyKey, PropertyValue);
					break;
				case API_PropertyListCollectionType:
					{
						Int32 i = 0;
						for (API_Variant variant : Property.value.listVariant.variants)
						{
							PropertyValue = GetPropertyValueString(variant);
							AddMetaDataProperty(Property.definition.valueType,
												PropertyKey + '_' + GS::ValueToUniString(++i), PropertyValue);
						}
						break;
					}
				case API_PropertySingleChoiceEnumerationCollectionType:
					PropertyValue = GetPropertyValueString(Property.value.singleEnumVariant.displayVariant);
					AddMetaDataProperty(Property.definition.valueType, PropertyKey, PropertyValue);
					break;
				case API_PropertyMultipleChoiceEnumerationCollectionType:
					{
						Int32 i = 0;
						for (API_SingleEnumerationVariant enumVariant : Property.value.multipleEnumVariant.variants)
						{
							PropertyValue = GetPropertyValueString(enumVariant.displayVariant);
							AddMetaDataProperty(Property.definition.valueType,
												PropertyKey + '_' + GS::ValueToUniString(++i), PropertyValue);
						}
						break;
					}
				case API_PropertyUndefinedCollectionType:
					PropertyValue = StringUndefined;
					break;
				default:
					PropertyValue = "";
					break;
			}
		}
	}
	else
	{
		UE_AC_DebugF("FMetaData::ExportProperties - FElementTools::GetElementProperties returned error %d\n", GSErr);
	}
}

GS::UniString FMetaData::GetPropertyValueString(const API_IFCPropertyValue& InIFCPropertyValue)
{
	GS::UniString PropertyValue = "";
	switch (InIFCPropertyValue.value.primitiveType)
	{
		case API_IFCPropertyAnyValueIntegerType:
			if (InIFCPropertyValue.value.intValue != 0)
				PropertyValue = GS::ValueToUniString(InIFCPropertyValue.value.intValue);
			break;
		case API_IFCPropertyAnyValueRealType:
			if (InIFCPropertyValue.value.doubleValue != 0)
				PropertyValue = GS::ValueToUniString(InIFCPropertyValue.value.doubleValue);
			break;
		case API_IFCPropertyAnyValueBooleanType:
			PropertyValue = InIFCPropertyValue.value.boolValue ? StringTrue : StringFalse;
			break;
		case API_IFCPropertyAnyValueLogicalType:
			PropertyValue = InIFCPropertyValue.value.boolValue ? StringTrue : StringFalse;
			break;
		case API_IFCPropertyAnyValueStringType:
			PropertyValue = InIFCPropertyValue.value.stringValue;
			break;
		default:
			// PropertyValue = "";
			break;
	}

	return PropertyValue;
}

GS::UniString FMetaData::GetPropertyValueString(const API_Variant& InVariant)
{
	GS::UniString PropertyValue = "";
	switch (InVariant.type)
	{
		case API_PropertyIntegerValueType:
			if (InVariant.intValue != 0)
				PropertyValue = GS::ValueToUniString(InVariant.intValue);
			break;
		case API_PropertyRealValueType:
			if (InVariant.doubleValue != 0.0)
				PropertyValue = GS::ValueToUniString(InVariant.doubleValue);
			break;
		case API_PropertyStringValueType:
			PropertyValue = InVariant.uniStringValue;
			break;
		case API_PropertyBooleanValueType:
			PropertyValue = InVariant.boolValue ? StringTrue : StringFalse;
			break;
		case API_PropertyGuidValueType:
			PropertyValue = APIGuidToString(InVariant.guidValue);
			break;
		default:
			PropertyValue = InVariant.uniStringValue;
			break;
	}

	return PropertyValue;
}

END_NAMESPACE_UE_AC
