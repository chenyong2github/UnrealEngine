// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
//#include "AnimNextRuntimeTest.generated.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DecoratorBase/DecoratorUID.h"
#include "DecoratorBase/DecoratorTemplate.h"
#include "DecoratorBase/NodeDescription.h"
#include "DecoratorBase/NodeHandle.h"
#include "DecoratorBase/NodeTemplate.h"

// TODO: Add USTRUCT and other similar types here, if required for tests

namespace UE::AnimNext
{
	const FNodeTemplate* BuildNodeTemplate(const TArray<FDecoratorUID>& NodeTemplateDecoratorList, TArray<uint8>& NodeTemplateBuffer);

	// Adds a node template to a shared data graph and returns a handle to it
	FNodeHandle AppendNodeToGraph(const FNodeTemplate& NodeTemplate, uint16& NodeUID, TArray<uint8>& GraphSharedDataBuffer);

	template<class SharedDataType>
	void InitNodeDecorator(const FNodeTemplate& NodeTemplate, const FNodeHandle NodeHandle, TArray<uint8>& GraphSharedDataBuffer, uint32 DecoratorIndex, const SharedDataType& SharedData)
	{
		FNodeDescription& NodeDesc = *reinterpret_cast<FNodeDescription*>(&GraphSharedDataBuffer[NodeHandle.GetSharedOffset()]);

		const FDecoratorTemplate& DecoratorTemplate = NodeTemplate.GetDecorators()[DecoratorIndex];
		*static_cast<SharedDataType*>(DecoratorTemplate.GetDecoratorDescription(NodeDesc)) = SharedData;
	}
}

#endif
