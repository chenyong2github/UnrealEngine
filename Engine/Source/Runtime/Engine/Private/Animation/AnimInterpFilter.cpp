// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimInterpFilter.h"

//======================================================================================================================
// FFIRFilter
//======================================================================================================================
float FFIRFilter::GetInterpolationCoefficient (EFilterInterpolationType InterpolationType, int32 CoefficientIndex) const
{
	const float Step = GetStep();

	switch (InterpolationType)
	{
		case BSIT_Average:
			return Step;	
		case BSIT_Linear:
			return Step*CoefficientIndex;
		case BSIT_Cubic:
			return Step*Step*Step*CoefficientIndex;
		default:
			// Note that BSIT_EaseInOut is not supported
			return 0.f;
	}
}

void FFIRFilter::CalculateCoefficient(EFilterInterpolationType InterpolationType)
{
	if ( IsValid() )
	{
		float Sum=0.f;
		for (int32 I=0; I<Coefficients.Num(); ++I)
		{
			Coefficients[I] = GetInterpolationCoefficient(InterpolationType, I);
			Sum += Coefficients[I];
		}

		// now normalize it, if not 1
		if ( fabs(Sum-1.f) > ZERO_ANIMWEIGHT_THRESH )
		{
			for (int32 I=0; I<Coefficients.Num(); ++I)
			{
				Coefficients[I]/=Sum;
			}
		}
	}
}

float FFIRFilter::GetFilteredData(float Input)
{
	if ( IsValid() )
	{
		FilterWindow[CurrentStack] = Input;
		float Result = CalculateFilteredOutput();

		CurrentStack = CurrentStack+1;
		if ( CurrentStack > FilterWindow.Num()-1 ) 
		{
			CurrentStack = 0;
		}

		LastOutput = Result;
		return Result;
	}

	LastOutput = Input;
	return Input;
}

float FFIRFilter::CalculateFilteredOutput() const
{
	float Output = 0.f;
	int32 StackIndex = CurrentStack;

	for ( int32 I=Coefficients.Num()-1; I>=0; --I )
	{
		Output += FilterWindow[StackIndex]*Coefficients[I];
		StackIndex = StackIndex-1;
		if (StackIndex < 0)
		{
			StackIndex = FilterWindow.Num()-1;
		}
	}

	return Output;
}

//======================================================================================================================
// FFIRFilterTimeBased
//======================================================================================================================
int32 FFIRFilterTimeBased::GetSafeCurrentStackIndex()
{
	// if valid range
	check ( CurrentStackIndex < FilterWindow.Num() );

	// see if it's expired yet
	if ( !FilterWindow[CurrentStackIndex].IsValid() )
	{
		return CurrentStackIndex;
	}

	// else see any other index is available
	// when you do this, go to forward, (oldest)
	// this should not be the case because most of times
	// current one should be the oldest one, but since 
	// we jumps when reallocation happens, we still do this
	for (int32 I=0; I<FilterWindow.Num(); ++I)
	{
		int32 NewIndex = CurrentStackIndex + I;
		if (NewIndex >= FilterWindow.Num())
		{
			NewIndex = NewIndex - FilterWindow.Num();
		}

		if ( !FilterWindow[NewIndex].IsValid() )
		{
			return NewIndex;
		}
	}

	// if current one isn't available anymore 
	// that means we need more stack
	const int32 NewIndex = FilterWindow.Num();
	FilterWindow.AddZeroed(5);
	return NewIndex;
}

void FFIRFilterTimeBased::RefreshValidFilters()
{
	NumValidFilter = 0;

	if ( WindowDuration > 0.f )
	{
		// run validation test
		for (int32 I=0; I<FilterWindow.Num(); ++I)
		{
			FilterWindow[I].CheckValidation(CurrentTime, WindowDuration);
			if ( FilterWindow[I].IsValid() )
			{
				++NumValidFilter;
			}
		}
	}
}

void FFIRFilterTimeBased::WrapToValue(float Input, float Range)
{
	if (Range <= 0.0f)
	{
		return;
	}
	float HalfRange = Range / 2.0f;

	switch (InterpolationType)
	{
	case BSIT_ExponentialDecay:
	case BSIT_SpringDamper:
	{
		if (FilterWindow.Num() != 0)
		{
			FilterWindow[0].Input = FMath::Wrap(FilterWindow[0].Input, Input - HalfRange, Input + HalfRange);
		}
	}
	break;
	default:
	{
		if (IsValid())
		{
			float NewLastOutput = FMath::Wrap(LastOutput, Input - HalfRange, Input + HalfRange);
			float Delta = NewLastOutput - LastOutput;
			if (Delta)
			{
				LastOutput = NewLastOutput;
				for (int32 Index = 0; Index != FilterWindow.Num() ; ++Index)
				{
					FilterWindow[Index].Input += Delta;
				}
			}
		}
	}
	break;
	}
}


float FFIRFilterTimeBased::UpdateAndGetFilteredData(float Input, float DeltaTime)
{
	if (DeltaTime > KINDA_SMALL_NUMBER)
	{
		float Result;
		CurrentTime += DeltaTime;

		switch (InterpolationType)
		{
			case BSIT_ExponentialDecay:
			{
				if (FilterWindow.Num() != 1)
				{
					FilterWindow.Empty(1);
					FilterWindow.Push(FFilterData(Input, 0.0f));
				}
				const float OrigValue = FilterWindow[0].Input;
				FMath::ExponentialSmoothingApprox(FilterWindow[0].Input, Input, DeltaTime, WindowDuration / EULERS_NUMBER);
				if (MaxSpeed > 0.0f)
				{
					// Clamp the speed
					FilterWindow[0].Input = FMath::Clamp(FilterWindow[0].Input, OrigValue - MaxSpeed * DeltaTime,
                                                         OrigValue + MaxSpeed * DeltaTime); 
				}
				Result = FilterWindow[0].Input;
			}
			break;
			case BSIT_SpringDamper:
			{
				if (FilterWindow.Num() != 2)
				{
					FilterWindow.Empty(2);
					// [0] element is the value, [1] element is the rate
					FilterWindow.Push(FFilterData(Input, 0.0f));
					FilterWindow.Push(FFilterData(0.0f, 0.0f));
				}
				const float OrigValue = FilterWindow[0].Input;
				FMath::SpringDamperSmoothing(FilterWindow[0].Input, FilterWindow[1].Input, Input, 0.0f, DeltaTime,
				                             WindowDuration / EULERS_NUMBER, DampingRatio);
				if (MaxSpeed > 0.0f)
				{
					// Clamp the speed
					FilterWindow[0].Input = FMath::Clamp(FilterWindow[0].Input, OrigValue - MaxSpeed * DeltaTime,
                                                         OrigValue + MaxSpeed * DeltaTime); 
					FilterWindow[1].Input = FMath::Clamp(FilterWindow[1].Input, -MaxSpeed, MaxSpeed);
				}
				if (bClamp)
				{
					// Clamp the value
					if (FilterWindow[0].Input > MaxValue)
					{
						FilterWindow[0].Input = MaxValue;
						if (FilterWindow[1].Input > 0.0f)
						{
							FilterWindow[1].Input = 0.0f;
						}
					}
					if (FilterWindow[0].Input < MinValue)
					{
						FilterWindow[0].Input = MinValue;
						if (FilterWindow[1].Input < 0.0f)
						{
							FilterWindow[1].Input = 0.0f;
						}
					}
				}
				Result = FilterWindow[0].Input;
			}
			break;
			default:
			{
				if (IsValid())
				{
					RefreshValidFilters();

					CurrentStackIndex = GetSafeCurrentStackIndex();
					FilterWindow[CurrentStackIndex].SetInput(Input, CurrentTime);
					Result = CalculateFilteredOutput();
					CurrentStackIndex = CurrentStackIndex + 1;
					if (CurrentStackIndex > FilterWindow.Num() - 1)
					{
						CurrentStackIndex = 0;
					}
				}
				else
				{
					Result = Input;
				}
			}
			break;
		}
		LastOutput = Result;
		return Result;
	}

	return LastOutput;
}

float FFIRFilterTimeBased::GetInterpolationCoefficient(const FFilterData& Data) const
{
	if (Data.IsValid())
	{
		const float Diff = Data.Diff(CurrentTime);
		if (Diff<=WindowDuration)
		{
			switch(InterpolationType)
			{
			case BSIT_Average:
				return 1.f;
			case BSIT_Linear:
				return 1.f - Diff/WindowDuration;
			case BSIT_Cubic:
				return 1.f - FMath::Cube(Diff/WindowDuration);
			case BSIT_EaseInOut:
				// Quadratic that starts and ends at 0, and reaches 1 half way through the window
				return 1.0f - 4.0f * FMath::Square(Diff/WindowDuration - 0.5f);
			default:
				break;
			}
		}
	}

	return 0.f;
}

float FFIRFilterTimeBased::CalculateFilteredOutput()
{
	check ( IsValid() );
	float SumCoefficient = 0.f;
	float SumInputs = 0.f;
	for (int32 I=0; I<FilterWindow.Num(); ++I)
	{
		const float Coefficient = GetInterpolationCoefficient(FilterWindow[I]);
		if (Coefficient > 0.f)
		{
			SumCoefficient += Coefficient;
			SumInputs += Coefficient * FilterWindow[I].Input;
		}
	}
	return SumCoefficient > 0.f ? SumInputs/SumCoefficient : 0.f;
}

