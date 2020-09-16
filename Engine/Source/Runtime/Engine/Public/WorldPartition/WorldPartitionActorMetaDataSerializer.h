// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "Misc/Guid.h"

#if WITH_EDITOR
class FActorMetaDataSerializer
{
public:
	FActorMetaDataSerializer()
		: bHasErrors(false)
	{}

	virtual bool IsReading() const =0;
	inline bool IsWriting() const { return !IsReading(); }

	virtual bool Serialize(FName Name, bool& Value)=0;
	virtual bool Serialize(FName Name, int8& Value)=0;
	virtual bool Serialize(FName Name, int32& Value)=0;
	virtual bool Serialize(FName Name, int64& Value)=0;
	virtual bool Serialize(FName Name, FGuid& Value)=0;
	virtual bool Serialize(FName Name, FVector& Value)=0;
	virtual bool Serialize(FName Name, FTransform& Value)=0;
	virtual bool Serialize(FName Name, FString& Value)=0;
	virtual bool Serialize(FName Name, FName& Value)=0;

	inline void SetHasErrors() { bHasErrors = true; }
	inline bool GetHasErrors() const { return bHasErrors; }

private:
	bool bHasErrors;
};

class FActorMetaDataReader: public FActorMetaDataSerializer
{
public:
	FActorMetaDataReader(const FAssetData& InAssetData)
		: AssetData(InAssetData)
	{}

	virtual bool IsReading() const override { return true; }

	virtual bool Serialize(FName Name, bool& Value) override;
	virtual bool Serialize(FName Name, int8& Value) override;
	virtual bool Serialize(FName Name, int32& Value) override;
	virtual bool Serialize(FName Name, int64& Value) override;
	virtual bool Serialize(FName Name, FGuid& Value) override;
	virtual bool Serialize(FName Name, FVector& Value) override;
	virtual bool Serialize(FName Name, FTransform& Value) override;
	virtual bool Serialize(FName Name, FString& Value) override;
	virtual bool Serialize(FName Name, FName& Value) override;

private:
	bool ReadTag(FName Name, FString& Value);

	const FAssetData& AssetData;
};

class FActorMetaDataWriter: public FActorMetaDataSerializer
{
public:
	FActorMetaDataWriter(TArray<UObject::FAssetRegistryTag>& InTags)
		: Tags(InTags)
	{}

	virtual bool IsReading() const override { return false; }

	virtual bool Serialize(FName Name, bool& Value) override;
	virtual bool Serialize(FName Name, int8& Value) override;
	virtual bool Serialize(FName Name, int32& Value) override;
	virtual bool Serialize(FName Name, int64& Value) override;
	virtual bool Serialize(FName Name, FGuid& Value) override;
	virtual bool Serialize(FName Name, FVector& Value) override;
	virtual bool Serialize(FName Name, FTransform& Value) override;
	virtual bool Serialize(FName Name, FString& Value) override;
	virtual bool Serialize(FName Name, FName& Value) override;

private:
	bool WriteTag(FName Name, const FString& Value);

	TArray<UObject::FAssetRegistryTag>& Tags;
};
#endif