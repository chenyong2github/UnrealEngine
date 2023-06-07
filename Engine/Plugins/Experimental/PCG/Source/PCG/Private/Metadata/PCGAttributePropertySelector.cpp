// Copyright Epic Games, Inc. All Rights Reserved.

#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataAttribute.h"

namespace PCGAttributePropertySelectorConstants
{
	const TCHAR* PropertyPrefix = TEXT("$");
	const TCHAR* ExtraSeparator = TEXT(".");
	const TCHAR PropertyPrefixChar = PropertyPrefix[0];
	const TCHAR ExtraSeparatorChar = ExtraSeparator[0];
}

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
	case EPCGAttributePropertySelection::ExtraProperty:
	{
		if (const UEnum* EnumPtr = StaticEnum<EPCGExtraProperties>())
		{
			return FName(EnumPtr->GetNameStringByValue((int64)ExtraProperty));
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

bool FPCGAttributePropertySelector::SetPointProperty(EPCGPointProperties InPointProperty, bool bResetExtraNames)
{
	const bool bHasExtraNames = !ExtraNames.IsEmpty();
	if (bResetExtraNames)
	{
		ExtraNames.Empty();
	}

	if (Selection == EPCGAttributePropertySelection::PointProperty && InPointProperty == PointProperty)
	{
		// Nothing changed, except perhaps the extra names
		return (bHasExtraNames && bResetExtraNames);
	}
	else
	{
		Selection = EPCGAttributePropertySelection::PointProperty;
		PointProperty = InPointProperty;
		return true;
	}
}

bool FPCGAttributePropertySelector::SetAttributeName(FName InAttributeName, bool bResetExtraNames)
{
	const bool bHasExtraNames = !ExtraNames.IsEmpty();
	if (bResetExtraNames)
	{
		ExtraNames.Empty();
	}

	if (Selection == EPCGAttributePropertySelection::Attribute && AttributeName == InAttributeName)
	{
		// Nothing changed, except perhaps the extra names
		return (bHasExtraNames && bResetExtraNames);
	}
	else
	{
		Selection = EPCGAttributePropertySelection::Attribute;
		AttributeName = InAttributeName;
		return true;
	}
}

bool FPCGAttributePropertySelector::SetExtraProperty(EPCGExtraProperties InExtraProperty, bool bResetExtraNames)
{
	const bool bHasExtraNames = !ExtraNames.IsEmpty();
	if (bResetExtraNames)
	{
		ExtraNames.Empty();
	}

	if (Selection == EPCGAttributePropertySelection::ExtraProperty && InExtraProperty == ExtraProperty)
	{
		// Nothing changed, except perhaps the extra names
		return (bHasExtraNames && bResetExtraNames);
	}
	else
	{
		Selection = EPCGAttributePropertySelection::ExtraProperty;
		ExtraProperty = InExtraProperty;
		return true;
	}
}

FText FPCGAttributePropertySelector::GetDisplayText() const
{
	FString Res;
	const FName Name = GetName();

	// Add a '$' if it is a property
	if (Selection != EPCGAttributePropertySelection::Attribute && (Name != NAME_None))
	{
		Res = FString(PCGAttributePropertySelectorConstants::PropertyPrefix) + Name.ToString();
	}
	else
	{
		Res = Name.ToString();
	}

	if (!ExtraNames.IsEmpty())
	{
		TArray<FString> AllNames;
		AllNames.Add(Res);
		AllNames.Append(ExtraNames);
		Res = FString::Join(AllNames, PCGAttributePropertySelectorConstants::ExtraSeparator);
	}

	return FText::FromString(Res);
}

#if WITH_EDITOR
bool FPCGAttributePropertySelector::IsValid() const
{
	return (Selection != EPCGAttributePropertySelection::Attribute) || FPCGMetadataAttributeBase::IsValidName(AttributeName);
}

bool FPCGAttributePropertySelector::Update(FString NewValue)
{
	TArray<FString> NewValues;
	if (NewValue.IsEmpty())
	{
		NewValues.Add(NewValue);
	}
	else
	{
		NewValue.ParseIntoArray(NewValues, PCGAttributePropertySelectorConstants::ExtraSeparator, /*InCullEmpty=*/ false);
	}

	const FString NewName = NewValues[0];
	TArray<FString> ExtraNamesTemp;
	if (NewValues.Num() > 1)
	{
		ExtraNamesTemp.Append(&NewValues[1], NewValues.Num() - 1);
	}

	const bool bExtraChanged = ExtraNamesTemp != ExtraNames;
	ExtraNames = MoveTemp(ExtraNamesTemp);

	if (!NewName.IsEmpty() && NewName[0] == PCGAttributePropertySelectorConstants::PropertyPrefixChar)
	{
		// Remove the first character of the name, where the first character is $.
		const FString NewNameWithoutPrefix = NewName.RightChop(1);

		if (const UEnum* EnumPtr = StaticEnum<EPCGPointProperties>())
		{
			int32 Index = EnumPtr->GetIndexByNameString(NewNameWithoutPrefix);
			if (Index != INDEX_NONE)
			{
				return SetPointProperty(static_cast<EPCGPointProperties>(EnumPtr->GetValueByIndex(Index)), /*bResetExtraNames=*/ false) || bExtraChanged;
			}
		}

		if (const UEnum* EnumPtr = StaticEnum<EPCGExtraProperties>())
		{
			int32 Index = EnumPtr->GetIndexByNameString(NewNameWithoutPrefix);
			if (Index != INDEX_NONE)
			{
				return SetExtraProperty(static_cast<EPCGExtraProperties>(EnumPtr->GetValueByIndex(Index)), /*bResetExtraNames=*/ false) || bExtraChanged;
			}
		}
	}

	return SetAttributeName(NewName.IsEmpty() ? NAME_None : FName(NewName), /*bResetExtraNames=*/ false) || bExtraChanged;
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

bool UPCGAttributePropertySelectorBlueprintHelpers::SetExtraProperty(FPCGAttributePropertySelector& Selector, EPCGExtraProperties InExtraProperty)
{
	return Selector.SetExtraProperty(InExtraProperty);
}

FName UPCGAttributePropertySelectorBlueprintHelpers::GetName(const FPCGAttributePropertySelector& Selector)
{
	return Selector.GetName();
}