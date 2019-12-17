// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

// Base for Network Simulation Model Defs (defines types used by the model)
class FNetSimModelDefBase
{
public:

	// -----------------------------------------------------------------------------------------------
	//	Must be set by user to compile:
	// -----------------------------------------------------------------------------------------------

	// The simulation class. This encompasses all state and functions for running the simulation but none of the networking/bookkeeping etc.
	using Simulation = void;

	// Buffer types: defines your Input/Sync/Aux state (and optional debug state)
	using BufferTypes = void;	

	// -----------------------------------------------------------------------------------------------
	//	Optional customization/overrides 
	// -----------------------------------------------------------------------------------------------

	// Model base class. E.g, common interface all NetworkedSimulationModels can be communicated with
	using Base = INetworkedSimulationModel;
	
	// Tick settings: defines variable vs fix step tick, etc
	using TickSettings = TNetworkSimTickSettings<>;
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