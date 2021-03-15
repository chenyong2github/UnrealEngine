// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataFactory.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundLog.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTrigger.h"
#include "Containers/CircularQueue.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "MetasoundFrontend"


namespace Metasound
{
	namespace MetasoundArrayNodesPrivate
	{
		// Convenience function for make FNodeClassMetadata of array nodes.
		METASOUNDFRONTEND_API FNodeClassMetadata CreateArrayNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface);

		// Retrieve the ElementType from an ArrayType
		template<typename ArrayType>
		struct TArrayElementType
		{
			// Default implementation has Type. 
		};

		// ElementType specialization for TArray types.
		template<typename ElementType>
		struct TArrayElementType<TArray<ElementType>>
		{
			using Type = ElementType;
		};
		
		// TODO: Remove me and swap after fixing FDataReferenceCollection::GetDataReadReferenceOrConstructWithVertexDefault
		//
		// This existing implementation of `GetDataReadReferenceOrConstructWithVertexDefault` does not automatically
		// utilize the set type of the FLiteral or the TDataReadReferenceLiteralFactory 
		// for that type. The implementation in FDataReferenceCollection should be replaced
		// with this implementation. It will require reworking of all nodes which
		// call `GetDataReadReferenceOrConstructWithVertexDefault`.
		template<typename DataType>
		TDataReadReference<DataType> GetDataReadReferenceOrConstructWithVertexDefault(const FDataReferenceCollection& InCollection, const FInputVertexInterface& InputVertices, const FString& InName, const FOperatorSettings& InSettings)
		{
			using FDataFactory = TDataReadReferenceLiteralFactory<DataType>;

			if (InCollection.ContainsDataReadReference<DataType>(InName))
			{
				return InCollection.GetDataReadReference<DataType>(InName);
			}
			else
			{
				// TODO: replace mechanism in data vertex to "CreateDefaultValue"
				// instead of get in order to avoid the need to have a `Clone` on 
				// FLiteral. Then move FLiteral when needed.
				const FLiteral& Literal = InputVertices[InName].GetDefaultValue();
				return FDataFactory::CreateExplicitArgs(InSettings, Literal);
			}
		}
	}

	namespace ArrayNodeVertexNames
	{
		/* Input Vertex Names */
		METASOUNDFRONTEND_API const FString& GetInputArrayName();
		METASOUNDFRONTEND_API const FString& GetInputLeftArrayName();
		METASOUNDFRONTEND_API const FString& GetInputRightArrayName();
		METASOUNDFRONTEND_API const FString& GetInputTriggerName();
		METASOUNDFRONTEND_API const FString& GetInputStartIndexName();
		METASOUNDFRONTEND_API const FString& GetInputEndIndexName();
		METASOUNDFRONTEND_API const FString& GetInputIndexName();
		METASOUNDFRONTEND_API const FString& GetInputValueName();

		/* Output Vertex Names */
		METASOUNDFRONTEND_API const FString& GetOutputNumName();
		METASOUNDFRONTEND_API const FString& GetOutputValueName();
		METASOUNDFRONTEND_API const FString& GetOutputArrayName();
	};

	/** TArrayNumOperator gets the number of elements in an Array. The operator
	 * uses the FNodeFacade and defines the vertex, metadata and vertex interface
	 * statically on the operator class. */
	template<typename ArrayType>
	class TArrayNumOperator : public TExecutableOperator<TArrayNumOperator<ArrayType>>
	{
	public:
		using FArrayDataReadReference = TDataReadReference<ArrayType>;

		// Declare the vertex interface
		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ArrayNodeVertexNames;

			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<ArrayType>(GetInputArrayName(), LOCTEXT("ArrayOpArrayNumInput", "Array to inspect."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<int32>(GetOutputNumName(), LOCTEXT("ArrayOpArrayNumOutput", "Number of elements in the array."))
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName DataTypeName = GetMetasoundDataTypeName<ArrayType>();
				FName OperatorName = TEXT("Num"); 
				FText NodeDisplayName = FText::Format(LOCTEXT("ArrayOpArrayNumDisplayNamePattern", "Num ({0})"), FText::FromString(GetMetasoundDataTypeString<ArrayType>()));
				FText NodeDescription = LOCTEXT("ArrayOpArrayNumDescription", "Number of elements in the array");
				FVertexInterface NodeInterface = GetDefaultInterface();
			
				return MetasoundArrayNodesPrivate::CreateArrayNodeClassMetadata(DataTypeName, OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};
			
			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();

			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace ArrayNodeVertexNames;
			using namespace MetasoundArrayNodesPrivate;

			const FInputVertexInterface& Inputs = InParams.Node.GetVertexInterface().GetInputInterface();

			// Get the input array or construct an empty one. 
			FArrayDataReadReference Array = GetDataReadReferenceOrConstructWithVertexDefault<ArrayType>(InParams.InputDataReferences, Inputs, GetInputArrayName(), InParams.OperatorSettings);

			return MakeUnique<TArrayNumOperator>(Array);
		}

		TArrayNumOperator(FArrayDataReadReference InArray)
		: Array(InArray)
		, Num(TDataWriteReference<int32>::CreateNew())
		{
			// Initialize value for downstream nodes.
			*Num = Array->Num();
		}

		virtual ~TArrayNumOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ArrayNodeVertexNames;

			FDataReferenceCollection Inputs;

			Inputs.AddDataReadReference(GetInputArrayName(), Array);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ArrayNodeVertexNames;

			FDataReferenceCollection Outputs;

			Outputs.AddDataReadReference(GetOutputNumName(), Num);

			return Outputs;
		}

		void Execute()
		{
			*Num = Array->Num();
		}

	private:

		FArrayDataReadReference Array;
		TDataWriteReference<int32> Num;
	};

	template<typename ArrayType>
	class TArrayNumNode : public FNodeFacade
	{
	public:
		TArrayNumNode(const FNodeInitData& InInitData)
		: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TArrayNumOperator<ArrayType>>())
		{
		}

		virtual ~TArrayNumNode() = default;
	};

	/** TArrayGetOperator copies a value from the array to the output when
	 * a trigger occurs. Initially, the output value is default constructed and
	 * will remain that way until until a trigger is encountered.
	 */
	template<typename ArrayType>
	class TArrayGetOperator : public TExecutableOperator<TArrayGetOperator<ArrayType>>
	{
	public:
		using FArrayDataReadReference = TDataReadReference<ArrayType>;
		using ElementType = typename MetasoundArrayNodesPrivate::TArrayElementType<ArrayType>::Type;

		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ArrayNodeVertexNames;
			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FTrigger>(GetInputTriggerName(), LOCTEXT("ArrayOpArrayGetTrigger", "Trigger to get value.")),
					TInputDataVertexModel<ArrayType>(GetInputArrayName(), LOCTEXT("ArrayOpArrayGetInput", "Input Array.")),
					TInputDataVertexModel<int32>(GetInputIndexName(), LOCTEXT("ArrayOpArrayGetIndex", "Index in Array."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<ElementType>(GetOutputValueName(), LOCTEXT("ArrayOpArrayGetOutput", "Value of element at array index."))
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName DataTypeName = GetMetasoundDataTypeName<ArrayType>();
				FName OperatorName = TEXT("Get"); 
				FText NodeDisplayName = FText::Format(LOCTEXT("ArrayOpArrayGetDisplayNamePattern", "Get ({0})"), FText::FromString(GetMetasoundDataTypeString<ArrayType>()));
				FText NodeDescription = LOCTEXT("ArrayOpArrayGetDescription", "Get element at index in array.");
				FVertexInterface NodeInterface = GetDefaultInterface();
			
				return MetasoundArrayNodesPrivate::CreateArrayNodeClassMetadata(DataTypeName, OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};
			
			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();

			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace ArrayNodeVertexNames;
			using namespace MetasoundArrayNodesPrivate;

			const FInputVertexInterface& Inputs = InParams.Node.GetVertexInterface().GetInputInterface();

			// Input Trigger
			TDataReadReference<FTrigger> Trigger = GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(InParams.InputDataReferences, Inputs, GetInputTriggerName(), InParams.OperatorSettings);
			
			// Input Array
			FArrayDataReadReference Array = GetDataReadReferenceOrConstructWithVertexDefault<ArrayType>(InParams.InputDataReferences, Inputs, GetInputArrayName(), InParams.OperatorSettings);

			// Input Index
			TDataReadReference<int32> Index = GetDataReadReferenceOrConstructWithVertexDefault<int32>(InParams.InputDataReferences, Inputs, GetInputIndexName(), InParams.OperatorSettings);

			return MakeUnique<TArrayGetOperator>(InParams.OperatorSettings, Trigger, Array, Index);
		}


		TArrayGetOperator(const FOperatorSettings& InSettings, TDataReadReference<FTrigger> InTrigger, FArrayDataReadReference InArray, TDataReadReference<int32> InIndex)
		: Trigger(InTrigger)
		, Array(InArray)
		, Index(InIndex)
		, Value(TDataWriteReferenceFactory<ElementType>::CreateAny(InSettings))
		{
		}

		virtual ~TArrayGetOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ArrayNodeVertexNames;

			FDataReferenceCollection Inputs;

			Inputs.AddDataReadReference(GetInputTriggerName(), Trigger);
			Inputs.AddDataReadReference(GetInputArrayName(), Array);
			Inputs.AddDataReadReference(GetInputIndexName(), Index);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ArrayNodeVertexNames;

			FDataReferenceCollection Outputs;

			Outputs.AddDataReadReference(GetOutputValueName(), Value);

			return Outputs;
		}

		void Execute()
		{
			// Only perform get on trigger.
			if (*Trigger)
			{
				const int32 IndexValue = *Index;
				const ArrayType& ArrayRef = *Array;

				if ((IndexValue >= 0) && (IndexValue < ArrayRef.Num()))
				{
					*Value = ArrayRef[IndexValue];
				}
				else
				{
					UE_LOG(LogMetasound, Error, TEXT("Attempt to get value at invalid index [ArraySize:%d, Index:%d]"), ArrayRef.Num(), IndexValue);
				}
			}
		}

	private:

		TDataReadReference<FTrigger> Trigger;
		FArrayDataReadReference Array;
		TDataReadReference<int32> Index;
		TDataWriteReference<ElementType> Value;
	};

	template<typename ArrayType>
	class TArrayGetNode : public FNodeFacade
	{
	public:
		TArrayGetNode(const FNodeInitData& InInitData)
		: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TArrayGetOperator<ArrayType>>())
		{
		}

		virtual ~TArrayGetNode() = default;
	};

	/** TArraySetOperator sets an element in an array to a specific value. */
	template<typename ArrayType>
	class TArraySetOperator : public TExecutableOperator<TArraySetOperator<ArrayType>>
	{
	public:
		using FArrayDataReadReference = TDataReadReference<ArrayType>;
		using FArrayDataWriteReference = TDataWriteReference<ArrayType>;
		using ElementType = typename MetasoundArrayNodesPrivate::TArrayElementType<ArrayType>::Type;

		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ArrayNodeVertexNames;
			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FTrigger>(GetInputTriggerName(), LOCTEXT("ArrayOpArraySetTrigger", "Trigger to set value.")),
					TInputDataVertexModel<ArrayType>(GetInputArrayName(), LOCTEXT("ArrayOpArraySetInput", "Input Array.")),
					TInputDataVertexModel<int32>(GetInputIndexName(), LOCTEXT("ArrayOpArraySetIndex", "Index in Array.")),
					TInputDataVertexModel<ElementType>(GetInputValueName(), LOCTEXT("ArrayOpArraySetElement", "Value to set"))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<ArrayType>(GetOutputArrayName(), LOCTEXT("ArrayOpArraySetOutput", "Array after setting."))
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName DataTypeName = GetMetasoundDataTypeName<ArrayType>();
				FName OperatorName = TEXT("Set"); 
				FText NodeDisplayName = FText::Format(LOCTEXT("ArrayOpArraySetDisplayNamePattern", "Set ({0})"), FText::FromString(GetMetasoundDataTypeString<ArrayType>()));
				FText NodeDescription = LOCTEXT("ArrayOpArraySetDescription", "Set element at index in array.");
				FVertexInterface NodeInterface = GetDefaultInterface();
			
				return MetasoundArrayNodesPrivate::CreateArrayNodeClassMetadata(DataTypeName, OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};
			
			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();

			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace ArrayNodeVertexNames;
			using namespace MetasoundArrayNodesPrivate;

			const FInputVertexInterface& Inputs = InParams.Node.GetVertexInterface().GetInputInterface();
			
			TDataReadReference<FTrigger> Trigger = GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(InParams.InputDataReferences, Inputs, GetInputTriggerName(), InParams.OperatorSettings);

			FArrayDataReadReference InitArray = GetDataReadReferenceOrConstructWithVertexDefault<ArrayType>(InParams.InputDataReferences, Inputs, GetInputArrayName(), InParams.OperatorSettings);
			FArrayDataWriteReference Array = TDataWriteReferenceFactory<ArrayType>::CreateExplicitArgs(InParams.OperatorSettings, *InitArray);

			TDataReadReference<int32> Index = GetDataReadReferenceOrConstructWithVertexDefault<int32>(InParams.InputDataReferences, Inputs, GetInputIndexName(), InParams.OperatorSettings);

			TDataReadReference<ElementType> Value = GetDataReadReferenceOrConstructWithVertexDefault<ElementType>(InParams.InputDataReferences, Inputs, GetInputValueName(), InParams.OperatorSettings);

			return MakeUnique<TArraySetOperator>(InParams.OperatorSettings, Trigger, InitArray, Array, Index, Value);
		}


		TArraySetOperator(const FOperatorSettings& InSettings, TDataReadReference<FTrigger> InTrigger, FArrayDataReadReference InInitArray, FArrayDataWriteReference InArray, TDataReadReference<int32> InIndex, TDataReadReference<ElementType> InValue)
		: OperatorSettings(InSettings)
		, Trigger(InTrigger)
		, InitArray(InInitArray)
		, Array(InArray)
		, Index(InIndex)
		, Value(InValue)
		{
		}

		virtual ~TArraySetOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ArrayNodeVertexNames;
			FDataReferenceCollection Inputs;

			Inputs.AddDataReadReference(GetInputTriggerName(), Trigger);
			Inputs.AddDataReadReference(GetInputArrayName(), InitArray);
			Inputs.AddDataReadReference(GetInputIndexName(), Index);
			Inputs.AddDataReadReference(GetInputValueName(), Value);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ArrayNodeVertexNames;
			FDataReferenceCollection Outputs;

			Outputs.AddDataReadReference(GetOutputArrayName(), Array);

			return Outputs;
		}

		void Execute()
		{
			if (*Trigger)
			{
				const int32 IndexValue = *Index;
				ArrayType& ArrayRef = *Array;

				if ((IndexValue >= 0) && (IndexValue < ArrayRef.Num()))
				{
					ArrayRef[IndexValue] = *Value;
				}
				else
				{
					UE_LOG(LogMetasound, Error, TEXT("Attempt to set value at invalid index [ArraySize:%d, Index:%d]"), ArrayRef.Num(), IndexValue);
				}
			}
		}

	private:
		FOperatorSettings OperatorSettings;

		TDataReadReference<FTrigger> Trigger;
		FArrayDataReadReference InitArray;
		FArrayDataWriteReference Array;
		TDataReadReference<int32> Index;
		TDataReadReference<ElementType> Value;
	};

	template<typename ArrayType>
	class TArraySetNode : public FNodeFacade
	{
	public:
		TArraySetNode(const FNodeInitData& InInitData)
		: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TArraySetOperator<ArrayType>>())
		{
		}

		virtual ~TArraySetNode() = default;
	};

	/** TArrayConcatOperator concatenates two arrays on trigger. */
	template<typename ArrayType>
	class TArrayConcatOperator : public TExecutableOperator<TArrayConcatOperator<ArrayType>>
	{
	public:
		using FArrayDataReadReference = TDataReadReference<ArrayType>;
		using FArrayDataWriteReference = TDataWriteReference<ArrayType>;
		using ElementType = typename MetasoundArrayNodesPrivate::TArrayElementType<ArrayType>::Type;

		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ArrayNodeVertexNames;

			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FTrigger>(GetInputTriggerName(), LOCTEXT("ArrayOpArrayConcatTrigger", "Trigger to set value.")),
					TInputDataVertexModel<ArrayType>(GetInputLeftArrayName(), LOCTEXT("ArrayOpArrayConcatInputLeft", "Input Left Array.")),
					TInputDataVertexModel<ArrayType>(GetInputRightArrayName(), LOCTEXT("ArrayOpArrayConcatInputRight", "Input Left Array."))
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<ArrayType>(GetOutputArrayName(), LOCTEXT("ArrayOpArrayConcatOutput", "Array after setting."))
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName DataTypeName = GetMetasoundDataTypeName<ArrayType>();
				FName OperatorName = TEXT("Concat"); 
				FText NodeDisplayName = FText::Format(LOCTEXT("ArrayOpArrayConcatDisplayNamePattern", "Concatenate ({0})"), FText::FromString(GetMetasoundDataTypeString<ArrayType>()));
				FText NodeDescription = LOCTEXT("ArrayOpArrayConcatDescription", "Concatenates two arrays on trigger.");
				FVertexInterface NodeInterface = GetDefaultInterface();
			
				return MetasoundArrayNodesPrivate::CreateArrayNodeClassMetadata(DataTypeName, OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};
			
			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();

			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace ArrayNodeVertexNames;
			using namespace MetasoundArrayNodesPrivate;

			const FInputVertexInterface& Inputs = InParams.Node.GetVertexInterface().GetInputInterface();
			
			TDataReadReference<FTrigger> Trigger = GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(InParams.InputDataReferences, Inputs, GetInputTriggerName(), InParams.OperatorSettings);

			FArrayDataReadReference LeftArray = GetDataReadReferenceOrConstructWithVertexDefault<ArrayType>(InParams.InputDataReferences, Inputs, GetInputLeftArrayName(), InParams.OperatorSettings);
			FArrayDataReadReference RightArray = GetDataReadReferenceOrConstructWithVertexDefault<ArrayType>(InParams.InputDataReferences, Inputs, GetInputRightArrayName(), InParams.OperatorSettings);

			FArrayDataWriteReference OutArray = TDataWriteReferenceFactory<ArrayType>::CreateExplicitArgs(InParams.OperatorSettings);

			return MakeUnique<TArrayConcatOperator>(Trigger, LeftArray, RightArray, OutArray);
		}


		TArrayConcatOperator(TDataReadReference<FTrigger> InTrigger, FArrayDataReadReference InLeftArray, FArrayDataReadReference InRightArray, FArrayDataWriteReference InOutArray)
		: Trigger(InTrigger)
		, LeftArray(InLeftArray)
		, RightArray(InRightArray)
		, OutArray(InOutArray)
		{
		}

		virtual ~TArrayConcatOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ArrayNodeVertexNames;
			FDataReferenceCollection Inputs;

			Inputs.AddDataReadReference(GetInputTriggerName(), Trigger);
			Inputs.AddDataReadReference(GetInputLeftArrayName(), LeftArray);
			Inputs.AddDataReadReference(GetInputRightArrayName(), RightArray);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ArrayNodeVertexNames;
			FDataReferenceCollection Outputs;

			Outputs.AddDataReadReference(GetOutputArrayName(), OutArray);

			return Outputs;
		}

		void Execute()
		{
			if (*Trigger)
			{
				*OutArray = *LeftArray;
				OutArray->Append(*RightArray);
			}
		}

	private:

		TDataReadReference<FTrigger> Trigger;
		FArrayDataReadReference LeftArray;
		FArrayDataReadReference RightArray;
		FArrayDataWriteReference OutArray;
	};

	template<typename ArrayType>
	class TArrayConcatNode : public FNodeFacade
	{
	public:
		TArrayConcatNode(const FNodeInitData& InInitData)
		: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TArrayConcatOperator<ArrayType>>())
		{
		}

		virtual ~TArrayConcatNode() = default;
	};

	/** TArraySubsetOperator slices an array on trigger. */
	template<typename ArrayType>
	class TArraySubsetOperator : public TExecutableOperator<TArraySubsetOperator<ArrayType>>
	{
	public:
		using FArrayDataReadReference = TDataReadReference<ArrayType>;
		using FArrayDataWriteReference = TDataWriteReference<ArrayType>;
		using ElementType = typename MetasoundArrayNodesPrivate::TArrayElementType<ArrayType>::Type;

		static const FVertexInterface& GetDefaultInterface()
		{
			using namespace ArrayNodeVertexNames;

			static const FVertexInterface DefaultInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FTrigger>(GetInputTriggerName(), LOCTEXT("ArrayOpArraySubsetTrigger", "Trigger to set value.")),
					TInputDataVertexModel<ArrayType>(GetInputArrayName(), LOCTEXT("ArrayOpArraySubsetInputLeft", "Input Array.")),
					TInputDataVertexModel<int32>(GetInputStartIndexName(), LOCTEXT("ArrayOpArraySubsetStartIndex", "First index to include.")),
					TInputDataVertexModel<int32>(GetInputEndIndexName(), LOCTEXT("ArrayOpArraySubsetEndIndex", "Last index to include."))

				),
				FOutputVertexInterface(
					TOutputDataVertexModel<ArrayType>(GetOutputArrayName(), LOCTEXT("ArrayOpArraySubsetOutput", "Subset of input array."))
				)
			);

			return DefaultInterface;
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto CreateNodeClassMetadata = []() -> FNodeClassMetadata
			{
				FName DataTypeName = GetMetasoundDataTypeName<ArrayType>();
				FName OperatorName = TEXT("Subset"); 
				FText NodeDisplayName = FText::Format(LOCTEXT("ArrayOpArraySubsetDisplayNamePattern", "Subset ({0})"), FText::FromString(GetMetasoundDataTypeString<ArrayType>()));
				FText NodeDescription = LOCTEXT("ArrayOpArraySubsetDescription", "Subset array on trigger.");
				FVertexInterface NodeInterface = GetDefaultInterface();
			
				return MetasoundArrayNodesPrivate::CreateArrayNodeClassMetadata(DataTypeName, OperatorName, NodeDisplayName, NodeDescription, NodeInterface);
			};
			
			static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();

			return Metadata;
		}

		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors)
		{
			using namespace ArrayNodeVertexNames;
			using namespace MetasoundArrayNodesPrivate;

			const FInputVertexInterface& Inputs = InParams.Node.GetVertexInterface().GetInputInterface();
			
			TDataReadReference<FTrigger> Trigger = GetDataReadReferenceOrConstructWithVertexDefault<FTrigger>(InParams.InputDataReferences, Inputs, GetInputTriggerName(), InParams.OperatorSettings);

			FArrayDataReadReference InArray = GetDataReadReferenceOrConstructWithVertexDefault<ArrayType>(InParams.InputDataReferences, Inputs, GetInputArrayName(), InParams.OperatorSettings);

			TDataReadReference<int32> StartIndex = GetDataReadReferenceOrConstructWithVertexDefault<int32>(InParams.InputDataReferences, Inputs, GetInputStartIndexName(), InParams.OperatorSettings);
			TDataReadReference<int32> EndIndex = GetDataReadReferenceOrConstructWithVertexDefault<int32>(InParams.InputDataReferences, Inputs, GetInputEndIndexName(), InParams.OperatorSettings);

			FArrayDataWriteReference OutArray = TDataWriteReferenceFactory<ArrayType>::CreateExplicitArgs(InParams.OperatorSettings);

			return MakeUnique<TArraySubsetOperator>(Trigger, InArray, StartIndex, EndIndex, OutArray);
		}


		TArraySubsetOperator(TDataReadReference<FTrigger> InTrigger, FArrayDataReadReference InInputArray, TDataReadReference<int32> InStartIndex, TDataReadReference<int32> InEndIndex, FArrayDataWriteReference InOutputArray)
		: Trigger(InTrigger)
		, InputArray(InInputArray)
		, StartIndex(InStartIndex)
		, EndIndex(InEndIndex)
		, OutputArray(InOutputArray)
		{
		}

		virtual ~TArraySubsetOperator() = default;

		virtual FDataReferenceCollection GetInputs() const override
		{
			using namespace ArrayNodeVertexNames;

			FDataReferenceCollection Inputs;

			Inputs.AddDataReadReference(GetInputTriggerName(), Trigger);
			Inputs.AddDataReadReference(GetInputArrayName(), InputArray);
			Inputs.AddDataReadReference(GetInputStartIndexName(), StartIndex);
			Inputs.AddDataReadReference(GetInputEndIndexName(), EndIndex);

			return Inputs;
		}

		virtual FDataReferenceCollection GetOutputs() const override
		{
			using namespace ArrayNodeVertexNames;

			FDataReferenceCollection Outputs;

			Outputs.AddDataReadReference(GetOutputArrayName(), OutputArray);

			return Outputs;
		}

		void Execute()
		{
			if (*Trigger)
			{
				OutputArray->Reset();

				const ArrayType& InputArrayRef = *InputArray;
				const int32 StartIndexValue = FMath::Max(0, *StartIndex);
				const int32 EndIndexValue = FMath::Min(InputArrayRef.Num(), *EndIndex + 1);

				if (StartIndexValue < EndIndexValue)
				{
					const int32 Num = EndIndexValue - StartIndexValue;
					OutputArray->Append(&InputArrayRef[StartIndexValue], Num);
				}
			}
		}

	private:

		TDataReadReference<FTrigger> Trigger;
		FArrayDataReadReference InputArray;
		TDataReadReference<int32> StartIndex;
		TDataReadReference<int32> EndIndex;
		FArrayDataWriteReference OutputArray;
	};

	template<typename ArrayType>
	class TArraySubsetNode : public FNodeFacade
	{
	public:
		TArraySubsetNode(const FNodeInitData& InInitData)
		: FNodeFacade(InInitData.InstanceName, InInitData.InstanceID, TFacadeOperatorClass<TArraySubsetOperator<ArrayType>>())
		{
		}

		virtual ~TArraySubsetNode() = default;
	};
}

#undef LOCTEXT_NAMESPACE
