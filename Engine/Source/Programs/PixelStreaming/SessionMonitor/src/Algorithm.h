// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

//
//Less verbose way to remove items from a container
//
template <typename C, typename T>
auto Erase(C& c, const T& v) -> decltype(c.begin())
{
	return c.erase(std::remove(c.begin(), c.end(), v), c.end());
}
