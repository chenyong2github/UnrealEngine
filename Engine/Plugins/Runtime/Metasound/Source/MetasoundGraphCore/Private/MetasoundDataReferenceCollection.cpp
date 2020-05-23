// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetasoundDataReferenceCollection.h"

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"

namespace Metasound
{
	bool FDataReferenceCollection::ContainsDataReadReference(const FString& InName, const FName& InTypeName) const
	{
		if (DataReadRefMap.Contains(InName))
		{
			const FDataRefWrapper& Wrapper = DataReadRefMap[InName];
			if (Wrapper.IsValid())
			{
				return InTypeName == Wrapper.GetDataReference()->GetDataTypeName();
			}
		}

		return false;
	}

	bool FDataReferenceCollection::ContainsDataWriteReference(const FString& InName, const FName& InTypeName) const
	{
		if (DataWriteRefMap.Contains(InName))
		{
			const FDataRefWrapper& Wrapper = DataWriteRefMap[InName];
			if (Wrapper.IsValid())
			{
				return InTypeName == Wrapper.GetDataReference()->GetDataTypeName();
			}
		}

		return false;
	}

	bool FDataReferenceCollection::AddDataReadReferenceFrom(const FString& InName, const FDataReferenceCollection& OtherCollection, const FString& OtherName, const FName& OtherTypeName)
	{
		if (OtherCollection.ContainsDataReadReference(OtherName, OtherTypeName))
		{
			DataReadRefMap.Add(InName, OtherCollection.DataReadRefMap[OtherName]);

			return true;
		}

		return false;
	}

	bool FDataReferenceCollection::AddDataWriteReferenceFrom(const FString& InName, const FDataReferenceCollection& OtherCollection, const FString& OtherName, const FName& OtherTypeName)
	{
		if (OtherCollection.ContainsDataWriteReference(OtherName, OtherTypeName))
		{
			DataWriteRefMap.Add(InName, OtherCollection.DataWriteRefMap[OtherName]);

			return true;
		}

		return false;
	}

	const IDataReference* FDataReferenceCollection::GetDataReference(const FDataReferenceMap& InMap, const FString& InName) const
	{
		if (InMap.Contains(InName))
		{
			return InMap[InName].GetDataReference();
		}

		return nullptr;
	}

	void FDataReferenceCollection::AddDataReference(FDataReferenceMap& InMap, const FString& InName, FDataRefWrapper&& InDataRef)
	{
		InMap.Add(InName, InDataRef);
	}
}
