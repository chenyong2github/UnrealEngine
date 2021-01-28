// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "MetasoundFacade.h"

namespace Metasound
{
	/** FTriggerAccumulatorNode
	 *
	 *  Creates a Trigger Accumulator Node, that accumulate triggers until hitting a count, then Trigger and reset.
	 */
	class METASOUNDSTANDARDNODES_API FTriggerAccumulatorNode : public FNodeFacade
	{
	public:

		/** Trigger Accumulator node constructor.
		 *
		 * @param InName - Name of this node.
		 * @param InTriggerAtCount - The count at which after accumulated triggers we trigger.
		 */
		FTriggerAccumulatorNode(const FString& InName, const FGuid& InInstanceID, int32 InDefaultTriggerAtCount);

		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FTriggerAccumulatorNode(const FNodeInitData& InitData);

		virtual ~FTriggerAccumulatorNode() = default;

		/** 
		 * Get Default Trigger At Value.
		 */
		int32 GetDefaultTriggerAt() const { return DefaultTriggerAtCount; }

	private:
		int32 DefaultTriggerAtCount = 1;
	};
} // namespace Metasound
