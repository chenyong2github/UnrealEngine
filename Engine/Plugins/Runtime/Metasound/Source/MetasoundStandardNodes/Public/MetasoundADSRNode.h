// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "MetasoundFacade.h"

namespace Metasound
{
	/** FADSRNode
	 *
	 *  Creates an Attack, Decay Sustain, Release audio processor node. 
	 */
	class METASOUNDSTANDARDNODES_API FADSRNode : public FNodeFacade
	{
		public:

			/** FADSR node constructor.
			 *
			 * @param InInstanceName - Name of this node.
			 * @param InInstanceID - ID of this node.
			 * @param InDefaultAttackMs - Default attack duration in milliseconds.
			 * @param InDefaultDecayMs - Default decay duration in milliseconds.
			 * @param InDefaultSustainLevel - Default sustain level [0.0f, 1.0f].
			 * @param InDefaultReleaseMs - Default release duration in milliseconds.
			 */
			FADSRNode(const FString& InInstanceName, const FGuid& InInstanceID, float InDefaultAttackMs, float InDefaultDecayMs, float InDefaultSustainLevel, float InDefaultReleaseMs);

			/**
			 * Constructor used by the Metasound Frontend.
			 */
			FADSRNode(const FNodeInitData& InitData);

			virtual ~FADSRNode();

			/** Return default sustain gain [0.0f, 1.0f] */
			float GetDefaultSustainLevel() const;

			/** Return default attack time in milliseconds */
			float GetDefaultAttackMs() const;

			/** Return default decay time in milliseconds */
			float GetDefaultDecayMs() const;

			/** Return default release time in milliseconds */
			float GetDefaultReleaseMs() const;

		private:
			float DefaultSustainLevel;

			float DefaultAttackMs;
			float DefaultDecayMs;
			float DefaultReleaseMs;
	};
} // namespace Metasound
