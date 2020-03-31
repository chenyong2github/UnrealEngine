// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "NetworkedSimulationModelBufferTypes.h"

// This is the "system driver", it has functions that the TNetworkedSimulationModel needs to call internally, 
// that are specific to the types but not specific to the simulation itself. This is main:
//	Finalize Frame: how to push the sync/aux state to the UE4 side.
//	Produce Input: how to produce the InputCmd when necessary.
//
// The driver is templatized on the BufferTypes. This is so that the driver (UE4 actor/component) just has to know about
// the underlying user structure types, and not about the networking model or simulation class.
//
// If we templatized the driver on the Model def, we would need separate UE4 actor/component for fixed vs non fixed tick simulations.
// This way, we can define a single UE4 piece that just handles the user input/sync/aux state and doesn't care about the other stuff
// defined in the Model def.

template<typename TBufferTypes>
class TNetworkedSimulationModelDriverBase
{
public:
	using TInputCmd = typename TBufferTypes::TInputCmd;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState= typename TBufferTypes::TAuxState;

	// Debug string that can be used in internal warning/error logs
	virtual FString GetDebugName() const = 0;

	// Owning object for Visual Logs so that the system can emit them internally
	virtual const AActor* GetVLogOwner() const = 0;

	// Call to visual log the given states. Note that not all 3 will always be present and you should check for nullptrs. Child classes most likely want to override ::VisualLog, not this
	virtual void InvokeVisualLog(const TInputCmd* Input, const TSyncState* Sync, const TAuxState* Aux, const FVisualLoggingParameters& SystemParameters) const
	{
		// String-ify the passed in states
		FStringOutputDevice StrOut;
		StrOut.SetAutoEmitLineTerminator(true);

		FStandardLoggingParameters LogParameters(&StrOut, EStandardLoggingContext::Full, SystemParameters.Frame);
		if (Input)
		{
			StrOut.Log(TEXT("Input:"));
			Input->Log(LogParameters);
			StrOut.Log(TEXT(""));
		}
		if (Sync)
		{
			StrOut.Log(TEXT("Sync:"));
			Sync->Log(LogParameters);
			StrOut.Log(TEXT(""));
		}
		if (Aux)
		{
			StrOut.Log(TEXT("Aux:"));
			Aux->Log(LogParameters);
		}

		SystemParameters.StateString = StrOut;
		VisualLog(Input, Sync, Aux, SystemParameters);
	}
	
	// Called whenever the sim is ready to process new local input.
	virtual void ProduceInput(const FNetworkSimTime SimTime, TInputCmd&) = 0;
	
	// Called from the Network Sim at the end of the sim frame when there is new sync data.
	virtual void FinalizeFrame(const typename TBufferTypes::TSyncState& SyncState, const typename TBufferTypes::TAuxState& AuxState) = 0;

protected:

	// Called to visual log the given states. Note that not all 3 will always be present and you should check for nullptrs.
	virtual void VisualLog(const TInputCmd* Input, const TSyncState* Sync, const TAuxState* Aux, const FVisualLoggingParameters& SystemParameters) const = 0;
};

// Outward facing class. This allows user simulations to extend the driver interface if necessary.
// Use case would be if writing custom RepControllers that required special communication with the driver.
template<typename TBufferTypes>
class TNetworkedSimulationModelDriver : public TNetworkedSimulationModelDriverBase<TBufferTypes> { };