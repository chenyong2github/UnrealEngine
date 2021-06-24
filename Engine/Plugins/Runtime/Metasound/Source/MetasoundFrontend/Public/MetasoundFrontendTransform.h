// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"


namespace Metasound
{
	namespace Frontend
	{
		/** Interface for transforms applied to documents. */
		class IDocumentTransform
		{
		public:
			virtual ~IDocumentTransform() = default;
			/** Return true if InDocument was modified, false otherwise. */
			virtual bool Transform(FDocumentHandle InDocument) const = 0;
		};

		/** Interface for transforms applied to a graph. */
		class IGraphTransform
		{
		public:
			virtual ~IGraphTransform() = default;
			/** Return true if InGraph was modified, false otherwise. */
			virtual bool Transform(FGraphHandle InGraph) const = 0;
		};

		/** Adds or swaps graph inputs and outputs to match the archetype. */
		class METASOUNDFRONTEND_API FSwapGraphArchetype : public IGraphTransform
		{
		public:
			FSwapGraphArchetype(const FMetasoundFrontendArchetype& InFromArchetype, const FMetasoundFrontendArchetype& InToArchetype);

			bool Transform(FGraphHandle InGraph) const override;

		private:
			using FVertexPair = TTuple<FMetasoundFrontendClassVertex, FMetasoundFrontendClassVertex>;
			TArray<FVertexPair> PairedInputs;
			TArray<FVertexPair> PairedOutputs;
			TArray<FMetasoundFrontendClassVertex> InputsToAdd;
			TArray<FMetasoundFrontendClassVertex> InputsToRemove;
			TArray<FMetasoundFrontendClassVertex> OutputsToAdd;
			TArray<FMetasoundFrontendClassVertex> OutputsToRemove;

		};

		/** Adds or swaps root graph inputs and outputs to match the document's archetype. */
		class METASOUNDFRONTEND_API FMatchRootGraphToArchetype : public IDocumentTransform
		{
		public:
			FMatchRootGraphToArchetype(const FMetasoundFrontendVersion& InArchetypeVersion)
				: ArchetypeVersion(InArchetypeVersion)
			{
			}

			bool Transform(FDocumentHandle InDocument) const override;

		private:
			void GetUpgradePathForDocument(const FMetasoundFrontendVersion& InCurrentVersion, const FMetasoundFrontendVersion& InTargetVersion, TArray<const IArchetypeRegistryEntry*>& OutUpgradePath) const;
			bool UpgradeDocumentArchetype(const TArray<const IArchetypeRegistryEntry*>& InUpgradePath, FDocumentHandle InDocument) const;
			bool ConformDocumentToArchetype(const FMetasoundFrontendArchetype& InTargetArchetype, FDocumentHandle InDocument) const;

			FMetasoundFrontendVersion ArchetypeVersion;
		};

		/** Base class for versioning a document. */
		class METASOUNDFRONTEND_API FVersionDocument : public IDocumentTransform
		{
			const FName Name;
			const FString& Path;

		public:
			static FMetasoundFrontendVersionNumber GetMaxVersion()
			{
				return FMetasoundFrontendVersionNumber { 1, 4 };
			}

			FVersionDocument(FName InName, const FString& InPath);

			bool Transform(FDocumentHandle InDocument) const override;
		};
	}
}
