// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UnrealTypeTraits.h"
#include "UObject/Class.h"

namespace UE::StructUtils
{
	template <typename T>
	void CheckStructType()
	{
		static_assert(!TIsDerivedFrom<T, struct FInstancedStruct>::IsDerived &&
					  !TIsDerivedFrom<T, struct FConstStructView>::IsDerived &&
					  !TIsDerivedFrom<T, struct FStructView>::IsDerived &&
					  !TIsDerivedFrom<T, struct FConstSharedStruct>::IsDerived, "It does not make sense to create a instanced struct over an other struct wrapper type");
	}

	/** Returns reference to the struct, this assumes that all data is valid. */
	template<typename T>
	T& GetStructRef(const UScriptStruct* ScriptStruct, uint8* StructMemory)
	{
		check(StructMemory != nullptr);
		check(ScriptStruct != nullptr);
		check(ScriptStruct == TBaseStructure<T>::Get() || ScriptStruct->IsChildOf(TBaseStructure<T>::Get()));
		return *((T*)StructMemory);
	}

	/** Returns pointer to the struct, or nullptr if cast is not valid. */
	template<typename T>
	T* GetStructPtr(const UScriptStruct* ScriptStruct, uint8* StructMemory)
	{
		if (StructMemory != nullptr 
			&& ScriptStruct 
			&& (ScriptStruct == TBaseStructure<T>::Get()
				|| ScriptStruct->IsChildOf(TBaseStructure<T>::Get())))
		{
			return ((T*)StructMemory);
		}
		return nullptr;
	}

	/** Returns const reference to the struct, this assumes that all data is valid. */
	template<typename T>
	const T& GetStructRef(const UScriptStruct* ScriptStruct, const uint8* StructMemory)
	{
		return GetStructRef<T>(ScriptStruct, const_cast<uint8*>(StructMemory));
	}

	/** Returns const pointer to the struct, or nullptr if cast is not valid. */
	template<typename T>
	const T* GetStructPtr(const UScriptStruct* ScriptStruct, const uint8* StructMemory)
	{
		return GetStructPtr<T>(ScriptStruct, const_cast<uint8*>(StructMemory));
	}
}
