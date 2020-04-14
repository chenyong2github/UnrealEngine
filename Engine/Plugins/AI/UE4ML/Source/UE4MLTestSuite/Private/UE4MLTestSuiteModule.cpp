// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE4MLTestSuiteModule.h"

#define LOCTEXT_NAMESPACE "UE4MLTestSuite"

class FUE4MLTestSuiteModule : public IUE4MLTestSuiteModule
{
};

IMPLEMENT_MODULE(FUE4MLTestSuiteModule, UE4MLTestSuite)

#undef LOCTEXT_NAMESPACE
