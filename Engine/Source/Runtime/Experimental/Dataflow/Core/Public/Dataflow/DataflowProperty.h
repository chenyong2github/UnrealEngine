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


		FProperty(EType InType, FName InName, FNode* Node = nullptr)
			: Type(InType)
			, Name(InName)
			, Category("")
		{
			BindProperty(Node);
		}
		virtual ~FProperty() {}

		static FProperty* NewProperty(EType InType,FName InName = FName(""), FNode* InNode = nullptr);

		const FName& GetName() const { return Name; }
		const EType GetType() const { return Type; }

		virtual SIZE_T SizeOf() const { return 0; }

		void GetCategory(const FName& InName) { Name = InName; }
		const FName& GetCategory() const { return Category; }

		void BindProperty(FNode* InNode);

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
		virtual SIZE_T SizeOf() const override final;

		const T& GetValue() const { return Value; }
		void SetValue(const T& InValue) { Value = InValue; }

		virtual void Serialize(FArchive& Ar) override final
		{
			Ar << Value;
		}

	};

} // Dataflow

#define DATAFLOW_PROPERTY(DECL, TYPE,ETYPE, SIZEOF)																	\
	template<> inline Dataflow::FProperty::EType Dataflow::TProperty<TYPE>::StaticType() { return EType::ETYPE; }	\
	template<> inline SIZE_T Dataflow::TProperty<TYPE>::SizeOf() const {return SIZEOF;}								\
	template struct DECL Dataflow::TPropertyParameters<TYPE>;														\
	template class Dataflow::TProperty<TYPE>;

DATAFLOW_PROPERTY(DATAFLOWCORE_API, bool, BOOL, sizeof(bool))
DATAFLOW_PROPERTY(DATAFLOWCORE_API, int32, INT, sizeof(int32))
DATAFLOW_PROPERTY(DATAFLOWCORE_API, float, FLOAT, sizeof(float))
DATAFLOW_PROPERTY(DATAFLOWCORE_API, double, DOUBLE, sizeof(double))
DATAFLOW_PROPERTY(DATAFLOWCORE_API, FString, STRING, Value.GetAllocatedSize())
DATAFLOW_PROPERTY(DATAFLOWCORE_API, FName, NAME, Value.ToString().GetAllocatedSize())

