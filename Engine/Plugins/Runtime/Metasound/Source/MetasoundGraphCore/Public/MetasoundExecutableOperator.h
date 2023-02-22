// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundOperatorInterface.h"

#include <type_traits>


namespace Metasound
{
	namespace MetasoundExecutableOperatorPrivate
	{
		// Helper template to determine whether a member function is declared
		// for a given template class.
		template <typename U>
		class TIsResetMethodDeclared 
		{
			private:
				template<typename T, T> 
				struct Helper;

				template<typename T>
				static uint8 Check(Helper<void(T::*)(const IOperator::FResetParams&), &T::Reset>*);

				template<typename T> static uint16 Check(...);

			public:
				static constexpr bool Value = sizeof(Check<U>(0)) == sizeof(uint8);
		};

		// Helper to determine whether an Reset(...) function exists. 
		//
		// Some nodes have an Execute() call without an Reset(...) call because
		// Reset(...) was introduced after several MetaSound releases. In order
		// allow operators to compile, we make the Intiailize(...) function optional
		// for subclasses of TExecutableOperator. This adapter determines whether
		// it Reset(...) exists or not.
		//
		// Note: This helper must be instantiated inside the functions and cannot
		// be part of the `TExecutableOperator<OperatorType>` class definition as
		// classes are not completely defined at the point in the compilation process
		// when static constexpr class members are evaluated. 
		template<typename OperatorType>
		struct TResetFunctionAdapter
		{
			// Returns an IOperator::FResetFunction if the OperatorType has a
			// Reset(...) class member function.
			static IOperator::FResetFunction GetResetFunction() 
			{
				if (TIsResetMethodDeclared<OperatorType>::Value)
				{
					return &TResetFunctionAdapter::ResetFunction;
				}
				else
				{
					return nullptr;
				}
			}

		private:
			static void ResetFunction(IOperator* InOperator, const IOperator::FResetParams& InParams)
			{
				if constexpr (TIsResetMethodDeclared<OperatorType>::Value)
				{
					OperatorType* DerivedOperator = static_cast<OperatorType*>(InOperator);

					check(nullptr != DerivedOperator);

					DerivedOperator->Reset(InParams);
				}
				else
				{
					checkNoEntry();
				}
			}
		};
	}

	// As a general rule, ExecutableDataTypes should be avoided whenever possible
	// as they incur an undesired cost and are generally not typically necessary.
	// This is primarily for the special case of trigger types, where state management
	// cannot be avoided (or rather an avoidable design has yet to be formulated).
	template<class DataType>
	struct TExecutableDataType
	{
		static constexpr bool bIsExecutable = false;

		static void Execute(const DataType& InData, const DataType& OutData)
		{
			// No-Op for base case as most DataTypes (ex POD) are not executable.
		}

		static void ExecuteInline(DataType& InData, bool bInUpdated)
		{
			// No-Op for base case as most DataTypes (ex POD) are not executable.
		}
	};

	/** Convenience class for supporting the IOperator interface's GetExecuteFunction virtual member function.
	 *
	 * Derived classes should inherit from this template class as well as implement a void Execute() member
	 * function.
	 *
	 * 	class MyOperator : public TExecutableOperator<MyOperator>
	 * 	{
	 * 	  public:
	 * 	  ...
	 * 	  void Execute()
	 * 	  {
	 *		  ...
	 * 	  }
	 * 	};
	 */

	template<class DerivedOperatorType>
	class TExecutableOperator : public IOperator
	{
	public:

		virtual ~TExecutableOperator() {}

		virtual FResetFunction GetResetFunction() override
		{
			return MetasoundExecutableOperatorPrivate::TResetFunctionAdapter<DerivedOperatorType>::GetResetFunction();
		}

		virtual FExecuteFunction GetExecuteFunction() override
		{
			return &TExecutableOperator<DerivedOperatorType>::ExecuteFunction;
		}

	private:

		static void ExecuteFunction(IOperator* InOperator)
		{
			DerivedOperatorType* DerivedOperator = static_cast<DerivedOperatorType*>(InOperator);

			check(nullptr != DerivedOperator);

			DerivedOperator->Execute();
		}
	};

	class FNoOpOperator : public IOperator
	{
		public:
			virtual ~FNoOpOperator() {}

			FExecuteFunction GetExecuteFunction() override
			{
				return nullptr;
			}

			FResetFunction GetResetFunction() override
			{
				return nullptr;
			}

			FDataReferenceCollection GetInputs() const override
			{
				return FDataReferenceCollection{};
			}

			FDataReferenceCollection GetOutputs() const override
			{
				return FDataReferenceCollection{};
			}
	};

	/** FExecuter
	 *
	 * Wraps an IOperator and provides an Execute() member function.
	 */
	class FExecuter : public IOperator
	{
		public:
			using FOperatorPtr = TUniquePtr<IOperator>;
			using FExecuteFunction = IOperator::FExecuteFunction;

			FExecuter()
			:	ExecuteFunction(&FExecuter::NoOpExecute)
			,	ResetFunction(&FExecuter::NoOpReset)
			{
			}

			FExecuter(FOperatorPtr InOperator)
			:	ExecuteFunction(&FExecuter::NoOpExecute)
			,	ResetFunction(&FExecuter::NoOpReset)
			{
				SetOperator(MoveTemp(InOperator));
			}

			void SetOperator(FOperatorPtr InOperator)
			{
				Operator = MoveTemp(InOperator);
				ExecuteFunction = nullptr;
				ResetFunction = nullptr;

				if (Operator.IsValid())
				{
					if (FExecuteFunction Func = Operator->GetExecuteFunction())
					{
						ExecuteFunction = Func;
					}

					if (FResetFunction Func = Operator->GetResetFunction())
					{
						ResetFunction = Func;
					}
				}

				if (!ExecuteFunction)
				{
					ExecuteFunction = &FExecuter::NoOpExecute;
				}

				if (!ResetFunction)
				{
					ResetFunction = &FExecuter::NoOpReset;
				}
			}

			void Execute()
			{
				// ExecuteFunction is always non-null. If the contained operator
				// does not provide a valid execution function, then a No-Op function
				// is utilized. 
				ExecuteFunction(Operator.Get());
			}

			void Reset(const IOperator::FResetParams& InParams)
			{
				// ResetFunction is always non-null. If the contained operator
				// does not provide a valid initialization function, then a No-Op function
				// is utilized. 
				ResetFunction(Operator.Get(), InParams);
			}

			bool IsNoOp()
			{
				return (ExecuteFunction == &FExecuter::NoOpExecute) && (ResetFunction == &FExecuter::NoOpReset);
			}

			bool IsValid() const
			{
				return Operator.IsValid();
			}

			virtual FDataReferenceCollection GetInputs() const override
			{
				static FDataReferenceCollection EmptyCollection;

				if (Operator.IsValid())
				{
					return Operator->GetInputs();
				}

				return EmptyCollection;
			}

			virtual FDataReferenceCollection GetOutputs() const override
			{
				static FDataReferenceCollection EmptyCollection;

				if (Operator.IsValid())
				{
					return Operator->GetOutputs();
				}

				return EmptyCollection;
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return ExecuteFunction;
			}

			virtual FResetFunction GetResetFunction() override
			{
				return ResetFunction;
			}

		private:
			FOperatorPtr Operator;

			FExecuteFunction ExecuteFunction;
			FResetFunction ResetFunction;

			static void NoOpExecute(IOperator*)
			{
			}

			static void NoOpReset(IOperator*, const IOperator::FResetParams&)
			{
			}
	};
}
