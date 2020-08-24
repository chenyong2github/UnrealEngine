// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"

#include "MetasoundFacade.h"

namespace Metasound
{
	/** FADSRNode
	 *
	 *  Creates an Attack, Decay Sustain, Release audio processer node. 
	 */
	class METASOUNDSTANDARDNODES_API FADSRNode : public FNodeFacade
	{
		public:

			/** FADSR node constructor.
			 *
			 * @param InName - Name of this node.
			 * @param InDefaultAttackMs - Default attack in milliseconds.
			 * @param InDefaultDecayMs - Default decay in milliseconds.
			 * @param InDefaultSustainMs - Default sustain in milliseconds.
			 * @param InDefaultReleaseMs - Default release in milliseconds.
			 */
			FADSRNode(const FString& InName, float InDefaultAttackMs, float InDefaultDecayMs, float InDefaultSustainMs, float InDefaultReleaseMs);

			/**
			 * Constructor used by the Metasound Frontend.
			 */
			FADSRNode(const FNodeInitData& InitData);

			virtual ~FADSRNode();

			/** Return default attack time in milliseconds */
			float GetDefaultAttackMs() const;
			/** Return default attack time in milliseconds */
			float GetDefaultDecayMs() const;
			/** Return default attack time in milliseconds */
			float GetDefaultSustainMs() const;
			/** Return default attack time in milliseconds */
			float GetDefaultReleaseMs() const;

		private:
			float DefaultAttackMs;
			float DefaultDecayMs;
			float DefaultSustainMs;
			float DefaultReleaseMs;
	};
} // namespace Metasound
