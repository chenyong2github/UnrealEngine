// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetasoundAudioBuffer.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNode.h"
#include "MetasoundOperatorInterface.h"

namespace Metasound
{
	class METASOUNDSTANDARDNODES_API FOscNode : public FNodeFacade
	{
	public:
		FOscNode(const FNodeInitData& InInitData);
	};
}
