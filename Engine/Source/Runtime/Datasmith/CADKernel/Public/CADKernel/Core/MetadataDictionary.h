// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"

#include "CADKernel/Core/Metadata.h"

namespace CADKernel
{
	class CADKERNEL_API FMetadataDictionary
	{

	private:
		TMap<FString, TSharedPtr<FMetadata>> MetadataMap;

		void AddValue(const FString& Name, TSharedPtr<FMetadata>& Attribute)
		{
			MetadataMap.Emplace(Name, Attribute);
		}

	public:

		FMetadataDictionary() = default;

		template<typename ValueType>
		void Add(const FString& Name, const ValueType& Value)
		{
			TSharedRef<FMetadata> NewAttribute = FMetadata::Create(Value);
			SetOrAdd(Name, NewAttribute);
		}

		void AddLayer(const FString& Name, const int32 LayerId, const FString& LayerName, const int32 LayerFlag)
		{
			TSharedRef<FMetadata> NewAttribute = FMetadata::CreateLayer(LayerId, LayerName, LayerFlag);
			SetOrAdd(Name, NewAttribute);
		}

		void AddRGBColor(const FString& Name, const double RedValue, const double GreenValue, const double BlueValue)
		{
			TSharedRef<FMetadata> NewAttribute = FMetadata::CreateRGBColor(RedValue, GreenValue, BlueValue);
			SetOrAdd(Name, NewAttribute);
		}

		const TSharedPtr<FMetadata> Get(const FString& Name)
		{
			TSharedPtr<FMetadata>* Attribute = MetadataMap.Find(Name);
			return Attribute == nullptr ? TSharedPtr<FMetadata>() : *Attribute;
		}

		int32 Count() const
		{
			return MetadataMap.Num();
		}

	private:

		void SetOrAdd(const FString Name, const TSharedPtr<FMetadata> NewAttribute)
		{
			TSharedPtr<FMetadata>* OldAttribute = MetadataMap.Find(Name);
			if (OldAttribute != nullptr)
			{
				(*OldAttribute).Reset();
				*OldAttribute = NewAttribute;
			}
			else
			{
				MetadataMap.Add(Name, NewAttribute);
			}
		}

	};
}


