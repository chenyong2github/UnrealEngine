// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundFrontendArchetypeRegistry.h"
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

				/** Updates internal state to speed up queries. */
				virtual void Prime() = 0;

				/** Find all FMetasoundFrontendClasses.
				  * (Optional) Include all versions (i.e. deprecated classes and versions of classes that are not the highest major version).
				  */
				virtual TArray<FMetasoundFrontendClass> FindAllClasses(bool bInIncludeAllVersions) = 0;

				/** Find all classes with the given ClassName.
				  * (Optional) Sort matches based on version.
				  */
				virtual TArray<FMetasoundFrontendClass> FindClassesWithName(const FMetasoundFrontendClassName& InName, bool bInSortByVersion) = 0;

				/** Find the highest version of a class with the given ClassName. Returns false if not found, true if found. */
				virtual bool FindClassWithHighestVersion(const FMetasoundFrontendClassName& InName, FMetasoundFrontendClass& OutClass) = 0;

				/** Find the class with the given ClassName & Major Version. Returns false if not found, true if found. */
				virtual bool FindClassWithMajorVersion(const FMetasoundFrontendClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass) = 0;

				virtual TArray<FMetasoundFrontendInterface> FindAllInterfaces(bool bInIncludeAllVersions = false) = 0;

				virtual TArray<FMetasoundFrontendInterface> FindUClassDefaultInterfaces(FName InUClassName) = 0;

				virtual TArray<FMetasoundFrontendVersion> FindAllRegisteredInterfacesWithName(FName InInterfaceName) = 0;

				virtual bool FindInterfaceWithHighestVersion(FName InInterfaceName, FMetasoundFrontendInterface& OutInterface) = 0;

				virtual ~ISearchEngine() = default;

			protected:
				ISearchEngine() = default;
		};
	}
}
