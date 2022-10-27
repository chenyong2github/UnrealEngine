// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/ContentBundle/ContentBundleDescriptor.h"
#include "Math/Color.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentBundleDescriptor)

UContentBundleDescriptor::UContentBundleDescriptor(const FObjectInitializer& ObjectInitializer)
	: DebugColor(FColor::Black)
{
}

bool UContentBundleDescriptor::IsValid() const
{
	return Guid.IsValid()
		&& !DisplayName.IsEmpty()
		&& !PackageRoot.IsEmpty();
}

#if WITH_EDITOR
void UContentBundleDescriptor::InitializeObject(const FString& InContentBundleName, const FString& InPackageRoot)
{
	Guid = FGuid::NewGuid();
	DisplayName = InContentBundleName;
	PackageRoot = InPackageRoot;
	InitDebugColor();
}

void UContentBundleDescriptor::PostLoad()
{
	InitDebugColor();

	Super::PostLoad();
}

void UContentBundleDescriptor::InitDebugColor()
{
	// If not set, generate a color based on guid
	if (DebugColor == FColor::Black)
	{
		DebugColor = FColor::MakeRandomSeededColor(GetTypeHash(Guid));
	}
}
#endif