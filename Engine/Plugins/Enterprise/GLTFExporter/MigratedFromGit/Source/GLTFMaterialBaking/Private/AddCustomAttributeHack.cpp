// Copyright Epic Games, Inc. All Rights Reserved.

#include "AddCustomAttributeHack.h"
#include "MaterialShared.h"

// NOTE: to access the raw array of custom attributes we need to use a dummy friend class
class FMaterialAttributePropertyDetails
{
public:

	static TArray<FMaterialCustomOutputAttributeDefintion>& GetCustomAttributes()
	{
		return FMaterialAttributeDefinitionMap::GMaterialPropertyAttributesMap.CustomAttributes;
	}
};

void AddCustomAttributeHack(const FGuid& AttributeID, const FString& AttributeName, const FString& FunctionName, EMaterialValueType ValueType, const FVector4& DefaultValue, MaterialAttributeBlendFunction BlendFunction)
{
	// NOTE: since the linker doesn't have access to the constructor we need a empty buffer
	uint8 Buffer[sizeof(FMaterialCustomOutputAttributeDefintion)] = { 0 };
	FMaterialCustomOutputAttributeDefintion& CustomAttribute = *reinterpret_cast<FMaterialCustomOutputAttributeDefintion*>(Buffer);

	CustomAttribute.AttributeID = AttributeID;
	CustomAttribute.DefaultValue = DefaultValue;
	CustomAttribute.AttributeName = AttributeName;
	CustomAttribute.Property = MP_CustomOutput;
	CustomAttribute.ValueType = ValueType;
	CustomAttribute.ShaderFrequency = SF_Pixel;
	CustomAttribute.TexCoordIndex = INDEX_NONE;
	CustomAttribute.BlendFunction = BlendFunction;
	CustomAttribute.bIsHidden = false;
	CustomAttribute.FunctionName = FunctionName;

	FMaterialAttributePropertyDetails::GetCustomAttributes().Add(CustomAttribute);
}
