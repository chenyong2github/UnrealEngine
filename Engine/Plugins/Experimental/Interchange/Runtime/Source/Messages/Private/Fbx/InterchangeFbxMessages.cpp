// Copyright Epic Games, Inc. All Rights Reserved.

#include "Fbx/InterchangeFbxMessages.h"


#define LOCTEXT_NAMESPACE "InterchangeFbxMessages"

FText UInterchangeResultMeshWarning_Generic::GetText() const
{
	// Any occurrences of {MeshName} in the supplied text will be replaced by the actual mesh name
	FFormatNamedArguments Args
	{
		{ TEXT("MeshName"), FText::FromString(MeshName) }
	};

	return FText::Format(Text, Args);
}


FText UInterchangeResultMeshError_Generic::GetText() const
{
	// Any occurrences of {MeshName} in the supplied text will be replaced by the actual mesh name
	FFormatNamedArguments Args
	{
		{ TEXT("MeshName"), FText::FromString(MeshName) }
	};

	return FText::Format(Text, Args);
}


FText UInterchangeResultMeshWarning_TooManyUVs::GetText() const
{
	FFormatNamedArguments Args
	{
		{ TEXT("MeshName"), FText::FromString(MeshName) },
		{ TEXT("ExcessUVs"), ExcessUVs}
	};

	return FText::Format(LOCTEXT("TooManyUVs", "Reached the maximum number of UV Channels for mesh '{MeshName}' - discarding {ExcessUVs} {ExcessUVs}|plural(one=channel,other=channels)."), Args);
}


#undef LOCTEXT_NAMESPACE
