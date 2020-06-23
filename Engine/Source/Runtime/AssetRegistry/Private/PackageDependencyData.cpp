// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageDependencyData.h"

FName FPackageDependencyData::GetImportPackageName(int32 ImportIndex)
{
	FPackageIndex LinkerIndex = FPackageIndex::FromImport(ImportIndex);
	while (LinkerIndex.IsImport())
	{
		FObjectImport& Resource = Imp(LinkerIndex);
		// If the import has a package name set, then that's the import package name,
		if (Resource.HasPackageName())
		{
			return Resource.GetPackageName();
		}
		// If our outer is null, then we have a package
		else if (Resource.OuterIndex.IsNull())
		{
			return Resource.ObjectName;
		}
		LinkerIndex = Resource.OuterIndex;
	}
	return NAME_None;
}
