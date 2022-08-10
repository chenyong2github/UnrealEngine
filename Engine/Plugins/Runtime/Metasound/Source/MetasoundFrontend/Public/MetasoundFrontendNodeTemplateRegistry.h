// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "Templates/SharedPointer.h"
#include "UObject/NoExportTypes.h"

namespace Metasound
{
	namespace Frontend
	{
		class METASOUNDFRONTEND_API INodeTemplate
		{
		public:
			virtual ~INodeTemplate() = default;

			virtual bool BuildTemplate(FMetasoundFrontendDocument& InOutDocument, FMetasoundFrontendGraph& InOutGraph, FMetasoundFrontendNode& InOutNode) const = 0;
			virtual const FMetasoundFrontendClass& GetFrontendClass() const = 0;
			virtual const FMetasoundFrontendVersion& GetVersion() const = 0;
			virtual bool IsValidNodeInterface(const FMetasoundFrontendNodeInterface& InNodeInterface) const = 0;
			virtual bool HasRequiredConnections(FConstNodeHandle InNodeHandle) const = 0;
		};

		class METASOUNDFRONTEND_API INodeTemplateRegistry
		{
		public:
			static INodeTemplateRegistry& Get();

			virtual ~INodeTemplateRegistry() = default;

			// Find a template with the given key. Returns null if entry not found with given key.
			virtual const INodeTemplate* FindTemplate(const FNodeRegistryKey& InKey) const = 0;
		};

		// Register & Unregister are not publicly accessible implementation as the API
		// is in beta and currently, only to be used by reroute nodes.
		void RegisterNodeTemplate(TUniquePtr<INodeTemplate>&& InTemplate);
		void UnregisterNodeTemplate(const FMetasoundFrontendVersion& InNodeTemplateVersion);
	} // namespace Frontend
} // namespace Metasound
