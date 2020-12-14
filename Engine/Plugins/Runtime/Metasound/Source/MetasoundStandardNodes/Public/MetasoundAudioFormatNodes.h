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
			FMonoAudioFormatNode(const FString& InInstanceName);
			FMonoAudioFormatNode(const FNodeInitData& InInitData);
	};

	class METASOUNDSTANDARDNODES_API FStereoAudioFormatNode : public FNodeFacade
	{
		public:
			FStereoAudioFormatNode(const FString& InInstanceName);
			FStereoAudioFormatNode(const FNodeInitData& InInitData);
	};
}
