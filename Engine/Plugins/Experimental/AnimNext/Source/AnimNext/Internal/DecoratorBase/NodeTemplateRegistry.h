// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DecoratorBase/NodeTemplateRegistryHandle.h"

namespace UE::AnimNext
{
	struct FNodeTemplate;

	/**
	 * FNodeTemplateRegistry
	 * 
	 * A global registry of all existing node templates that can be shared between animation graph instances.
	 * 
	 * @see FNodeTemplate
	 */
	struct ANIMNEXT_API FNodeTemplateRegistry final
	{
		// Access the global registry
		static FNodeTemplateRegistry& Get();

		// Finds or adds the specified node template and returns its offset
		FNodeTemplateRegistryHandle FindOrAdd(const FNodeTemplate* NodeTemplate);

		// Removes the specified node template from the registry
		void Unregister(const FNodeTemplate* NodeTemplate);

		// Finds and returns a node template based on its handle or nullptr if the handle is invalid
		const FNodeTemplate* Find(FNodeTemplateRegistryHandle TemplateHandle) const;

		// Returns the number of registered node templates
		uint32 GetNum() const;

	private:
		FNodeTemplateRegistry() = default;

		// Module lifetime functions
		static void Init();
		static void Destroy();

		// We only ever append node templates to this contiguous buffers
		// This is an optimization, we share these and by being contiguous
		// we improve cache locality and cache line density
		// Because node templates are trivially copyable, we could remove
		// from this buffer when there are holes and coalesce everything.
		// To do so, we would have to fix-up any outstanding handles within
		// the shared data of loaded anim graphs.
		TArray<uint8>								TemplateBuffer;
		TMap<uint32, FNodeTemplateRegistryHandle>	TemplateUIDToHandleMap;

		friend class FModule;
	};
}
