// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystemTypes.h"

#if UE_MOVIESCENE_ENTITY_DEBUG

namespace UE
{
namespace MovieScene
{

/** Defines a static type identifier for the natvis visualizer */
enum class EComponentDebugType
{
	Unknown,
	Float,
	Uint16,
	Object,
	Property,
	InstanceHandle,
	EntityID,
};

/**
 * Debug information for a component type
 */
struct FComponentTypeDebugInfo
{
	FString DebugName;
	const TCHAR* DebugTypeName = nullptr;
	EComponentDebugType Type = EComponentDebugType::Unknown;
};

template<typename T> struct TComponentDebugType                      { static const EComponentDebugType Type = EComponentDebugType::Unknown;  };
template<>           struct TComponentDebugType<float>               { static const EComponentDebugType Type = EComponentDebugType::Float;    };
template<>           struct TComponentDebugType<uint16>              { static const EComponentDebugType Type = EComponentDebugType::Uint16;   };
template<>           struct TComponentDebugType<UObject*>            { static const EComponentDebugType Type = EComponentDebugType::Object;   };
template<>           struct TComponentDebugType<FMovieSceneEntityID> { static const EComponentDebugType Type = EComponentDebugType::EntityID; };


} // namespace MovieScene
} // namespace UE


#endif // UE_MOVIESCENE_ENTITY_DEBUG