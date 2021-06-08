// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

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
				virtual bool Transform(FDocumentHandle InDocument) const = 0;
		};

		/** Adds or swaps root graph inputs and outputs to match the document's archetype. */
		class METASOUNDFRONTEND_API FMatchRootGraphToArchetype : public IDocumentTransform
		{
			public:
				FMatchRootGraphToArchetype(const FMetasoundFrontendArchetype& InArchetype)
					: Archetype(InArchetype)
				{
				}

				bool Transform(FDocumentHandle InDocument) const override;

			private:
				FMetasoundFrontendArchetype Archetype;
		};

		/** Base class for versioning a document. */
		class METASOUNDFRONTEND_API FVersionDocument : public IDocumentTransform
		{
			const FName Name;
			const FString& Path;

			public:
				static FMetasoundFrontendVersionNumber GetMaxVersion()
				{
					return FMetasoundFrontendVersionNumber { 1, 2 };
				}

				FVersionDocument(FName InName, const FString& InPath);

				bool Transform(FDocumentHandle InDocument) const override;
		};
	}
}
