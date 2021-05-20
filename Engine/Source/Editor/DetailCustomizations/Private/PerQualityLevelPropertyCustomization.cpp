// Copyright Epic Games, Inc. All Rights Reserved.

#include "PerQualityLevelPropertyCustomization.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "SPerQualityLevelPropertiesWidget.h"
#include "ScopedTransaction.h"
#include "IPropertyUtilities.h"
#include "UObject/MetaData.h"
#include "Scalability.h"

#define LOCTEXT_NAMESPACE "PerOverridePropertyCustomization"

template<typename OverrideType>
TSharedRef<SWidget> FPerQualityLevelPropertyCustomization<OverrideType>::GetWidget(FName InQualityLevelName, TSharedRef<IPropertyHandle> StructPropertyHandle) const
{
	TSharedPtr<IPropertyHandle>	EditProperty;

	if (InQualityLevelName == NAME_None)
	{
		EditProperty = StructPropertyHandle->GetChildHandle(FName("Default"));
	}
	else
	{
		TSharedPtr<IPropertyHandle>	MapProperty = StructPropertyHandle->GetChildHandle(FName("PerQuality"));
		if (MapProperty.IsValid())
		{
			uint32 NumChildren = 0;
			MapProperty->GetNumChildren(NumChildren);
			for (uint32 ChildIdx = 0; ChildIdx < NumChildren; ChildIdx++)
			{
				TSharedPtr<IPropertyHandle> ChildProperty = MapProperty->GetChildHandle(ChildIdx);
				if (ChildProperty.IsValid())
				{
					TSharedPtr<IPropertyHandle> KeyProperty = ChildProperty->GetKeyHandle();
					if (KeyProperty.IsValid())
					{
						FName KeyName;
						if (KeyProperty->GetValue(KeyName) == FPropertyAccess::Success && KeyName == InQualityLevelName)
						{
							EditProperty = ChildProperty;
							break;
						}
					}
				}
			}
		}

	}

	// Push down struct metadata to per-quality level properties
	{
		// First get the source map
		const TMap<FName, FString>* SourceMap = StructPropertyHandle->GetMetaDataProperty()->GetMetaDataMap();
		// Iterate through source map, setting each key/value pair in the destination
		for (const auto& It : *SourceMap)
		{
			EditProperty->SetInstanceMetaData(*It.Key.ToString(), *It.Value);
		}

		// Copy instance metadata as well
		const TMap<FName, FString>* InstanceSourceMap = StructPropertyHandle->GetInstanceMetaDataMap();
		for (const auto& It : *InstanceSourceMap)
		{
			EditProperty->SetInstanceMetaData(*It.Key.ToString(), *It.Value);
		}
	}

	if (EditProperty.IsValid())
	{
		return EditProperty->CreatePropertyValueWidget(false);
	}
	else
	{
		return
			SNew(STextBlock)
			.Text(NSLOCTEXT("FPerQualityLevelPropertyCustomization", "GetWidget", "Could not find valid property"))
			.ColorAndOpacity(FLinearColor::Red);
	}
}

template<typename OverrideType>
float FPerQualityLevelPropertyCustomization<OverrideType>::CalcDesiredWidth(TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	int32 NumOverrides = 0;
	TSharedPtr<IPropertyHandle>	MapProperty = StructPropertyHandle->GetChildHandle(FName("PerQuality"));
	if (MapProperty.IsValid())
	{
		TArray<const void*> RawData;
		MapProperty->AccessRawData(RawData);
		for (const void* Data : RawData)
		{
			const TMap<FName, typename OverrideType::ValueType>* PerQualityLevelMap = (const TMap<FName, typename OverrideType::ValueType>*)(Data);
			NumOverrides = FMath::Max<int32>(PerQualityLevelMap->Num(), NumOverrides);
		}
	}
	return (float)(1 + NumOverrides) * 125.f;
}

template<typename OverrideType>
bool FPerQualityLevelPropertyCustomization<OverrideType>::AddOverride(FName InQualityLevelName, TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	FScopedTransaction Transaction(LOCTEXT("AddOverride", "Add Quality Level Override"));

	TSharedPtr<IPropertyHandle>	PerQualityLevelProperty = StructPropertyHandle->GetChildHandle(FName("PerQuality"));
	TSharedPtr<IPropertyHandle>	DefaultProperty = StructPropertyHandle->GetChildHandle(FName("Default"));
	if (PerQualityLevelProperty.IsValid() && DefaultProperty.IsValid())
	{
		TSharedPtr<IPropertyHandleMap> MapProperty = PerQualityLevelProperty->AsMap();
		if (MapProperty.IsValid())
		{
			MapProperty->AddItem();
			uint32 NumChildren = 0;
			PerQualityLevelProperty->GetNumChildren(NumChildren);
			for (uint32 ChildIdx = 0; ChildIdx < NumChildren; ChildIdx++)
			{
				TSharedPtr<IPropertyHandle> ChildProperty = PerQualityLevelProperty->GetChildHandle(ChildIdx);
				if (ChildProperty.IsValid())
				{
					TSharedPtr<IPropertyHandle> KeyProperty = ChildProperty->GetKeyHandle();
					if (KeyProperty.IsValid())
					{
						FName KeyName;
						if (KeyProperty->GetValue(KeyName) == FPropertyAccess::Success && KeyName == NAME_None)
						{
							// Set Key
							KeyProperty->SetValue(InQualityLevelName);

							// Set Value
							typename OverrideType::ValueType DefaultValue;
							DefaultProperty->GetValue(DefaultValue);
							ChildProperty->SetValue(DefaultValue);

							if (PropertyUtilities.IsValid())
							{
								PropertyUtilities.Pin()->ForceRefresh();
							}

							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

template<typename OverrideType>
bool FPerQualityLevelPropertyCustomization<OverrideType>::RemoveOverride(FName InQualityLevelName, TSharedRef<IPropertyHandle> StructPropertyHandle)
{

	FScopedTransaction Transaction(LOCTEXT("RemoveQualityLevelOverride", "Remove Quality Level Override"));

	TSharedPtr<IPropertyHandle>	MapProperty = StructPropertyHandle->GetChildHandle(FName("PerQuality"));
	if (MapProperty.IsValid())
	{
		TArray<const void*> RawData;
		MapProperty->AccessRawData(RawData);
		for (const void* Data : RawData)
		{
			TMap<FName, typename OverrideType::ValueType>* PerQualityLevelMap = (TMap<FName, typename OverrideType::ValueType>*)(Data);
			check(PerQualityLevelMap);
			TArray<FName> KeyArray;
			PerQualityLevelMap->GenerateKeyArray(KeyArray);
			for (FName QualityLevelName : KeyArray)
			{
				if (QualityLevelName == InQualityLevelName)
				{

					PerQualityLevelMap->Remove(InQualityLevelName);

					if (PropertyUtilities.IsValid())
					{
						PropertyUtilities.Pin()->ForceRefresh();
					}
					return true;
				}
			}
		}
	}
	return false;

}

template<typename OverrideType>
TArray<FName> FPerQualityLevelPropertyCustomization<OverrideType>::GetOverrideNames(TSharedRef<IPropertyHandle> StructPropertyHandle) const
{
	TArray<FName> QualityLevelOverrideNames;

	TSharedPtr<IPropertyHandle>	MapProperty = StructPropertyHandle->GetChildHandle(FName("PerQuality"));
	if (MapProperty.IsValid())
	{
		TArray<const void*> RawData;
		MapProperty->AccessRawData(RawData);
		for (const void* Data : RawData)
		{
			const TMap<FName, typename OverrideType::ValueType>* PerQualityLevelMap = (const TMap<FName, typename OverrideType::ValueType>*)(Data);
			check(PerQualityLevelMap);
			TArray<FName> KeyArray;
			PerQualityLevelMap->GenerateKeyArray(KeyArray);
			for (FName QualityLevelName : KeyArray)
			{
				QualityLevelOverrideNames.AddUnique(QualityLevelName);
			}
		}

	}
	return QualityLevelOverrideNames;
}

template<typename OverrideType>
void FPerQualityLevelPropertyCustomization<OverrideType>::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FPerQualityLevelPropertyCustomization<OverrideType>::PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

	HeaderRow.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
	.ValueContent()
		.MinDesiredWidth(FPerQualityLevelPropertyCustomization<OverrideType>::CalcDesiredWidth(StructPropertyHandle))
		.MaxDesiredWidth((float)(static_cast<int32>(QualityLevelProperty::EQualityLevels::Num) + 1) * 125.0f)
		[
			SNew(SPerQualityLevelPropertiesWidget)
			.OnGenerateWidget(this, &FPerQualityLevelPropertyCustomization<OverrideType>::GetWidget, StructPropertyHandle)
			.OnAddEntry(this, &FPerQualityLevelPropertyCustomization<OverrideType>::AddOverride, StructPropertyHandle)
			.OnRemoveEntry(this, &FPerQualityLevelPropertyCustomization<OverrideType>::RemoveOverride, StructPropertyHandle)
			.EntryNames(this, &FPerQualityLevelPropertyCustomization<OverrideType>::GetOverrideNames, StructPropertyHandle)
		];
}

template<typename OverrideType>
TSharedRef<IPropertyTypeCustomization> FPerQualityLevelPropertyCustomization<OverrideType>::MakeInstance()
{
	return MakeShareable(new FPerQualityLevelPropertyCustomization<OverrideType>);
}



/* Only explicitly instantiate the types which are supported
*****************************************************************************/
template class FPerQualityLevelPropertyCustomization<FPerQualityLevelInt>;


#undef LOCTEXT_NAMESPACE