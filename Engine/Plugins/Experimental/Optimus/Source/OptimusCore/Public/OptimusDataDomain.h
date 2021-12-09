// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

#include "OptimusDataDomain.generated.h"

namespace Optimus::DomainName
{
	extern OPTIMUSCORE_API const FName Singleton;
	extern OPTIMUSCORE_API const FName Vertex;
	extern OPTIMUSCORE_API const FName Triangle;
	extern OPTIMUSCORE_API const FName Bone;
}

/** A struct to hold onto a single-level data domain such as kernel execution domain and 
  * user-defined resources. The reason it's in a struct is so that we can apply a property 
  * panel customization for it to make it easier to select from a pre-defined list of data 
  * domains.
*/
USTRUCT()
struct OPTIMUSCORE_API FOptimusDataDomain
{
	GENERATED_BODY()

	FOptimusDataDomain() = default;
	FOptimusDataDomain(FName InDataDomainName) : Name(InDataDomainName) {}

	// The name of the context that this resource/kernel applies to.
	UPROPERTY(EditAnywhere, Category = Domain)
	FName Name = Optimus::DomainName::Vertex;
};

/** A struct to hold onto a multi-level data domain, as defined by compute kernels and data
*   interfaces. A multi-level data domain is used to describe a nested levels of data domains
*   where each element in a higher domain hold a series of elements in another domain (e.g.
*	bone data per vertex, where there are varying number of bone elements per vertex).
*/
USTRUCT()
struct OPTIMUSCORE_API FOptimusMultiLevelDataDomain
{
	GENERATED_BODY()

	FOptimusMultiLevelDataDomain() = default;
	FOptimusMultiLevelDataDomain(FName InRootName) : LevelNames({InRootName}) {}
	FOptimusMultiLevelDataDomain(TArray<FName> InLevelNames) : LevelNames(InLevelNames) {}

	// The name of the context that this resource/kernel applies to.
	UPROPERTY(EditAnywhere, Category = Domain)
	TArray<FName> LevelNames{Optimus::DomainName::Vertex};

	/** Returns true if the multi-level domain is empty */  
	bool IsEmpty() const
	{
		return LevelNames.IsEmpty();
	}
	
	/** Returns true if this multi-level data domain is valid */
	bool IsValid() const
	{
		for (FName Name: LevelNames)
		{
			if (Name.IsNone())
			{
				return false;
			}
		}
		return !LevelNames.IsEmpty();
	}
};
