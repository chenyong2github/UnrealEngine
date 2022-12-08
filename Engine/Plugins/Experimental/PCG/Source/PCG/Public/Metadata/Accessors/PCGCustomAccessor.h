// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPCGAttributeAccessorTpl.h"

#include "PCGPoint.h"

/**
* Templated accessor class for custom point properties. Need a getter and a setter, defined in the
* FPCGPoint class.
* Key supported: Points
*/
template <typename T>
class FPCGCustomPointAccessor : public IPCGAttributeAccessorT<FPCGCustomPointAccessor<T>>
{
public:
	using Type = T;
	using Super = IPCGAttributeAccessorT<FPCGCustomPointAccessor<T>>;

	FPCGCustomPointAccessor(const FPCGPoint::PointCustomPropertyGetter& InGetter, const FPCGPoint::PointCustomPropertySetter& InSetter)
		: Super(/*bInReadOnly=*/ false)
		, Getter(InGetter)
		, Setter(InSetter)
	{}

	bool GetRangeImpl(TArrayView<T> OutValues, int32 Index, const IPCGAttributeAccessorKeys& Keys) const
	{
		TArray<const FPCGPoint*> PointKeys;
		PointKeys.SetNum(OutValues.Num());
		TArrayView<const FPCGPoint*> PointKeysView(PointKeys);
		if (!Keys.GetKeys(Index, PointKeysView))
		{
			return false;
		}

		for (int32 i = 0; i < OutValues.Num(); ++i)
		{
			Getter(*PointKeys[i], &OutValues[i]);
		}

		return true;
	}

	bool SetRangeImpl(TArrayView<const T> InValues, int32 Index, IPCGAttributeAccessorKeys& Keys, EPCGAttributeAccessorFlags Flags)
	{
		TArray<FPCGPoint*> PointKeys;
		PointKeys.SetNum(InValues.Num());
		TArrayView<FPCGPoint*> PointKeysView(PointKeys);
		if (!Keys.GetKeys(Index, PointKeysView))
		{
			return false;
		}

		for (int32 i = 0; i < InValues.Num(); ++i)
		{
			Setter(*PointKeys[i], &InValues[i]);
		}

		return true;
	}

private:
	FPCGPoint::PointCustomPropertyGetter Getter;
	FPCGPoint::PointCustomPropertySetter Setter;
};

/**
* Very simple accessor that returns a constant value. Read only
* Key supported: All
*/
template <typename T>
class FPCGConstantValueAccessor : public IPCGAttributeAccessorT<FPCGConstantValueAccessor<T>>
{
public:
	using Type = T;
	using Super = IPCGAttributeAccessorT<FPCGConstantValueAccessor<T>>;

	FPCGConstantValueAccessor(const T& InValue)
		: Super(/*bInReadOnly=*/ true)
		, Value(InValue)
	{}

	bool GetRangeImpl(TArrayView<T> OutValues, int32, const IPCGAttributeAccessorKeys&) const
	{
		for (int32 i = 0; i < OutValues.Num(); ++i)
		{
			OutValues[i] = Value;
		}

		return true;
	}

	bool SetRangeImpl(TArrayView<const T>, int32, IPCGAttributeAccessorKeys&, EPCGAttributeAccessorFlags)
	{
		return false;
	}

private:
	T Value;
};