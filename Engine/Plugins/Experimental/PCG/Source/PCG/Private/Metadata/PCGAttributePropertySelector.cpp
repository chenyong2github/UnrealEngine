// Copyright Epic Games, Inc. All Rights Reserved.

#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataAttribute.h"

FName FPCGAttributePropertySelector::GetName() const
{
	switch (Selection)
	{
	case EPCGAttributePropertySelection::PointProperty:
	{
		if (const UEnum* EnumPtr = StaticEnum<EPCGPointProperties>())
		{
			// Need to use the string version and not the name version, because the name verison has "EPCGPointProperties::" as a prefix.
			return FName(EnumPtr->GetNameStringByValue((int64)PointProperty));
		}
		else
		{
			return NAME_None;
		}
	}
	default:
		return AttributeName;
	}
}

bool FPCGAttributePropertySelector::SetPointProperty(EPCGPointProperties InPointProperty)
{
	if (Selection == EPCGAttributePropertySelection::PointProperty && InPointProperty == PointProperty)
	{
		// Nothing changed
		return false;
	}
	else
	{
		Selection = EPCGAttributePropertySelection::PointProperty;
		PointProperty = InPointProperty;
		return true;
	}
}

bool FPCGAttributePropertySelector::SetAttributeName(FName InAttributeName)
{
	if (Selection == EPCGAttributePropertySelection::Attribute && AttributeName == InAttributeName)
	{
		// Nothing changed
		return false;
	}
	else
	{
		Selection = EPCGAttributePropertySelection::Attribute;
		AttributeName = InAttributeName;
		return true;
	}
}

// TODO: To be removed when UI widget is done.
void FPCGAttributePropertySelector::Update()
{
	const FString AttributeNameStr = AttributeName.ToString();

	// If we attribute we select starts with a '$' and matches a property, we convert it to a property
	if (!AttributeNameStr.IsEmpty() && AttributeNameStr[0] == TEXT('$'))
	{
		if (const UEnum* EnumPtr = StaticEnum<EPCGPointProperties>())
		{
			int32 Index = EnumPtr->GetIndexByNameString(AttributeNameStr.Mid(/*Start=*/1));
			if (Index != INDEX_NONE)
			{
				SetPointProperty(static_cast<EPCGPointProperties>(EnumPtr->GetValueByIndex(Index)));
				return;
			}
		}
	}
}

#if WITH_EDITOR
bool FPCGAttributePropertySelector::IsValid() const
{
	return (Selection != EPCGAttributePropertySelection::Attribute) || FPCGMetadataAttributeBase::IsValidName(AttributeName);
}

FText FPCGAttributePropertySelector::GetDisplayText() const
{
	const FName Name = GetName();

	// Add a '$' if it is a property
	if (Selection == EPCGAttributePropertySelection::PointProperty && (Name != NAME_None))
	{
		return FText::FromString(FString(TEXT("$")) + Name.ToString());
	}
	else
	{
		return FText::FromName(Name);
	}
}

bool FPCGAttributePropertySelector::Update(FString NewValue)
{
	if (!NewValue.IsEmpty() && NewValue[0] == TEXT('$'))
	{
		if (const UEnum* EnumPtr = StaticEnum<EPCGPointProperties>())
		{
			int32 Index = EnumPtr->GetIndexByNameString(NewValue.Mid(/*Start=*/1));
			if (Index != INDEX_NONE)
			{
				return SetPointProperty(static_cast<EPCGPointProperties>(EnumPtr->GetValueByIndex(Index)));
			}
		}
	}

	return SetAttributeName(NewValue.IsEmpty() ? NAME_None : FName(NewValue));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////

bool UPCGAttributePropertySelectorBlueprintHelpers::SetPointProperty(FPCGAttributePropertySelector& Selector, EPCGPointProperties InPointProperty)
{
	return Selector.SetPointProperty(InPointProperty);
}

bool UPCGAttributePropertySelectorBlueprintHelpers::SetAttributeName(FPCGAttributePropertySelector& Selector, FName InAttributeName)
{
	return Selector.SetAttributeName(InAttributeName);
}

FName UPCGAttributePropertySelectorBlueprintHelpers::GetName(const FPCGAttributePropertySelector& Selector)
{
	return Selector.GetName();
}