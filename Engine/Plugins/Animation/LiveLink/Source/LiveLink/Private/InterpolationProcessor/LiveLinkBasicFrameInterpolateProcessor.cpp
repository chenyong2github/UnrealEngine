// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InterpolationProcessor/LiveLinkBasicFrameInterpolateProcessor.h"
#include "Roles/LiveLinkBasicRole.h"
#include "Roles/LiveLinkBasicTypes.h"


/**
 * ULiveLinkFrameInterpolationProcessor
 */
TSubclassOf<ULiveLinkRole> ULiveLinkBasicFrameInterpolationProcessor::GetRole() const
{
	return ULiveLinkBasicRole::StaticClass();
}

ULiveLinkFrameInterpolationProcessor::FWorkerSharedPtr ULiveLinkBasicFrameInterpolationProcessor::FetchWorker()
{
	if (!BaseInstance.IsValid())
	{
		BaseInstance = MakeShared<FLiveLinkBasicFrameInterpolationProcessorWorker, ESPMode::ThreadSafe>(bInterpolatePropertyValues);
	}

	return BaseInstance;
}

ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::FLiveLinkBasicFrameInterpolationProcessorWorker(bool bInInterpolatePropertyValues)
	: bInterpolatePropertyValues(bInInterpolatePropertyValues)
{}

TSubclassOf<ULiveLinkRole> ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::GetRole() const
{
	return ULiveLinkBasicRole::StaticClass();
}

namespace LiveLinkInterpolation
{
	void Interpolate(const UStruct* InStruct, bool bCheckForInterpFlag, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData);
	void InterpolateProperty(FProperty* Property, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData);

	template<class T>
	T BlendValue(const T& A, const T& B, float InBlendWeight)
	{
		return FMath::Lerp(A, B, InBlendWeight);
	}

	template<>
	FTransform BlendValue(const FTransform& A, const FTransform& B, float InBlendWeight)
	{
		const ScalarRegister ABlendWeight(1.0f - InBlendWeight);
		const ScalarRegister BBlendWeight(InBlendWeight);

		FTransform Output = A * ABlendWeight;
		Output.AccumulateWithShortestRotation(B, BBlendWeight);
		Output.NormalizeRotation();
		return Output;
	}

	template<class T>
	void Interpolate(const FStructProperty* StructProperty, float InBlendWeight, const void* DataA, const void* DataB, void* DataResult)
	{
		for (int32 ArrayIndex = 0; ArrayIndex < StructProperty->ArrayDim; ++ArrayIndex)
		{
			const T* ValuePtrA = StructProperty->ContainerPtrToValuePtr<T>(DataA, ArrayIndex);
			const T* ValuePtrB = StructProperty->ContainerPtrToValuePtr<T>(DataB, ArrayIndex);
			T* ValuePtrResult = StructProperty->ContainerPtrToValuePtr<T>(DataResult, ArrayIndex);

			T ValueResult = BlendValue(*ValuePtrA, *ValuePtrB, InBlendWeight);
			StructProperty->CopySingleValue(ValuePtrResult, &ValueResult);
		}
	}

	void Interpolate(const UStruct* InStruct, bool bCheckForInterpFlag, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData)
	{
		for (TFieldIterator<FProperty> Itt(InStruct); Itt; ++Itt)
		{
			FProperty* Property = *Itt;
			if (!bCheckForInterpFlag || Property->HasAnyPropertyFlags(CPF_Interp))
			{
				if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
				{
					for (int32 DimIndex = 0; DimIndex < ArrayProperty->ArrayDim; ++DimIndex)
					{
						const void* Data0 = ArrayProperty->ContainerPtrToValuePtr<const void>(InFrameDataA, DimIndex);
						const void* Data1 = ArrayProperty->ContainerPtrToValuePtr<const void>(InFrameDataB, DimIndex);
						void* DataResult = ArrayProperty->ContainerPtrToValuePtr<void>(OutFrameData, DimIndex);

						FScriptArrayHelper ArrayHelperA(ArrayProperty, Data0);
						FScriptArrayHelper ArrayHelperB(ArrayProperty, Data1);
						FScriptArrayHelper ArrayHelperResult(ArrayProperty, DataResult);

						int32 MinValue = FMath::Min(ArrayHelperA.Num(), FMath::Min(ArrayHelperB.Num(), ArrayHelperResult.Num()));
						for (int32 ArrayIndex = 0; ArrayIndex < MinValue; ++ArrayIndex)
						{
							InterpolateProperty(ArrayProperty->Inner, InBlendWeight, ArrayHelperA.GetRawPtr(ArrayIndex), ArrayHelperB.GetRawPtr(ArrayIndex), ArrayHelperResult.GetRawPtr(ArrayIndex));
						}
					}
				}
				else
				{
					InterpolateProperty(Property, InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
				}
			}
		}
	}

	void InterpolateProperty(FProperty* Property, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData)
	{
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct->GetFName() == NAME_Vector)
			{
				Interpolate<FVector>(StructProperty, InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Vector4)
			{
				Interpolate<FVector4>(StructProperty, InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Rotator)
			{
				Interpolate<FRotator>(StructProperty, InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Quat)
			{
				Interpolate<FQuat>(StructProperty, InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Transform)
			{
				Interpolate<FTransform>(StructProperty, InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_LinearColor)
			{
				Interpolate<FLinearColor>(StructProperty, InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
			}
			else
			{
				for (int32 ArrayIndex = 0; ArrayIndex < StructProperty->ArrayDim; ++ArrayIndex)
				{
					const void* Data0 = StructProperty->ContainerPtrToValuePtr<const void>(InFrameDataA, ArrayIndex);
					const void* Data1 = StructProperty->ContainerPtrToValuePtr<const void>(InFrameDataB, ArrayIndex);
					void* DataResult = StructProperty->ContainerPtrToValuePtr<void>(OutFrameData, ArrayIndex);
					Interpolate(StructProperty->Struct, false, InBlendWeight, Data0, Data1, DataResult);
				}
			}
		}
		else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				for (int32 ArrayIndex = 0; ArrayIndex < NumericProperty->ArrayDim; ++ArrayIndex)
				{
					const void* Data0 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataA, ArrayIndex);
					double Value0 = NumericProperty->GetFloatingPointPropertyValue(Data0);
					const void* Data1 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataB, ArrayIndex);
					double Value1 = NumericProperty->GetFloatingPointPropertyValue(Data1);

					double ValueResult = FMath::Lerp(Value0, Value1, InBlendWeight);

					void* DataResult = NumericProperty->ContainerPtrToValuePtr<void>(OutFrameData, ArrayIndex);
					NumericProperty->SetFloatingPointPropertyValue(DataResult, ValueResult);
				}
			}
			else if (NumericProperty->IsInteger() && !NumericProperty->IsEnum())
			{
				for (int32 ArrayIndex = 0; ArrayIndex < NumericProperty->ArrayDim; ++ArrayIndex)
				{
					const void* Data0 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataA, ArrayIndex);
					int64 Value0 = NumericProperty->GetSignedIntPropertyValue(Data0);
					const void* Data1 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataB, ArrayIndex);
					int64 Value1 = NumericProperty->GetSignedIntPropertyValue(Data1);

					int64 ValueResult = FMath::Lerp(Value0, Value1, InBlendWeight);

					void* DataResult = NumericProperty->ContainerPtrToValuePtr<void>(OutFrameData, ArrayIndex);
					NumericProperty->SetIntPropertyValue(DataResult, ValueResult);
				}
			}
		}
	}

	template<class TTimeType>
	void Interpolate(TTimeType InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame, bool bInInterpolatePropertyValues)
	{
		int32 FrameDataIndexA = INDEX_NONE;
		int32 FrameDataIndexB = INDEX_NONE;
		if (ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::FindInterpolateIndex(InTime, InSourceFrames, FrameDataIndexA, FrameDataIndexB))
		{
			if (FrameDataIndexA == FrameDataIndexB)
			{
				// Copy over the frame directly
				OutBlendedFrame.FrameData.InitializeWith(InSourceFrames[FrameDataIndexA]);
			}
			else
			{
				const FLiveLinkFrameDataStruct& FrameDataA = InSourceFrames[FrameDataIndexA];
				const FLiveLinkFrameDataStruct& FrameDataB = InSourceFrames[FrameDataIndexB];

				const double BlendFactor = ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::GetBlendFactor(InTime, FrameDataA, FrameDataB);
				if (FMath::IsNearlyZero(BlendFactor))
				{
					OutBlendedFrame.FrameData.InitializeWith(FrameDataA);
				}
				else if (FMath::IsNearlyEqual(1.0, BlendFactor))
				{
					OutBlendedFrame.FrameData.InitializeWith(FrameDataB);
				}
				else
				{
					ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::FGenericInterpolateOptions InterpolationOptions;
					InterpolationOptions.bInterpolatePropertyValues = bInInterpolatePropertyValues;

					ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::GenericInterpolate(BlendFactor, InterpolationOptions, FrameDataA, FrameDataB, OutBlendedFrame.FrameData);
				}
			}
		}
		else if (InSourceFrames.Num())
		{
			OutBlendedFrame.FrameData.InitializeWith(InSourceFrames[0].GetStruct(), InSourceFrames[0].GetBaseData());
		}
	}
}

void ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::Interpolate(double InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame)
{
	LiveLinkInterpolation::Interpolate(InTime, InStaticData, InSourceFrames, OutBlendedFrame, bInterpolatePropertyValues);
}

void ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::Interpolate(const FQualifiedFrameTime& InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame)
{
	LiveLinkInterpolation::Interpolate(InTime, InStaticData, InSourceFrames, OutBlendedFrame, bInterpolatePropertyValues);
}

void ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::GenericInterpolate(double InBlendWeight, const FGenericInterpolateOptions& Options, const FLiveLinkFrameDataStruct& FrameDataA, const FLiveLinkFrameDataStruct& FrameDataB, FLiveLinkFrameDataStruct& OutBlendedFrameData)
{
	check(FrameDataA.GetStruct() == FrameDataB.GetStruct());

	const FLiveLinkFrameDataStruct& FrameWhenCanNotBlend = (InBlendWeight > 0.5f) ? FrameDataB : FrameDataA;

	if (Options.bCopyClosestFrame)
	{
		OutBlendedFrameData.InitializeWith(FrameDataA.GetStruct(), FrameWhenCanNotBlend.GetBaseData());
	}
	else
	{
		OutBlendedFrameData.InitializeWith(FrameDataA.GetStruct(), nullptr);
		if (Options.bCopyClosestMetaData)
		{
			OutBlendedFrameData.GetBaseData()->MetaData = FrameWhenCanNotBlend.GetBaseData()->MetaData;
		}
	}

	OutBlendedFrameData.GetBaseData()->WorldTime = FLiveLinkWorldTime(FMath::Lerp(FrameDataA.GetBaseData()->WorldTime.GetOffsettedTime(), FrameDataB.GetBaseData()->WorldTime.GetOffsettedTime(), InBlendWeight), 0.0);

	if (Options.bInterpolatePropertyValues)
	{
		const TArray<float>& PropertiesA = FrameDataA.GetBaseData()->PropertyValues;
		const TArray<float>& PropertiesB = FrameDataB.GetBaseData()->PropertyValues;
		TArray<float>& PropertiesResult = OutBlendedFrameData.GetBaseData()->PropertyValues;

		int32 NumOfProperties = FMath::Min(PropertiesA.Num(), PropertiesB.Num());
		PropertiesResult.SetNum(NumOfProperties);

		for (int32 PropertyIndex = 0; PropertyIndex < NumOfProperties; ++PropertyIndex)
		{
			PropertiesResult[PropertyIndex] = FMath::Lerp(PropertiesA[PropertyIndex], PropertiesB[PropertyIndex], InBlendWeight);
		}
	}
	else
	{
		OutBlendedFrameData.GetBaseData()->PropertyValues = FrameWhenCanNotBlend.GetBaseData()->PropertyValues;
	}

	if (Options.bInterpolateInterpProperties)
	{
		LiveLinkInterpolation::Interpolate(FrameDataA.GetStruct(), true, InBlendWeight, FrameDataA.GetBaseData(), FrameDataB.GetBaseData(), OutBlendedFrameData.GetBaseData());
	}
}

double ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::GetBlendFactor(double InTime, const FLiveLinkFrameDataStruct& FrameDataA, const FLiveLinkFrameDataStruct& FrameDataB)
{
	const double FrameATime = FrameDataA.GetBaseData()->WorldTime.GetOffsettedTime();
	const double FrameBTime = FrameDataB.GetBaseData()->WorldTime.GetOffsettedTime();
	return (InTime - FrameATime) / (FrameBTime - FrameATime);
}

double ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::GetBlendFactor(FQualifiedFrameTime InTime, const FLiveLinkFrameDataStruct& FrameDataA, const FLiveLinkFrameDataStruct& FrameDataB)
{
	const double FrameASeconds = FrameDataA.GetBaseData()->MetaData.SceneTime.AsSeconds();
	const double FrameBSeconds = FrameDataB.GetBaseData()->MetaData.SceneTime.AsSeconds();
	return (InTime.AsSeconds() - FrameASeconds) / (FrameBSeconds - FrameASeconds);
}

 bool ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::FindInterpolateIndex(double InTime, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, int32& OutFrameIndexA, int32& OutFrameIndexB)
 {
	if (InSourceFrames.Num() == 0)
	{
		return false;
	}

	for (int32 FrameIndex = InSourceFrames.Num() - 1; FrameIndex >= 0; --FrameIndex)
	{
		const FLiveLinkFrameDataStruct& SourceFrameData = InSourceFrames[FrameIndex];
		if (SourceFrameData.GetBaseData()->WorldTime.GetOffsettedTime() <= InTime)
		{
			if (FrameIndex == InSourceFrames.Num() - 1)
			{
				OutFrameIndexA = FrameIndex;
				OutFrameIndexB = FrameIndex;
				return true;
			}
			else
			{

				OutFrameIndexA = FrameIndex;
				OutFrameIndexB = FrameIndex + 1;
				return true;
			}
		}
	}

	return false;
}

 bool ULiveLinkBasicFrameInterpolationProcessor::FLiveLinkBasicFrameInterpolationProcessorWorker::FindInterpolateIndex(FQualifiedFrameTime InTime, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, int32& OutFrameIndexA, int32& OutFrameIndexB)
 {
	 if (InSourceFrames.Num() == 0)
	 {
		 return false;
	 }

	const double InTimeInSeconds = InTime.AsSeconds();
	for (int32 FrameIndex = InSourceFrames.Num() - 1; FrameIndex >= 0; --FrameIndex)
	{
		const FLiveLinkFrameDataStruct& SourceFrameData = InSourceFrames[FrameIndex];
		if (SourceFrameData.GetBaseData()->MetaData.SceneTime.AsSeconds() <= InTimeInSeconds)
		{
			if (FrameIndex == InSourceFrames.Num() - 1)
			{
				OutFrameIndexA = FrameIndex;
				OutFrameIndexB = FrameIndex;
				return true;
			}
			else
			{
				OutFrameIndexA = FrameIndex;
				OutFrameIndexB = FrameIndex + 1;
				return true;
			}
		}
	}

	return false;
 }
