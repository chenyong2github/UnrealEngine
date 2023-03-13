// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextInterface_Bool.h"
#include "AnimNextInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextInterface_Bool)

bool UAnimNextInterface_Bool_And::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	using namespace UE::AnimNext;
	
	check(Inputs.Num() > 0);

	bool bInterfaceResult = true;
	bool bIntermediateResult = false;

	// Get result to write to
	bool& bOutResult = Context.GetResult<bool>();
	
	for(const TScriptInterface<IAnimNextInterface>& Input : Inputs)
	{
		// Get any inputs we may have
		bInterfaceResult &= Interface::GetDataSafe(Input, Context, bIntermediateResult);

		// AND the inputs
		bOutResult = bOutResult && bIntermediateResult;
	}

	return bInterfaceResult;
}

bool UAnimNextInterface_Bool_Not::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	using namespace UE::AnimNext;

	// Get result to write to
	bool& bOutResult = Context.GetResult<bool>();
	
	// Get any input we may have
	bool bInputResult = false;
	const bool bInterfaceResult = Interface::GetDataSafe(Input, Context, bInputResult);

	// NOT the input, write to output
	bOutResult = !bInputResult;

	return bInterfaceResult;
}
