// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Templates/IsInvocable.h"
#include "Misc/ScopeExit.h"
#include <type_traits>

namespace LowLevelTasks
{
	namespace TTaskDelegate_Impl
	{
		template<typename ReturnType>
		inline ReturnType MakeDummyValue()
		{
			return *(reinterpret_cast<ReturnType*>(uintptr_t(1)));
		}

		template<>
		inline void MakeDummyValue<void>()
		{
			return;
		}
	}
	//version of TUniqueFunction<ReturnType()> that is less wasteful with it's memory
	//this class might be removed when TUniqueFunction<ReturnType()> is fixed
	template<uint32 TotalSize = PLATFORM_CACHE_LINE_SIZE, typename ReturnType = void>
	class alignas(8) TTaskDelegate
	{
		template<uint32, typename>
		friend class TTaskDelegate;

		using ThisClass = TTaskDelegate<TotalSize, ReturnType>;
		struct TTaskDelegateBase
		{
			virtual void Move(TTaskDelegateBase&, void*, void*, uint32) {check(false); };
			virtual ReturnType Call(void*) const { check(false); return TTaskDelegate_Impl::MakeDummyValue<ReturnType>(); };
			virtual ReturnType CallAndMove(ThisClass&, void*, uint32) {check(false); return TTaskDelegate_Impl::MakeDummyValue<ReturnType>(); };
			virtual void Destroy(void*) { check(false); };
			virtual bool IsHeapAllocated() const { check(false); return false; }
			virtual bool IsSet() const { check(false); return false; }
			virtual uint32 DelegateSize() const { check(false); return 0; }
		};

		struct TTaskDelegateDummy final : TTaskDelegateBase
		{
			void Move(TTaskDelegateBase&, void*, void*, uint32) override {};
			ReturnType Call(void*) const override { check(false); return TTaskDelegate_Impl::MakeDummyValue<ReturnType>(); };
			ReturnType CallAndMove(ThisClass&, void*, uint32) override { check(false); return TTaskDelegate_Impl::MakeDummyValue<ReturnType>(); };
			void Destroy(void*) override {};
			bool IsHeapAllocated() const override { return false; }
			bool IsSet() const override { return false; }
			uint32 DelegateSize() const override { return 0; }
		};

		template<typename TCallableType, bool HeapAllocated>
		struct TTaskDelegateImpl;

		template<typename TCallableType>
		struct TTaskDelegateImpl<TCallableType, false> final : TTaskDelegateBase
		{
			template<typename TCallable>
			inline TTaskDelegateImpl(TCallable&& Callable, void* InlineData)
			{
				static_assert(TIsInvocable<TCallableType>::Value, "TCallableType is not invocable");
				static_assert(TIsSame<ReturnType, decltype(Callable())>::Value, "TCallableType return type does not match");
				static_assert(sizeof(TTaskDelegateImpl<TCallableType, false>) == sizeof(TTaskDelegateBase), "Size must match the Baseclass");
				new (InlineData) TCallableType(Forward<TCallable>(Callable));
			}

			inline void Move(TTaskDelegateBase& DstWrapper, void* DstData, void* SrcData, uint32 DestInlineSize) override
			{
				TCallableType* SrcPtr = reinterpret_cast<TCallableType*>(SrcData);
				if ((sizeof(TCallableType) <= DestInlineSize) && (uintptr_t(DstData) % alignof(TCallableType)) == 0)
				{
					new (&DstWrapper) TTaskDelegateImpl<TCallableType, false>(MoveTemp(*SrcPtr), DstData);
				}
				else
				{
					new (&DstWrapper) TTaskDelegateImpl<TCallableType, true>(MoveTemp(*SrcPtr), DstData);
				}
				new (this) TTaskDelegateDummy();
			}

			inline ReturnType Call(void* InlineData) const override
			{
				TCallableType* LocalPtr = reinterpret_cast<TCallableType*>(InlineData);
				return (*LocalPtr)();
			}

			ReturnType CallAndMove(ThisClass& Destination, void* InlineData, uint32 DestInlineSize) override
			{
				ON_SCOPE_EXIT
				{
					Move(Destination.CallableWrapper, Destination.InlineStorage, InlineData, DestInlineSize);
				};
				return Call(InlineData);		
			}

			void Destroy(void* InlineData) override
			{
				TCallableType* LocalPtr = reinterpret_cast<TCallableType*>(InlineData);
				LocalPtr->~TCallableType();
			}

			bool IsHeapAllocated() const override
			{ 
				return false; 
			}

			bool IsSet() const override 
			{ 
				return true; 
			}

			uint32 DelegateSize() const override 
			{ 
				return sizeof(TCallableType); 
			}
		};

		template<typename TCallableType>
		struct TTaskDelegateImpl<TCallableType, true> final : TTaskDelegateBase
		{
		private:
			inline TTaskDelegateImpl(void* DstData, void* SrcData)
			{
				memcpy(DstData, SrcData, sizeof(TCallableType*));
			}

		public:
			template<typename TCallable>
			inline TTaskDelegateImpl(TCallable&& Callable, void* InlineData)
			{
				static_assert(TIsInvocable<TCallableType>::Value, "TCallableType is not invocable");
				static_assert(TIsSame<ReturnType, decltype(Callable())>::Value, "TCallableType return type does not match");
				static_assert(sizeof(TTaskDelegateImpl<TCallableType, true>) == sizeof(TTaskDelegateBase), "Size must match the Baseclass");
				TCallableType** HeapPtr = reinterpret_cast<TCallableType**>(InlineData);
				*HeapPtr = new TCallableType(Forward<TCallable>(Callable));
			}

			inline void Move(TTaskDelegateBase& DstWrapper, void* DstData, void* SrcData, uint32 DestInlineSize) override
			{
				new (&DstWrapper) TTaskDelegateImpl<TCallableType, true>(DstData, SrcData);
				new (this) TTaskDelegateDummy();
			}

			inline ReturnType Call(void* InlineData) const override
			{
				TCallableType* HeapPtr = reinterpret_cast<TCallableType*>(*reinterpret_cast<void* const*>(InlineData));
				return (*HeapPtr)();
			}

			ReturnType CallAndMove(ThisClass& Destination, void* InlineData, uint32 DestInlineSize) override
			{
				ON_SCOPE_EXIT
				{
					Move(Destination.CallableWrapper, Destination.InlineStorage, InlineData, DestInlineSize);
				};
				return Call(InlineData);			
			}

			void Destroy(void* InlineData) override
			{
				TCallableType* HeapPtr = reinterpret_cast<TCallableType*>(*reinterpret_cast<void**>(InlineData));
				delete HeapPtr;
			}

			bool IsHeapAllocated() const override 
			{ 
				return true; 
			}

			bool IsSet() const override 
			{ 
				return true; 
			}

			uint32 DelegateSize() const override 
			{ 
				return sizeof(TCallableType); 
			}
		};

	public:
		TTaskDelegate() 
		{
			static_assert(TotalSize % 8 == 0,  "Totalsize must be dividable by 8");
			static_assert(TotalSize >= (sizeof(TTaskDelegateBase) + sizeof(void*)),  "Totalsize must be large enough to fit a vtable and pointer");
			new (&CallableWrapper) TTaskDelegateDummy();
		}

		template<uint32 SourceTotalSize>
		TTaskDelegate(const TTaskDelegate<SourceTotalSize, ReturnType>&) = delete;

		template<uint32 SourceTotalSize>
		TTaskDelegate(TTaskDelegate<SourceTotalSize, ReturnType>&& Other)
		{
			static_cast<TTaskDelegateBase*>(&Other.CallableWrapper)->Move(CallableWrapper, InlineStorage, Other.InlineStorage, InlineStorageSize);
		}

		template<typename TCallable>
		TTaskDelegate(TCallable&& Callable)
		{
			using TCallableType = typename TRemoveConst<typename TRemoveReference<TCallable>::Type>::Type;
			if ((sizeof(TCallableType) <= InlineStorageSize) && ((uintptr_t(InlineStorageSize) % alignof(TCallableType)) == 0))
			{
				new (&CallableWrapper) TTaskDelegateImpl<TCallableType, false>(Forward<TCallable>(Callable), InlineStorage);
			}
			else
			{
				new (&CallableWrapper) TTaskDelegateImpl<TCallableType, true>(Forward<TCallable>(Callable), InlineStorage);
			}
		}

		~TTaskDelegate()
		{
			static_cast<TTaskDelegateBase*>(&CallableWrapper)->Destroy(InlineStorage);
		}

		ReturnType operator()() const
		{
			return static_cast<const TTaskDelegateBase*>(&CallableWrapper)->Call(InlineStorage);
		}

		template<uint32 DestTotalSize>
		ReturnType CallAndMove(TTaskDelegate<DestTotalSize, ReturnType>& Destination)
		{
			checkSlow(!Destination.IsSet());
			return static_cast<TTaskDelegateBase*>(&CallableWrapper)->CallAndMove(Destination, InlineStorage, TTaskDelegate<DestTotalSize, ReturnType>::InlineStorageSize);
		}

		template<uint32 SourceTotalSize>
		ThisClass& operator= (const TTaskDelegate<SourceTotalSize, ReturnType>&) = delete;

		template<uint32 SourceTotalSize>
		ThisClass& operator= (TTaskDelegate<SourceTotalSize, ReturnType>&& Other)
		{
			static_cast<TTaskDelegateBase*>(&CallableWrapper)->Destroy(InlineStorage);
			static_cast<TTaskDelegateBase*>(&Other.CallableWrapper)->Move(CallableWrapper, InlineStorage, Other.InlineStorage, InlineStorageSize);
			return *this;
		}

		template<typename TCallable>
		ThisClass& operator= (TCallable&& Callable)
		{
			using TCallableType = typename TRemoveConst<typename TRemoveReference<TCallable>::Type>::Type;
			static_cast<TTaskDelegateBase*>(&CallableWrapper)->Destroy(InlineStorage);
			if ((sizeof(TCallableType) <= InlineStorageSize) && ((uintptr_t(InlineStorageSize) % alignof(TCallableType)) == 0))
			{
				new (&CallableWrapper) TTaskDelegateImpl<TCallableType, false>(Forward<TCallable>(Callable), InlineStorage);
			}
			else
			{
				new (&CallableWrapper) TTaskDelegateImpl<TCallableType, true>(Forward<TCallable>(Callable), InlineStorage);
			}
			return *this;
		}

		void Destroy()
		{
			static_cast<TTaskDelegateBase*>(&CallableWrapper)->Destroy(InlineStorage);
			new (&CallableWrapper) TTaskDelegateDummy();
		}

		bool IsHeapAllocated() const
		{
			return static_cast<const TTaskDelegateBase*>(&CallableWrapper)->IsHeapAllocated();
		}

		bool IsSet() const
		{
			return static_cast<const TTaskDelegateBase*>(&CallableWrapper)->IsSet();
		}

		uint32 DelegateSize() const  
		{ 
			return static_cast<const TTaskDelegateBase*>(&CallableWrapper)->DelegateSize(); 
		}

	private:
		static constexpr uint32 InlineStorageSize = TotalSize - sizeof(TTaskDelegateBase);
		mutable char InlineStorage[InlineStorageSize];
		TTaskDelegateBase CallableWrapper;
	};
}