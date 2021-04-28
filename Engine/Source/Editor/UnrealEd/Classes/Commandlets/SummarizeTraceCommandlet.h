// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SummarizeTraceCommandlet.cpp:
	  Commandlet for summarizing utrace cpu events into to csv
=============================================================================*/

#pragma once
#include "Commandlets/Commandlet.h"
#include "SummarizeTraceCommandlet.generated.h"

UCLASS(config=Editor)
class USummarizeTraceCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UCommandlet Interface

	virtual int32 Main(const FString& CmdLineParams) override;
	
	//~ End UCommandlet Interface
};
