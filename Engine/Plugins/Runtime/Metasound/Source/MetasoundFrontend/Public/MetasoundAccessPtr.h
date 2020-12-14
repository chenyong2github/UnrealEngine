// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Metasound
{
	namespace Frontend
	{
		class FAccessPoint;

		struct FAccessToken {};

		/** TAccessPtr
		 *
		 * TAccessPtr is used to determine whether an object has been destructed or not.
		 * It is useful when an object cannot be wrapped in a TSharedPtr. A TAccessPtr
		 * works similar to a TWeakPtr, but it cannot pin the object. Object pointers 
		 * held within a TAccessPtr should only be accessed on a thread where the objects
		 * cannot be destructed.
		 *
		 * If the TAccessPtr's underlying object is accessed when the pointer is invalid,
		 * a fallback object will be returned. 
		 *
		 * @tparam Type - The type of object to track.
		 */
		template<typename Type>
		class TAccessPtr
		{
		public:
			static Type FallbackObject;

			/** Create an invalid access pointer. */
			static TAccessPtr<Type> CreateInvalid()
			{
				TSharedPtr<FAccessToken> InvalidToken;
				return TAccessPtr<Type>(InvalidToken, FallbackObject);
			}

			/** Returns true if object is valid. Should only be called in same
			 * thread which created the token. */
			bool IsValid() const { return Token.IsValid(); }

			FORCEINLINE explicit operator bool() const
			{
				return IsValid();
			}

			Type* Get() const
			{
				if (IsValid())
				{
					return Object;
				}
				return nullptr;
			}

			Type& operator*() const
			{
				if (ensure(IsValid()))
				{
					return *Object;
				}

				return FallbackObject;
			}

			Type& operator->() const
			{
				if (ensure(IsValid()))
				{
					return *Object;
				}

				return FallbackObject;
			}

			TAccessPtr() = delete;

			TAccessPtr(const TAccessPtr<Type>& InOther) = default;
			TAccessPtr<Type>& operator=(const TAccessPtr<Type>& InOther) = default;

			TAccessPtr(TAccessPtr<Type>&& InOther) = default;
			TAccessPtr& operator=(TAccessPtr<Type>&& InOther) = default;

		private:
			template<typename RelatedType>
			friend TAccessPtr<RelatedType> MakeAccessPtr(const FAccessPoint& InAccessPoint, RelatedType& InRef);

			TWeakPtr<FAccessToken> Token;
			Type* Object;

			TAccessPtr(TWeakPtr<FAccessToken> AccessToken, Type& InRef)
			:	Token(AccessToken)
			,	Object(&InRef)
			{
			}
		};

		template<typename Type>
		Type TAccessPtr<Type>::FallbackObject = Type();

		/** FAccessPoint acts as a lifecycle tracker for the TAccessPtrs it creates. 
		 * When this object is destructed, all associated TAccessPtrs will become invalid.
		 */
		class FAccessPoint
		{
		public:

			FAccessPoint() 
			{
				Token = MakeShared<FAccessToken>();
			}

		private:
			template<typename Type>
			friend TAccessPtr<Type> MakeAccessPtr(const FAccessPoint& InAccessPoint, Type& InRef);

			FAccessPoint(const FAccessPoint&) = delete;
			FAccessPoint& operator=(const FAccessPoint&) = delete;
			FAccessPoint(FAccessPoint&&) = delete;
			FAccessPoint& operator=(FAccessPoint&&) = delete;

			TSharedPtr<FAccessToken> Token;
		};

		template<typename Type>
		TAccessPtr<Type> MakeAccessPtr(const FAccessPoint& InAccessPoint, Type& InRef)
		{
			return TAccessPtr<Type>(InAccessPoint.Token, InRef);
		}
	}
}
