// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/TypeHash.h"

namespace UE::ConcertSyncCore
{
	class FActivityDependecyGraph;
	/** Enforces type-safe usage of IDs by avoiding implicit conversions. */
	struct CONCERTSYNCCORE_API FActivityNodeID
	{
		size_t ID;

		FActivityNodeID() = default;
		
		template<typename T>
		explicit FActivityNodeID(T&& ID)
			: ID(Forward<T>(ID))
		{}

		FActivityNodeID(const FActivityNodeID& Other)
			: ID(Other.ID)
		{}

		bool HasAnyDependency(const FActivityDependecyGraph&) const;

		/** Conversion back to to size_t is not a common programmer mistake */
		explicit operator size_t() const { return ID; }

		friend bool operator==(const FActivityNodeID& Left, const FActivityNodeID& Right)
		{
			return Left.ID == Right.ID;
		}

		friend bool operator!=(const FActivityNodeID& Left, const FActivityNodeID& Right)
		{
			return !(Left == Right);
		}

		FORCEINLINE friend uint32 GetTypeHash(const FActivityNodeID& NodeID)
		{
			return ::GetTypeHash(NodeID.ID);
		}
	};
}