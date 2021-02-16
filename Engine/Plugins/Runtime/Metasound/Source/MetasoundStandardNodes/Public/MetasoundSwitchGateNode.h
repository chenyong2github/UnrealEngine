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
	// Pending array support...
	//typedef TArray<FAudioBuffer> FAudioBufferArray;
	//DECLARE_METASOUND_DATA_REFERENCE_TYPES(FAudioBufferArray, METASOUNDSTANDARDNODES_API, FAudioBufferArrayTypeInfo, FAudioBufferArrayReadRef, FAudioBufferArrayWriteRef)

	/** FGateNode
	 *
	 *	The switch node selects a single audio buffer from an array of options.
	 *	The selection can change at any time during execution, however there is no
	 *	blending between nodes.
	*/
	class METASOUNDSTANDARDNODES_API FSwitchNode : public FNodeFacade
	{
	public:
		FSwitchNode(const FString& InName, const FGuid& InInstanceID);
		FSwitchNode(const FNodeInitData& InInitData);
	};

	/** FGateNode
	 *
	 *  The gate node routes an audio buffer to a selected output buffer.
	 *	The output buffer can be changed during execution.
	 */
	class METASOUNDSTANDARDNODES_API FGateNode : public FNodeFacade
	{
	public:
		FGateNode(const FString& InName, const FGuid& InInstanceID);
		FGateNode(const FNodeInitData& InInitData);
	};
}
