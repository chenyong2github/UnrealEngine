// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetworkedSimulationModelCues.h"

DEFINE_LOG_CATEGORY(LogNetSimCues);

FGlobalCueTypeTable FGlobalCueTypeTable::Singleton;

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

	FVector ImpactLocation;

	void NetSerialize(FArchive& Ar)
	{
		Ar << ImpactLocation;
	}

	static bool Unique(const FMockImpactCue& A, const FMockImpactCue& B)
	{
		const float ErrorTolerance = 1.f;
		return !A.ImpactLocation.Equals(B.ImpactLocation, ErrorTolerance);
	}

};
NETSIMCUE_REGISTER(FMockImpactCue, TEXT("Impact"));

// ----------------

struct FMockDamageCue
{
	NETSIMCUE_BODY();

	int32 SourceID;
	int32 DamageType;
	FVector HitLocation;
	
	void NetSerialize(FArchive& Ar)
	{
		Ar << SourceID;
		Ar << DamageType;
		Ar << HitLocation;
	}

	static bool Unique(const FMockDamageCue& A, const FMockDamageCue& B)
	{
		const float ErrorTolerance = 1.f;
		return A.SourceID != B.SourceID || A.DamageType != B.DamageType || !A.HitLocation.Equals(B.HitLocation, ErrorTolerance);
	}
};
NETSIMCUE_REGISTER(FMockDamageCue, TEXT("Damage"));

struct FMockHealingCue
{
	NETSIMCUE_BODY();

	int32 SourceID;
	int32 HealingAmount;
	
	void NetSerialize(FArchive& Ar)
	{
		Ar << SourceID;
		Ar << HealingAmount;
	}

	static bool Unique(const FMockHealingCue& A, const FMockHealingCue& B)
	{
		const float ErrorTolerance = 1.f;
		return A.SourceID != B.SourceID || A.HealingAmount != B.HealingAmount;
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

	void HandleCue(FMockImpactCue& ImpactCue, const FNetworkSimTime& ElapsedTime)
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

	void HandleCue(FMockDamageCue& DamageCue, const FNetworkSimTime& ElapsedTime)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleCue: Damage. SourceID: %d DamageType: %d HitLocation: %s"), DamageCue.SourceID, DamageCue.DamageType, *DamageCue.HitLocation.ToString());
	}

	void HandleCue(FMockHealingCue& HealingCue, const FNetworkSimTime& ElapsedTime)
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
		static void TickSimulation(FCueDispatcher& Dispatcher)
		{
			Dispatcher.Invoke<FMockImpactCue>( { FVector(1.f, 2.f, 3.f) } );
		}
	};

	struct FChildSimulation
	{
		static void TickSimulation(FCueDispatcher& Dispatcher)
		{
			FBaseSimulation::TickSimulation(Dispatcher);

			Dispatcher.Invoke<FMockDamageCue>( { 1, 2, FVector(1.f, 2.f, 3.f) } );
			Dispatcher.Invoke<FMockHealingCue>( { 10, 32 } );
		}
	};

	FCueDispatcher ServerDispatcher;
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
	FCueDispatcher ClientDispatcher;
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