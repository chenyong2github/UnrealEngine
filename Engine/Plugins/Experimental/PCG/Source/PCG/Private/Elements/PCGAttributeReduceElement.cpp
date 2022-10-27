// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGAttributeReduceElement.h"

#include "PCGData.h"
#include "PCGParamData.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataEntryKeyIterator.h"
#include "Helpers/PCGSettingsHelpers.h"

namespace PCGAttributeReduceElement
{
	template <typename T>
	bool Average(IPCGMetadataEntryIterator& Iterator, const FPCGMetadataAttribute<T>* InAttribute, T& OutValue)
	{
		if constexpr (!PCG::Private::MetadataTraits<T>::CanSubAdd || !PCG::Private::MetadataTraits<T>::CanInterpolate)
		{
			return false;
		}
		else
		{
			// Can't have a repeat iterator
			check(!Iterator.IsRepeat());

			int32 NumberOfEntries = 0;
			OutValue = PCG::Private::MetadataTraits<T>::ZeroValue();
			for (; !Iterator.IsEnd(); ++Iterator)
			{
				OutValue = PCG::Private::MetadataTraits<T>::Add(OutValue, InAttribute->GetValueFromItemKey(*Iterator));
				++NumberOfEntries;
			}

			if (NumberOfEntries != 0)
			{
				OutValue = PCG::Private::MetadataTraits<T>::WeightedSum(PCG::Private::MetadataTraits<T>::ZeroValue(), OutValue, 1.0f / NumberOfEntries);
			}

			return true;
		}
	}

	template <typename T, bool bIsMin>
	bool MinMax(IPCGMetadataEntryIterator& Iterator, const FPCGMetadataAttribute<T>* InAttribute, T& OutValue)
	{
		if constexpr (!PCG::Private::MetadataTraits<T>::CanMinMax)
		{
			return false;
		}
		else
		{
			// Can't have a repeat iterator
			check(!Iterator.IsRepeat());

			int32 NumberOfEntries = 0;
			for (; !Iterator.IsEnd(); ++Iterator)
			{
				T AttributeValue = InAttribute->GetValueFromItemKey(*Iterator);

				if (NumberOfEntries == 0)
				{
					OutValue = AttributeValue;
				}
				else
				{
					if constexpr (bIsMin)
					{
						OutValue = PCG::Private::MetadataTraits<T>::Min(OutValue, AttributeValue);
					}
					else
					{
						OutValue = PCG::Private::MetadataTraits<T>::Max(OutValue, AttributeValue);
					}
				}
				++NumberOfEntries;
			}

			return true;
		}
	}
}

#if WITH_EDITOR
FName UPCGAttributeReduceSettings::GetDefaultNodeName() const
{
	return TEXT("AttributeReduce");
}
#endif

FName UPCGAttributeReduceSettings::AdditionalTaskName() const
{
	if (const UEnum* EnumPtr = FindObject<UEnum>(nullptr, TEXT("/Script/PCG.EPCGAttributeReduceOperation"), true))
	{
		const FString OperationName = EnumPtr->GetNameStringByValue(static_cast<int>(Operation));

		if (InputAttributeName != OutputAttributeName && OutputAttributeName != NAME_None)
		{
			return FName(FString::Printf(TEXT("Reduce %s to %s: %s"), *InputAttributeName.ToString(), *OutputAttributeName.ToString(), *OperationName));
		}
		else
		{
			return FName(FString::Printf(TEXT("Reduce %s: %s"), *InputAttributeName.ToString(), *OperationName));
		}
	}
	else
	{
		return NAME_None;
	}
}

TArray<FPCGPinProperties> UPCGAttributeReduceSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Spatial, /*bInAllowMultipleConnections=*/ false);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGAttributeReduceSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);

	return PinProperties;
}

FPCGElementPtr UPCGAttributeReduceSettings::CreateElement() const
{
	return MakeShared<FPCGAttributeReduceElement>();
}

bool FPCGAttributeReduceElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAttributeReduceElement::Execute);

	check(Context);

	const UPCGAttributeReduceSettings* Settings = Context->GetInputSettings<UPCGAttributeReduceSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	if (Inputs.Num() != 1)
	{
		PCGE_LOG(Error, "Input pin doesn't have the right number of inputs.");
		return true;
	}

	const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Inputs[0].Data);

	if (!SpatialData)
	{
		PCGE_LOG(Error, "Input is not a spatial data.");
		return true;
	}

	if (!SpatialData->Metadata)
	{
		PCGE_LOG(Error, "Input data doesn't have metadata");
		return true;
	}

	if (!SpatialData->Metadata->HasAttribute(Settings->InputAttributeName))
	{
		PCGE_LOG(Error, "Input metadata doesn't have attribute \"%s\"", *Settings->InputAttributeName.ToString());
		return true;
	}

	const UPCGPointData* PointData = Cast<UPCGPointData>(SpatialData);

	const FPCGMetadataAttributeBase* InputAttribute = SpatialData->Metadata->GetConstAttribute(Settings->InputAttributeName);
	const FName OutputAttributeName = (Settings->OutputAttributeName == NAME_None) ? Settings->InputAttributeName : Settings->OutputAttributeName;
	UPCGParamData* OutputParamData = NewObject<UPCGParamData>();
	
	FPCGMetadataAttributeBase* OutputAttribute = OutputParamData->Metadata->CopyAttribute(InputAttribute, OutputAttributeName, /*bKeepParent=*/ false, /*bCopyEntries=*/ false, /*bCopyValues=*/ false);

	auto DoOperation = [InputAttribute, OutputAttribute, Operation = Settings->Operation, SpatialData, PointData, OutputParamData](auto DummyValue) -> bool
	{
		using AttributeType = decltype(DummyValue);

		bool bSuccess = false;
		const bool bIsRepeat = false;

		const FPCGMetadataAttribute<AttributeType>* TypedInputAttribute = static_cast<const FPCGMetadataAttribute<AttributeType>*>(InputAttribute);
		FPCGMetadataAttribute<AttributeType>* TypedOutputAttribute = static_cast<FPCGMetadataAttribute<AttributeType>*>(OutputAttribute);

		AttributeType OutputValue{};

		TUniquePtr<IPCGMetadataEntryIterator> Iterator;
		if (PointData)
		{
			Iterator = MakeUnique<FPCGMetadataEntryPointIterator>(PointData, bIsRepeat);
		}
		else
		{
			Iterator = MakeUnique<FPCGMetadataEntryAttributeIterator>(*InputAttribute, bIsRepeat);
		}

		switch (Operation)
		{
		case EPCGAttributeReduceOperation::Average:
			bSuccess = PCGAttributeReduceElement::Average<AttributeType>(*Iterator, TypedInputAttribute, OutputValue);
			break;
		case EPCGAttributeReduceOperation::Max:
			bSuccess = PCGAttributeReduceElement::MinMax<AttributeType, /*bIsMin*/false>(*Iterator, TypedInputAttribute, OutputValue);
			break;
		case EPCGAttributeReduceOperation::Min:
			bSuccess = PCGAttributeReduceElement::MinMax<AttributeType, /*bIsMin*/true>(*Iterator, TypedInputAttribute, OutputValue);
			break;
		default:
			break;
		}

		if (bSuccess)
		{
			TypedOutputAttribute->SetDefaultValue(OutputValue);
			TypedOutputAttribute->SetValueFromValueKey(OutputParamData->Metadata->AddEntry(), PCGDefaultValueKey);
		}

		return bSuccess;
	};

	if (!PCGMetadataAttribute::CallbackWithRightType(InputAttribute->GetTypeId(), DoOperation))
	{
		PCGE_LOG(Error, "Operation was not compatible with the attribute type.");
		return true;
	}

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	FPCGTaggedData& Output = Outputs.Emplace_GetRef();
	Output.Data = OutputParamData;

	return true;
}