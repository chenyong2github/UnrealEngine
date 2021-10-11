// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/UnrealString.h"
#include "Templates/EnableIf.h"

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



// Template nonsense to implement optional user-defined functions and operations.
// Mainly we want to avoid base classes on the user states, so calls that are optional
// can go through NpModelUtil to either do nothing or some default implementation, or
// be overridden by the user when they need to.
template<typename AsyncModelDef>
struct NpModelUtilBase
{
	HOIST_ASYNCMODELDEF_TYPES();

	
	struct CToStringFuncable
	{
		template <typename InType>
		auto Requires(InType* T) -> decltype(T->ToString());
	};

	template<typename T, bool bEnable=TModels<CToStringFuncable, T>::Value>
	static typename TEnableIf<bEnable, FString>::Type ToString(const T* State)
	{
		return State->ToString();
	}

	
	template<typename T, bool bEnable=TModels<CToStringFuncable, T>::Value>
	static typename TEnableIf<!bEnable, FString>::Type ToString(const T* State)
	{
		return FString(TEXT("{requires ToString()}"));
	}
};

template<typename AsyncModelDef>
struct NpModelUtil : NpModelUtilBase<AsyncModelDef>
{

};


} // namespace UE_NP