// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundAssetBase.h"
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

		class METASOUNDFRONTEND_API FRebuildPresetRootGraph : public IDocumentTransform
		{
		public:
			FRebuildPresetRootGraph(FConstDocumentHandle InReferencedDocument)
				: ReferencedDocument(InReferencedDocument)
			{
			}

			bool Transform(FDocumentHandle InDocument) const override;

		private:
			FConstDocumentHandle ReferencedDocument;
		};

		class METASOUNDFRONTEND_API FAutoUpdateRootGraph : public IDocumentTransform
		{
		public:
			FAutoUpdateRootGraph() = default;

			bool Transform(FDocumentHandle InDocument) const override;
		};

		/** Synchronizes the document's root graph's display name with that of the asset. */
		class METASOUNDFRONTEND_API FSynchronizeAssetClassDisplayName : public IDocumentTransform
		{
		public:
			FSynchronizeAssetClassDisplayName(FName InAssetName)
				: AssetName(InAssetName)
			{
			}

			bool Transform(FDocumentHandle InDocument) const override;

		private:
			FName AssetName;
		};

		/** Regenerates the class' name, effectively causing the class to get registered as
		  * a new class (useful for when an asset is duplicated). */
		class METASOUNDFRONTEND_API FRegenerateAssetClassName : public IDocumentTransform
		{
		public:
			FRegenerateAssetClassName() = default;

			bool Transform(FDocumentHandle InDocument) const override;
		};

		/** Base class for versioning a document. */
		class METASOUNDFRONTEND_API FVersionDocument : public IDocumentTransform
		{
			const FName Name;
			const FString& Path;

		public:
			static FMetasoundFrontendVersionNumber GetMaxVersion()
			{
				return FMetasoundFrontendVersionNumber { 1, 6 };
			}

			FVersionDocument(FName InName, const FString& InPath);

			bool Transform(FDocumentHandle InDocument) const override;
		};
	}
}
