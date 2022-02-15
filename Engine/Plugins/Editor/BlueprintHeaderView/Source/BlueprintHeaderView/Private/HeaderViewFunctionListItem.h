// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SBlueprintHeaderView.h"

class UK2Node_FunctionEntry;

/** A header view list item that displays a function declaration */
struct FHeaderViewFunctionListItem : public FHeaderViewListItem
{
	/** Creates a list item for the Header view representing a function declaration for the given blueprint function */
	static FHeaderViewListItemPtr Create(const UK2Node_FunctionEntry* FunctionEntry);

protected:
	FHeaderViewFunctionListItem(const UK2Node_FunctionEntry* FunctionEntry);

	/** Returns a string containing the specifiers for the UFUNCTION line */
	FString GetConditionalUFunctionSpecifiers(const UFunction* SigFunction) const;

	/** Adds Function parameters to the RichText and PlainText strings */
	void AppendFunctionParameters(const UFunction* SignatureFunction);

};