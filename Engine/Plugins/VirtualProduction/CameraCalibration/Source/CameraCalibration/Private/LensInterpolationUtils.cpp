// Copyright Epic Games, Inc. All Rights Reserved.


#include "LensInterpolationUtils.h"

#include "LensData.h"


//Property interpolation utils largely inspired from livelink interp code
namespace LensInterpolationUtils
{
	void BilinearInterpolateProperty(FProperty* Property
		, float MainCoefficient
		, float DeltaMinFocus
		, float DeltaMaxFocus
		, float DeltaMinZoom
		, float DeltaMaxZoom
		, const void* DataA, const void* InDataB, const void* InDataC, const void* InDataD, void* OutData);

	void InterpolateProperty(FProperty* Property, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData);

	template<typename Type>
	void BilinearInterpolate(const FStructProperty* StructProperty
		, float MainCoefficient
		, float DeltaMinFocus
		, float DeltaMaxFocus
		, float DeltaMinZoom
		, float DeltaMaxZoom
		, const void* DataA
		, const void* DataB
		, const void* DataC
		, const void* DataD
		, void* DataResult)
	{
		const Type* ValuePtrA = StructProperty->ContainerPtrToValuePtr<Type>(DataA);
		const Type* ValuePtrB = StructProperty->ContainerPtrToValuePtr<Type>(DataB);
		const Type* ValuePtrC = StructProperty->ContainerPtrToValuePtr<Type>(DataC);
		const Type* ValuePtrD = StructProperty->ContainerPtrToValuePtr<Type>(DataD);
		Type* ValuePtrResult = StructProperty->ContainerPtrToValuePtr<Type>(DataResult);

		Type ValueResult = BilinearBlendValue(MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, *ValuePtrA, *ValuePtrB, *ValuePtrC, *ValuePtrD);
		StructProperty->CopySingleValue(ValuePtrResult, &ValueResult);
	}

	void BilinearInterpolate(const UStruct* InStruct
		, float MainCoefficient
		, float DeltaMinFocus
		, float DeltaMaxFocus
		, float DeltaMinZoom
		, float DeltaMaxZoom
		, const void* DataA, const void* DataB, const void* DataC, const void* DataD, void* OutFrameData)
	{
		for (TFieldIterator<FProperty> Itt(InStruct); Itt; ++Itt)
		{
			FProperty* Property = *Itt;

			//Add support for arrays if required
			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				//ArrayProps have an ArrayDim of 1 but just to be sure...
				for (int32 DimIndex = 0; DimIndex < ArrayProperty->ArrayDim; ++DimIndex)
				{
					const void* FrameDataA = Property->ContainerPtrToValuePtr<const void>(DataA, DimIndex);
					const void* FrameDataB = Property->ContainerPtrToValuePtr<const void>(DataB, DimIndex);
					const void* FrameDataC = Property->ContainerPtrToValuePtr<const void>(DataC, DimIndex);
					const void* FrameDataD = Property->ContainerPtrToValuePtr<const void>(DataD, DimIndex);
					void* DataResult = Property->ContainerPtrToValuePtr<void>(OutFrameData, DimIndex);

					FScriptArrayHelper ArrayHelperA(ArrayProperty, FrameDataA);
					FScriptArrayHelper ArrayHelperB(ArrayProperty, FrameDataB);
					FScriptArrayHelper ArrayHelperC(ArrayProperty, FrameDataC);
					FScriptArrayHelper ArrayHelperD(ArrayProperty, FrameDataD);
					FScriptArrayHelper ArrayHelperResult(ArrayProperty, DataResult);

					const int32 MinValue = FMath::Min(ArrayHelperA.Num(), FMath::Min(ArrayHelperB.Num(), FMath::Min(ArrayHelperC.Num(), ArrayHelperD.Num())));
					ArrayHelperResult.Resize(MinValue);

					for (int32 ArrayIndex = 0; ArrayIndex < MinValue; ++ArrayIndex)
					{
						BilinearInterpolateProperty(ArrayProperty->Inner, MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom
							, ArrayHelperA.GetRawPtr(ArrayIndex)
							, ArrayHelperB.GetRawPtr(ArrayIndex)
							, ArrayHelperC.GetRawPtr(ArrayIndex)
							, ArrayHelperD.GetRawPtr(ArrayIndex)
							, ArrayHelperResult.GetRawPtr(ArrayIndex));
					}
				}
			}
			else if (Property->ArrayDim > 1)
			{
				for (int32 DimIndex = 0; DimIndex < Property->ArrayDim; ++DimIndex)
				{
					const void* FrameDataA = Property->ContainerPtrToValuePtr<const void>(DataA, DimIndex);
					const void* FrameDataB = Property->ContainerPtrToValuePtr<const void>(DataB, DimIndex);
					const void* FrameDataC = Property->ContainerPtrToValuePtr<const void>(DataC, DimIndex);
					const void* FrameDataD = Property->ContainerPtrToValuePtr<const void>(DataD, DimIndex);
					void* DataResult = Property->ContainerPtrToValuePtr<void>(OutFrameData, DimIndex);

					BilinearInterpolateProperty(Property, MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, FrameDataA, FrameDataB, FrameDataC, FrameDataD, DataResult);
				}
			}
			else
			{
				BilinearInterpolateProperty(Property, MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, DataA, DataB, DataC, DataD, OutFrameData);
			}

		}
	}

	void BilinearInterpolateProperty(FProperty* Property
		, float MainCoefficient
		, float DeltaMinFocus
		, float DeltaMaxFocus
		, float DeltaMinZoom
		, float DeltaMaxZoom
		, const void* InFrameDataA, const void* InFrameDataB, const void* InFrameDataC, const void* InFrameDataD, void* OutFrameData)
	{
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct->GetFName() == NAME_Vector)
			{
				BilinearInterpolate<FVector>(StructProperty, MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, InFrameDataA, InFrameDataB, InFrameDataC, InFrameDataD, OutFrameData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Vector2D)
			{
				BilinearInterpolate<FVector2D>(StructProperty, MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, InFrameDataA, InFrameDataB, InFrameDataC, InFrameDataD, OutFrameData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Vector4)
			{
				BilinearInterpolate<FVector4>(StructProperty, MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, InFrameDataA, InFrameDataB, InFrameDataC, InFrameDataD, OutFrameData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Rotator)
			{
				BilinearInterpolate<FRotator>(StructProperty, MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, InFrameDataA, InFrameDataB, InFrameDataC, InFrameDataD, OutFrameData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Quat)
			{
				BilinearInterpolate<FQuat>(StructProperty, MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, InFrameDataA, InFrameDataB, InFrameDataC, InFrameDataD, OutFrameData);
			}
			else
			{
				const void* Data0 = StructProperty->ContainerPtrToValuePtr<const void>(InFrameDataA);
				const void* Data1 = StructProperty->ContainerPtrToValuePtr<const void>(InFrameDataB);
				const void* Data2 = StructProperty->ContainerPtrToValuePtr<const void>(InFrameDataC);
				const void* Data3 = StructProperty->ContainerPtrToValuePtr<const void>(InFrameDataD);
				void* DataResult = StructProperty->ContainerPtrToValuePtr<void>(OutFrameData);
				BilinearInterpolate(StructProperty->Struct, MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, Data0, Data1, Data2, Data3, DataResult);
			}
		}
		else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				const void* Data0 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataA);
				double Value0 = NumericProperty->GetFloatingPointPropertyValue(Data0);
				const void* Data1 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataB);
				double Value1 = NumericProperty->GetFloatingPointPropertyValue(Data1);
				const void* Data2 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataC);
				double Value2 = NumericProperty->GetFloatingPointPropertyValue(Data2);
				const void* Data3 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataD);
				double Value3 = NumericProperty->GetFloatingPointPropertyValue(Data3);


				double ValueResult = BilinearBlendValue(MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, Value0, Value1, Value2, Value3);

				void* DataResult = NumericProperty->ContainerPtrToValuePtr<void>(OutFrameData);
				NumericProperty->SetFloatingPointPropertyValue(DataResult, ValueResult);
			}
			else if (NumericProperty->IsInteger() && !NumericProperty->IsEnum())
			{
				const void* Data0 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataA);
				int64 Value0 = NumericProperty->GetSignedIntPropertyValue(Data0);
				const void* Data1 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataB);
				int64 Value1 = NumericProperty->GetSignedIntPropertyValue(Data1);
				const void* Data2 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataC);
				int64 Value2 = NumericProperty->GetSignedIntPropertyValue(Data2);
				const void* Data3 = NumericProperty->ContainerPtrToValuePtr<const void>(InFrameDataD);
				int64 Value3 = NumericProperty->GetSignedIntPropertyValue(Data3);

				int64 ValueResult = BilinearBlendValue(MainCoefficient, DeltaMinFocus, DeltaMaxFocus, DeltaMinZoom, DeltaMaxZoom, Value0, Value1, Value2, Value3);

				void* DataResult = NumericProperty->ContainerPtrToValuePtr<void>(OutFrameData);
				NumericProperty->SetIntPropertyValue(DataResult, ValueResult);
			}
		}
	}

	template<typename Type>
	void Interpolate(const FStructProperty* StructProperty, float InBlendWeight, const void* DataA, const void* DataB, void* DataResult)
	{
		const Type* ValuePtrA = StructProperty->ContainerPtrToValuePtr<Type>(DataA);
		const Type* ValuePtrB = StructProperty->ContainerPtrToValuePtr<Type>(DataB);
		Type* ValuePtrResult = StructProperty->ContainerPtrToValuePtr<Type>(DataResult);

		Type ValueResult = BlendValue(InBlendWeight, *ValuePtrA, *ValuePtrB);
		StructProperty->CopySingleValue(ValuePtrResult, &ValueResult);
	}

	void Interpolate(const UStruct* InStruct, float InBlendWeight, const void* InFrameDataA, const void* InFrameDataB, void* OutFrameData)
	{
		for (TFieldIterator<FProperty> Itt(InStruct); Itt; ++Itt)
		{
			FProperty* Property = *Itt;

			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				//ArrayProps have an ArrayDim of 1 but just to be sure...
				for (int32 DimIndex = 0; DimIndex < ArrayProperty->ArrayDim; ++DimIndex)
				{
					const void* Data0 = ArrayProperty->ContainerPtrToValuePtr<const void>(InFrameDataA, DimIndex);
					const void* Data1 = ArrayProperty->ContainerPtrToValuePtr<const void>(InFrameDataB, DimIndex);
					void* DataResult = ArrayProperty->ContainerPtrToValuePtr<void>(OutFrameData, DimIndex);

					FScriptArrayHelper ArrayHelperA(ArrayProperty, Data0);
					FScriptArrayHelper ArrayHelperB(ArrayProperty, Data1);
					FScriptArrayHelper ArrayHelperResult(ArrayProperty, DataResult);

					const int32 MinValue = FMath::Min(ArrayHelperA.Num(), ArrayHelperB.Num());
					ArrayHelperResult.Resize(MinValue);

					for (int32 ArrayIndex = 0; ArrayIndex < MinValue; ++ArrayIndex)
					{
						InterpolateProperty(ArrayProperty->Inner, InBlendWeight, ArrayHelperA.GetRawPtr(ArrayIndex), ArrayHelperB.GetRawPtr(ArrayIndex), ArrayHelperResult.GetRawPtr(ArrayIndex));
					}
				}
			}
			else if (Property->ArrayDim > 1)
			{
				for (int32 DimIndex = 0; DimIndex < Property->ArrayDim; ++DimIndex)
				{
					const void* Data0 = Property->ContainerPtrToValuePtr<const void>(InFrameDataA, DimIndex);
					const void* Data1 = Property->ContainerPtrToValuePtr<const void>(InFrameDataB, DimIndex);
					void* DataResult = Property->ContainerPtrToValuePtr<void>(OutFrameData, DimIndex);

					InterpolateProperty(Property, InBlendWeight, Data0, Data1, DataResult);
				}
			}
			else
			{
				InterpolateProperty(Property, InBlendWeight, InFrameDataA, InFrameDataB, OutFrameData);
			}
		}
	}

	void InterpolateProperty(FProperty* Property, float InBlendWeight, const void* InDataA, const void* InDataB, void* OutData)
	{
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct->GetFName() == NAME_Vector)
			{
				Interpolate<FVector>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Vector2D)
			{
				Interpolate<FVector2D>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Vector4)
			{
				Interpolate<FVector4>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Rotator)
			{
				Interpolate<FRotator>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else if (StructProperty->Struct->GetFName() == NAME_Quat)
			{
				Interpolate<FQuat>(StructProperty, InBlendWeight, InDataA, InDataB, OutData);
			}
			else
			{
				const void* Data0 = StructProperty->ContainerPtrToValuePtr<const void>(InDataA);
				const void* Data1 = StructProperty->ContainerPtrToValuePtr<const void>(InDataB);
				void* DataResult = StructProperty->ContainerPtrToValuePtr<void>(OutData);
				Interpolate(StructProperty->Struct, InBlendWeight, Data0, Data1, DataResult);
			}
		}
		else if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				const void* Data0 = NumericProperty->ContainerPtrToValuePtr<const void>(InDataA);
				const double Value0 = NumericProperty->GetFloatingPointPropertyValue(Data0);
				const void* Data1 = NumericProperty->ContainerPtrToValuePtr<const void>(InDataB);
				const double Value1 = NumericProperty->GetFloatingPointPropertyValue(Data1);

				const double ValueResult = BlendValue(InBlendWeight, Value0, Value1);

				void* DataResult = NumericProperty->ContainerPtrToValuePtr<void>(OutData);
				NumericProperty->SetFloatingPointPropertyValue(DataResult, ValueResult);
			}
			else if (NumericProperty->IsInteger() && !NumericProperty->IsEnum())
			{
				const void* Data0 = NumericProperty->ContainerPtrToValuePtr<const void>(InDataA);
				const int64 Value0 = NumericProperty->GetSignedIntPropertyValue(Data0);
				const void* Data1 = NumericProperty->ContainerPtrToValuePtr<const void>(InDataB);
				const int64 Value1 = NumericProperty->GetSignedIntPropertyValue(Data1);

				const int64 ValueResult = BlendValue(InBlendWeight, Value0, Value1);

				void* DataResult = NumericProperty->ContainerPtrToValuePtr<void>(OutData);
				NumericProperty->SetIntPropertyValue(DataResult, ValueResult);
			}
		}
	}

	bool FindInterpolationIndex(float InRawValue, TArrayView<FEncoderPoint> InSourceData, int32& OutPointIndexA, int32& OutPointIndexB)
	{
		if (InSourceData.Num() <= 0)
		{
			return false;
		}

		for (int32 Index = InSourceData.Num() - 1; Index >= 0; --Index)
		{
			const FEncoderPoint& DataPoint = InSourceData[Index];
			if (DataPoint.NormalizedValue <= InRawValue)
			{
				if (Index == InSourceData.Num() - 1)
				{
					OutPointIndexA = Index;
					OutPointIndexB = Index;
					return true;
				}
				else
				{
					OutPointIndexA = Index;
					OutPointIndexB = Index + 1;
					return true;
				}
			}
		}

		OutPointIndexA = 0;
		OutPointIndexB = 0;
		return true;
	}

	bool InterpolateEncoderValue(float InNormalizedValue, TArrayView<FEncoderPoint> InSourceData, float& OutEvaluatedValue)
	{
		int32 PointAIndex = 0;
		int32 PointBIndex = 0;
		if (FindInterpolationIndex(InNormalizedValue, InSourceData, PointAIndex, PointBIndex))
		{
			check(InSourceData.IsValidIndex(PointAIndex) && InSourceData.IsValidIndex(PointBIndex));

			const FEncoderPoint& PointA = InSourceData[PointAIndex];
			const FEncoderPoint& PointB = InSourceData[PointBIndex];

			const float BlendingFactor = GetBlendFactor(InNormalizedValue, PointA.NormalizedValue, PointB.NormalizedValue);
			OutEvaluatedValue = BlendValue(BlendingFactor, PointA.ValueInPhysicalUnits, PointB.ValueInPhysicalUnits);
			return true;
		}

		return false;
	}

	float GetBlendFactor(float InValue, float ValueA, float ValueB)
	{
		//Keep input in range
		InValue = FMath::Clamp(InValue, ValueA, ValueB);

		const float Divider = ValueB - ValueA;
		if (!FMath::IsNearlyZero(Divider))
		{
			return (InValue - ValueA) / Divider;
		}
		else
		{
			return 1.0f;
		}
	}
}


