// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvalGraph/EvalGraphNodeParameters.h"

namespace Eg
{
	class FNode;

	template<class T> inline EVALGRAPHCORE_API FName GraphConnectionTypeName();


	//
	// Input Output Base
	//

	class EVALGRAPHCORE_API FConnectionBase
	{

	protected:
		FName Type;
		FName Name;
		FGuid  Guid;
		FNode* OwningNode = nullptr;

		friend class FNode;
		static void AddBaseInput(FNode* InNode, FConnectionBase*);
		static void AddBaseOutput(FNode* InNode, FConnectionBase*);


	public:
		FConnectionBase(FName InType, FName InName, FNode* OwningNode = nullptr, FGuid InGuid = FGuid::NewGuid());
		virtual ~FConnectionBase() {};

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

