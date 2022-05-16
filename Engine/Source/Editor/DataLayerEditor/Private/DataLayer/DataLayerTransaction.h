// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScopedTransaction.h"

class FScopedDataLayerTransaction
{
public:
	FScopedDataLayerTransaction(const FText& SessionName, UWorld*)
		: ScopedTransaction(SessionName)
	{}

private:
	FScopedTransaction ScopedTransaction;
};