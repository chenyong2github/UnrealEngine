// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "IO/IoHash.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "UObject/NameTypes.h"

class ITargetPlatform;
class UPackage;
struct FIoHash;

namespace UE::TargetDomain
{

inline bool TryCollectKeyAndDependencies(UPackage* Package, const ITargetPlatform* TargetPlatform,
	FIoHash* OutHash, TArray<FName>* OutBuildDependencies, TArray<FName>* OutRuntimeOnlyDependencies, FString* OutErrorMessage)
{
	// Implementation is coming in a separate changelist that mostly involves the PackageBuildDependencyTracker
	if (OutErrorMessage) *OutErrorMessage = TEXT("Not yet implemented.");
	return false;
}

inline FCbObject GetDependenciesObject(UPackage* Package, const ITargetPlatform* TargetPlatform, FString* ErrorMessage)
{
	FIoHash TargetDomainKey;
	TArray<FName> BuildDependencies;
	TArray<FName> RuntimeOnlyDependencies;
	if (!UE::TargetDomain::TryCollectKeyAndDependencies(Package, TargetPlatform, &TargetDomainKey, &BuildDependencies, &RuntimeOnlyDependencies, ErrorMessage))
	{
		return FCbObject();
	}

	FCbWriter Writer;
	Writer.BeginObject();
	Writer << "targetdomainkey" << TargetDomainKey;
	TStringBuilder<128> PackageNameBuffer;
	if (!BuildDependencies.IsEmpty())
	{
		Writer.BeginArray("builddependencies");
		for (FName DependencyName : BuildDependencies)
		{
			DependencyName.ToString(PackageNameBuffer);
			Writer << PackageNameBuffer;
		}
		Writer.EndArray();
	}
	if (!RuntimeOnlyDependencies.IsEmpty())
	{
		Writer.BeginArray("runtimeonlydependencies");
		for (FName DependencyName : RuntimeOnlyDependencies)
		{
			DependencyName.ToString(PackageNameBuffer);
			Writer << PackageNameBuffer;
		}
		Writer.EndArray();
	}
	return Writer.Save().AsObject();
}

}
