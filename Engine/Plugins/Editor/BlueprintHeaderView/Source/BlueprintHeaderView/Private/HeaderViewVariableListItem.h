// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SBlueprintHeaderView.h"

struct FBPVariableDescription;

/** A header view list item that displays a variable declaration */
struct FHeaderViewVariableListItem : public FHeaderViewListItem
{
	/** Creates a list item for the Header view representing a variable declaration for the given blueprint variable */
	static FHeaderViewListItemPtr Create(const FBPVariableDescription* VariableDesc, const FProperty& VarProperty);

protected:
	FHeaderViewVariableListItem(const FBPVariableDescription* VariableDesc, const FProperty& VarProperty);

	/** Formats a line declaring a delegate type and appends it to the item strings */
	void FormatDelegateDeclaration(const FMulticastDelegateProperty& DelegateProp);

	/** Returns a string containing the specifiers for the UPROPERTY line */
	FString GetConditionalUPropertySpecifiers(const FProperty& VarProperty) const;

	/** Returns the name of the owning class */
	FString GetOwningClassName(const FProperty& VarProperty) const;

};