// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundBuilderInterface.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundVertex.h"
#include <type_traits>

namespace Metasound
{
	// Helper template to determine whether a static class function is declared
	// for a given template class.
	template <typename U>
	class TIsFactoryMethodDeclared
	{
		private:
			template<typename T, T> 
			struct Helper;

			// Check for "static TUniquePtr<IOperator> U::CreateOperator(const FCreateOperatorParams& Inparams, FBuildErrorArray& OutErrors)"
			template<typename T>
			static uint8 Check(Helper<TUniquePtr<IOperator>(*)(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors), &T::CreateOperator>*);

			template<typename T> static uint16 Check(...);

		public:

			// If the function exists, then "Value" is true. Otherwise "Value" is false.
			static constexpr bool Value = sizeof(Check<U>(0)) == sizeof(uint8_t);
	};

	// Helper template to determine whether a static class function is declared
	// for a given template class.
	template <typename U>
	class TIsNodeInfoDeclared
	{
		private:
			template<typename T, T> 
			struct Helper;

			// Check for "static const FNodeInfo& U::CreateOperator(const FCreateOperatorParams& Inparams, FBuildErrorArray& OutErrors)"
			template<typename T>
			static uint8 Check(Helper<const FNodeInfo&(*)(), &T::GetNodeInfo>*);

			template<typename T> static uint16 Check(...);

		public:

			// If the function exists, then "Value" is true. Otherwise "Value" is false.
			static constexpr bool Value = sizeof(Check<U>(0)) == sizeof(uint8_t);
	};

	/** TFacadeOperatorClass encapsulates an operator type and checks that the
	 * required static functions exist to build the facade operator class.  It 
	 * is required to call the FNodeFacade template constructor.
	 */
	template<typename OperatorType>
	struct TFacadeOperatorClass
	{
		// Require that OperatorType is subclass of IOperator
		static_assert(std::is_base_of<IOperator, OperatorType>::value, "To use the FNodeFacade consturctor, the OperatorType must be derived from IOperator");

		// Require static TUniquePtr<IOperator> OperatorType::CreateOperator(const FCreateOperatorParams&, TArray<TUniquePtr<IOperatorBuildError>>&) exists.
		static_assert(TIsFactoryMethodDeclared<OperatorType>::Value, "To use the FNodeFacade constructor, the OperatorType must have the static function \"static TUniquePtr<IOperator> OperatorType::CreateOperator(const FCreateOperatorParams&, TArray<TUniquePtr<IOperatorBuildError>>&)\"");

		// Require static const FNodeInfo& OperatorType::GetNodeInfo() exists.
		static_assert(TIsNodeInfoDeclared<OperatorType>::Value, "To use the FNodeFacade constructor, the OperatorType must have the static function \"static const FNodeInfo& OperatorType::GetNodeInfo()\"");

		static const FNodeInfo& GetNodeInfo() 
		{ 
			return OperatorType::GetNodeInfo(); 
		}

		static TUniquePtr<TUniquePtr<IOperator>> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) 
		{
			return OperatorType::CreateOperator(InParams, OutErrors);
		}

		typedef OperatorType Type;
	};

	/** FNodeFacade implements a significant amount of boilerplate code required
	 * to build a Metasound INode. FNodeFacade is particularly useful for an INode
	 * which has a static FVertexInterface, and always creates the same IOperator type.
	 * 
	 * The type of the concrete IOperator class to instantiate is defined in the
	 * FNodeFacade constructors TFacadeOperatorClass<>.
	 */
	class METASOUNDGRAPHCORE_API FNodeFacade : public FNode
	{
			// Factory class for create an IOperator by using a TFunction matching
			// the signature of the CreateOperator function.
			class METASOUNDGRAPHCORE_API FFactory : public IOperatorFactory
			{
				using FCreateOperatorFunction = TFunction<TUniquePtr<IOperator>(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)>;

				public:
					FFactory(FCreateOperatorFunction InCreateFunc);

					virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override;

				private:
					FCreateOperatorFunction CreateFunc;
			};

		public: 

			FNodeFacade() = delete;

			/** FNodeFacade constructor using the OperatorType template parameter
			 * to get the node info, operator factory method and vertex interface. 
			 *
			 * @param InInstanceName - Instance name for the node.
			 * @param OperatorClass - Template class wrapper for the underlying 
			 * 						  IOperator which is created by this node.
			 */
			template<typename OperatorType>
			FNodeFacade(const FString& InInstanceName, TFacadeOperatorClass<OperatorType> OperatorClass)
			:	FNode(InInstanceName, OperatorType::GetNodeInfo())
			, 	Factory(MakeShared<FFactory, ESPMode::ThreadSafe>(&OperatorType::CreateOperator))
			{
				VertexInterface = GetMetadata().DefaultInterface;
			}

			virtual ~FNodeFacade() = default;

			virtual const FVertexInterface& GetVertexInterface() const override;

			/** FNodeFacade has a static vertex interface. This will return true
			 * only if the input interface is equal to the default interface.
			 */
			virtual bool SetVertexInterface(const FVertexInterface& InInterface) override;

			/** FNodeFacade has a static vertex interface. This will return true
			 * only if the input interface is equal to the default interface.
			 */
			virtual bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const override;

			/** Return a reference to the default operator factory. */
			virtual FOperatorFactorySharedRef GetDefaultOperatorFactory() const override;

		private:

			TSharedRef<IOperatorFactory, ESPMode::ThreadSafe> Factory;

			FVertexInterface VertexInterface;
	};
}
