// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestFixtures/CoreTestFixture.h"
#include "TestCommon/CoreUtilities.h"
#include "TestCommon/CoreUObjectUtilities.h"

FCoreTestFixture::FCoreTestFixture()
{
	InitTaskGraphAndDependencies();
	InitCoreUObject();
}

FCoreTestFixture::~FCoreTestFixture()
{
	//CleanupCoreUObject();
	//CleanupTaskGraphAndDependencies();
}
