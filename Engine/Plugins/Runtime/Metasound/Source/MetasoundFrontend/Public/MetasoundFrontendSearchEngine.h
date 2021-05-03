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

				/** Find all FMetasoundFrontendClasses.
				  * (Optional) Include deprecated classes (i.e. versions of classes that are not the highest major version).
				  */
				virtual TArray<FMetasoundFrontendClass> FindAllClasses(bool bInIncludeDeprecated) = 0;

				/** Find all classes with the given ClassName.
				  * (Optional) Sort matches based on version.
				  */
				virtual TArray<FMetasoundFrontendClass> FindClassesWithName(const FNodeClassName& InName, bool bInSortByVersion) = 0;

				/** Find the highest version of a class with the given ClassName. Returns false if not found, true if found. */
				virtual bool FindClassWithHighestVersion(const FNodeClassName& InName, FMetasoundFrontendClass& OutClass) = 0;

				/** Find the class with the given ClassName & Major Version. Returns false if not found, true if found. */
				virtual bool FindClassWithMajorVersion(const FNodeClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass) = 0;

				virtual ~ISearchEngine() = default;

			protected:
				ISearchEngine() = default;
		};
	}
}
