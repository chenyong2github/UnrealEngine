// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundArrayNodes.h"
#include "MetasoundNodeRegistrationMacro.h"

#include <type_traits>

namespace Metasound
{

	namespace MetasoundArrayNodesPrivate
	{
		// TArrayNodeSupport acts as a configuration sturct to determine whether
		// a particular TArrayNode can be instantiated for a specific ArrayType.
		//
		// Some ArrayNodes require that the array elements have certain properties
		// such as default element constructors, element copy constructors, etc.
		template<typename ArrayType>
		struct TArrayNodeSupport
		{
			using ElementType = typename TArrayElementType<ArrayType>::Type;
			
			// Array num is supported for all array types.
			static constexpr bool bIsArrayNumSupported = true;

			// Element must be default parsable to create get operator because a
			// value must be returned even if the index is invalid. Also values are
			// assigned by copy.
			static constexpr bool bIsArrayGetSupported = TIsParsable<ElementType>::Value && std::is_copy_assignable<ElementType>::value;

			// Element must be copy assignable to set the value.
			static constexpr bool bIsArraySetSupported = std::is_copy_assignable<ElementType>::value && std::is_copy_constructible<ElementType>::value;

			// Elements must be copy constructible
			static constexpr bool bIsArrayConcatSupported = std::is_copy_constructible<ElementType>::value;

			// Elements must be copy constructible
			static constexpr bool bIsArraySubsetSupported = std::is_copy_constructible<ElementType>::value;
		};

		template<typename ArrayType, typename std::enable_if<TArrayNodeSupport<ArrayType>::bIsArrayGetSupported, bool>::type = true>
		bool RegisterArrayGetNode()
		{
			using FNodeType = typename Metasound::TArrayGetNode<ArrayType>;
			return RegisterNodeWithFrontend<FNodeType>();
		}

		template<typename ArrayType, typename std::enable_if<!TArrayNodeSupport<ArrayType>::bIsArrayGetSupported, bool>::type = true>
		bool RegisterArrayGetNode()
		{
			// No op if not supported
			return true;
		}

		template<typename ArrayType, typename std::enable_if<TArrayNodeSupport<ArrayType>::bIsArraySetSupported, bool>::type = true>
		bool RegisterArraySetNode()
		{
			using FNodeType = typename Metasound::TArraySetNode<ArrayType>;

			static_assert(TArrayNodeSupport<ArrayType>::bIsArraySetSupported, "TArraySetNode<> is not supported by array type");

			return RegisterNodeWithFrontend<FNodeType>();
		}

		template<typename ArrayType, typename std::enable_if<!TArrayNodeSupport<ArrayType>::bIsArraySetSupported, bool>::type = true>
		bool RegisterArraySetNode()
		{
			// No op if not supported
			return true;
		}

		template<typename ArrayType, typename std::enable_if<TArrayNodeSupport<ArrayType>::bIsArraySubsetSupported, bool>::type = true>
		bool RegisterArraySubsetNode()
		{
			using FNodeType = typename Metasound::TArraySubsetNode<ArrayType>;

			static_assert(TArrayNodeSupport<ArrayType>::bIsArraySubsetSupported, "TArraySubsetNode<> is not supported by array type");

			return RegisterNodeWithFrontend<FNodeType>();
		}

		template<typename ArrayType, typename std::enable_if<!TArrayNodeSupport<ArrayType>::bIsArraySubsetSupported, bool>::type = true>
		bool RegisterArraySubsetNode()
		{
			// No op if not supported
			return true;
		}

		template<typename ArrayType, typename std::enable_if<TArrayNodeSupport<ArrayType>::bIsArrayConcatSupported, bool>::type = true>
		bool RegisterArrayConcatNode()
		{
			using FNodeType = typename Metasound::TArrayConcatNode<ArrayType>;

			static_assert(TArrayNodeSupport<ArrayType>::bIsArrayConcatSupported, "TArrayConcatNode<> is not supported by array type");

			return RegisterNodeWithFrontend<FNodeType>();
		}

		template<typename ArrayType, typename std::enable_if<!TArrayNodeSupport<ArrayType>::bIsArrayConcatSupported, bool>::type = true>
		bool RegisterArrayConcatNode()
		{
			// No op if not supported
			return true;
		}

		template<typename ArrayType>
		bool RegisterArrayNumNode()
		{
			static_assert(TArrayNodeSupport<ArrayType>::bIsArrayNumSupported, "TArrayNumNode<> is not supported by array type");

			return ensureAlways(RegisterNodeWithFrontend<Metasound::TArrayNumNode<ArrayType>>());
		}
	}

	/** Registers all available array nodes which can be instantiated for the given
	 * ArrayType. Some nodes cannot be instantiated due to limitations of the 
	 * array elements.
	 */
	template<typename ArrayType>
	bool RegisterArrayNodes()
	{
		using namespace MetasoundArrayNodesPrivate;

		bool bSuccess = RegisterArrayNumNode<ArrayType>();
		bSuccess = bSuccess && RegisterArrayGetNode<ArrayType>();
		bSuccess = bSuccess && RegisterArraySetNode<ArrayType>();
		bSuccess = bSuccess && RegisterArraySubsetNode<ArrayType>();
		bSuccess = bSuccess && RegisterArrayConcatNode<ArrayType>();

		return bSuccess;
	}
}
