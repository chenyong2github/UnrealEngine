// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetworkSimulationModel.h"

// ------------------------------------------------------------------------------------------------------------
//	Testing Fixed vs Variable tick rate struct declration
// ------------------------------------------------------------------------------------------------------------



struct FNetSimDummy { };
using FDummyBufferTypes = TNetworkSimBufferTypes<FNetSimDummy, FNetSimDummy, FNetSimDummy>;

template<typename TTickSettings>
void TickSettingsTest(TTickSettings& Settings)
{
	using TBufferTypes = TInternalBufferTypes<FDummyBufferTypes, TTickSettings>;

	TSimulationTickState<TTickSettings>	TickState;

	float RealTime = FMath::Rand();
	TickState.GiveSimulationTime(RealTime);

}

void NetSimulationTypesTest()
{
	TNetworkSimTickSettings<> DefaultTickSettings;
	TickSettingsTest(DefaultTickSettings);


	TNetworkSimTickSettings<0, 60> VariableTickSettingsWithMax;
	TickSettingsTest(VariableTickSettingsWithMax);

	TNetworkSimTickSettings<20> FixedTickSettings;
	TickSettingsTest(FixedTickSettings);

	//TNetworkSimTickSettings<20, 60> StaticAssertSettings;
	//TickSettingsTest(StaticAssertSettings);
}
