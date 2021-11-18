// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InstancedFoliage.h"

namespace UE::LevelSnapshots::Foliage::Private
{
	/** Data we'll send to FFoliageImpl::Serialize */
	class FFoliageInfoData
	{
		EFoliageImplType ImplType = EFoliageImplType::Unknown;
		FName ComponentName;
	
		TArray<uint8> SerializedData;

		FArchive& SerializeInternal(FArchive& Ar);
	
	public:

		void Save(FFoliageInfo& DataToReadFrom, const FCustomVersionContainer& VersionInfo);
		void ApplyTo(FFoliageInfo& DataToWriteInto, const FCustomVersionContainer& VersionInfo) const;

		EFoliageImplType GetImplType() const { return ImplType; }
		TOptional<FName> GetComponentName() const { return ImplType == EFoliageImplType::StaticMesh && !ComponentName.IsNone() ? TOptional<FName>(ComponentName) : TOptional<FName>(); }
	
		friend FArchive& operator<<(FArchive& Ar, FFoliageInfoData& MeshInfo)
		{
			return MeshInfo.SerializeInternal(Ar);
		}
	};
}
