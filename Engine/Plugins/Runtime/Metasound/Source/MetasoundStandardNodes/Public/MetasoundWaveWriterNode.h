// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFacade.h"
#include "MetasoundVertex.h"


namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FWaveWriterNode : public FNodeFacade
	{
		public:
			FWaveWriterNode(const FVertexName& InName, const FGuid& InInstanceID);
			FWaveWriterNode(const FNodeInitData& InInitData);
	};
} // namespace Metasound

