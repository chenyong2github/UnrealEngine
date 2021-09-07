// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class AActor;
class APlayerController;

namespace UE_NP {

using FAsyncModelDefID = int32;
#define NP_ASYNC_MODEL_BODY() static UE_NP::FAsyncModelDefID ID;

struct FNetworkPredictionAsyncModelDef
{
	// Actual defs should have:
	// NP_MODEL_BODY(); 
	//	static const TCHAR* GetName() { return TEXT("YourName"); }
	//	static constexpr int32 GetSortPriority() { return (int32)ENetworkPredictionSortPriority::PreKinematicMovers; }

	using InputCmdType		= void;	// The state the controller is authoritative over (e.g, can be the client)
	using NetStateType	= void;	// The object state that we do network and track frame-to-frame
	using LocalStateType = void; // Local state that is not networked and we don't track frame-to-frame. (e.g, the local PhysicsProxy*)
	using OutStateType		= void;	// Output state marshaled back to the GT
	
	using ControlKeyType = APlayerController*;	// Identifier for the controller

	using SimulationTickType = void; // what to call ::SimulationTick on 
};

#define HOIST_ASYNCMODELDEF_TYPES() \
	using InputCmdType = typename AsyncModelDef::InputCmdType; \
	using LocalStateType = typename AsyncModelDef::LocalStateType; \
	using NetStateType = typename AsyncModelDef::NetStateType; \
	using OutStateType = typename AsyncModelDef::OutStateType; \
	using ControlKeyType = typename AsyncModelDef::ControlKeyType; \
	using SimulationTickType = typename AsyncModelDef::SimulationTickType;

} // namespace UE_NP