// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/IsArithmetic.h"
#include "Templates/UnrealTypeTraits.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"

/** Concept to check if T has NumericLimits */
struct CHasNumericLimits {
	template <typename T>
	auto Requires() -> decltype(TNumericLimits<T>::NumericType);
};

/** Various RemoteControl type traits */
template <typename ValueType>
struct TRemoteControlTypeTraits;

/** RemoteControlTypeTraits for const types */
template <typename ValueType>
struct TRemoteControlTypeTraits<const ValueType> 
    : public TRemoteControlTypeTraits<ValueType>
{ };

/** RemoteControlTypeTraits for volatile types */
template <typename ValueType>
struct TRemoteControlTypeTraits<volatile ValueType> 
    : public TRemoteControlTypeTraits<ValueType>
{ };

/** RemoteControlTypeTraits for const volatile types */
template <typename ValueType>
struct TRemoteControlTypeTraits<const volatile ValueType> 
    : public TRemoteControlTypeTraits<ValueType>
{ };

/** RemoteControlTypeTraits for numeric types */
template <typename ValueType, typename TEnableIf<TModels<CHasNumericLimits, ValueType>::Value>::Type* = nullptr>
struct TRemoteControlTypeTraits<ValueType>
{
	typedef ValueType Type;
	
	static constexpr Type DefaultMin()
	{
		return TNumericLimits<Type>::Min();
	}

	static constexpr Type DefaultMax()
	{
		return TNumericLimits<Type>::Max();
	}
};

/** RemoteControlTypeTraits for FVector */
template <>
struct TRemoteControlTypeTraits<FVector>
{
	typedef FVector Type;
	
	static constexpr Type DefaultMin()
	{
		return Type::ZeroVector;
	}

	static constexpr Type DefaultMax()
	{
		return Type::OneVector;
	}
};

/** RemoteControlTypeTraits for FVector2D */
template <>
struct TRemoteControlTypeTraits<FVector2D>
{
	typedef FVector2D Type;
	
	static constexpr Type DefaultMin()
	{
		return Type::ZeroVector;
	}

	static constexpr Type DefaultMax()
	{
		return Type::UnitVector;
	}
};

/** RemoteControlTypeTraits for FVector4 */
template <>
struct TRemoteControlTypeTraits<FVector4>
{
	typedef FVector4 Type;
	
	static constexpr Type DefaultMin()
	{
		return Type(EForceInit::ForceInitToZero);
	}

	static constexpr Type DefaultMax()
	{
		return Type(1.0f, 1.0f, 1.0f, 1.0f);
	}
};

/** RemoteControlTypeTraits for FRotator */
template <>
struct TRemoteControlTypeTraits<FRotator>
{
	typedef FRotator Type;
	
	static constexpr Type DefaultMin()
	{
		return FRotator::ZeroRotator;
	}

	static constexpr Type DefaultMax()
	{
		return FRotator(90.0f, 90.0f, 90.0f);
	}
};

/** RemoteControlTypeTraits for FQuat */
template <>
struct TRemoteControlTypeTraits<FQuat>
{
	typedef FQuat Type;
	
	static constexpr Type DefaultMin()
	{
		return FQuat(0.0f, 0.0f, 0.0f, 0.0f);
	}

	static constexpr Type DefaultMax()
	{
		return Type::Identity;
	}
};

/** RemoteControlTypeTraits for FTransform */
template <>
struct TRemoteControlTypeTraits<FTransform>
{
	typedef FTransform Type;
	
	static constexpr Type DefaultMin()
	{
		return FTransform(
			TRemoteControlTypeTraits<FRotator>::DefaultMin(),
			TRemoteControlTypeTraits<FVector>::DefaultMin(),
			TRemoteControlTypeTraits<FVector>::DefaultMax()); // scale is Max cause it shouldn't be zero		
	}

	static constexpr Type DefaultMax()
	{
		return FTransform(
	TRemoteControlTypeTraits<FRotator>::DefaultMax(),
	TRemoteControlTypeTraits<FVector>::DefaultMax(),
	TRemoteControlTypeTraits<FVector>::DefaultMax()); // scale is Max cause it shouldn't be zero	
	}
};

/** RemoteControlTypeTraits for FIntPoint */
template <>
struct TRemoteControlTypeTraits<FIntPoint>
{
	typedef FIntPoint Type;
	
	static constexpr Type DefaultMin()
	{
		return Type::ZeroValue;
	}

	static constexpr Type DefaultMax()
	{
		return Type(1, 1);
	}
};

/** RemoteControlTypeTraits for FIntVector */
template <>
struct TRemoteControlTypeTraits<FIntVector>
{
	typedef FIntVector Type;
	
	static constexpr Type DefaultMin()
	{
		return Type::ZeroValue;
	}

	static constexpr Type DefaultMax()
	{
		return Type(1, 1, 1);
	}
};

/** RemoteControlTypeTraits for FBox */
template <>
struct TRemoteControlTypeTraits<FBox>
{
	typedef FBox Type;
	
	static constexpr Type DefaultMin()
	{
		return Type(TRemoteControlTypeTraits<FVector>::DefaultMin(), TRemoteControlTypeTraits<FVector>::DefaultMax());
	}

	static constexpr Type DefaultMax()
	{
		return Type(TRemoteControlTypeTraits<FVector>::DefaultMax(), TRemoteControlTypeTraits<FVector>::DefaultMax() * 2.0f);
	}
};

/** RemoteControlTypeTraits for FBox2D */
template <>
struct TRemoteControlTypeTraits<FBox2D>
{
	typedef FBox2D Type;
	
	static constexpr Type DefaultMin()
	{
		return Type(TRemoteControlTypeTraits<FVector2D>::DefaultMin(), TRemoteControlTypeTraits<FVector2D>::DefaultMax());
	}

	static constexpr Type DefaultMax()
	{
		return Type(TRemoteControlTypeTraits<FVector2D>::DefaultMax(), TRemoteControlTypeTraits<FVector2D>::DefaultMax() * 2.0f);
	}
};

/** RemoteControlTypeTraits for FBoxSphereBounds */
template <>
struct TRemoteControlTypeTraits<FBoxSphereBounds>
{
	typedef FBoxSphereBounds Type;
	
	static constexpr Type DefaultMin()
	{
		return Type(TRemoteControlTypeTraits<FBox>::DefaultMin());
	}

	static constexpr Type DefaultMax()
	{
		return Type(TRemoteControlTypeTraits<FBox>::DefaultMax());
	}
};

/** RemoteControlTypeTraits for FColor */
template <>
struct TRemoteControlTypeTraits<FColor>
{
	typedef FColor Type;
	
	static constexpr Type DefaultMin()
	{
		return Type(
			TRemoteControlTypeTraits<uint8>::DefaultMin(),
			TRemoteControlTypeTraits<uint8>::DefaultMin(),
			TRemoteControlTypeTraits<uint8>::DefaultMin());
	}

	static constexpr Type DefaultMax()
	{
		return Type(
			 TRemoteControlTypeTraits<uint8>::DefaultMax(),
			TRemoteControlTypeTraits<uint8>::DefaultMax(),
			TRemoteControlTypeTraits<uint8>::DefaultMax());
	}
};

/** RemoteControlTypeTraits for FLinearColor */
template <>
struct TRemoteControlTypeTraits<FLinearColor>
{
	typedef FLinearColor Type;
	
	static constexpr Type DefaultMin()
	{
		return Type(
			TRemoteControlTypeTraits<float>::DefaultMin(),
			TRemoteControlTypeTraits<float>::DefaultMin(),
			TRemoteControlTypeTraits<float>::DefaultMin());
	}

	static constexpr Type DefaultMax()
	{
		return Type(
			TRemoteControlTypeTraits<float>::DefaultMax(),
			TRemoteControlTypeTraits<float>::DefaultMax(),
			TRemoteControlTypeTraits<float>::DefaultMax());
	}
};
