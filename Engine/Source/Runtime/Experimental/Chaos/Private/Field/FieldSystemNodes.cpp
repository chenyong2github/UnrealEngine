// Copyright Epic Games, Inc. All Rights Reserved.

#include "Field/FieldSystemNodes.h"
#include "Field/FieldSystemNoiseAlgo.h"
#include "Async/ParallelFor.h"
#include "Chaos/Vector.h"

FFieldNodeBase * FieldNodeFactory(FFieldNodeBase::EFieldType BaseType,FFieldNodeBase::ESerializationType Type)
{
	switch (Type)
	{
	case FFieldNodeBase::ESerializationType::FieldNode_FUniformInteger:
		return new FUniformInteger();
	case FFieldNodeBase::ESerializationType::FieldNode_FRadialIntMask:
		return new FRadialIntMask();
	case FFieldNodeBase::ESerializationType::FieldNode_FUniformScalar:
		return new FUniformScalar();
	case FFieldNodeBase::ESerializationType::FieldNode_FWaveScalar:
		return new FWaveScalar();
	case FFieldNodeBase::ESerializationType::FieldNode_FRadialFalloff:
		return new FRadialFalloff();
	case FFieldNodeBase::ESerializationType::FieldNode_FPlaneFalloff:
		return new FPlaneFalloff();
	case FFieldNodeBase::ESerializationType::FieldNode_FBoxFalloff:
		return new FBoxFalloff();
	case FFieldNodeBase::ESerializationType::FieldNode_FNoiseField:
		return new FNoiseField();
	case FFieldNodeBase::ESerializationType::FieldNode_FUniformVector:
		return new FUniformVector();
	case FFieldNodeBase::ESerializationType::FieldNode_FRadialVector:
		return new FRadialVector();
	case FFieldNodeBase::ESerializationType::FieldNode_FRandomVector:
		return new FRandomVector();
	case FFieldNodeBase::ESerializationType::FieldNode_FSumScalar:
		return new FSumScalar();
	case FFieldNodeBase::ESerializationType::FieldNode_FSumVector:
		return new FSumVector();
	case FFieldNodeBase::ESerializationType::FieldNode_FConversionField:
		if(BaseType==FFieldNodeBase::EFieldType::EField_Int32)
			return new FConversionField<float,int32>();
		else if (BaseType == FFieldNodeBase::EFieldType::EField_Float)
			return new FConversionField<int32,float>();
		break;
	case FFieldNodeBase::ESerializationType::FieldNode_FCullingField:
		if (BaseType == FFieldNodeBase::EFieldType::EField_Int32)
			return new FCullingField<int32>();
		if (BaseType == FFieldNodeBase::EFieldType::EField_Float)
			return new FCullingField<float>();
		if (BaseType == FFieldNodeBase::EFieldType::EField_FVector)
			return new FCullingField<FVector>();
		break;
	case FFieldNodeBase::ESerializationType::FieldNode_FReturnResultsTerminal:
		if(BaseType==FFieldNodeBase::EFieldType::EField_Int32)
			return new FReturnResultsTerminal<int32>();
		else if (BaseType == FFieldNodeBase::EFieldType::EField_Float)
			return new FReturnResultsTerminal<float>();
		else if (BaseType == FFieldNodeBase::EFieldType::EField_FVector)
			return new FReturnResultsTerminal<FVector>();
		break;
	}
	return nullptr;
}


template<class T>
void SerializeInternal(FArchive& Ar, TUniquePtr< FFieldNode<T> >& Field)
{
	uint8 dType = Field.IsValid() ? (uint8)Field->Type() : (uint8)FFieldNodeBase::EFieldType::EField_None;
	Ar << dType;

	uint8 sType = Field.IsValid() ? (uint8)Field->SerializationType() : (uint8)FFieldNodeBase::ESerializationType::FieldNode_Null;
	Ar << sType;

	if (Ar.IsLoading())
	{
		Field.Reset(static_cast<FFieldNode<T>* >(FieldNodeFactory((FFieldNodeBase::EFieldType)dType, (FFieldNodeBase::ESerializationType)sType)));
	}

	if (Field.IsValid())
	{
		Field->Serialize(Ar);
	}
}
template<class Enum>
void SerializeInternal(FArchive& Ar, Enum& Var)
{
	uint8 Type = (uint8)Var;
	Ar << Type;
	Var = (Enum)Type;
}


/**
* FUniformInteger
*/
void FUniformInteger::Evaluate(FFieldContext& Context, TArrayView<int32>& Results) const
{
	int32 MagnitudeVal = Magnitude;

	int32 NumSamples = Context.SampleIndices.Num();
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		Results[Context.SampleIndices[SampleIndex].Result] = MagnitudeVal;
	}
}
void FUniformInteger::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
}
bool FUniformInteger::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		return Super::operator==(Node)
			&& Magnitude == static_cast<const FUniformInteger*>(&Node)->Magnitude;
	}
	return false;
}


/**
* FRadialIntMask
*/
void FRadialIntMask::Evaluate(FFieldContext& Context, TArrayView<int32>& Results) const
{
	float Radius2 = Radius * Radius;

	int32 NumSamples = Context.SampleIndices.Num();
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];
		{
			float Result;
			float Delta2 = (Position - Context.Samples[Index.Sample]).SizeSquared();

			if(Delta2 < Radius2)
			{
				Result = InteriorValue;
			}
			else
			{
				Result = ExteriorValue;
			}

			switch (SetMaskCondition) 
			{
			case ESetMaskConditionType::Field_Set_Always:
				Results[Index.Result] = Result;
				break;
			case ESetMaskConditionType::Field_Set_IFF_NOT_Interior:
				if (Results[Index.Result] != InteriorValue) 
				{
					Results[Index.Result] = Result;
				}
				break;
			case ESetMaskConditionType::Field_Set_IFF_NOT_Exterior:
				if (Results[Index.Result] != ExteriorValue) 
				{
					Results[Index.Result] = Result;
				}
				break;
			}
		}
	}
}
void FRadialIntMask::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Radius;
	Ar << Position;
	Ar << InteriorValue;
	Ar << ExteriorValue;
	SerializeInternal<ESetMaskConditionType>(Ar, SetMaskCondition);
}
bool FRadialIntMask::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FRadialIntMask* Other = static_cast<const FRadialIntMask*>(&Node);
		return Super::operator==(Node)
			&& Radius == Other->Radius
			&& Position==Other->Position
			&& InteriorValue==Other->InteriorValue
			&& ExteriorValue==Other->ExteriorValue
			&& SetMaskCondition== Other->SetMaskCondition;
	}
	return false;
}

/**
* FUniformScalar
*/
void FUniformScalar::Evaluate(FFieldContext& Context, TArrayView<float>& Results) const
{
	float MagnitudeVal = Magnitude;

	int32 NumSamples = Context.SampleIndices.Num();
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		Results[Context.SampleIndices[SampleIndex].Result] = MagnitudeVal;
	}
}
void FUniformScalar::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
}
bool FUniformScalar::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		return Super::operator==(Node)
			&& Magnitude == static_cast<const FUniformScalar*>(&Node)->Magnitude;
	}
	return false;
}

/**
* FWaveScalar
*/
void FWaveScalar::Evaluate(FFieldContext& Context, TArrayView<float>& Results) const
{
	const float Velocity = (Period != 0.0 ) ? Wavelength / Period : 0.0;

	const float Wavenumber = (Wavelength != 0.0 ) ? 2.0 * PI / Wavelength : 0.0;
	const float DeltaTime = FMath::Max(Context.TimeSeconds, 0.0f);
	const float Radius = Wavelength * DeltaTime / Period;
	const float Decay = DeltaTime / Period;

	int32 NumSamples = Context.SampleIndices.Num();
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];

		const float Distance = (Context.Samples[Index.Sample] - Position).Size();
		const float Phase = Wavenumber * (Distance - Radius);

		if (Function == EWaveFunctionType::Field_Wave_Cosine)
		{
			Results[Index.Result] = Magnitude * FMath::Cos(Phase);
		}
		else if (Function == EWaveFunctionType::Field_Wave_Gaussian)
		{
			Results[Index.Result] = Magnitude * FMath::Exp(-Phase * Phase);
		}
		else if (Function == EWaveFunctionType::Field_Wave_Falloff)
		{
			if (Distance < Radius && Radius > 0)
			{
				const float Fraction = (1.0 - Distance / Radius);
				if (Falloff == EFieldFalloffType::Field_FallOff_None)
				{
					Results[Index.Result] = Magnitude;
				}
				else if (Falloff == EFieldFalloffType::Field_Falloff_Linear)
				{
					Results[Index.Result] = Magnitude * Fraction;
				}
				else if (Falloff == EFieldFalloffType::Field_Falloff_Squared)
				{
					Results[Index.Result] =  Magnitude * Fraction * Fraction;
				}
				else if (Falloff == EFieldFalloffType::Field_Falloff_Inverse && Fraction > 0.0)
				{
					Results[Index.Result] =  Magnitude / Fraction;
				}
				else if (Falloff == EFieldFalloffType::Field_Falloff_Logarithmic)
				{
					Results[Index.Result] = Magnitude * FMath::Loge(Fraction + 1.0) / FMath::Loge(10.0);
				}
			}
			else
			{
				Results[Index.Result] = 0.0;
			}
		}
		else if (Function == EWaveFunctionType::Field_Wave_Decay)
		{
			Results[Index.Result] = Magnitude * FMath::Exp( - Decay * Decay );
		}
	}
}
void FWaveScalar::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
	Ar << Position;
	Ar << Wavelength;
	Ar << Period;
	SerializeInternal<EWaveFunctionType>(Ar, Function);
	SerializeInternal<EFieldFalloffType>(Ar, Falloff);
}
bool FWaveScalar::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		return Super::operator==(Node)
			&& Magnitude == static_cast<const FWaveScalar*>(&Node)->Magnitude
			&& Position == static_cast<const FWaveScalar*>(&Node)->Position
			&& Wavelength == static_cast<const FWaveScalar*>(&Node)->Wavelength
			&& Period == static_cast<const FWaveScalar*>(&Node)->Period
			&& Function == static_cast<const FWaveScalar*>(&Node)->Function
			&& Falloff == static_cast<const FWaveScalar*>(&Node)->Falloff;
	}
	return false;
}


/**
* Function Utils
*/

float ScaleFunctionResult(const float& MinRange, const float& DeltaRange, const float& NodeMagnitude, const float& FunctionResult)
{
	return NodeMagnitude * (MinRange + DeltaRange * FunctionResult);
}

template<EFieldFalloffType FalloffType>
float EvalFalloffFunction(const float& MinRange, const float& DeltaRange, const float& NodeMagnitude, const float& FalloffValue)
{ 
	return 0.0;
}

template<>
float EvalFalloffFunction<EFieldFalloffType::Field_FallOff_None>(const float& MinRange, const float& DeltaRange, const float& NodeMagnitude, const float& FalloffValue)
{
	return ScaleFunctionResult(MinRange, DeltaRange, NodeMagnitude, 1.0);
}

template<>
float EvalFalloffFunction<EFieldFalloffType::Field_Falloff_Linear>(const float& MinRange, const float& DeltaRange, const float& NodeMagnitude, const float& FalloffValue)
{
	return ScaleFunctionResult(MinRange, DeltaRange, NodeMagnitude, FalloffValue);
}

template<>
float EvalFalloffFunction<EFieldFalloffType::Field_Falloff_Squared>(const float& MinRange, const float& DeltaRange, const float& NodeMagnitude, const float& FalloffValue)
{
	return ScaleFunctionResult(MinRange, DeltaRange, NodeMagnitude, FalloffValue * FalloffValue);
}

template<>
float EvalFalloffFunction<EFieldFalloffType::Field_Falloff_Inverse>(const float& MinRange, const float& DeltaRange, const float& NodeMagnitude, const float& FalloffValue)
{
	return (FalloffValue > SMALL_NUMBER) ? ScaleFunctionResult(MinRange, DeltaRange, NodeMagnitude, 1.0 / FalloffValue) : 0.0;
}

template<>
float EvalFalloffFunction<EFieldFalloffType::Field_Falloff_Logarithmic>(const float& MinRange, const float& DeltaRange, const float& NodeMagnitude, const float& FalloffValue)
{
	return ScaleFunctionResult(MinRange, DeltaRange, NodeMagnitude, FMath::LogX(10, FalloffValue + 1.0));
}

/**
* FRadialFalloff
*/

template<EFieldFalloffType FalloffType>
void FRadialFalloff::Evaluator(const FFieldContext& Context, TArrayView<float>& Results) const
{
	if (Radius > 0.f)
	{
		const float DeltaRange = (MaxRange - MinRange);
		const int32 NumSamples = Context.SampleIndices.Num();
		for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
		{
			const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];
			{
				Results[Index.Result] = Default;
				const float Delta = (Context.Samples[Index.Sample] - Position).Size();

				if (Delta < Radius)
				{
					const float Function = 1.0 - Delta / Radius;
					Results[Index.Result] = EvalFalloffFunction<FalloffType>(MinRange, DeltaRange, Magnitude, Function);
				}
			}
		}
	}
}

void FRadialFalloff::Evaluate(FFieldContext& Context, TArrayView<float>& Results) const
{
	switch (Falloff)
	{
	case EFieldFalloffType::Field_FallOff_None:
		Evaluator<EFieldFalloffType::Field_FallOff_None>(Context, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Linear:
		Evaluator<EFieldFalloffType::Field_Falloff_Linear>(Context, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Squared:
		Evaluator<EFieldFalloffType::Field_Falloff_Squared>(Context, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Inverse:
		Evaluator<EFieldFalloffType::Field_Falloff_Inverse>(Context, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Logarithmic:
		Evaluator<EFieldFalloffType::Field_Falloff_Logarithmic>(Context,  Results);
		break;
	}
}
void FRadialFalloff::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
	Ar << MinRange;
	Ar << MaxRange;
	Ar << Default;
	Ar << Radius;
	Ar << Position;
	SerializeInternal<EFieldFalloffType>(Ar, Falloff);

}
bool FRadialFalloff::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FRadialFalloff* Other = static_cast<const FRadialFalloff*>(&Node);
		return Super::operator==(Node)
			&& Magnitude == Other->Magnitude
			&& MinRange == Other->MinRange
			&& MaxRange == Other->MaxRange
			&& Default == Other->Default
			&& Radius == Other->Radius
			&& Position == Other->Position
			&& Falloff == Other->Falloff;
	}
	return false;
}

/**
* FPlaneFalloff
*/

template<EFieldFalloffType FalloffType>
void FPlaneFalloff::Evaluator(const FFieldContext& Context, const FPlane& Plane, TArrayView<float>& Results) const
{
	if (Distance > 0.f)
	{
		const float DeltaRange = (MaxRange - MinRange);
		const int32 NumSamples = Context.SampleIndices.Num();
		for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
		{
			const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];
			{
				Results[Index.Result] = Default;
				const float Delta = Plane.PlaneDot(Context.Samples[Index.Sample]);

				if (Delta < -SMALL_NUMBER && Delta > -Distance)
				{
					const float Function = 1.0 + Delta / Distance;
					Results[Index.Result] = EvalFalloffFunction<FalloffType>(MinRange, DeltaRange, Magnitude, Function);
				}
			}
		}
	}
}

void
FPlaneFalloff::Evaluate(FFieldContext& Context, TArrayView<float>& Results) const
{
	FPlane Plane(Position, Normal);
	switch (Falloff)
	{
	case EFieldFalloffType::Field_FallOff_None:
		Evaluator<EFieldFalloffType::Field_FallOff_None>(Context, Plane, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Linear:
		Evaluator<EFieldFalloffType::Field_Falloff_Linear>(Context, Plane, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Squared:
		Evaluator<EFieldFalloffType::Field_Falloff_Squared>(Context, Plane, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Inverse:
		Evaluator<EFieldFalloffType::Field_Falloff_Inverse>(Context, Plane, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Logarithmic:
		Evaluator<EFieldFalloffType::Field_Falloff_Logarithmic>(Context, Plane, Results);
		break;
	}
}
void FPlaneFalloff::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
	Ar << MinRange;
	Ar << MaxRange;
	Ar << Default;
	Ar << Distance;
	Ar << Position;
	Ar << Normal;
	SerializeInternal<EFieldFalloffType>(Ar, Falloff);

}
bool FPlaneFalloff::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FPlaneFalloff* Other = static_cast<const FPlaneFalloff*>(&Node);
		return Super::operator==(Node)
			&& Magnitude == Other->Magnitude
			&& MinRange == Other->MinRange
			&& MaxRange == Other->MaxRange
			&& Default == Other->Default
			&& Distance == Other->Distance
			&& Position == Other->Position
			&& Normal == Other->Normal
			&& Falloff == Other->Falloff;
	}
	return false;
}

/**
* FBoxFalloff
*/

template<EFieldFalloffType FalloffType>
void FBoxFalloff::Evaluator(const FFieldContext& Context, TArrayView<float>& Results) const
{
	const float DeltaRange = (MaxRange - MinRange);

	static const float HalfBox = 50;
	static const FBox UnitBox(FVector(-HalfBox), FVector(HalfBox));

	const int32 NumSamples = Context.SampleIndices.Num();
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];
		{
			Results[Index.Result] = Default;
			const FVector LocalPoint = Transform.InverseTransformPosition(Context.Samples[Index.Sample]);
			if (UnitBox.IsInside(LocalPoint))
			{
				const FVector Distance(FMath::Abs(LocalPoint.X)- HalfBox, FMath::Abs(LocalPoint.Y) - HalfBox, FMath::Abs(LocalPoint.Z) - HalfBox);
				const float Delta = FMath::Min(FMath::Max(Distance.X, FMath::Max(Distance.Y, Distance.Z)), 0.0f);
				const float Function = - Delta / HalfBox;

				Results[Index.Result] = EvalFalloffFunction<FalloffType>(MinRange, DeltaRange, Magnitude, Function);
			}
		}
	}
}

void
FBoxFalloff::Evaluate(FFieldContext& Context, TArrayView<float>& Results) const
{
	switch (Falloff)
	{
	case EFieldFalloffType::Field_FallOff_None:
		Evaluator<EFieldFalloffType::Field_FallOff_None>(Context, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Linear:
		Evaluator<EFieldFalloffType::Field_Falloff_Linear>(Context, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Squared:
		Evaluator<EFieldFalloffType::Field_Falloff_Squared>(Context, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Inverse:
		Evaluator<EFieldFalloffType::Field_Falloff_Inverse>(Context, Results);
		break;
	case EFieldFalloffType::Field_Falloff_Logarithmic:
		Evaluator<EFieldFalloffType::Field_Falloff_Logarithmic>(Context, Results);
		break;
	}
}
void FBoxFalloff::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
	Ar << MinRange;
	Ar << MaxRange;
	Ar << Default;
	Ar << Transform;
	SerializeInternal<EFieldFalloffType>(Ar, Falloff);
}
bool FBoxFalloff::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FBoxFalloff* Other = static_cast<const FBoxFalloff*>(&Node);
		bool bRet = Super::operator==(Node);
		bRet &= Magnitude == Other->Magnitude;
		bRet &= MinRange == Other->MinRange;
		bRet &= MaxRange == Other->MaxRange;
		bRet &= Default == Other->Default;
		bRet &= Transform.Equals(Other->Transform);
		bRet &= Falloff == Other->Falloff;
		return bRet;
	}
	return false;
}


/**
* FNoiseField
*/
void
FNoiseField::Evaluate(FFieldContext& Context, TArrayView<float>& Results) const
{
	const float DeltaRange = (MaxRange - MinRange);
	const int32 NumSamples = Context.SampleIndices.Num();

	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];

		float Dummy = 0.0f;
		FVector LocalPoint = Transform.InverseTransformPosition(Context.Samples[Index.Sample]);
		LocalPoint = FVector(FMath::Modf(LocalPoint.X, &Dummy) * 0.5 + 0.5, 
							 FMath::Modf(LocalPoint.Y, &Dummy) * 0.5 + 0.5,
							 FMath::Modf(LocalPoint.Z, &Dummy) * 0.5 + 0.5) * 255;

		// Samples for the Perlin noise must be btw 0->255
		float PerlinValue = 0.0;
		Field::PerlinNoise::Sample(&PerlinValue, LocalPoint.X, LocalPoint.Y, LocalPoint.Z);

		// Perlin noise result is btw -1 -> 1
		PerlinValue = 0.5 * ( PerlinValue + 1.0 );
		Results[Index.Result] = ScaleFunctionResult(MinRange, DeltaRange, 1.0, PerlinValue);
	}
}
void FNoiseField::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << MinRange;
	Ar << MaxRange;
	Ar << Transform;
}
bool FNoiseField::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FNoiseField* Other = static_cast<const FNoiseField*>(&Node);
		return Super::operator==(Node)
			&& MinRange == Other->MinRange
			&& MaxRange == Other->MaxRange
			&& Transform.Equals(Other->Transform);
	}
	return false;
}


/**
* FUniformVector
*/
void FUniformVector::Evaluate(FFieldContext& Context, TArrayView<FVector>& Results) const
{
	const FVector DirectionVal = Direction;
	const float MagnitudeVal = Magnitude;

	const int32 NumSamples = Context.SampleIndices.Num();
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		Results[Context.SampleIndices[SampleIndex].Result] = MagnitudeVal * DirectionVal;
	}
}
void FUniformVector::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
	Ar << Direction;
}
bool FUniformVector::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FUniformVector* Other = static_cast<const FUniformVector*>(&Node);
		return Super::operator==(Node)
			&& Magnitude == Other->Magnitude
			&& Direction == Other->Direction;
	}
	return false;
}

/**
* FRadialVector
*/
void FRadialVector::Evaluate(FFieldContext& Context, TArrayView<FVector>& Results) const
{
	const int32 NumSamples = Context.SampleIndices.Num();
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];
		{
			Results[Index.Result] = Magnitude * (Context.Samples[Index.Sample] - Position).GetSafeNormal();
		}
	}
	
}
void FRadialVector::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
	Ar << Position;
}
bool FRadialVector::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FRadialVector* Other = static_cast<const FRadialVector*>(&Node);
		return Super::operator==(Node)
			&& Magnitude == Other->Magnitude
			&& Position == Other->Position;
	}
	return false;
}

/**
* FRandomVector
*/
void FRandomVector::Evaluate(FFieldContext& Context, TArrayView<FVector>& Results) const
{
	const int32 NumSamples = Context.SampleIndices.Num();
	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];
		{
			Results[Index.Result].X = FMath::RandRange(-1.f, 1.f);
			Results[Index.Result].Y = FMath::RandRange(-1.f, 1.f);
			Results[Index.Result].Z = FMath::RandRange(-1.f, 1.f);
			Results[Index.Result].Normalize();
			Results[Index.Result] *= Magnitude;
		}
	}
}
void FRandomVector::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
}
bool FRandomVector::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		return Super::operator==(Node)
			&& Magnitude == static_cast<const FRandomVector*>(&Node)->Magnitude;
	}
	return false;
}


/**
* FSumScalar
*/
void FSumScalar::Evaluate(FFieldContext& ContextIn, TArrayView<float>& Results) const
{
	TUniquePtr<FFieldSystemMetaDataResults<float> > ResultsData(new FFieldSystemMetaDataResults<float>(Results));
	FScopedFieldContextMetaData ScopedMetaData(ContextIn, ResultsData.Get());

	const int32 NumResults = Results.Num();
	const int32 NumSamples = ContextIn.SampleIndices.Num();

	const FFieldNode<float> * RightField = ScalarRight.Get();
	const FFieldNode<float> * LeftField = ScalarLeft.Get();

	float MagnitudeVal = Magnitude;
	if (LeftField != nullptr && RightField != nullptr)
	{
		TArray<float> ResultsBuffer;
		ResultsBuffer.SetNumUninitialized(2 * NumResults);
		TArrayView<float> Buffers[2] = {
			TArrayView<float>(&ResultsBuffer[0],NumResults),
			TArrayView<float>(&ResultsBuffer[NumResults],NumResults),
		};

		TArray<const FFieldNode<float> * > FieldNodes = { LeftField,RightField };

		for (int32 i = 0; i < 2; i++)
		{
			if (ensureMsgf(FieldNodes[i]->Type() == FFieldNode<float>::StaticType(),
				TEXT("Field system SumScalar expects float input arrays.")))
			{
				FieldNodes[i]->Evaluate(ContextIn, Buffers[i]);
			}
		}

		switch (Operation)
		{
		case EFieldOperationType::Field_Multiply:
			for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
			{
				const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
				{
					Results[Index.Result] = Buffers[1][Index.Result] * Buffers[0][Index.Result];
				}
			}
			break;
		case EFieldOperationType::Field_Divide:
			for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
			{
				const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
				{
					Results[Index.Result] = Buffers[0][Index.Result] / Buffers[1][Index.Result];
				}
			}
			break;
		case EFieldOperationType::Field_Add:
			for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
			{
				const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
				{
					Results[Index.Result] = Buffers[1][Index.Result] + Buffers[0][Index.Result];
				}
			}
			break;
		case EFieldOperationType::Field_Substract:
			for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
			{
				const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
				{
					Results[Index.Result] = Buffers[0][Index.Result] - Buffers[1][Index.Result];
				}
			}
			break;
		}
	}
	else if (LeftField != nullptr && ensureMsgf(ScalarLeft->Type() == FFieldNode<float>::StaticType(),
		TEXT("Field system SumScalar expects float input arrays.")))
	{
		LeftField->Evaluate(ContextIn, Results);

	}
	else if (ScalarRight != nullptr &&  ensureMsgf(ScalarRight->Type() == FFieldNode<float>::StaticType(),
		TEXT("Field system SumScalar expects float input arrays.")))
	{
		ScalarRight->Evaluate(ContextIn, Results);
	}

	if (MagnitudeVal != 1.0)
	{
		for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
		{
			const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
			{
				Results[Index.Result] *= MagnitudeVal;
			}
		}
	}
}
void FSumScalar::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << Magnitude;
	SerializeInternal<float>(Ar, ScalarRight);
	SerializeInternal<float>(Ar, ScalarLeft);
	SerializeInternal<EFieldOperationType>(Ar, Operation);
}
bool FSumScalar::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FSumScalar* Other = static_cast<const FSumScalar*>(&Node);
		bool bRet = Super::operator==(Node);
		bRet &= Magnitude == Other->Magnitude;
		bRet &= FieldsEqual(ScalarRight,Other->ScalarRight);
		bRet &= FieldsEqual(ScalarLeft,Other->ScalarLeft);
		bRet &= Operation == Other->Operation;
		return bRet;
	}
	return false;
}

/**
* FSumVector
*/
void FSumVector::Evaluate(FFieldContext& ContextIn, TArrayView<FVector>& Results) const
{
	TUniquePtr<FFieldSystemMetaDataResults<FVector>> ResultsData(new FFieldSystemMetaDataResults<FVector>(Results));
	FScopedFieldContextMetaData ScopedMetaData(ContextIn, ResultsData.Get());

	int32 NumResults = Results.Num();
	int32 NumSamples = ContextIn.SampleIndices.Num();

	const FFieldNode<float> * ScalarField = Scalar.Get();
	const FFieldNode<FVector> * RightVectorField = VectorRight.Get();
	const FFieldNode<FVector> * LeftVectorField = VectorLeft.Get();

	float MagnitudeVal = Magnitude;
	if (RightVectorField != nullptr && LeftVectorField != nullptr)
	{
		TArray<FVector> Buffer;
		Buffer.SetNumUninitialized(2 * NumResults);
		TArrayView<FVector> Buffers[2] = {
			TArrayView<FVector>(&Buffer[0],NumResults),
			TArrayView<FVector>(&Buffer[NumResults],NumResults),
		};

		LeftVectorField->Evaluate(ContextIn, Buffers[0]);
		RightVectorField->Evaluate(ContextIn, Buffers[1]);

		switch (Operation)
		{
		case EFieldOperationType::Field_Multiply:
			for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
			{
				const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
				{
					Results[Index.Result] = Buffers[1][Index.Result] * Buffers[0][Index.Result];
				}
			}
			break;
		case EFieldOperationType::Field_Divide:
			for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
			{
		  const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
				{
					Results[Index.Result] = Buffers[0][Index.Result] / Buffers[1][Index.Result];
				}
			}
			break;
		case EFieldOperationType::Field_Add:
			for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
			{
		  const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
				{
					Results[Index.Result] = Buffers[1][Index.Result] + Buffers[0][Index.Result];
				}
			}
			break;
		case EFieldOperationType::Field_Substract:
			for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
			{
		  const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
				{
					Results[Index.Result] = Buffers[0][Index.Result] - Buffers[1][Index.Result];
				}
			}
			break;
		}
	}
	else if (LeftVectorField != nullptr)
	{
		LeftVectorField->Evaluate(ContextIn, Results);
	}
	else if (RightVectorField != nullptr)
	{
		RightVectorField->Evaluate(ContextIn, Results);
	}

	if (ScalarField != nullptr)
	{
		TArray<float> Buffer;
		Buffer.SetNumUninitialized(NumResults);
		TArrayView<float> BufferView(&Buffer[0], NumResults);

		ScalarField->Evaluate(ContextIn, BufferView);

		for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
		{
			 const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
			{
				Results[Index.Result] *= Buffer[Index.Result];
			}
		}
	}

	if (MagnitudeVal != 1.0)
	{
		for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
		{
			const FFieldContextIndex& Index = ContextIn.SampleIndices[SampleIndex];
			{
				Results[Index.Result] *= MagnitudeVal;
			}
		}
	}
}
void FSumVector::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar << Magnitude;
	SerializeInternal<float>(Ar, Scalar);
	SerializeInternal<FVector>(Ar, VectorRight);
	SerializeInternal<FVector>(Ar, VectorLeft);
	SerializeInternal<EFieldOperationType>(Ar, Operation);
}
bool FSumVector::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FSumVector* Other = static_cast<const FSumVector*>(&Node);
		return Super::operator==(Node)
			&& Magnitude == Other->Magnitude
			&& FieldsEqual(Scalar,Other->Scalar)
			&& FieldsEqual(VectorRight, Other->VectorRight)
			&& FieldsEqual(VectorLeft, Other->VectorLeft)
			&& Operation == Other->Operation;
	}
	return false;
}


/**
* FConversionField<InT,OutT>
*/
template<class InT, class OutT>
void FConversionField<InT,OutT>::Evaluate(FFieldContext& Context, TArrayView<OutT>& Results) const
{
	int32 NumSamples = Context.SampleIndices.Num();

	TArray<InT> Array;
	Array.Init(0.f, Results.Num());
	TArrayView<InT> ArrayView(&(Array[0]), Array.Num());
	InputField->Evaluate(Context, ArrayView);

	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];
		Results[Index.Result] = (OutT)Array[Index.Result];
	}
}
template<class InT, class OutT>
void FConversionField<InT, OutT>::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	SerializeInternal<InT>(Ar, InputField);
}
template<class InT, class OutT>
bool FConversionField<InT, OutT>::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FConversionField<InT, OutT>* Other = static_cast<const FConversionField<InT, OutT>*>(&Node);
		return Super::operator==(Node)
			&& FieldsEqual(InputField,Other->InputField);
	}
	return false;
}

template class FConversionField<int32, float>;
template class FConversionField<float, int32>;

/**
*  FCullingField<T>
*/
template<class T>
void FCullingField<T>::Evaluate(FFieldContext& Context, TArrayView<T>& Results) const
{
	int32 NumResults = Results.Num();
	int32 NumSamples = Context.SampleIndices.Num();

	const FFieldNode<float> * CullingField = Culling.Get();
	const FFieldNode<T> * InputField = Input.Get();

	TArray<FFieldContextIndex> IndexBuffer;
	if (CullingField != nullptr)
	{
		if (ensureMsgf(CullingField->Type() == FFieldNode<float>::StaticType(),
			TEXT("Field Node CullingFields Culling input expects a float input array.")))
		{
			FFieldSystemMetaDataCulling* CullingData = static_cast<FFieldSystemMetaDataCulling*>(Context.MetaData[FFieldSystemMetaData::EMetaType::ECommandData_Culling]);
			
			if(CullingData)
			{
				CullingData->bCullingActive = true;
			}

			TArray<float> EvaluationBuffer;
			EvaluationBuffer.Init(0.f, NumResults);
			TArrayView<float> EvaluationBufferView(&(EvaluationBuffer[0]), NumResults);
			CullingField->Evaluate(Context, EvaluationBufferView);

			int NewEvaluationSize = 0;
			for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
			{
				const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];
				{
					if (Operation == EFieldCullingOperationType::Field_Culling_Outside)
					{
						if (EvaluationBuffer[Index.Result] != 0)
						{
							NewEvaluationSize++;
						}
					}
					else
					{
						if (EvaluationBuffer[Index.Result] == 0)
						{
							NewEvaluationSize++;
						}
					}
				}
			}
			IndexBuffer.SetNumUninitialized(NewEvaluationSize);
			for (int32 SampleIndex = 0, j=0; SampleIndex < NumSamples; SampleIndex++)
			{
				const FFieldContextIndex& Index = Context.SampleIndices[SampleIndex];
				{
					if (Operation == EFieldCullingOperationType::Field_Culling_Outside)
					{
						if (EvaluationBuffer[Index.Result] != 0)
						{
							IndexBuffer[j] = Context.SampleIndices[SampleIndex];
							j++;

							if(CullingData)
							{
								CullingData->EvaluatedIndexBuffer.Add(Context.SampleIndices[SampleIndex]);
							}
						}
					}
					else 
					{
						if (EvaluationBuffer[Index.Result] == 0)
						{
							IndexBuffer[j] = Context.SampleIndices[SampleIndex];
							j++;

							if(CullingData)
							{
								CullingData->EvaluatedIndexBuffer.Add(Context.SampleIndices[SampleIndex]);
							}
						}
					}
				}
			}
		}

		if (InputField != nullptr && IndexBuffer.Num())
		{
			TArrayView<FFieldContextIndex> IndexBufferView(&(IndexBuffer[0]), IndexBuffer.Num());
			FFieldContext LocalContext(IndexBufferView, Context.Samples, Context.MetaData, Context.TimeSeconds);
			InputField->Evaluate(LocalContext, Results);
		}

	}
	else if( InputField!=nullptr)
	{
		InputField->Evaluate(Context, Results);
	}
}
template<class T>
void FCullingField<T>::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	SerializeInternal<float>(Ar, Culling);
	SerializeInternal<T>(Ar, Input);
	SerializeInternal<EFieldCullingOperationType>(Ar, Operation);
}
template<class T>
bool FCullingField<T>::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		const FCullingField<T>* Other = static_cast<const FCullingField<T>*>(&Node);
		return Super::operator==(Node)
			&& FieldsEqual(Culling,Other->Culling)
			&& FieldsEqual(Input,Other->Input)
			&& Operation == Other->Operation;
	}
	return false;
}
template class CHAOS_API FCullingField<int32>;
template class CHAOS_API FCullingField<float>;
template class CHAOS_API FCullingField<FVector>;


/**
* FReturnResultsTerminal<T>
*/
template<class T>
void FReturnResultsTerminal<T>::Evaluate(FFieldContext& Context, TArrayView<T>& Results) const
{
	if (ensureMsgf(Context.MetaData.Contains(FFieldSystemMetaData::EMetaType::ECommandData_Results),
		TEXT("Return results nodes can only be used upstream from a 'results expector', for example as an input "
			"to a Operator Node . See documentation for details.")))
	{
		FFieldSystemMetaDataResults<T> * ResultsMetaData = static_cast<FFieldSystemMetaDataResults<T>*>(Context.MetaData[FFieldSystemMetaData::EMetaType::ECommandData_Results]);
		ensure(ResultsMetaData->Results.Num() == Results.Num());

		int32 NumSamples = Context.SampleIndices.Num();
		for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
		{
			Results[Context.SampleIndices[SampleIndex].Result] = ResultsMetaData->Results[Context.SampleIndices[SampleIndex].Result];
		}
	}
}
template<class T>
void FReturnResultsTerminal<T>::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
}
template<class T>
bool FReturnResultsTerminal<T>::operator==(const FFieldNodeBase& Node)
{
	if (Node.SerializationType() == SerializationType())
	{
		return true;
	}
	return false;
}
template class CHAOS_API FReturnResultsTerminal<int32>;
template class CHAOS_API FReturnResultsTerminal<float>;
template class CHAOS_API FReturnResultsTerminal<FVector>;
