// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowNodeParameters.h"

namespace Dataflow
{
	class FNode;

	template<class T> inline DATAFLOWCORE_API FName GraphConnectionTypeName();

	struct FPin
	{
		enum class EDirection : uint8 {
			NONE = 0,
			INPUT,
			OUTPUT
		};
		EDirection Direction;
		FName Type;
		FName Name;
	};

	//
	// Input Output Base
	//

	class DATAFLOWCORE_API FConnection
	{

	protected:
		FPin::EDirection Direction;
		FName Type;
		FName Name;
		FGuid  Guid;
		FNode* OwningNode = nullptr;

		friend class FNode;
		static void BindInput(FNode* InNode, FConnection*);
		static void BindOutput(FNode* InNode, FConnection*);


	public:
		FConnection(FPin::EDirection Direction, FName InType, FName InName, FNode* OwningNode = nullptr, FGuid InGuid = FGuid::NewGuid());
		virtual ~FConnection() {};

		FNode* GetOwningNode() { return OwningNode; }
		const FNode* GetOwningNode() const { return OwningNode; }

		FPin::EDirection GetDirection() const { return Direction; }

		FName GetType() const { return Type; }

		FGuid GetGuid() const { return Guid; }
		void SetGuid(FGuid InGuid) { Guid = InGuid; }

		FName GetName() const { return Name; }
		void SetName(FName InName) { Name = InName; }


		virtual bool AddConnection(FConnection* In) { return false; };
		virtual bool RemoveConnection(FConnection* In) { return false; }

		virtual TArray< FConnection* > GetConnectedInputs() { return TArray<FConnection* >(); }
		virtual TArray< FConnection* > GetConnectedOutputs() { return TArray<FConnection* >(); }
		virtual void Invalidate() {};

	};
}
