// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvalGraph/EvalGraphNodeParameters.h"

namespace Eg
{
	class FNode;

	//
	// Input Output Base
	//

	class EVALGRAPH_API FConnectionTypeBase
	{

	protected:
		EGraphConnectionType Type;
		FName Name;
		FGuid  Guid;
		FNode* OwningNode = nullptr;

		friend class FNode;
		static void AddBaseInput(FNode* InNode, FConnectionTypeBase*);
		static void AddBaseOutput(FNode* InNode, FConnectionTypeBase*);


	public:
		FConnectionTypeBase(EGraphConnectionType InType, FName InName, FNode* OwningNode = nullptr, FGuid InGuid = FGuid::NewGuid());
		virtual ~FConnectionTypeBase() {};

		EGraphConnectionType GetType() const { return Type; }

		FGuid GetGuid() const { return Guid; }
		void SetGuid(FGuid InGuid) { Guid = InGuid; }

		FName GetName() const { return Name; }
		void SetName(FName InName) { Name = InName; }


		virtual bool AddConnection(FConnectionTypeBase* In) { return false; };
		virtual bool RemoveConnection(FConnectionTypeBase* In) { return false; }

		virtual TArray< FConnectionTypeBase* > GetBaseInputs() { return TArray<FConnectionTypeBase* >(); }
		virtual TArray< FConnectionTypeBase* > GetBaseOutputs() { return TArray<FConnectionTypeBase* >(); }
		virtual void Invalidate() {};

	};

	template<typename T> class FOutput;

	//
	//  Input
	//

	template<class T>
	struct EVALGRAPH_API FInputParameters {
		FInputParameters(FName InName, FNode* InOwner, T InDefault = T())
			: Name(InName)
			, Owner(InOwner)
			, Default(InDefault) {}
		FName Name;
		FNode* Owner = nullptr;
		T Default;
	};

	template<class T>
	class EVALGRAPH_API FInput : public FConnectionTypeBase
	{
		friend class FConnectionTypeBase;

		typedef FConnectionTypeBase Super;
		T Default;
		FOutput<T>* Connection;
	public:
		FInput(const FInputParameters<T>& Param, FGuid InGuid = FGuid::NewGuid());
	
		const T& GetDefault() const { return Default; }
		
		const FOutput<T>* GetConnection() const { return Connection; }
		FOutput<T>* GetConnection() { return Connection; }

		virtual bool AddConnection(FConnectionTypeBase* InOutput) override
		{ 
			ensure(Connection == nullptr);
			if (ensure(InOutput->GetType()==this->GetType()))
			{
				Connection = (FOutput<T>*)InOutput;
				return true;
			}
			return false;
		}

		virtual bool RemoveConnection(FConnectionTypeBase* InOutput) override
		{ 
			if (ensure(Connection == (FOutput<T>*)InOutput))
			{
				Connection = nullptr;
				return true;
			}
			return false;
		}

		T GetValue(const FContext& Context)
		{
			if (Connection)
			{
				return Connection->Evaluate(Context);
			}
			return Default;
		}

		void SetValue(const T& Value, const FContext& Context);

		virtual TArray< FConnectionTypeBase* > GetBaseOutputs() override
		{ 
			TArray<FConnectionTypeBase* > RetList;
			if (GetConnection())
			{
				RetList.Add(GetConnection());
			}
			return RetList;
		}

		virtual void Invalidate() override;

	};


	//
	// Output
	//

	struct EVALGRAPH_API FOutputParameters {
		FOutputParameters(FName InName, FNode * InOwner) 
			: Name(InName)
			, Owner(InOwner) {}
		FName Name;
		FNode* Owner = nullptr;
	};

	template<class T>
	class EVALGRAPH_API FOutput : public FConnectionTypeBase
	{
		typedef FConnectionTypeBase Super;
		friend class FConnectionTypeBase;

		uint32 CacheKey = UINT_MAX;
		TCacheValue<T> Cache;
		TArray< FInput<T>* > Connections;
	
	public:
		FOutput(const FOutputParameters& Param, FGuid InGuid = FGuid::NewGuid());

		const TArray<FInput<T>*>& GetConnections() const { return Connections; }
		TArray<FInput<T>* >& GetConnections() { return Connections; }

		virtual TArray<FConnectionTypeBase*> GetBaseInputs() override
		{ 
			TArray<FConnectionTypeBase*> RetList;
			RetList.Reserve(Connections.Num());
			for (FInput<T>* Ptr : Connections) { RetList.Add(Ptr); }
			return RetList;
		}


		virtual bool AddConnection(FConnectionTypeBase* InOutput) override
		{
			if (ensure(InOutput->GetType() == this->GetType()))
			{
				Connections.Add((FInput<T>*)InOutput);
				return true;
			}
			return false;
		}
		
		virtual bool RemoveConnection(FConnectionTypeBase* InInput) override { Connections.RemoveSwap((FInput<T>*)InInput); return true; }

		void SetValue(T InVal, const FContext& Context);

		T Evaluate(const FContext& Context);

		virtual void Invalidate() override;

	};
}

