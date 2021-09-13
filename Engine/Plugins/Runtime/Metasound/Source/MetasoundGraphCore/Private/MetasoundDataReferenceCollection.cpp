// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetasoundDataReferenceCollection.h"

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"

namespace Metasound
{
	bool FDataReferenceCollection::ContainsDataReadReference(const FVertexName& InName, const FName& InTypeName) const
	{
		if (const FDataRefWrapper* Wrapper = DataReadRefMap.Find(InName))
		{
			if (const IDataReference* Ref = Wrapper->GetDataReference())
			{
				return InTypeName == Ref->GetDataTypeName();
			}
		}

		return false;
	}

	bool FDataReferenceCollection::ContainsDataWriteReference(const FVertexName& InName, const FName& InTypeName) const
	{
		if (const FDataRefWrapper* Wrapper = DataWriteRefMap.Find(InName))
		{
			if (const IDataReference* Ref = Wrapper->GetDataReference())
			{
				return InTypeName == Ref->GetDataTypeName();
			}
		}

		return false;
	}

	bool FDataReferenceCollection::AddDataReadReferenceFrom(const FVertexName& InName, const FDataReferenceCollection& OtherCollection, const FVertexName& OtherName, const FName& OtherTypeName)
	{
		if (OtherCollection.ContainsDataReadReference(OtherName, OtherTypeName))
		{
			DataReadRefMap.Add(InName, OtherCollection.DataReadRefMap[OtherName]);

			return true;
		}
		else if (OtherCollection.ContainsDataWriteReference(OtherName, OtherTypeName))
		{
			DataReadRefMap.Add(InName, OtherCollection.DataWriteRefMap[OtherName]);

			return true;
		}

		return false;
	}

	bool FDataReferenceCollection::AddDataWriteReferenceFrom(const FVertexName& InName, const FDataReferenceCollection& OtherCollection, const FVertexName& OtherName, const FName& OtherTypeName)
	{
		if (OtherCollection.ContainsDataWriteReference(OtherName, OtherTypeName))
		{
			DataWriteRefMap.Add(InName, OtherCollection.DataWriteRefMap[OtherName]);

			return true;
		}

		return false;
	}

	const IDataReference* FDataReferenceCollection::GetDataReference(const FDataReferenceMap& InMap, const FVertexName& InName) const
	{
		if (const FDataRefWrapper* RefWrapper = InMap.Find(InName))
		{
			return RefWrapper->GetDataReference();
		}

		return nullptr;
	}

	void FDataReferenceCollection::AddDataReference(FDataReferenceMap& InMap, const FVertexName& InName, FDataRefWrapper&& InDataRef)
	{
		InMap.Add(InName, InDataRef);
	}
}
