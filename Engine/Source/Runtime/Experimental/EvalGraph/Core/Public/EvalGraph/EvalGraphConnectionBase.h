// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvalGraph/EvalGraphNodeParameters.h"

namespace Eg
{
	class FNode;

	template<class T> inline EVALGRAPHCORE_API FName GraphConnectionTypeName();

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

	class EVALGRAPHCORE_API FConnectionBase
	{

	protected:
		FPin::EDirection Direction;
		FName Type;
		FName Name;
		FGuid  Guid;
		FNode* OwningNode = nullptr;

		friend class FNode;
		static void AddBaseInput(FNode* InNode, FConnectionBase*);
		static void AddBaseOutput(FNode* InNode, FConnectionBase*);


	public:
		FConnectionBase(FPin::EDirection Direction, FName InType, FName InName, FNode* OwningNode = nullptr, FGuid InGuid = FGuid::NewGuid());
		virtual ~FConnectionBase() {};

		FNode* GetOwningNode() { return OwningNode; }
		const FNode* GetOwningNode() const { return OwningNode; }

		FPin::EDirection GetDirection() const { return Direction; }

		FName GetType() const { return Type; }

		FGuid GetGuid() const { return Guid; }
		void SetGuid(FGuid InGuid) { Guid = InGuid; }

		FName GetName() const { return Name; }
		void SetName(FName InName) { Name = InName; }


		virtual bool AddConnection(FConnectionBase* In) { return false; };
		virtual bool RemoveConnection(FConnectionBase* In) { return false; }

		virtual TArray< FConnectionBase* > GetBaseInputs() { return TArray<FConnectionBase* >(); }
		virtual TArray< FConnectionBase* > GetBaseOutputs() { return TArray<FConnectionBase* >(); }
		virtual void Invalidate() {};

	};
}

