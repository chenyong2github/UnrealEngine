// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Containers/ObservableArray.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::Test
{
	
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObservableArrayTest, "Slate.ObservableArray", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 *
 */
bool FObservableArrayTest::RunTest(const FString& Parameters)
{
	::UE::Slate::Containers::EObservableArrayChangedAction ExpectedAction = ::UE::Slate::Containers::EObservableArrayChangedAction::Add;
	int32 ExpectedCounter = 0;
	int32 Counter = 0;
	TArray<int32> ExpectedArray;
	FObservableArrayTest* Self = this;
	auto TestExpectedAction = [Self , &ExpectedAction, &Counter, &ExpectedArray](::UE::Slate::Containers::TObservableArray<int32>::ObservableArrayChangedArgsType Args)
	{
		Self->AddErrorIfFalse(Args.GetAction() == ExpectedAction, TEXT("The notification occurs with the wrong action"));
		if (Args.GetAction() == Slate::Containers::EObservableArrayChangedAction::Add)
		{
			Self->AddErrorIfFalse(ExpectedArray == Args.GetItems(), TEXT("The notification occurs with the GetItems"));
		}
		else if (Args.GetAction() == Slate::Containers::EObservableArrayChangedAction::Remove)
		{
			Self->AddErrorIfFalse(ExpectedArray == Args.GetItems(), TEXT("The notification occurs with the wrong GetItmes"));
		}
		++Counter;
	};

	{
		{
			TArray<int32> ArrayValues;
			::UE::Slate::Containers::TObservableArray<int32> ObservableValues;
			ObservableValues.OnArrayChanged().AddLambda(TestExpectedAction);
			{
				ExpectedAction = ::UE::Slate::Containers::EObservableArrayChangedAction::Add;

				{
					ExpectedArray.Reset();
					ExpectedArray.Add(2);

					ArrayValues.Add(2);
					ObservableValues.Add(2);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Add(3);

					ArrayValues.Emplace(3);
					ObservableValues.Emplace(3);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Add(4);

					ArrayValues.EmplaceAt(2, 4);
					ObservableValues.EmplaceAt(2, 4);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Add(1);

					ArrayValues.EmplaceAt(0, 1);
					ObservableValues.EmplaceAt(0, 1);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Add(5);

					ArrayValues.Add(5);
					ObservableValues.Add(5);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Append({ 6, 7, 8, 9, 10, 11, 12 });

					ArrayValues.Append(ExpectedArray);
					ObservableValues.Append(ExpectedArray);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Add(9);

					ArrayValues.Append(ExpectedArray);
					ObservableValues.Append(ExpectedArray);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
			}
			{
				ExpectedAction = ::UE::Slate::Containers::EObservableArrayChangedAction::Swap;
				ExpectedArray.Reset();

				ArrayValues.Swap(1, 2);
				ObservableValues.Swap(1, 2);
				AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
				++ExpectedCounter;
				AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
			}
			{
				ExpectedAction = ::UE::Slate::Containers::EObservableArrayChangedAction::Remove;

				{
					ExpectedArray.Reset();
					ExpectedArray.Add(2);

					ArrayValues.RemoveSingle(2);
					ObservableValues.RemoveSingle(2);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					bool bContains = ObservableValues.Contains(2);
					if (bContains)
					{
						ExpectedArray.Reset();
						ExpectedArray.Add(2);
					}
					ArrayValues.RemoveSingle(2);
					bool bWasRemoved = ObservableValues.RemoveSingle(2) > 0;
					if (bWasRemoved)
					{
						++ExpectedCounter;
					}
					AddErrorIfFalse(bWasRemoved == bContains, TEXT("ObservableValues.Contains == ObservableValues.RemoveSingle"));
					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Add(3);

					ArrayValues.RemoveSingleSwap(3);
					ObservableValues.RemoveSingleSwap(3);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Add(ObservableValues[2]);

					ArrayValues.RemoveAt(2);
					ObservableValues.RemoveAt(2);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					int32 ToRemoveIndex = 2;
					int32 NumberToRemove = 3;
					for (int32 Index = ToRemoveIndex; Index < NumberToRemove + ToRemoveIndex; ++Index)
					{
						if (ObservableValues.IsValidIndex(Index))
						{
							ExpectedArray.Add(ObservableValues[Index]);
						}
					}

					ArrayValues.RemoveAt(ToRemoveIndex, NumberToRemove);
					ObservableValues.RemoveAt(ToRemoveIndex, NumberToRemove);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Add(ObservableValues[1]);

					ArrayValues.RemoveAtSwap(1);
					ObservableValues.RemoveAtSwap(1);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					ExpectedArray.Add(ObservableValues[0]);

					ArrayValues.RemoveAtSwap(0);
					ObservableValues.RemoveAtSwap(0);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
				{
					ExpectedArray.Reset();
					int32 ToRemoveIndex = 1;
					int32 NumberToRemove = 4;
					for (int32 Index = ToRemoveIndex; Index < ToRemoveIndex + NumberToRemove; ++Index)
					{
						if (ObservableValues.IsValidIndex(Index))
						{
							ExpectedArray.Add(ObservableValues[Index]);
						}
					}

					ArrayValues.RemoveAtSwap(ToRemoveIndex, NumberToRemove);
					ObservableValues.RemoveAtSwap(ToRemoveIndex, NumberToRemove);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
			}
			{
				ExpectedAction = ::UE::Slate::Containers::EObservableArrayChangedAction::Reset;

				ArrayValues.Reset();
				ObservableValues.Reset();
				AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
				++ExpectedCounter;
				AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
			}
			{
				ExpectedAction = ::UE::Slate::Containers::EObservableArrayChangedAction::Add;

				{
					ExpectedArray.Reset();
					ExpectedArray.Add(2);

					ArrayValues.Add(2);
					ObservableValues.Add(2);

					AddErrorIfFalse(ArrayValues == ObservableValues, TEXT("ObservableValues == ArrayValues"));
					++ExpectedCounter;
					AddErrorIfFalse(Counter == ExpectedCounter, "The delegate was not executed");
				}
			}
		}
	}

	return true;
}
} //namespace

#endif //WITH_DEV_AUTOMATION_TESTS

