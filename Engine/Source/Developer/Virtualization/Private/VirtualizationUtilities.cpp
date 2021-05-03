// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualizationUtilities.h"

#include "Misc/StringBuilder.h"
#include "Virtualization/PayloadId.h"

namespace UE::Virtualization::Utils
{

void PayloadIdToPath(const FPayloadId& Id, FStringBuilderBase& OutPath)
{
	OutPath.Reset();
	OutPath << Id;

	TStringBuilder<10> Directory;
	Directory << OutPath.ToView().Left(2) << TEXT("/");
	Directory << OutPath.ToView().Mid(2, 2) << TEXT("/");
	Directory << OutPath.ToView().Mid(4, 2) << TEXT("/");

	OutPath.ReplaceAt(0, 6, Directory);

	OutPath << TEXT(".payload");
}

FString PayloadIdToPath(const FPayloadId& Id)
{
	TStringBuilder<52> Path;
	PayloadIdToPath(Id, Path);

	return FString(Path);
}

} // namespace UE::Virtualization::Utils
