// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/LinkerInstancingContext.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/NameTypes.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Templates/Tuple.h"

void FLinkerInstancingContext::FixupSoftObjectPath(FSoftObjectPath& InOutSoftObjectPath) const
{
	if (IsInstanced() && GetSoftObjectPathRemappingEnabled())
	{
		// Try remapping AssetPathName before remapping LongPackageName
		if (FSoftObjectPath RemappedAssetPath = RemapPath(InOutSoftObjectPath); RemappedAssetPath != InOutSoftObjectPath)
		{
			InOutSoftObjectPath = RemappedAssetPath;
		}
		else if (FName LongPackageName = InOutSoftObjectPath.GetLongPackageFName(), RemappedPackage = RemapPackage(LongPackageName); RemappedPackage != LongPackageName)
		{
			InOutSoftObjectPath = FSoftObjectPath(RemappedPackage, InOutSoftObjectPath.GetAssetFName(), InOutSoftObjectPath.GetSubPathString());
		}
#if !WITH_EDITOR
		else if (!GeneratedPackagesFolder.IsEmpty())
		{
			check(!InstancedPackageSuffix.IsEmpty());

			FNameBuilder TmpSoftObjectPathBuilder;
			InOutSoftObjectPath.ToString(TmpSoftObjectPathBuilder);

			// Does this package path start with the generated folder path?
			FStringView TmpSoftObjectPathView = TmpSoftObjectPathBuilder.ToView();
			if (TmpSoftObjectPathView.StartsWith(GeneratedPackagesFolder))
			{
				// ... and is that generated folder path immediately preceding the package name?
				if (const int32 ExtraSlashIndex = TmpSoftObjectPathView.Find(TEXTVIEW("/"), GeneratedPackagesFolder.Len()); ExtraSlashIndex == INDEX_NONE)
				{
					FNameBuilder PackageNameBuilder;
					PackageNameBuilder.Append(InOutSoftObjectPath.GetLongPackageName());
					PackageNameBuilder.Append(InstancedPackageSuffix);
					FTopLevelAssetPath SuffixTopLevelAsset(FName(PackageNameBuilder.ToString()), InOutSoftObjectPath.GetAssetFName());
					InOutSoftObjectPath = FSoftObjectPath(SuffixTopLevelAsset, InOutSoftObjectPath.GetSubPathString());
				}
			}
		}
#endif
	}
}

void FLinkerInstancingContext::BuildPackageMapping(FName Original, FName Instanced)
{
	check(GeneratedPackagesFolder.IsEmpty() && InstancedPackageSuffix.IsEmpty());

	AddPackageMapping(Original, Instanced);

#if !WITH_EDITOR
	if (bSoftObjectPathRemappingEnabled)
	{
		FNameBuilder TmpOriginal(Original);
		FNameBuilder TmpInstanced(Instanced);
		FStringView OriginalView = TmpOriginal.ToView();
		FStringView InstancedView = TmpInstanced.ToView();

		// Stash the suffix used for this instance so we can also apply it to generated packages
		if (InstancedView.StartsWith(OriginalView))
		{
			InstancedPackageSuffix = InstancedView.Mid(OriginalView.Len());
		}

		// Is this a generated partitioned map package? If so, we'll also need to handle re-mapping paths to our persistent map package
		if (!InstancedPackageSuffix.IsEmpty())
		{
			const FStringView GeneratedFolderName = TEXTVIEW("/_Generated_/");
			
			// Does this package path include the generated folder?
			if (const int32 GeneratedFolderStartIndex = OriginalView.Find(GeneratedFolderName); GeneratedFolderStartIndex != INDEX_NONE)
			{
				// ... and is that generated folder immediately preceding the package name?
				const int32 GeneratedFolderEndIndex = GeneratedFolderStartIndex + GeneratedFolderName.Len();
				if (const int32 ExtraSlashIndex = OriginalView.Find(TEXTVIEW("/"), GeneratedFolderEndIndex); ExtraSlashIndex == INDEX_NONE)
				{
					GeneratedPackagesFolder = OriginalView.Left(GeneratedFolderEndIndex);

					FNameBuilder PackageNameBuilder(FName(OriginalView.Left(GeneratedFolderStartIndex)));
					FName PersistentPackageName(PackageNameBuilder);
					PackageNameBuilder.Append(InstancedPackageSuffix);
					FName PersistentPackageInstanceName(PackageNameBuilder);
										
					PackageMapping.Add(MakeTuple(PersistentPackageName, PersistentPackageInstanceName));
				}
			}

			if (GeneratedPackagesFolder.IsEmpty())
			{
				GeneratedPackagesFolder = OriginalView;
				GeneratedPackagesFolder += GeneratedFolderName;
			}
		}
	}
#endif
}

