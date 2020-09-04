// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AssetRegistryInterface.h"

IAssetRegistryInterface* IAssetRegistryInterface::Default = nullptr;
IAssetRegistryInterface* IAssetRegistryInterface::GetPtr()
{
	return Default;
}

namespace UE
{
namespace AssetRegistry
{
namespace Private
{
	IAssetRegistry* IAssetRegistrySingleton::Singleton = nullptr;
}
}
}
