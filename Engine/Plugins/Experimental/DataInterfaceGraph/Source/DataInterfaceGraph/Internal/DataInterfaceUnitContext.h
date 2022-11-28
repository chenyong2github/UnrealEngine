// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataInterfaceContext.h"
#include "Units/RigUnitContext.h"

struct FDataInterfaceUnitContext : public FRigUnitContext
{
	FDataInterfaceUnitContext()
		: FRigUnitContext()
		, Interface(nullptr)
		, DataInterfaceContext(nullptr)
		, bResult(nullptr)
	{}

	FDataInterfaceUnitContext(const IDataInterface* InInterface, const UE::DataInterface::FContext& InDataInterfaceContext, bool& bInResult)
		: FRigUnitContext()
		, Interface(InInterface)
		, DataInterfaceContext(&InDataInterfaceContext)
		, bResult(&bInResult)
	{}

	const IDataInterface* Interface;
	const UE::DataInterface::FContext* DataInterfaceContext;
	bool* bResult;
};
