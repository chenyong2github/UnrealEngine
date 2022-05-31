// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosArchive.h"
#include "Serialization/Archive.h"

namespace Dataflow
{
	class FNode;

	template<class T>
	struct DATAFLOWCORE_API TPropertyParameters {
		FName Name = FName("");
		T Value = T();
		FNode* Node = nullptr;
	};

	//
	// FParameter
	//
	class  DATAFLOWCORE_API FProperty 
	{
	public:
		enum class EType : uint8 {
			NONE = 0,
			BOOL,
			INT,
			FLOAT,
			DOUBLE,
			STRING,
			NAME,
			// <new types go here>
			MAX
		};


		FProperty(EType InType, FName InName, FNode* Node)
			: Type(InType)
			, Name(InName)
			, Category("")
		{
			BindProperty(Node, this);
		}
		virtual ~FProperty() {}


		const FName& GetName() const { return Name; }
		const EType GetType() const { return Type; }

		void GetCategory(const FName& InName) { Name = InName; }
		const FName& GetCategory() const { return Category; }

		void BindProperty(FNode* InNode, FProperty* That);

		virtual void Serialize(FArchive& Ar) {}

	private:
		EType Type;
		FName Name;
		FName Category;
	};

	//
	// TParameter
	//
	template<class T>
	class DATAFLOWCORE_API TProperty : public FProperty
	{
		T Value = T();
	public:

		TProperty(TPropertyParameters<T> InParam)
			: FProperty(StaticType(), InParam.Name, InParam.Node)
			, Value(InParam.Value)
		{}

		static FProperty::EType StaticType();

		const T& GetValue() const { return Value; }
		void SetValue(const T& InValue) { Value = InValue; }

		virtual void Serialize(FArchive& Ar) override
		{
			Ar << Value;
		}

	};

} // Dataflow

#define DATAFLOW_PROPERTY(DECL, TYPE,ETYPE)																			\
	template<> inline Dataflow::FProperty::EType Dataflow::TProperty<TYPE>::StaticType() { return EType::ETYPE; }	\
	template struct DECL Dataflow::TPropertyParameters<TYPE>;														\
	template class Dataflow::TProperty<TYPE>;

DATAFLOW_PROPERTY(DATAFLOWCORE_API, bool, BOOL)
DATAFLOW_PROPERTY(DATAFLOWCORE_API, int, INT)
DATAFLOW_PROPERTY(DATAFLOWCORE_API, float, FLOAT)
DATAFLOW_PROPERTY(DATAFLOWCORE_API, double, DOUBLE)
DATAFLOW_PROPERTY(DATAFLOWCORE_API, FString, STRING)
DATAFLOW_PROPERTY(DATAFLOWCORE_API, FName, NAME)
//DATAFLOW_PROPERTY(DATAFLOWCORE_API, Dataflow::FFilename, FILE)


