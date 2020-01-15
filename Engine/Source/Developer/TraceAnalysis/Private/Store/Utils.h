// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
template <typename T>
inline uint32 QuickStoreHash(const T* String)
{
	uint32 Value = 5381;
	for (; *String; ++String)
	{
		Value = ((Value << 5) + Value) + *String;
	}
	return Value;
}


} // namespace Trace
