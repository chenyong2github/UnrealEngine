// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_Name.generated.h"

USTRUCT(meta=(Abstract, Category="Core|Name", NodeColor = "0.7 0.05 0.5"))
struct FRigUnit_NameBase : public FRigUnit
{
	GENERATED_BODY()
};

/**
 * Concatenates two strings together to make a new string
 */
USTRUCT(meta = (DisplayName = "Concat", Keywords = "Add,+,Combine,Merge,Append"))
struct FRigUnit_NameConcat : public FRigUnit_NameBase
{
	GENERATED_BODY()

	FRigUnit_NameConcat()
	{
		A = B = Result = NAME_None;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName A;

	UPROPERTY(meta=(Input))
	FName B;

	UPROPERTY(meta=(Output))
	FName Result;
};

/**
 * Returns the left or right most characters from the string chopping the given number of characters from the start or the end
 */
USTRUCT(meta = (DisplayName = "Chop", Keywords = "Truncate,-,Remove,Subtract,Split"))
struct FRigUnit_NameTruncate : public FRigUnit_NameBase
{
	GENERATED_BODY()

	FRigUnit_NameTruncate()
	{
		Name = NAME_None;
		Count = 1;
		FromEnd = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	FName Name;

	// Number of characters to remove from left or right
	UPROPERTY(meta=(Input))
	int32 Count;

	// if set to true the characters will be removed from the end
	UPROPERTY(meta=(Input))
	bool FromEnd;

	// the part of the string without the chopped characters
	UPROPERTY(meta=(Output))
	FName Remainder;

	// the part of the name that has been chopped off
	UPROPERTY(meta = (Output))
	FName Chopped;
};

/**
 * Replace all occurrences of a substring in this string
 */
USTRUCT(meta = (DisplayName = "Replace", Keywords = "Search,Emplace,Find"))
struct FRigUnit_NameReplace : public FRigUnit_NameBase
{
	GENERATED_BODY()

	FRigUnit_NameReplace()
	{
		Name = Old = New = NAME_None;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FName Name;

	UPROPERTY(meta = (Input))
	FName Old;

	UPROPERTY(meta = (Input))
	FName New;

	UPROPERTY(meta = (Output))
	FName Result;
};

/**
 * Tests whether this string ends with given string
 */
USTRUCT(meta = (DisplayName = "Ends With", Keywords = "Right"))
struct FRigUnit_EndsWith : public FRigUnit_NameBase
{
	GENERATED_BODY()

	FRigUnit_EndsWith()
	{
		Name = Ending = NAME_None;
		Result = false;
	}

	RIGVM_METHOD()
		virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FName Name;

	UPROPERTY(meta = (Input))
	FName Ending;

	UPROPERTY(meta = (Output))
	bool Result;
};

/**
 * Tests whether this string starts with given string
 */
USTRUCT(meta = (DisplayName = "Starts With", Keywords = "Left"))
struct FRigUnit_StartsWith : public FRigUnit_NameBase
{
	GENERATED_BODY()

	FRigUnit_StartsWith()
	{
		Name = Start = NAME_None;
		Result = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FName Name;

	UPROPERTY(meta = (Input))
	FName Start;

	UPROPERTY(meta = (Output))
	bool Result;
};

/**
 * Returns true or false if a given name exists in another given name
 */
USTRUCT(meta = (DisplayName = "Contains", Keywords = "Contains,Find,Has,Search"))
struct FRigUnit_Contains : public FRigUnit_NameBase
{
	GENERATED_BODY()

		FRigUnit_Contains()
	{
		Name = Search = NAME_None;
		Result = false;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FName Name;

	UPROPERTY(meta = (Input))
	FName Search;

	UPROPERTY(meta = (Output))
	bool Result;
};