// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextInterface_Wrapper.h"
#include "Param/ParamTypeHandle.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextInterface_Wrapper)

UE::AnimNext::FParamTypeHandle UAnimNextInterface_Wrapper::GetReturnTypeHandleImpl() const
{
	// We mimic the return type of our output
	return Output.GetInterface() ? Output.GetInterface()->GetReturnTypeHandle() : UE::AnimNext::FParamTypeHandle();
}

bool UAnimNextInterface_Wrapper::GetDataImpl(const UE::AnimNext::FContext& Context) const
{
	// Parameterize output with inputs
	//const UE::AnimNext::Interface::FContext ParameterizedContext = Context.WithParameters(Inputs);
	//return IAnimNextInterface::StaticGetDataRaw(Output, GetReturnTypeName(), ParameterizedContext, Result);
	return false;
}
