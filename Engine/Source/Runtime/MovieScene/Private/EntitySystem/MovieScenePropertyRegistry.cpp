// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieScenePropertyRegistry.h"


namespace UE
{
namespace MovieScene
{


FCompositePropertyTypeID FPropertyRegistry::DefineCompositeProperty(FComponentTypeID PropertyType, FComponentTypeID InitialValueType, FComponentTypeID PreAnimatedValueType)
{
	const int32 CompositeOffset = CompositeDefinitions.Num();
	checkf(CompositeOffset <= MAX_uint16, TEXT("Maximum number of composite definitions reached"));

	FPropertyDefinition NewDefinition = {
		nullptr,
		0,
		static_cast<uint16>(CompositeOffset),
		0,
		PropertyType,
		PreAnimatedValueType,
		InitialValueType,
	};

	FCompositePropertyTypeID ID = { Properties.Add(MoveTemp(NewDefinition)) };
	return ID;
}



} // namespace MovieScene
} // namespace UE