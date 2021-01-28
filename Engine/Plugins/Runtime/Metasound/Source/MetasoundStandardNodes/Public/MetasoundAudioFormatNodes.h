// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundFacade.h"

namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FMonoAudioFormatNode : public FNodeFacade
	{
		public:
			FMonoAudioFormatNode(const FString& InInstanceName, const FGuid& InInstanceID);
			FMonoAudioFormatNode(const FNodeInitData& InInitData);
	};

	class METASOUNDSTANDARDNODES_API FStereoAudioFormatNode : public FNodeFacade
	{
		public:
			FStereoAudioFormatNode(const FString& InInstanceName, const FGuid& InInstnaceID);
			FStereoAudioFormatNode(const FNodeInitData& InInitData);
	};
}
