// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UDataflowComponent;


class DATAFLOWENGINEPLUGIN_API FDataflowSelectionState
{
public:
	struct ObjectID
	{
		ObjectID(FString InName, int32 InID) : Name(InName), ID(InID) {}

		bool operator==(const ObjectID& A) const {
			return A.Name.Equals(Name);
		}

		FString Name;
		int32 ID;
	};

	FDataflowSelectionState() {}
	FDataflowSelectionState(const UDataflowComponent* DataflowComponent)
	{
		check(DataflowComponent);
	}

	enum EMode
	{
		DSS_Dataflow_None,
		DSS_Dataflow_Object,
		DSS_Dataflow_Max
	};

	void UpdateSelection(UDataflowComponent* DataflowComponent);

	bool IsEmpty() const
	{
		return Nodes.Num() == 0;
	}

	bool operator==(const FDataflowSelectionState& A) const {
		return A.Nodes == Nodes;
	}
	bool operator!=(const FDataflowSelectionState& A) const {
		return !this->operator==(A);
	}

	TArray<ObjectID> Nodes;
};