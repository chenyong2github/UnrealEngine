// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "Physics/PhysicsInterfaceDeclares.h"


class FPhysicsUserData_Chaos
{
private:
	enum EType
	{
		Invalid,
		BodyInstance,
		PhysScene
	};

	EType Type;
	void* Payload;

public:
	FPhysicsUserData_Chaos() : Type(EType::Invalid), Payload(nullptr) { }
	FPhysicsUserData_Chaos(FBodyInstance* InPayload) : Type(EType::BodyInstance), Payload(InPayload) { }
	FPhysicsUserData_Chaos(FPhysScene* InPayload) : Type(EType::PhysScene), Payload(InPayload) { }

	template <class T> static T* Get(void* UserData);
	template <class T> static void Set(void* UserData, T* InPayload);

	/*
	template <class T> static T* Get(intptr_t UserData)
	{
		return Get<T>((void*)UserData);
	}

	template <class T> static void Set(intptr_t UserData, T* InPayload)
	{
		return Get<T>((void*)UserData);
	}
	*/

private:

	template <class T, EType TType> static T* Get(void* UserData)
	{
		FPhysicsUserData_Chaos* ChaosUserData = (FPhysicsUserData_Chaos*)UserData;
		return UserData && ChaosUserData->Type == TType ? (T*)(ChaosUserData->Payload) : nullptr;
	}
};

template<> FORCEINLINE FBodyInstance* FPhysicsUserData_Chaos::Get(void* UserData)
{
	return Get<FBodyInstance, EType::BodyInstance>(UserData);
}

template<> FORCEINLINE FPhysScene* FPhysicsUserData_Chaos::Get(void* UserData)
{
	return Get<FPhysScene, EType::PhysScene>(UserData);
}