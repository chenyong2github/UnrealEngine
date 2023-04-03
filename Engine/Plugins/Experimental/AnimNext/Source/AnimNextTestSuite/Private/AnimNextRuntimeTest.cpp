// Copyright Epic Games, Inc. All Rights Reserved.


#include "AnimNextRuntimeTest.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DecoratorBase/Decorator.h"
#include "DecoratorBase/DecoratorRegistry.h"
#include "DecoratorBase/NodeInstance.h"
#include "DecoratorBase/NodeTemplateRegistry.h"

namespace UE::AnimNext
{
	uint32 GetNodeTemplateUID(const TArray<FDecoratorUID>& NodeTemplateDecoratorList)
	{
		uint32 NodeTemplateUID = 0;
		for (const FDecoratorUID DecoratorUID : NodeTemplateDecoratorList)
		{
			NodeTemplateUID = HashCombineFast(NodeTemplateUID, DecoratorUID.GetUID());
		}

		return NodeTemplateUID;
	}

	uint32 GetNodeInstanceSize(const TArray<FDecoratorUID>& NodeTemplateDecoratorList)
	{
		const FDecoratorRegistry& Registry = FDecoratorRegistry::Get();

		uint32 NodeInstanceSize = sizeof(FNodeInstance);
		for (const FDecoratorUID DecoratorUID : NodeTemplateDecoratorList)
		{
			const FDecorator* Decorator = Registry.Find(DecoratorUID);

			const FDecoratorMemoryLayout MemoryLayout = Decorator->GetDecoratorMemoryDescription();
			NodeInstanceSize = Align(NodeInstanceSize, MemoryLayout.InstanceDataAlignment);
			NodeInstanceSize += MemoryLayout.InstanceDataSize;
		}

		return NodeInstanceSize;
	}

	void AppendTemplateDecorator(const TArray<FDecoratorUID>& NodeTemplateDecoratorList, int32 DecoratorIndex, TArray<uint8>& NodeTemplateBuffer, uint32& SharedDataOffset, uint32& InstanceDataOffset)
	{
		const FDecoratorRegistry& Registry = FDecoratorRegistry::Get();

		const FDecoratorUID DecoratorUID = NodeTemplateDecoratorList[DecoratorIndex];
		const FDecoratorRegistryHandle DecoratorHandle = Registry.FindHandle(DecoratorUID);
		const FDecorator* Decorator = Registry.Find(DecoratorHandle);
		const EDecoratorMode DecoratorMode = Decorator->GetMode();

		uint32 AdditiveIndexOrNumAdditive;
		if (DecoratorMode == EDecoratorMode::Base)
		{
			// Find out how many additive decorators we have
			AdditiveIndexOrNumAdditive = 0;
			for (int32 Index = DecoratorIndex + 1; Index < NodeTemplateDecoratorList.Num(); ++Index)	// Skip ourself
			{
				const FDecorator* ChildDecorator = Registry.Find(NodeTemplateDecoratorList[Index]);
				if (ChildDecorator->GetMode() == EDecoratorMode::Base)
				{
					break;	// Found another base decorator, we are done
				}

				// We are additive
				AdditiveIndexOrNumAdditive++;
			}
		}
		else
		{
			// Find out our additive index
			AdditiveIndexOrNumAdditive = 1;	// Skip ourself
			for (int32 Index = DecoratorIndex - 1; Index >= 0; --Index)
			{
				const FDecorator* ParentDecorator = Registry.Find(NodeTemplateDecoratorList[Index]);
				if (ParentDecorator->GetMode() == EDecoratorMode::Base)
				{
					break;	// Found our base decorator, we are done
				}

				// We are additive
				AdditiveIndexOrNumAdditive++;
			}
		}

		// Append and update our offsets
		const int32 BufferIndex = NodeTemplateBuffer.AddUninitialized(sizeof(FDecoratorTemplate));
		new(&NodeTemplateBuffer[BufferIndex]) FDecoratorTemplate(DecoratorUID, DecoratorHandle, DecoratorMode, AdditiveIndexOrNumAdditive, SharedDataOffset, InstanceDataOffset);

		const FDecoratorMemoryLayout MemoryLayout = Decorator->GetDecoratorMemoryDescription();
		SharedDataOffset = Align(SharedDataOffset, MemoryLayout.SharedDataAlignment);
		SharedDataOffset += MemoryLayout.SharedDataSize;
		InstanceDataOffset = Align(InstanceDataOffset, MemoryLayout.InstanceDataAlignment);
		InstanceDataOffset += MemoryLayout.InstanceDataSize;
	}

	const FNodeTemplate* BuildNodeTemplate(const TArray<FDecoratorUID>& NodeTemplateDecoratorList, TArray<uint8>& NodeTemplateBuffer)
	{
		const int32 BufferIndex = NodeTemplateBuffer.AddUninitialized(sizeof(FNodeTemplate));
		new(&NodeTemplateBuffer[BufferIndex]) FNodeTemplate(GetNodeTemplateUID(NodeTemplateDecoratorList), GetNodeInstanceSize(NodeTemplateDecoratorList), NodeTemplateDecoratorList.Num());

		uint32 SharedDataOffset = sizeof(FNodeDescription);
		uint32 InstanceDataOffset = sizeof(FNodeInstance);

		for (int32 DecoratorIndex = 0; DecoratorIndex < NodeTemplateDecoratorList.Num(); ++DecoratorIndex)
		{
			AppendTemplateDecorator(NodeTemplateDecoratorList, DecoratorIndex, NodeTemplateBuffer, SharedDataOffset, InstanceDataOffset);
		}

		return reinterpret_cast<const FNodeTemplate*>(&NodeTemplateBuffer[0]);
	}

	uint32 GetNodeSharedSize(const FNodeTemplate& NodeTemplate)
	{
		const FDecoratorRegistry& Registry = FDecoratorRegistry::Get();

		const uint32 NumDecorators = NodeTemplate.GetNumDecorators();
		const FDecoratorTemplate* DecoratorTemplates = NodeTemplate.GetDecorators();

		uint32 NodeSharedSize = sizeof(FNodeDescription);
		for (uint32_t DecoratorIndex = 0; DecoratorIndex < NumDecorators; ++DecoratorIndex)
		{
			const FDecorator* Decorator = Registry.Find(DecoratorTemplates[DecoratorIndex].GetRegistryHandle());

			const FDecoratorMemoryLayout MemoryLayout = Decorator->GetDecoratorMemoryDescription();
			NodeSharedSize = Align(NodeSharedSize, MemoryLayout.SharedDataAlignment);
			NodeSharedSize += MemoryLayout.SharedDataSize;
		}

		return NodeSharedSize;
	}

	// Adds a node template to a shared data graph and returns a handle to it
	FNodeHandle AppendNodeToGraph(const FNodeTemplate& NodeTemplate, uint16& NodeUID, TArray<uint8>& GraphSharedDataBuffer)
	{
		FNodeTemplateRegistry& Registry = FNodeTemplateRegistry::Get();

		const uint32 NodeSharedDataSize = GetNodeSharedSize(NodeTemplate);
		const uint32 BufferIndex = GraphSharedDataBuffer.AddZeroed(NodeSharedDataSize);
		new(&GraphSharedDataBuffer[BufferIndex]) FNodeDescription(NodeUID++, Registry.FindOrAdd(&NodeTemplate));

		return FNodeHandle(BufferIndex);
	}
}
#endif
