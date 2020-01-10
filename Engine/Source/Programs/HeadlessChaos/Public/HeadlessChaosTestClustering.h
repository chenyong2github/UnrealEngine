// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"

namespace ChaosTest {

	/**/
	template<class T>
	void ImplicitCluster();
	
	template<class T>
	void FractureCluster();

	template<class T>
	void PartialFractureCluster();
}