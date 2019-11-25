// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetworkedSimulationModelCues.h"

DEFINE_LOG_CATEGORY(LogNetSimCues);

FGlobalCueTypeTable FGlobalCueTypeTable::Singleton;

constexpr FNetworkSimTime FCueDispatcherTraitsBase::ReplicationWindow;

// ------------------------------------------------------------------------------------------------------------------------------------------
//	Mock Cue example
// ------------------------------------------------------------------------------------------------------------------------------------------

// Minimal example of NetSimCue pipeline
#define ENABLE_MOCK_CUE 0
#if ENABLE_MOCK_CUE

// ---------------------------------------------------------------------
// Mock Cue Types
// ---------------------------------------------------------------------

struct FMockImpactCue
{
	NETSIMCUE_BODY();

	FMockImpactCue() = default;
	FMockImpactCue(const FVector& InImpact) : ImpactLocation(InImpact) { }

	FVector ImpactLocation;

	void NetSerialize(FArchive& Ar)
	{
		Ar << ImpactLocation;
	}

	bool NetUnique(const FMockImpactCue& Other) const
	{
		const float ErrorTolerance = 1.f;
		return !ImpactLocation.Equals(Other.ImpactLocation, ErrorTolerance);
	}

};
NETSIMCUE_REGISTER(FMockImpactCue, TEXT("Impact"));

// ----------------

struct FMockDamageCue
{
	NETSIMCUE_BODY();

	FMockDamageCue() = default;
	FMockDamageCue(const int32 InSourceID, const int32 InDamageType, const FVector& InHitLocation) 
		: SourceID(InSourceID), DamageType(InDamageType), HitLocation(InHitLocation) { }

	int32 SourceID;
	int32 DamageType;
	FVector HitLocation;
	
	void NetSerialize(FArchive& Ar)
	{
		Ar << SourceID;
		Ar << DamageType;
		Ar << HitLocation;
	}

	bool NetUnique(const FMockDamageCue& Other) const
	{
		const float ErrorTolerance = 1.f;
		return SourceID != Other.SourceID || DamageType != Other.DamageType || !HitLocation.Equals(Other.HitLocation, ErrorTolerance);
	}
};
NETSIMCUE_REGISTER(FMockDamageCue, TEXT("Damage"));

struct FMockHealingCue
{
	NETSIMCUE_BODY();
	
	FMockHealingCue() = default;
	FMockHealingCue(const int32 InSourceID, const int32 InHealingAmount) 
		: SourceID(InSourceID), HealingAmount(InHealingAmount) { }

	int32 SourceID;
	int32 HealingAmount;
	
	void NetSerialize(FArchive& Ar)
	{
		Ar << SourceID;
		Ar << HealingAmount;
	}

	bool NetUnique(const FMockHealingCue& Other) const
	{
		const float ErrorTolerance = 1.f;
		return SourceID != Other.SourceID || HealingAmount != Other.HealingAmount;
	}
};
NETSIMCUE_REGISTER(FMockHealingCue, TEXT("Healing"));

// ---------------------------------------------------------------------
// Mock Handlers (equevilent to actor component hierarchy)
// ---------------------------------------------------------------------

class TestHandlerBase
{
public:

	template<typename TDispatchTable>
	static void RegisterNetSimCueTypes(TDispatchTable& DispatchTable)
	{
		DispatchTable.template RegisterType<FMockImpactCue>();
	}

	void HandleCue(const FMockImpactCue& ImpactCue, const FNetSimCueSystemParamemters& SystemParameters)
	{
		UE_LOG(LogTemp, Warning, TEXT("Impact!"));
	}
};

class TestHandlerChild : public TestHandlerBase
{
public:

	template<typename TDispatchTable>
	static void RegisterNetSimCueTypes(TDispatchTable& DispatchTable)
	{
		TestHandlerBase::RegisterNetSimCueTypes(DispatchTable);
		DispatchTable.template RegisterType<FMockDamageCue>();
		DispatchTable.template RegisterType<FMockHealingCue>();
	}

	using TestHandlerBase::HandleCue; // Required for us to "use" the HandleCue methods in our parent class

	void HandleCue(const FMockDamageCue& DamageCue, const FNetSimCueSystemParamemters& SystemParameters)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleCue: Damage. SourceID: %d DamageType: %d HitLocation: %s"), DamageCue.SourceID, DamageCue.DamageType, *DamageCue.HitLocation.ToString());
	}

	void HandleCue(const FMockHealingCue& HealingCue, const FNetSimCueSystemParamemters& SystemParameters)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleCue: Healing. SourceID: %d Healing Amount: %d"), HealingCue.SourceID, HealingCue.HealingAmount);
	}
};

NETSIMCUEHANDLER_REGISTER(TestHandlerChild);

void TestCues()
{
	// -------------------------------------
	// Simuation/Invoke
	// -------------------------------------

	struct FBaseSimulation
	{
		static void TickSimulation(FNetSimCueDispatcher& Dispatcher)
		{
			Dispatcher.Invoke<FMockImpactCue>(FVector(1.f, 2.f, 3.f));
		}
	};

	struct FChildSimulation
	{
		static void TickSimulation(FNetSimCueDispatcher& Dispatcher)
		{
			FBaseSimulation::TickSimulation(Dispatcher);

			Dispatcher.Invoke<FMockDamageCue>(1, 2, FVector(1.f, 2.f, 3.f));
			Dispatcher.Invoke<FMockHealingCue>(10, 32);
		}
	};

	TNetSimCueDispatcher<void> ServerDispatcher;
	FChildSimulation::TickSimulation(ServerDispatcher);

	// -------------------------------------
	// Send
	// -------------------------------------

	FNetBitWriter TempWriter(1024 << 3);
	ServerDispatcher.NetSerializeSavedCues(TempWriter, false);

	// -------------------------------------
	// Receive
	// -------------------------------------

	FNetBitReader TempReader(nullptr, TempWriter.GetData(), TempWriter.GetNumBits());
	TNetSimCueDispatcher<void> ClientDispatcher;
	ClientDispatcher.NetSerializeSavedCues(TempReader, false);

	TestHandlerChild MyObject;

	ClientDispatcher.DispatchCueRecord(MyObject, FNetworkSimTime());
}

FAutoConsoleCommandWithWorldAndArgs NetworkSimulationModelCueCmd(TEXT("nms.CueTest"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* World) 
{
	TestCues();
}));

#endif // ENABLE_MOCK_CUE