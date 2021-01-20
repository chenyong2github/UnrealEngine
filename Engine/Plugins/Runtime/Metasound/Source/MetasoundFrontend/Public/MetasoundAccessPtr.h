// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UnrealTemplate.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#define METASOUND_FRONTEND_ACCESSPTR_DEBUG_INFO 1
#else
#define METASOUND_FRONTEND_ACCESSPTR_DEBUG_INFO 0
#endif
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
		 * held within a TAccessPtr must only be accessed on the thread where the objects
		 * gets destructed to avoid having the object destructed while in use.
		 *
		 * If the TAccessPtr's underlying object is accessed when the pointer is invalid,
		 * a fallback object will be returned. 
		 *
		 * @tparam Type - The type of object to track.
		 */
		template<typename Type>
		class TAccessPtr
		{
			enum EConstCast { Tag };

		public:
			using FTokenType = FAccessToken;

			static Type FallbackObject;

			/** Returns true if object is valid. Should only be called in same
			 * thread which created the token. */
			bool IsValid() const { return (Get() != nullptr); }

			FORCEINLINE explicit operator bool() const
			{
				return IsValid();
			}

			Type* Get() const
			{
#if METASOUND_FRONTEND_ACCESSPTR_DEBUG_INFO 
				CachedObjectPtr = GetObject();
#endif
				return GetObject();
			}

			Type& operator*() const
			{
				if (Type* Object = Get())
				{
					return *Object;
				}

				checkNoEntry();
				return FallbackObject;
			}

			Type* operator->() const
			{
				if (Type* Object = Get())
				{
					return Object;
				}

				return &FallbackObject;
			}

			template<typename MemberType>
			TAccessPtr<MemberType> GetMemberAccessPtr(TFunction<MemberType*(Type&)> InGetMember) const
			{
				TFunction<MemberType*()> GetMemberFromObject = [=]() -> MemberType*
				{
					if (Type* Object = Get())
					{
						return InGetMember(*Object);
					}
					return static_cast<MemberType*>(nullptr);
				};

				return TAccessPtr<MemberType>(GetMemberFromObject);
			}


			TAccessPtr()
			//: Object(nullptr)
			: GetObject([]() { return static_cast<Type*>(nullptr); })
			{
#if METASOUND_FRONTEND_ACCESSPTR_DEBUG_INFO 
				Get();
#endif
			}


			template<typename OtherType>
			TAccessPtr(const TAccessPtr<OtherType>& InOther, EConstCast InTag)
			//: Token(InOther.Token)
			//, Object(const_cast<Type*>(InOther.Object))
			{
				TFunction<OtherType*()> OtherGetObject = InOther.GetObject;
				GetObject = [=]() -> Type*
				{
					return const_cast<Type*>(OtherGetObject());
				};
#if METASOUND_FRONTEND_ACCESSPTR_DEBUG_INFO 
				Get();
#endif
			} 

			template <
				typename OtherType,
				typename = decltype(ImplicitConv<Type*>((OtherType*)nullptr))
			>
			TAccessPtr(const TAccessPtr<OtherType>& InOther)
			//: Token(InOther.Token)
			//, Object(InOther.Object)
			{
				TFunction<OtherType*()> OtherGetObject = InOther.GetObject;

				GetObject = [=]() -> Type*
				{
					return static_cast<Type*>(OtherGetObject());
				};
#if METASOUND_FRONTEND_ACCESSPTR_DEBUG_INFO 
				Get();
#endif
			}

			TAccessPtr(const TAccessPtr<Type>& InOther) = default;
			TAccessPtr<Type>& operator=(const TAccessPtr<Type>& InOther) = default;

			TAccessPtr(TAccessPtr<Type>&& InOther) = default;
			TAccessPtr& operator=(TAccessPtr<Type>&& InOther) = default;

		private:
			template<typename RelatedType>
			friend TAccessPtr<RelatedType> MakeAccessPtr(const FAccessPoint& InAccessPoint, RelatedType& InRef);

			template<typename ToType, typename FromType> 
			friend TAccessPtr<ToType> ConstCastAccessPtr(const TAccessPtr<FromType>& InAccessPtr);

			template<typename OtherType>
			friend class TAccessPtr;

#if METASOUND_FRONTEND_ACCESSPTR_DEBUG_INFO 
			mutable Type* CachedObjectPtr = nullptr;
#endif
			TFunction<Type*()> GetObject;

			TAccessPtr(TFunction<Type*()> InGetObject)
			: GetObject(InGetObject)
			{
#if METASOUND_FRONTEND_ACCESSPTR_DEBUG_INFO 
				Get();
#endif
			}

			TAccessPtr(TWeakPtr<FTokenType> AccessToken, Type& InRef)
			{
				Type* RefPtr = &InRef;

				GetObject = [=]() -> Type*
				{
					Type* Object = nullptr;
					if (AccessToken.IsValid())
					{
						Object = RefPtr;
					}
					return Object;
				};
#if METASOUND_FRONTEND_ACCESSPTR_DEBUG_INFO 
				Get();
#endif
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
			using FTokenType = FAccessToken;

			FAccessPoint() 
			{
				Token = MakeShared<FTokenType>();
			}

			FAccessPoint(const FAccessPoint&) 
			{
				// Do not copy token from other access point on copy.
				Token = MakeShared<FTokenType>();
			}

			// Do not copy token from other access point on assignment
			FAccessPoint& operator=(const FAccessPoint&) 
			{
				return *this;
			}

		private:
			template<typename Type>
			friend TAccessPtr<Type> MakeAccessPtr(const FAccessPoint& InAccessPoint, Type& InRef);

			FAccessPoint(FAccessPoint&&) = delete;
			FAccessPoint& operator=(FAccessPoint&&) = delete;

			TSharedPtr<FTokenType> Token;
		};

		template<typename Type>
		TAccessPtr<Type> MakeAccessPtr(const FAccessPoint& InAccessPoint, Type& InRef)
		{
			return TAccessPtr<Type>(InAccessPoint.Token, InRef);
		}

		template<typename ToType, typename FromType> 
		TAccessPtr<ToType> ConstCastAccessPtr(const TAccessPtr<FromType>& InAccessPtr)
		{
			return TAccessPtr<ToType>(InAccessPtr, TAccessPtr<ToType>::EConstCast::Tag);
		}
	}
}
