// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// Manages how to allocate a budget of levels to an ordered list of items (e.g., how to distribute minimum LOD levels to the N closest characters)
struct FOrderedBudget
{
public:
	FOrderedBudget()
		: ValueForOutOfBounds(0)
	{
	}

	// Returns the budget number for the Index-th closest character
	FORCEINLINE int32 GetBudgetForIndex(int32 Index) const
	{
		return (Index < BudgetValues.Num()) ? BudgetValues[Index] : ValueForOutOfBounds;
	}

	// Returns the number of items specified in the budget
	FORCEINLINE int32 GetBudgetLength() const
	{
		return BudgetValues.Num();
	}

	// Recreates the budget given a specification string. For example, the specification "0,2,3,5" would correspond to the following budget:
	//   Level0: 0
	//   Level1: 2
	//   Level2: 3
	//   Level3: 5
	// Creating a table that contains:
	//   1,1,2,2,2,3,3,3,3,3 (out of bounds = 4)
	// Returns true if the budget was modified, or false if the existing budget already matched
	bool RecreateBudget(const FString& Specification, float InBudgetValuesScale = 1.0f)
	{
		const bool bSpecificationDiffers = Specification != BudgetString || BudgetValuesScale != InBudgetValuesScale;

		if (bSpecificationDiffers)
		{
			BudgetValues.Reset();
			BudgetString = Specification;

			TArray<FString> BudgetStrings;
			BudgetString.ParseIntoArray(/*out*/ BudgetStrings, TEXT(","), /*bCullEmpty=*/ false);

			int32 LevelIndex = 0;
			for (const FString& BudgetItem : BudgetStrings)
			{
				const int32 CountForThisLevel = FCString::Atoi(*BudgetItem) * InBudgetValuesScale;
				check(CountForThisLevel >= 0);

				for (int32 ThisLevelIndex = 0; ThisLevelIndex < CountForThisLevel; ++ThisLevelIndex)
				{
					BudgetValues.Add(LevelIndex);
				}

				++LevelIndex;
			}

			BudgetValuesScale = InBudgetValuesScale;
			ValueForOutOfBounds = LevelIndex;
		}

		return bSpecificationDiffers;
	}

private:
	// This is the budget value for the i-th closest character (e.g., there will be be 10 entries if the budgets ranges sum to cover 10)
	TArray<int32> BudgetValues;

	// The scalar to apply to the budget values.
	float BudgetValuesScale = 1.0f;

	// This is the budget value for things with an index further away than BudgetByIndex.Num()
	int32 ValueForOutOfBounds;

	// My budget string
	FString BudgetString;
};
