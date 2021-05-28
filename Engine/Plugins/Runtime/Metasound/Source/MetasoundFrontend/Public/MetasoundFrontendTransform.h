// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundFrontendController.h"

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
				bool Transform(FDocumentHandle InDocument) const override;
		};

		/** Base class for versioning a document. */
		class METASOUNDFRONTEND_API FVersionDocument : public IDocumentTransform
		{
			public:
				bool Transform(FDocumentHandle InDocument) const;
		};
	}
}
