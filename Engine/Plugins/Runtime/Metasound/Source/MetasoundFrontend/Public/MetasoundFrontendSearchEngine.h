// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundFrontendDocument.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	namespace Frontend
	{
		/** Interface for frontend search engine. A frontend search engine provides
		 * a simple interface for common frontend queries. It also serves as an
		 * opportunity to cache queries to reduce CPU load. 
		 */
		class METASOUNDFRONTEND_API ISearchEngine
		{
			public:
				/** Return an instance of a search engine. */
				static ISearchEngine& Get();

				/** Find all FMetasoundFrontendClasses with a matching FNodeClassName. */
				virtual TArray<FMetasoundFrontendClass> FindClassesWithClassName(const FNodeClassName& InName) = 0;

				virtual ~ISearchEngine() = default;

			protected:

				ISearchEngine() = default;
		};
	}
}
