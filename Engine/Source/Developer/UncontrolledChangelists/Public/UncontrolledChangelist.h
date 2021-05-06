// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlChangelist.h"
#include "Misc/Guid.h"

class FUncontrolledChangelist : public TSharedFromThis<FUncontrolledChangelist, ESPMode::ThreadSafe>
{
public:
	static constexpr const TCHAR* GUID_NAME = TEXT("guid");
	static constexpr const TCHAR* NAME_NAME = TEXT("name");
	static const FGuid DEFAULT_UNCONTROLLED_CHANGELIST_GUID;
	static const FText DEFAULT_UNCONTROLLED_CHANGELIST_NAME;

public:
	FUncontrolledChangelist();
	
	FUncontrolledChangelist(const FGuid& InGuid, const FString& InName);

	bool operator==(const FUncontrolledChangelist& InOther) const
	{
		return Guid == InOther.Guid;
	}

	bool operator!=(const FUncontrolledChangelist& InOther) const
	{
		return Guid != InOther.Guid;
	}

	bool IsDefault() const
	{
		return Guid == DEFAULT_UNCONTROLLED_CHANGELIST_GUID;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FUncontrolledChangelist& PerforceUncontrolledChangelist)
	{
		return GetTypeHash(PerforceUncontrolledChangelist.Guid);
	}

	FString ToString() const
	{
		return Name.IsEmpty() ? Guid.ToString() : Name;
	}

	/**
	 * Serialize the Uncontrolled Changelist to a Json Object.
	 * @param	OutJsonObject	The Json Object used to serialize.
	 */
	void Serialize(TSharedRef<class FJsonObject> OutJsonObject) const;
	
	/**
	 * Deserialize the Uncontrolled Changelist from a Json Object.
	 * @param 	InJsonValue 	The Json Object to read from.
	 * @return 	True if Deserialization succeeded.
	 */
	bool Deserialize(const TSharedRef<class FJsonObject> InJsonValue);

private:
	FGuid		Guid;
	FString		Name;
};

typedef TSharedRef<FUncontrolledChangelist, ESPMode::ThreadSafe> FUncontrolledChangelistRef;
