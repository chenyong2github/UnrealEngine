// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interface/AnimNextInterface.h"
#include "Interface/IAnimNextInterface.h"
#include "Interface/InterfaceContext.h"

namespace UE::AnimNext::Private
{

static bool CheckCompatibility(const IAnimNextInterface* InAnimNextInterface, const UE::AnimNext::FContext& InContext)
{
	check(InAnimNextInterface);
	return InContext.GetResultParam().GetTypeHandle() == InAnimNextInterface->GetReturnTypeHandle();
}

}

bool IAnimNextInterface::GetDataIfCompatibleInternal(const UE::AnimNext::FContext& InContext) const
{
	if(UE::AnimNext::Private::CheckCompatibility(this, InContext))
	{
		return GetDataRawInternal(InContext);
	}

	return false;
}

bool IAnimNextInterface::GetData(const UE::AnimNext::FContext& Context) const
{
	return GetDataIfCompatibleInternal(Context);
}

bool IAnimNextInterface::GetDataChecked(const UE::AnimNext::FContext& Context) const
{
	check(UE::AnimNext::Private::CheckCompatibility(this, Context));
	return GetDataRawInternal(Context);
}

bool IAnimNextInterface::GetData(const UE::AnimNext::FContext& Context, UE::AnimNext::FParam& OutResult) const
{
	const UE::AnimNext::FContext CallingContext = Context.WithResult(OutResult);
	return GetDataIfCompatibleInternal(CallingContext);
}

bool IAnimNextInterface::GetDataChecked(const UE::AnimNext::FContext& Context, UE::AnimNext::FParam& OutResult) const
{
	const UE::AnimNext::FContext CallingContext = Context.WithResult(OutResult);
	check(UE::AnimNext::Private::CheckCompatibility(this, CallingContext));
	return GetDataRawInternal(CallingContext);
}

bool IAnimNextInterface::GetDataRawInternal(const UE::AnimNext::FContext& InContext) const
{
	// TODO: debug recording here on context

	const UE::AnimNext::FContext CallContext = InContext.WithCallRaw(this);
	return GetDataImpl(CallContext);
}

UE::AnimNext::FParamTypeHandle IAnimNextInterface::GetReturnTypeHandle() const
{
	return GetReturnTypeHandleImpl();
}