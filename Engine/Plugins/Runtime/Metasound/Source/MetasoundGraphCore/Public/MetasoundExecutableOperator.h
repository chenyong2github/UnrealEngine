// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundOperatorInterface.h"

namespace Metasound
{
	/** Convenience class for supporting the IOperator interface's GetExecuteFunction virtual member function.
	 *
	 * Dervied classes should inherit from this template class as well as implement a void Execute() member
	 * function. 
	 *
	 * 	class MyOperator : public TExecutableOperator<MyOperator>
	 * 	{
	 * 	  public:
	 * 	  ...
	 * 	  void Execute()
	 * 	  {
	 * 	     ...
	 * 	  }
	 * 	};
	 */
	template<class DerivedOperatorType>
	class TExecutableOperator : public IOperator
	{
		public:

			virtual ~TExecutableOperator() {}

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

	/** FExecutableOperatorWrapper 
	 *
	 * Wraps an IOperator and provides an Execute() member function.
	 */
	class FExecutableOperatorWrapper 
	{
		public:
			using FOperatorPtr = TUniquePtr<IOperator>;
			using FExecuteFunction = IOperator::FExecuteFunction;

			FExecutableOperatorWrapper(FOperatorPtr InOperator)
			:	Operator(MoveTemp(InOperator))
			,	ExecuteFunction(&FExecutableOperatorWrapper::NoOp)
			{
				if (Operator.IsValid())
				{
					FExecuteFunction Func = Operator->GetExecuteFunction();

					if (nullptr != Func)
					{
						ExecuteFunction = Func;
					}
				}
			}

			virtual ~FExecutableOperatorWrapper()
			{
			}

			void Execute()
			{
				ExecuteFunction(Operator.Get());
			}

			bool IsNoOp()
			{
				return (ExecuteFunction == &FExecutableOperatorWrapper::NoOp);
			}

		private:
			FOperatorPtr Operator;

			FExecuteFunction ExecuteFunction;

			static void NoOp(IOperator*)
			{
			}
	};
}
