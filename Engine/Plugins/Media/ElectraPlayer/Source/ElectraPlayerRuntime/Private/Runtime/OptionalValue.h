// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"


/**
 * "Optional" value. Includes a boolean to indicate whether or not it is set.
**/
template<typename T>
struct TMediaOptionalValue
{
	TMediaOptionalValue() : ValueSet(false) {}
	TMediaOptionalValue(const T& v) : OptionalValue(v), ValueSet(true) {}
	TMediaOptionalValue(const TMediaOptionalValue& rhs) : OptionalValue(rhs.OptionalValue), ValueSet(rhs.ValueSet) {}
	void Set(const T& v) { OptionalValue = v; ValueSet = true; }
	void SetIfNot(const T& v) { if (!IsSet()) Set(v); }
	bool IsSet(void) const { return(ValueSet); }
	const T& Value(void) const { return(OptionalValue); }
	T GetWithDefault(const T& Default) const { return ValueSet ? OptionalValue : Default; }
	void Reset(void) { ValueSet = false; }
private:
	// Hide assignment from public view
	TMediaOptionalValue& operator = (const T& v) { Set(v); return(*this); }
	T		OptionalValue;
	bool	ValueSet;
};
