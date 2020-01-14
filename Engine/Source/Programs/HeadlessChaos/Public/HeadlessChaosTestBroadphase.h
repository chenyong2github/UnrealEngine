// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "HeadlessChaosTestUtility.h"

namespace ChaosTest {

	template<typename T>
	void GridBPTest();

	template<typename T>
	void GridBPTest2();

	template<typename T>
	void AABBTreeTest();

	template<typename T>
	void AABBTreeTimesliceTest();

	template<typename T>
	void BroadphaseCollectionTest();

	void TestPendingSpatialDataHandlePointerConflict();
}