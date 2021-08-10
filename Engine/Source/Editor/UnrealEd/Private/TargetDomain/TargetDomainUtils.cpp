// Copyright Epic Games, Inc. All Rights Reserved.

#include "TargetDomain/TargetDomainUtils.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Cooker/PackageBuildDependencyTracker.h"
#include "EditorDomain/EditorDomainUtils.h"
#include "IO/IoHash.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"

namespace UE::TargetDomain
{

bool TryCollectKeyAndDependencies(UPackage* Package, const ITargetPlatform* TargetPlatform, FIoHash* OutHash, TArray<FName>* OutBuildDependencies,
	TArray<FName>* OutRuntimeOnlyDependencies, FString* OutErrorMessage)
{
	// Implementation is coming in a separate changelist that mostly involves the PackageBuildDependencyTracker
	if (OutErrorMessage) *OutErrorMessage = TEXT("Not yet implemented.");
	return false;
}

FCbObject CollectDependenciesObject(UPackage* Package, const ITargetPlatform* TargetPlatform, FString* ErrorMessage)
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
	Writer.EndObject();
	return Writer.Save().AsObject();
}


bool TryFetchKeyAndDependencies(FName PackageName, const ITargetPlatform* TargetPlatform, FIoHash* OutHash, TArray<FName>* OutDependencies)
{
	// TODO: Read from oplog for the packagename, return true if and only if it has a targetdomainkey and the generated key from the collection of dependencies matches
	return false;
}

bool TryFetchDependencies(FName PackageName, const ITargetPlatform* TargetPlatform,
	TArray<FName>& OutBuildDependencies, TArray<FName>& OutRuntimeOnlyDependencies)
{
	// TODO: Read from oplog for the packagename, return true if and only if it has a targetdomainkey and the generated key from the collection of dependencies matches
	return false;
}

}
