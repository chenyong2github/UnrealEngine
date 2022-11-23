// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGAttributeSelectElement.h"

#include "PCGData.h"
#include "PCGParamData.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataEntryKeyIterator.h"
#include "Helpers/PCGSettingsHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGAttributeSelectElement)

namespace PCGAttributeSelectElement
{
	// Of course, Dot doesn't exist for FVector4...
	template <typename T>
	inline double Dot(const T& A, const T& B)
	{
		if constexpr (std::is_same_v<FVector4, T>)
		{
			return Dot4(A, B);
		}
		else
		{
			return A.Dot(B);
		}
	}

	// No need to do a dot product if the axis is either X, Y, Z or W.
	// We still need to check the type for Z and W, since it needs to compile even if we
	// will never call this function with the wrong type for a given axis.
	// If T is a scalar, just return InValue. Use decltype(auto) to return an int if T is an int.
	// It will allow to do comparison between int, instead of losing precision by converting it to double.
	template <typename T, int Axis>
	inline decltype(auto) Projection(const T& InValue, const T& InAxis)
	{
		if constexpr (PCG::Private::IsOfTypes<T, FVector2D, FVector, FVector4>())
		{
			if constexpr (Axis == 0)
			{
				return InValue.X;
			}
			else if constexpr (Axis == 1)
			{
				return InValue.Y;
			}
			else if constexpr (Axis == 2 && PCG::Private::IsOfTypes<T, FVector, FVector4>())
			{
				return InValue.Z;
			}
			else if constexpr (Axis == 3 && PCG::Private::IsOfTypes<T, FVector4>())
			{
				return InValue.W;
			}
			else
			{
				return Dot(InValue, InAxis);
			}
		}
		else
		{
			// Use T constructor to force the compiler to match decltype(auto) to T.
			// Otherwise, it would match to const T, which leads to issues.
			return T(InValue);
		}
	}

	template <typename T, bool bIsMin, int Axis>
	bool MinMaxSelect(IPCGMetadataEntryIterator& Iterator, const FPCGMetadataAttribute<T>* InAttribute, const T& InAxis, T& OutValue, int32& OutIndex)
	{
		// Can't have a repeat iterator
		check(!Iterator.IsRepeat());

		// Will be int or double
		using CompareType = decltype(Projection<T, Axis>(T{}, InAxis));

		CompareType MinMaxValue{};

		int32 NumberOfEntries = 0;
		for (; !Iterator.IsEnd(); ++Iterator)
		{
			T AttributeValue = InAttribute->GetValueFromItemKey(*Iterator);
			CompareType CurrentValue = Projection<T, Axis>(AttributeValue, InAxis);

			if constexpr (bIsMin)
			{
				if (NumberOfEntries == 0 || CurrentValue < MinMaxValue)
				{
					MinMaxValue = CurrentValue;
					OutValue = AttributeValue;
					OutIndex = NumberOfEntries;
				}
			}
			else
			{
				if (NumberOfEntries == 0 || CurrentValue > MinMaxValue)
				{
					MinMaxValue = CurrentValue;
					OutValue = AttributeValue;
					OutIndex = NumberOfEntries;
				}
			}

			++NumberOfEntries;
		}

		return NumberOfEntries != 0;
	}

	template <typename T, int Axis>
	bool MedianSelect(IPCGMetadataEntryIterator& Iterator, const FPCGMetadataAttribute<T>* InAttribute, const T& InAxis, T& OutValue, int32& OutIndex)
	{
		// Can't have a repeat iterator
		check(!Iterator.IsRepeat());

		// Will be int or double
		using CompareType = decltype(Projection<T, Axis>(T{}, InAxis));

		struct Item
		{
			Item(int32 InIndex, CompareType InCompareValue, T&& InAttributeValue)
				: Index(InIndex)
				, CompareValue(InCompareValue)
				, AttributeValue(Forward<T>(InAttributeValue)) 
			{}

			int32 Index;
			CompareType CompareValue;
			T AttributeValue;
		};

		int32 NumberOfEntries = 0;
		TArray<Item> Items;
		for (; !Iterator.IsEnd(); ++Iterator)
		{
			T AttributeValue = InAttribute->GetValueFromItemKey(*Iterator);
			CompareType CurrentValue = Projection<T, Axis>(AttributeValue, InAxis);

			Items.Emplace(NumberOfEntries++, MoveTemp(CurrentValue), MoveTemp(AttributeValue));
		}

		if (NumberOfEntries == 0)
		{
			return false;
		}

		Algo::Sort(Items, [](const Item& A, const Item& B) -> bool { return A.CompareValue < B.CompareValue; });

		// Since we need to return an index, we can't do the mean on 2 values if the number of entries is even, since it will yield a value that might not exist in the original dataset.
		// In this case we arbitrarily chose one entry.
		const int32 Index = NumberOfEntries / 2;
		OutValue = Items[Index].AttributeValue;
		OutIndex = Items[Index].Index;

		return true;
	}

	template <typename T, int Axis>
	inline bool DispatchOperation(IPCGMetadataEntryIterator& Iterator, const FPCGMetadataAttribute<T>* InAttribute, const T& InAxis, EPCGAttributeSelectOperation Operation, T& OutValue, int32& OutIndex)
	{
		switch (Operation)
		{
		case EPCGAttributeSelectOperation::Min:
			return PCGAttributeSelectElement::MinMaxSelect<T, /*bIsMin=*/true, Axis>(Iterator, InAttribute, InAxis, OutValue, OutIndex);
		case EPCGAttributeSelectOperation::Max:
			return PCGAttributeSelectElement::MinMaxSelect<T, /*bIsMin=*/false, Axis>(Iterator, InAttribute, InAxis, OutValue, OutIndex);
		case EPCGAttributeSelectOperation::Median:
			return PCGAttributeSelectElement::MedianSelect<T, Axis>(Iterator, InAttribute, InAxis, OutValue, OutIndex);
		default:
			return false;
		}
	}
}

#if WITH_EDITOR
FName UPCGAttributeSelectSettings::GetDefaultNodeName() const
{
	return TEXT("AttributeSelect");
}
#endif

FName UPCGAttributeSelectSettings::AdditionalTaskName() const
{
	if (const UEnum* EnumOpPtr = FindObject<UEnum>(nullptr, TEXT("/Script/PCG.EPCGAttributeSelectOperation"), true))
	{
		if (const UEnum* EnumAxisPtr = FindObject<UEnum>(nullptr, TEXT("/Script/PCG.EPCGAttributeSelectAxis"), true))
		{
			const FString OperationName = EnumOpPtr->GetNameStringByValue(static_cast<int>(Operation));
			FString AxisName;
			if (Axis == EPCGAttributeSelectAxis::CustomAxis)
			{
				AxisName = FString::Printf(TEXT("(%.2f, %.2f, %.2f, %.2f)"), CustomAxis.X, CustomAxis.Y, CustomAxis.Z, CustomAxis.W);
			}
			else
			{
				AxisName = EnumAxisPtr->GetNameStringByValue(static_cast<int>(Axis));
			}

			if (InputAttributeName != OutputAttributeName && OutputAttributeName != NAME_None)
			{
				return FName(FString::Printf(TEXT("Select %s to %s: %s on %s"), *InputAttributeName.ToString(), *OutputAttributeName.ToString(), *OperationName, *AxisName));
			}
			else
			{
				return FName(FString::Printf(TEXT("Select %s: %s on %s"), *InputAttributeName.ToString(), *OperationName, *AxisName));
			}
		}
	}

	return NAME_None;
}

TArray<FPCGPinProperties> UPCGAttributeSelectSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Spatial, /*bInAllowMultipleConnections=*/ false);
	PinProperties.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param, /*bInAllowMultipleConnections=*/ false);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGAttributeSelectSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGAttributeSelectConstants::OutputAttributeLabel, EPCGDataType::Param);
	PinProperties.Emplace(PCGAttributeSelectConstants::OutputPointLabel, EPCGDataType::Point);

	return PinProperties;
}

FPCGElementPtr UPCGAttributeSelectSettings::CreateElement() const
{
	return MakeShared<FPCGAttributeSelectElement>();
}

bool FPCGAttributeSelectElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAttributeSelectElement::Execute);

	check(Context);

	const UPCGAttributeSelectSettings* Settings = Context->GetInputSettings<UPCGAttributeSelectSettings>();
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

	const UPCGPointData* PointData = Cast<UPCGPointData>(SpatialData);
	if (!PointData && Context->Node && Context->Node->IsOutputPinConnected(PCGAttributeSelectConstants::OutputPointLabel))
	{
		PCGE_LOG(Warning, "Not a point data as input, will output nothing in the %s output pin", *PCGAttributeSelectConstants::OutputPointLabel.ToString());
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

	const FPCGMetadataAttributeBase* InputAttribute = SpatialData->Metadata->GetConstAttribute(Settings->InputAttributeName);
	const FName OutputAttributeName = (Settings->OutputAttributeName == NAME_None) ? Settings->InputAttributeName : Settings->OutputAttributeName;
	UPCGParamData* OutputParamData = NewObject<UPCGParamData>();

	FPCGMetadataAttributeBase* OutputAttribute = OutputParamData->Metadata->CopyAttribute(InputAttribute, OutputAttributeName, /*bKeepParent=*/ false, /*bCopyEntries=*/ false, /*bCopyValues=*/ false);

	if (!OutputAttribute)
	{
		PCGE_LOG(Error, "Error while creating target attribute %s", *OutputAttributeName.ToString());
		return true;
	}

	auto DoOperation = [&](auto DummyValue) -> int32
	{
		using AttributeType = decltype(DummyValue);

		if constexpr (!PCG::Private::IsOfTypes<AttributeType, int32, int64, float, double, FVector2D, FVector, FVector4>())
		{
			PCGE_LOG(Error, "Attribute type is not a Vector nor a scalar");
			return -1;
		}
		else
		{
			const FPCGMetadataAttribute<AttributeType>* TypedInputAttribute = static_cast<const FPCGMetadataAttribute<AttributeType>*>(InputAttribute);
			FPCGMetadataAttribute<AttributeType>* TypedOutputAttribute = static_cast<FPCGMetadataAttribute<AttributeType>*>(OutputAttribute);

			bool bSuccess = false;
			const bool bIsRepeat = false;
			AttributeType OutputValue{};
			AttributeType Axis{};
			int32 OutputIndex = -1;

			// First we need to verify if the axis we want to project on is valid for dimension of our vector type.
			// If it is a scalar, we won't project anything.
			if constexpr (PCG::Private::IsOfTypes<AttributeType, FVector2D, FVector, FVector4>())
			{
				bool bIsValid = false;

				switch (Settings->Axis)
				{
				case EPCGAttributeSelectAxis::X:
				case EPCGAttributeSelectAxis::Y:
					bIsValid = true;
					break;
				case EPCGAttributeSelectAxis::Z:
					bIsValid = PCG::Private::IsOfTypes<AttributeType, FVector, FVector4>();
					break;
				case EPCGAttributeSelectAxis::W:
					bIsValid = PCG::Private::IsOfTypes<AttributeType, FVector4>();
					break;
				case EPCGAttributeSelectAxis::CustomAxis:
					Axis = AttributeType(Settings->CustomAxis);
					bIsValid = !Axis.Equals(AttributeType::Zero());
					break;
				default:
					break;
				}

				if (!bIsValid)
				{
					PCGE_LOG(Error, "Invalid axis for attribute type.");
					return -1;
				}
			}

			// Then create our iterator depending if it is a point data or a spatial data
			TUniquePtr<IPCGMetadataEntryIterator> Iterator;
			if (PointData)
			{
				Iterator = MakeUnique<FPCGMetadataEntryPointIterator>(PointData, bIsRepeat);
			}
			else
			{
				Iterator = MakeUnique<FPCGMetadataEntryAttributeIterator>(*InputAttribute, bIsRepeat);
			}

			// Finally dispatch the operation depending on the axis.
			// If the axis is X, Y, Z or W, the projection is overkill (Dot product), so use templates to 
			// indicate which coordinate we should take. If the axis value is -1, it will do the projection with the custom axis, passed as parameter.
			switch (Settings->Axis)
			{
			case EPCGAttributeSelectAxis::X:
				bSuccess = PCGAttributeSelectElement::DispatchOperation<AttributeType, 0>(*Iterator, TypedInputAttribute, Axis, Settings->Operation, OutputValue, OutputIndex);
				break;
			case EPCGAttributeSelectAxis::Y:
				bSuccess = PCGAttributeSelectElement::DispatchOperation<AttributeType, 1>(*Iterator, TypedInputAttribute, Axis, Settings->Operation, OutputValue, OutputIndex);
				break;
			case EPCGAttributeSelectAxis::Z:
				bSuccess = PCGAttributeSelectElement::DispatchOperation<AttributeType, 2>(*Iterator, TypedInputAttribute, Axis, Settings->Operation, OutputValue, OutputIndex);
				break;
			case EPCGAttributeSelectAxis::W:
				bSuccess = PCGAttributeSelectElement::DispatchOperation<AttributeType, 3>(*Iterator, TypedInputAttribute, Axis, Settings->Operation, OutputValue, OutputIndex);
				break;
			case EPCGAttributeSelectAxis::CustomAxis:
				bSuccess = PCGAttributeSelectElement::DispatchOperation<AttributeType, -1>(*Iterator, TypedInputAttribute, Axis, Settings->Operation, OutputValue, OutputIndex);
				break;
			default:
				break;
			}

			if (bSuccess)
			{
				TypedOutputAttribute->SetDefaultValue(OutputValue);
				TypedOutputAttribute->SetValueFromValueKey(OutputParamData->Metadata->AddEntry(), PCGDefaultValueKey);
			}
			else
			{
				PCGE_LOG(Error, "Invalid axis for attribute type.");
				OutputIndex = -1;
			}

			return OutputIndex;
		}
	};

	int32 OutputIndex = PCGMetadataAttribute::CallbackWithRightType(InputAttribute->GetTypeId(), DoOperation);
	if (OutputIndex < 0)
	{
		return true;
	}

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	FPCGTaggedData& Output = Outputs.Emplace_GetRef();
	Output.Data = OutputParamData;
	Output.Pin = PCGAttributeSelectConstants::OutputAttributeLabel;

	if (PointData && Context->IsOutputConnectedOrInspecting(PCGAttributeSelectConstants::OutputPointLabel))
	{
		UPCGPointData* OutputPointData = NewObject<UPCGPointData>();
		OutputPointData->InitializeFromData(PointData);
		OutputPointData->GetMutablePoints().Add(PointData->GetPoint(OutputIndex));

		FPCGTaggedData& PointOutput = Outputs.Emplace_GetRef();
		PointOutput.Data = OutputPointData;
		PointOutput.Pin = PCGAttributeSelectConstants::OutputPointLabel;
	}

	return true;
}
