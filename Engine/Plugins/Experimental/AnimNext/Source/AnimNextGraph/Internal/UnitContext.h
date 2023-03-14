// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interface/InterfaceContext.h"
#include "Units/RigUnitContext.h"

struct FAnimNextUnitContext : public FRigUnitContext
{
	FAnimNextUnitContext()
		: FRigUnitContext()
		, Interface(nullptr)
		, InterfaceContext(nullptr)
		, bResult(nullptr)
	{}

	FAnimNextUnitContext(const IAnimNextInterface* InInterface, const UE::AnimNext::FContext& InInterfaceContext, bool& bInResult)
		: FRigUnitContext()
		, Interface(InInterface)
		, InterfaceContext(&InInterfaceContext)
		, bResult(&bInResult)
	{}

	const IAnimNextInterface* Interface;
	const UE::AnimNext::FContext* InterfaceContext;
	bool* bResult;
};
