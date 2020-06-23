// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Code common between all vehicle systems
 */
template <typename T>
class TVehicleSystem
{
public:

	TVehicleSystem() : SetupPtr(nullptr)
	{
	}

	TVehicleSystem(const T* SetupIn) : SetupPtr(SetupIn)
	{
	}

	T& AccessSetup()
	{
		check(SetupPtr != nullptr);
		return (*SetupPtr);
	}

	const T& Setup() const
	{
		check(SetupPtr != nullptr);
		return (*SetupPtr);
	}

	T* SetupPtr;
};