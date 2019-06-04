// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Describes a serializable type (with a Serialize member function) taking the given argument types.
 */
template <typename SerializableType>
struct CSerializable {
	template <typename... ArgTypes>
	auto Requires(SerializableType& Obj, ArgTypes&&... Args) -> decltype(
		Obj.Serialize(Forward<ArgTypes>(Args)...)
	);
};
