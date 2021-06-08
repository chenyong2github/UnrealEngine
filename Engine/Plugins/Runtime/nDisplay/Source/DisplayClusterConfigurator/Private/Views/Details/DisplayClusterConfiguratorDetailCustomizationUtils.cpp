// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorDetailCustomizationUtils.h"

#include "DetailLayoutBuilder.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"

TSharedPtr<IPropertyHandle> FDisplayClusterConfiguratorNestedPropertyHelper::GetNestedProperty(const FString& PropertyPath)
{
	if (CachedPropertyHandles.Contains(PropertyPath))
	{
		return CachedPropertyHandles[PropertyPath];
	}

	TArray<FString> Properties;
	PropertyPath.Replace(TEXT("->"), TEXT(".")).ParseIntoArray(Properties, TEXT("."));

	if (Properties.Num())
	{
		int32 CurrentPropertyIndex = 0;
		FString CurrentPropertyPath;
		TSharedPtr<IPropertyHandle> CurrentPropertyHandle = FindCachedHandle(Properties, CurrentPropertyPath, CurrentPropertyIndex);

		if (!CurrentPropertyHandle.IsValid())
		{
			CurrentPropertyIndex = 0;
			CurrentPropertyPath = Properties[CurrentPropertyIndex];
			CurrentPropertyHandle = LayoutBuilder.GetProperty(FName(*CurrentPropertyPath));
		}

		++CurrentPropertyIndex;

		while (CurrentPropertyIndex < Properties.Num() && CurrentPropertyHandle)
		{
			CurrentPropertyHandle = GetProperty(Properties[CurrentPropertyIndex], CurrentPropertyHandle, CurrentPropertyPath);
			CurrentPropertyPath += TEXT(".") + Properties[CurrentPropertyIndex];
			++CurrentPropertyIndex;
		}

		return CurrentPropertyHandle;
	}

	return TSharedPtr<IPropertyHandle>();
}

void FDisplayClusterConfiguratorNestedPropertyHelper::GetNestedProperties(const FString& PropertyPath, TArray<TSharedPtr<IPropertyHandle>>& OutPropertyHandles)
{
	TArray<FString> Properties;
	PropertyPath.Replace(TEXT("->"), TEXT(".")).ParseIntoArray(Properties, TEXT("."));

	if (Properties.Num())
	{
		int32 CurrentPropertyIndex = 0;

		TMap<FString, TSharedPtr<IPropertyHandle>> CurrentPropertyHandles;

		FString CachedPropertyPath;
		if (TSharedPtr<IPropertyHandle> CachedPropertyHandle = FindCachedHandle(Properties, CachedPropertyPath, CurrentPropertyIndex))
		{
			CurrentPropertyHandles.Add(CachedPropertyPath, CachedPropertyHandle);
		}
		else
		{
			CurrentPropertyHandles.Add(Properties[CurrentPropertyIndex], LayoutBuilder.GetProperty(FName(*Properties[CurrentPropertyIndex])));
		}

		do
		{
			// First, if the current nested properties are lists, expand them into list elements.
			TMap<FString, TSharedPtr<IPropertyHandle>> NextPropertyHandles;
			for (const TPair<FString, TSharedPtr<IPropertyHandle>>& CurrentPropertyPair : CurrentPropertyHandles)
			{
				if (IsListType(CurrentPropertyPair.Value))
				{
					uint32 NumElements;
					CurrentPropertyPair.Value->GetNumChildren(NumElements);

					for (uint32 Index = 0; Index < NumElements; ++Index)
					{
						if (TSharedPtr<IPropertyHandle> ElementHandle = CurrentPropertyPair.Value->GetChildHandle(Index))
						{
							const FString NextPropertyPath = CurrentPropertyPair.Key + FString::Format(TEXT("[{0}]"), { Index });
							NextPropertyHandles.Add(NextPropertyPath, ElementHandle);
						}
					}
				}
				else
				{
					NextPropertyHandles.Add(CurrentPropertyPair);
				}
			}

			CurrentPropertyHandles.Empty();
			++CurrentPropertyIndex;

			// Next, grab the next set of properties from the expanded list elements. For now, assume that
			// the elements of any lists are not lists themselves.
			if (CurrentPropertyIndex < Properties.Num())
			{
				for (const TPair<FString, TSharedPtr<IPropertyHandle>>& NextPropertyPair : NextPropertyHandles)
				{
					if (TSharedPtr<IPropertyHandle> PropertyHandle = GetProperty(Properties[CurrentPropertyIndex], NextPropertyPair.Value, NextPropertyPair.Key))
					{
						const FString CurrentPropertyPath = NextPropertyPair.Key + TEXT(".") + Properties[CurrentPropertyIndex];
						CurrentPropertyHandles.Add(CurrentPropertyPath, PropertyHandle);
					}
				}
			}
			else
			{
				CurrentPropertyHandles.Append(NextPropertyHandles);
			}

		} while (CurrentPropertyIndex < Properties.Num() && CurrentPropertyHandles.Num());

		CurrentPropertyHandles.GenerateValueArray(OutPropertyHandles);
	}
}

void FDisplayClusterConfiguratorNestedPropertyHelper::GetNestedPropertyKeys(const FString& PropertyPath, TArray<FString>& OutPropertyKeys)
{
	TArray<TSharedPtr<IPropertyHandle>> PropertyHandles;
	GetNestedProperties(PropertyPath, PropertyHandles);

	for (const TSharedPtr<IPropertyHandle>& PropertyHandle : PropertyHandles)
	{
		TSharedPtr<IPropertyHandle> KeyHandle = PropertyHandle->GetKeyHandle();

		FString KeyValue;
		if (KeyHandle.IsValid() && KeyHandle->GetValue(KeyValue) == FPropertyAccess::Success)
		{
			OutPropertyKeys.Add(KeyValue);
		}
	}
}

TSharedPtr<IPropertyHandle> FDisplayClusterConfiguratorNestedPropertyHelper::GetProperty(const FString& PropertyName, const TSharedPtr<IPropertyHandle>& Parent, const FString& ParentPath)
{
	const FString PropertyPath = ParentPath + TEXT(".") + PropertyName;
	if (CachedPropertyHandles.Contains(PropertyPath))
	{
		return CachedPropertyHandles[PropertyPath];
	}

	if (PropertyName.Contains(TEXT("[")) && PropertyName.Contains(TEXT("]")))
	{
		FString ListPropertyName;
		FString IndexStr;
		PropertyName.Split(TEXT("["), &ListPropertyName, &IndexStr);

		int32 Index = FCString::Atoi(*IndexStr);

		const FString ListPropertyPath = ParentPath + TEXT(".") + ListPropertyName;

		TSharedPtr<IPropertyHandle> ListPropertyHandle;
		if (CachedPropertyHandles.Contains(ListPropertyPath))
		{
			ListPropertyHandle = CachedPropertyHandles[ListPropertyPath];
		}
		else
		{
			ListPropertyHandle = Parent->GetChildHandle(FName(*ListPropertyName));
		}

		if (IsListType(ListPropertyHandle))
		{
			CachedPropertyHandles.Add(ListPropertyPath, ListPropertyHandle);

			uint32 NumElements;
			ListPropertyHandle->GetNumChildren(NumElements);

			if (Index >= 0 && Index < (int32)NumElements)
			{
				if (TSharedPtr<IPropertyHandle> ElementHandle = ListPropertyHandle->GetChildHandle(Index))
				{
					CachedPropertyHandles.Add(PropertyPath, ElementHandle);
					return ElementHandle;
				}
			}
		}

		return TSharedPtr<IPropertyHandle>();
	}
	else
	{
		if (TSharedPtr<IPropertyHandle> PropertyHandle = Parent->GetChildHandle(FName(*PropertyName)))
		{
			CachedPropertyHandles.Add(PropertyPath, PropertyHandle);
			return PropertyHandle;
		}

		return TSharedPtr<IPropertyHandle>();
	}

}

TSharedPtr<IPropertyHandle> FDisplayClusterConfiguratorNestedPropertyHelper::FindCachedHandle(const TArray<FString>& PropertyPath, FString& OutFoundPath, int32& OutFoundIndex)
{
	FString CurrentPath = FString::Join(PropertyPath, TEXT("."));
	int32 CurrentIndex = PropertyPath.Num();

	while (!CachedPropertyHandles.Contains(CurrentPath) && CurrentIndex > 0)
	{
		--CurrentIndex;
		CurrentPath.RemoveFromEnd(PropertyPath[CurrentIndex]);
		CurrentPath.RemoveFromEnd(TEXT("."));
	}

	// If the cache contains a handle for a property somewhere in the desired path, return it
	if (CachedPropertyHandles.Contains(CurrentPath))
	{
		OutFoundPath = CurrentPath;
		OutFoundIndex = CurrentIndex - 1;
		return CachedPropertyHandles[CurrentPath];
	}

	return TSharedPtr<IPropertyHandle>();
}

bool FDisplayClusterConfiguratorNestedPropertyHelper::IsListType(const TSharedPtr<IPropertyHandle>& PropertyHandle)
{
	return PropertyHandle->AsArray() || PropertyHandle->AsSet() || PropertyHandle->AsMap();
}