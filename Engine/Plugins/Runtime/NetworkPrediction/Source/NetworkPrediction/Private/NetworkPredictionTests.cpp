// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkedSimulationModel.h"

// ------------------------------------------------------------------------------------------------------------
//	Testing Fixed vs Variable tick rate struct declration
// ------------------------------------------------------------------------------------------------------------

DEFINE_LOG_CATEGORY_STATIC(NetworkSimulationTests, Log, All);

struct FNetSimDummy { };
using FDummyBufferTypes = TNetworkSimBufferTypes<FNetSimDummy, FNetSimDummy, FNetSimDummy>;

template<typename TTickSettings>
void TickSettingsTest(TTickSettings& Settings)
{
	using TBufferTypes = TInternalBufferTypes<FDummyBufferTypes, TTickSettings>;

	TSimulationTicker<TTickSettings> Ticker;

	float RealTime = FMath::Rand();
	Ticker.GiveSimulationTime(RealTime);

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

template<typename TBuffer>
void NetSimulationTestBuffer(TBuffer& Buffer)
{
	for (int32 i=0; i < 10; ++i)
	{
		typename TBuffer::ElementType NewData;
		*Buffer.WriteFrame(i) = NewData;
		UE_LOG(NetworkSimulationTests, Display, TEXT("%s"), *Buffer.GetBasicDebugStr());

		for (auto It = Buffer.CreateIterator(); It; ++It)
		{
			UE_LOG(NetworkSimulationTests, Display, TEXT("   [%d] %s"), It.Frame(), *It.Element()->ToString());
		}

		for (auto It = Buffer.CreateConstIterator(); It; ++It)
		{
			UE_LOG(NetworkSimulationTests, Display, TEXT("   [%d] %s"), It.Frame(), *It.Element()->ToString());
		}
	}
}

void NetSimulationBufferTest()
{
	struct FState
	{
		int32 x = 0;
		int32 y = 0;
		int32 z = 0;
		float w = 1.f;

		FString ToString() const { return FString::Printf(TEXT("%d %d %d %f"), x, y, z, w); }
	};

	TNetworkSimSparseBuffer<FState> SparseBuffer;
	TNetworkSimContiguousBuffer<FState> ContiguousBuffer;

	NetSimulationTestBuffer(SparseBuffer);
	NetSimulationTestBuffer(ContiguousBuffer);


	//TNetworkSimBufferIterator<TNetworkSimSparseBuffer<FState>> It(SparseBuffer);
	//It.Frame();
}
