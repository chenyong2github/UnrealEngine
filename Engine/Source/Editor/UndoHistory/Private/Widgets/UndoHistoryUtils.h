// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FUndoHistoryUtils
{
public:

	struct FBasicPropertyInfo
	{
		FString PropertyName;
		FString PropertyType;
		EPropertyFlags PropertyFlags;

		FBasicPropertyInfo(FString InPropertyName, FString InPropertyType, EPropertyFlags InPropertyFlags)
			: PropertyName(MoveTemp(InPropertyName))
			, PropertyType(MoveTemp(InPropertyType))
			, PropertyFlags(InPropertyFlags)
		{}
	};

	static TArray<FBasicPropertyInfo> GetChangedPropertiesInfo(const UClass* InObjectClass, const TArray<FName>& InChangedProperties)
	{
		TArray<FBasicPropertyInfo> Properties;
		FString ClassName;

		if (!InObjectClass)
		{
			return Properties;
		}

		for (TFieldIterator<FProperty> Property(InObjectClass); Property; ++Property)
		{
			if (!InChangedProperties.Contains(FName(*Property->GetName())))
			{
				continue;
			}

			if (Property->GetClass() == FObjectProperty::StaticClass() || Property->GetClass() == FStructProperty::StaticClass() || Property->GetClass() == FEnumProperty::StaticClass())
			{
				Property->GetCPPMacroType(ClassName);
			}
			else if (Property->GetClass() == FArrayProperty::StaticClass())
			{
				Property->GetCPPMacroType(ClassName);
				ClassName = FString::Printf(TEXT("TArray<%s>"), *ClassName);
			}
			else
			{
				ClassName = Property->GetClass()->GetName();
				ClassName.RemoveFromEnd("Property");
			}

			Properties.Emplace(Property->GetName(), MoveTemp(ClassName), Property->GetPropertyFlags());
		}
		return Properties;
	}
};