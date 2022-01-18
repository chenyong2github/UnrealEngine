// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Internationalization/Text.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundVertex.h"
#include "Templates/Function.h"

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

		/** Adds or swaps document members (inputs, outputs) and removing any document members where necessary and adding those missing. */
		class METASOUNDFRONTEND_API FModifyRootGraphInterfaces : public IDocumentTransform
		{
		public:
			FModifyRootGraphInterfaces(const TArray<FMetasoundFrontendInterface>& InInterfacesToRemove, const TArray<FMetasoundFrontendInterface>& InInterfacesToAdd);
			FModifyRootGraphInterfaces(const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToRemove, const TArray<FMetasoundFrontendVersion>& InInterfaceVersionsToAdd);

			// Whether or not to propagate node locations to new members. Setting to false
			// results in members not having a default physical location in the editor graph.
			void SetDefaultNodeLocations(bool bInSetDefaultNodeLocations);

			// Override function used to match removed members with added members, allowing
			// transform to preserve connections made between removed interface members & new interface members
			// that may be related but not be named the same.
			void SetNamePairingFunction(const TFunction<bool(FName, FName)>& InNamePairingFunction);

			bool Transform(FDocumentHandle InDocument) const override;

		private:
			void Init(const TFunction<bool(FName, FName)>* InNamePairingFunction = nullptr);

			bool bSetDefaultNodeLocations = true;

			TArray<FMetasoundFrontendInterface> InterfacesToRemove;
			TArray<FMetasoundFrontendInterface> InterfacesToAdd;

			using FVertexPair = TTuple<FMetasoundFrontendClassVertex, FMetasoundFrontendClassVertex>;
			TArray<FVertexPair> PairedInputs;
			TArray<FVertexPair> PairedOutputs;

			TArray<FMetasoundFrontendClassInput> InputsToAdd;
			TArray<FMetasoundFrontendClassInput> InputsToRemove;
			TArray<FMetasoundFrontendClassOutput> OutputsToAdd;
			TArray<FMetasoundFrontendClassOutput> OutputsToRemove;

		};

		/** Updates document's given interface to the most recent version. */
		class METASOUNDFRONTEND_API FUpdateRootGraphInterface : public IDocumentTransform
		{
		public:
			FUpdateRootGraphInterface(const FMetasoundFrontendVersion& InInterfaceVersion)
				: InterfaceVersion(InInterfaceVersion)
			{
			}

			bool Transform(FDocumentHandle InDocument) const override;

		private:
			void GetUpdatePathForDocument(const FMetasoundFrontendVersion& InCurrentVersion, const FMetasoundFrontendVersion& InTargetVersion, TArray<const IInterfaceRegistryEntry*>& OutUpgradePath) const;
			bool UpdateDocumentInterface(const TArray<const IInterfaceRegistryEntry*>& InUpgradePath, FDocumentHandle InDocument) const;

			FMetasoundFrontendVersion InterfaceVersion;
		};

		/** Completely rebuilds the graph connecting a preset's inputs to the reference
		 * document's root graph. It maintains previously set input values entered upon 
		 * the presets wrapping graph. */
		class METASOUNDFRONTEND_API FRebuildPresetRootGraph : public IDocumentTransform
		{
		public:
			/** Create transform.
			 * @param InReferenceDocument - The document containing the wrapped MetaSound graph.
			 */
			FRebuildPresetRootGraph(FConstDocumentHandle InReferencedDocument)
				: ReferencedDocument(InReferencedDocument)
			{
			}

			bool Transform(FDocumentHandle InDocument) const override;

		private:

			// Get the class inputs needed for this preset. Input literals set on 
			// the parent graph will be used if they exist. 
			TArray<FMetasoundFrontendClassInput> GenerateRequiredClassInputs(const FConstGraphHandle& InParentGraph) const;

			// Get the class Outputs needed for this preset.
			TArray<FMetasoundFrontendClassOutput> GenerateRequiredClassOutputs(const FConstGraphHandle& InParentGraph) const;

			// Add inputs to parent graph and connect to wrapped graph node.
			void AddAndConnectInputs(const TArray<FMetasoundFrontendClassInput>& InClassInputs, FGraphHandle& InParentGraphHandle, FNodeHandle& InReferencedNode) const;

			// Add outputs to parent graph and connect to wrapped graph node.
			void AddAndConnectOutputs(const TArray<FMetasoundFrontendClassOutput>& InClassOutputs, FGraphHandle& InParentGraphHandle, FNodeHandle& InReferencedNode) const;

			FConstDocumentHandle ReferencedDocument;
		};

		/** Automatically updates all nodes and respective dependencies in graph where
		  * newer versions exist in the loaded MetaSound Class Node Registry.
		  */
		class METASOUNDFRONTEND_API FAutoUpdateRootGraph : public IDocumentTransform
		{
		public:
			FAutoUpdateRootGraph(FString&& InAssetPath)
				: AssetPath(MoveTemp(InAssetPath))
			{
			}

			bool Transform(FDocumentHandle InDocument) const override;

			const FString AssetPath;
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
				return FMetasoundFrontendVersionNumber { 1, 7 };
			}

			FVersionDocument(FName InName, const FString& InPath);

			bool Transform(FDocumentHandle InDocument) const override;
		};
	}
}
