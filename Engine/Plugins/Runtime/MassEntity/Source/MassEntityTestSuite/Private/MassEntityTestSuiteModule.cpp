// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityTestSuiteModule.h"

#define LOCTEXT_NAMESPACE "PipeTestSuite"

class FPipeTestSuiteModule : public IPipeTestSuiteModule
{
};

IMPLEMENT_MODULE(FPipeTestSuiteModule, PipeTestSuite)

#undef LOCTEXT_NAMESPACE
