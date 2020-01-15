// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "NetworkedSimulationModelBufferTypes.h"

/** This is the "system driver", it has functions that the TNetworkedSimulationModel needs to call internally, that are specific to the types but not specific to the simulation itself. */
template<typename TBufferTypes>
class TNetworkedSimulationModelDriver
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

// Internal traits that are derived from the user settings (E.g, these can't go in FNetSimModelDefBase because they are dependent on the user types)
// These can be overridden as well via template specialization but we don't expect this to be common.
template<typename T>
struct TNetSimModelTraitsBase
{
	// Internal buffer types: the model may wrap the user structs (e.g, input cmds + frame time) for internal usage
	using InternalBufferTypes = TInternalBufferTypes< typename T::BufferTypes, typename T::TickSettings>;
	
	// Driver class: interface that the NetSimModel needs user code to implement in order to function
	using Driver = TNetworkedSimulationModelDriver<typename T::BufferTypes>;
};

template<typename T> struct TNetSimModelTraits : public TNetSimModelTraitsBase<T> { };