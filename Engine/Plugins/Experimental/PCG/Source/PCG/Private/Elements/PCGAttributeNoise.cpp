// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGAttributeNoise.h"

#include "PCGCommon.h"
#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGAsync.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#include "Math/RandomStream.h"
#include "PCGPoint.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGAttributeNoise)

#define LOCTEXT_NAMESPACE "PCGAttributeNoiseSettings"

namespace PCGAttributeNoiseSettings
{
	template <typename T>
	void ProcessNoise(T& InOutValue, FRandomStream& InRandomSource, const UPCGAttributeNoiseSettings* InSettings, const bool bClampResult)
	{
		if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
		{
			const EPCGAttributeNoiseMode Mode = InSettings->Mode;
			const float NoiseMin = InSettings->NoiseMin;
			const float NoiseMax = InSettings->NoiseMax;
			const bool bInvertSource = InSettings->bInvertSource;

			const double Noise = InRandomSource.FRandRange(NoiseMin, NoiseMax);

			if (bInvertSource)
			{
				InOutValue = static_cast<T>(1.0 - InOutValue);
			}

			if (Mode == EPCGAttributeNoiseMode::Minimum)
			{
				InOutValue = FMath::Min<T>(InOutValue, Noise);
			}
			else if (Mode == EPCGAttributeNoiseMode::Maximum)
			{
				InOutValue = FMath::Max<T>(InOutValue, Noise);
			}
			else if (Mode == EPCGAttributeNoiseMode::Add)
			{
				InOutValue = static_cast<T>(InOutValue + Noise);
			}
			else if (Mode == EPCGAttributeNoiseMode::Multiply)
			{
				InOutValue = static_cast<T>(InOutValue * Noise);
			}
			else //if (Mode == EPCGAttributeNoiseMode::Set)
			{
				InOutValue = static_cast<T>(Noise);
			}

			if (bClampResult)
			{
				InOutValue = FMath::Clamp<T>(InOutValue, 0, 1);
			}
		}
		else if constexpr (std::is_same_v<FVector2D, T>)
		{
			ProcessNoise(InOutValue.X, InRandomSource, InSettings, bClampResult);
			ProcessNoise(InOutValue.Y, InRandomSource, InSettings, bClampResult);
		}
		else if constexpr (std::is_same_v<FVector, T>)
		{
			ProcessNoise(InOutValue.X, InRandomSource, InSettings, bClampResult);
			ProcessNoise(InOutValue.Y, InRandomSource, InSettings, bClampResult);
			ProcessNoise(InOutValue.Z, InRandomSource, InSettings, bClampResult);
		}
		else if constexpr (std::is_same_v<FVector4, T> || std::is_same_v<FQuat, T>)
		{
			ProcessNoise(InOutValue.X, InRandomSource, InSettings, bClampResult);
			ProcessNoise(InOutValue.Y, InRandomSource, InSettings, bClampResult);
			ProcessNoise(InOutValue.Z, InRandomSource, InSettings, bClampResult);
			ProcessNoise(InOutValue.W, InRandomSource, InSettings, bClampResult);
		}
		else if constexpr (std::is_same_v<FRotator, T>)
		{
			ProcessNoise(InOutValue.Roll, InRandomSource, InSettings, bClampResult);
			ProcessNoise(InOutValue.Pitch, InRandomSource, InSettings, bClampResult);
			ProcessNoise(InOutValue.Yaw, InRandomSource, InSettings, bClampResult);
		}
	}
}

UPCGAttributeNoiseSettings::UPCGAttributeNoiseSettings()
{
	bUseSeed = true;
	InputSource.SetPointProperty(EPCGPointProperties::Density);
}

void UPCGAttributeNoiseSettings::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (DensityMode_DEPRECATED != EPCGAttributeNoiseMode::Set)
	{
		Mode = DensityMode_DEPRECATED;
		DensityMode_DEPRECATED = EPCGAttributeNoiseMode::Set;
	}

	if (DensityNoiseMin_DEPRECATED != 0.f)
	{
		NoiseMin = DensityNoiseMin_DEPRECATED;
		DensityNoiseMin_DEPRECATED = 0.f;
	}

	if (DensityNoiseMax_DEPRECATED != 1.f)
	{
		NoiseMax = DensityNoiseMax_DEPRECATED;
		DensityNoiseMax_DEPRECATED = 1.f;
	}

	if (bInvertSourceDensity_DEPRECATED)
	{
		bInvertSource = bInvertSourceDensity_DEPRECATED;
		bInvertSourceDensity_DEPRECATED = false;
	}
#endif // WITH_EDITOR
}

#if WITH_EDITOR
void UPCGAttributeNoiseSettings::ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);

	// Overridable properties have been renamed, rename all pins by their counterpart, to avoid breaking existing graphs.
	const TArray<TPair<FName, FName>> OldToNewPinNames =
	{
		{TEXT("Density Mode"), TEXT("Mode")},
		{TEXT("Density Noise Min"), TEXT("Noise Min")},
		{TEXT("Density Noise Max"),TEXT("Noise Max")},
		{TEXT("Invert Source Density"), TEXT("Invert Source")}
	};

	for (const TPair<FName, FName>& OldToNew : OldToNewPinNames)
	{
		InOutNode->RenameInputPin(OldToNew.Key, OldToNew.Value);
	}
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGAttributeNoiseSettings::CreateElement() const
{
	return MakeShared<FPCGAttributeNoiseElement>();
}

FPCGContext* FPCGAttributeNoiseElement::Initialize(const FPCGDataCollection& InInputData, TWeakObjectPtr<UPCGComponent> InSourceComponent, const UPCGNode* InNode)
{
	FPCGAttributeNoiseContext* Context = new FPCGAttributeNoiseContext();
	Context->InputData = InInputData;
	Context->SourceComponent = InSourceComponent;
	Context->Node = InNode;

	return Context;
}

bool FPCGAttributeNoiseElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAttributeNoiseElement::Execute);

	FPCGAttributeNoiseContext* Context = static_cast<FPCGAttributeNoiseContext*>(InContext);

	const UPCGAttributeNoiseSettings* Settings = Context->GetInputSettings<UPCGAttributeNoiseSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Precompute a seed based on the settings one and the component one
	const int Seed = Context->GetSeed();

	while (Context->CurrentInput < Inputs.Num())
	{
		int32 CurrentInput = Context->CurrentInput;
		const FPCGTaggedData& Input = Inputs[CurrentInput];

		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAttributeNoiseElement::InputLoop);
		
		const TArray<FPCGPoint>* InputPoints = nullptr;
		TArray<FPCGPoint>* OutputPoints = nullptr;

		if (!Context->bDataPreparedForCurrentInput)
		{
			const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

			if (!SpatialData)
			{
				PCGE_LOG(Error, GraphAndLog, LOCTEXT("InputMissingSpatialData", "Unable to get Spatial data from input"));
				Context->CurrentInput++;
				continue;
			}

			const UPCGPointData* PointData = SpatialData->ToPointData(Context);

			if (!PointData)
			{
				PCGE_LOG(Error, GraphAndLog, LOCTEXT("InputMissingPointData", "Unable to get Point data from input"));
				Context->CurrentInput++;
				continue;
			}

			InputPoints = &PointData->GetPoints();

			// Create a dummy accessor on the input before allocating the output, to avoid doing useless allocation.
			TUniquePtr<const IPCGAttributeAccessor> TempInputAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(PointData, Settings->InputSource);
			if (!TempInputAccessor)
			{
				Outputs.RemoveAt(Outputs.Num() - 1);
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("CantCreateAccessor", "Could not find Attribute/Property {0}"), FText::FromName(Settings->InputSource.GetName())));
				Context->CurrentInput++;
				continue;
			}

			// Also need to make sure the accessor is a "noisable" type
			if (!PCG::Private::IsOfTypes<int32, int64, float, double, FVector, FVector2D, FVector4, FRotator, FQuat>(TempInputAccessor->GetUnderlyingType()))
			{
				Outputs.RemoveAt(Outputs.Num() - 1);
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("AttributeIsNotANumericalType", "Attribute/Property {0} is not a numerical type, we can't apply noise to it."), FText::FromName(Settings->InputSource.GetName())));
				Context->CurrentInput++;
				continue;
			}

			TempInputAccessor.Reset();

			FPCGTaggedData& Output = Outputs.Add_GetRef(Input);
			UPCGPointData* OutputData = NewObject<UPCGPointData>();
			OutputData->InitializeFromData(PointData);
			OutputPoints = &OutputData->GetMutablePoints();
			OutputPoints->SetNumUninitialized(InputPoints->Num());
			Output.Data = OutputData;

			// Then create the accessor/keys
			Context->InputAccessor = PCGAttributeAccessorHelpers::CreateAccessor(OutputData, Settings->InputSource);
			Context->Keys = PCGAttributeAccessorHelpers::CreateKeys(OutputData, Settings->InputSource);

			// It won't fail because we validated on the input data and output is initialized from input.
			check(Context->InputAccessor && Context->Keys);

			// Allocate temp buffer and create output accessor if needed
			const bool bValid = PCGMetadataAttribute::CallbackWithRightType(Context->InputAccessor->GetUnderlyingType(), [this, Context, Settings, OutputData](auto&& Dummy) -> bool
			{
				using AttributeType = std::decay_t<decltype(Dummy)>;
				int32 NumPoints = Context->Keys->GetNum();
				Context->TempValuesBuffer.SetNumUninitialized(sizeof(AttributeType) * NumPoints);

				// If we have a different output target, create accessor (and also create the attribute if it doesn't exist), and check that we can broadcast to it
				if (Settings->bOutputTargetDifferentFromInputSource)
				{
					Context->OptionalOutputAccessor = PCGAttributeAccessorHelpers::CreateAccessor(OutputData, Settings->OutputTarget);
					if (!Context->OptionalOutputAccessor && Settings->OutputTarget.Selection == EPCGAttributePropertySelection::Attribute)
					{
						OutputData->Metadata->CreateAttribute<AttributeType>(Settings->OutputTarget.GetName(), AttributeType{}, /*bAllowsInterpolation=*/ true, /*bOverrideParent=*/false);
						Context->OptionalOutputAccessor = PCGAttributeAccessorHelpers::CreateAccessor(OutputData, Settings->OutputTarget);
					}

					if (!Context->OptionalOutputAccessor)
					{
						PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("OutputTargetInvalid", "Failed to find/create Attribute/Property {0}."), FText::FromName(Settings->OutputTarget.GetName())));
						return false;
					}

					if (!PCG::Private::IsBroadcastable(Context->InputAccessor->GetUnderlyingType(), Context->OptionalOutputAccessor->GetUnderlyingType()))
					{
						PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("CantBroadcast", "Cannot convert Attribute {0} ({1}) into Attribute {2} ({3})."),
							FText::FromName(Settings->InputSource.GetName()),
							FText::FromString(PCG::Private::GetTypeName(Context->InputAccessor->GetUnderlyingType())),
							FText::FromName(Settings->OutputTarget.GetName()),
							FText::FromString(PCG::Private::GetTypeName(Context->OptionalOutputAccessor->GetUnderlyingType()))));
						return false;
					}
				}

				return true;
			});

			if (!bValid)
			{
				Outputs.RemoveAt(Outputs.Num() - 1);
				Context->CurrentInput++;
				continue;
			}

			Context->bDataPreparedForCurrentInput = true;
		}
		else
		{
			OutputPoints = &CastChecked<UPCGPointData>(Outputs[CurrentInput].Data)->GetMutablePoints();
			InputPoints = &CastChecked<const UPCGPointData>(Inputs[CurrentInput].Data)->GetPoints();
		}

		check(InputPoints && OutputPoints);

		// Force clamp on Density
		const FPCGAttributePropertySelector& Selector = Settings->bOutputTargetDifferentFromInputSource ? Settings->OutputTarget : Settings->InputSource;
		const bool bClampResult = Settings->bClampResult || (Selector.Selection == EPCGAttributePropertySelection::PointProperty && Selector.PointProperty == EPCGPointProperties::Density);

		// Dummy Initialize, we already initialized the output points before.
		auto Initialize = []() {};

		const bool bDone = FPCGAsync::AsyncProcessingOneToOneEx(&Context->AsyncState, OutputPoints->Num(), Initialize, [InputPoints, OutputPoints, Settings, Seed, bClampResult, Context](int32 StartReadIndex, int32 StartWriteIndex) -> int32
		{
			PCGMetadataAttribute::CallbackWithRightType(Context->InputAccessor->GetUnderlyingType(), [Settings, StartReadIndex, StartWriteIndex, InputPoints, OutputPoints, Context, Seed, bClampResult](auto&& Dummy)
			{
				using AttributeType = std::decay_t<decltype(Dummy)>;

				// Copying the point
				FPCGPoint& OutPoint = (*OutputPoints)[StartWriteIndex];
				OutPoint = (*InputPoints)[StartReadIndex];

				// Reinterpret the buffer to store our temporary value.
				AttributeType& Value = *(reinterpret_cast<AttributeType*>(Context->TempValuesBuffer.GetData()) + StartReadIndex);

				if (Context->InputAccessor->Get<AttributeType>(Value, StartReadIndex, *Context->Keys))
				{
					FRandomStream RandomSource(PCGHelpers::ComputeSeed(Seed, OutPoint.Seed));
					PCGAttributeNoiseSettings::ProcessNoise(Value, RandomSource, Settings, bClampResult);
				}
			});

			return true;
		}, /*bEnableTimeSlicing=*/true);

		if (bDone)
		{
			PCGMetadataAttribute::CallbackWithRightType(Context->InputAccessor->GetUnderlyingType(), [Context](auto&& Value)
			{
				using AttributeType = std::decay_t<decltype(Value)>;
				int32 NumPoints = Context->Keys->GetNum();
				TArrayView<AttributeType> Values(reinterpret_cast<AttributeType*>(Context->TempValuesBuffer.GetData()), NumPoints);
				if (Context->OptionalOutputAccessor)
				{
					Context->OptionalOutputAccessor->SetRange<AttributeType>(Values, 0, *Context->Keys, EPCGAttributeAccessorFlags::AllowBroadcast);
				}
				else
				{
					Context->InputAccessor->SetRange<AttributeType>(Values, 0, *Context->Keys);
				}
			});
			
			Context->CurrentInput++;
			Context->bDataPreparedForCurrentInput = false;
		}
		
		if (!bDone || Context->ShouldStop())
		{
			return false;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
