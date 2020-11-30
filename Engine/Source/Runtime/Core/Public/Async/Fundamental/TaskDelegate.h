// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Templates/IsInvocable.h"
#include <type_traits>

namespace LowLevelTasks
{
	//version of TUniqueFunction<void()> that is less wasteful with it's memory
	//this class might be removed when TUniqueFunction<void()> is fixed
	template<uint32 TotalSize = PLATFORM_CACHE_LINE_SIZE>
	class alignas(8) TTaskDelegate
	{
		template<uint32>
		friend class TTaskDelegate;

		using ThisClass = TTaskDelegate<TotalSize>;
		struct TTaskDelegateBase
		{
			virtual void Move(TTaskDelegateBase&, void*, void*, uint32) {check(false); };
			virtual void Call(const void*) const { check(false); };
			virtual void CallAndMove(ThisClass&, void*, uint32) {check(false); };
			virtual void Destroy(void*) { check(false); };
			virtual bool IsHeapAllocated() const { check(false); return false; }
			virtual bool IsSet() const { check(false); return false; }
			virtual uint32 DelegateSize() const { check(false); return 0; }
		};

		struct TTaskDelegateDummy final : TTaskDelegateBase
		{
			void Move(TTaskDelegateBase&, void*, void*, uint32) override {};
			void Call(const void*) const override {};
			void CallAndMove(ThisClass&, void*, uint32) override {};
			void Destroy(void*) override {};
			bool IsHeapAllocated() const override { return false; }
			bool IsSet() const override { return false; }
			uint32 DelegateSize() const override { return 0; }
		};

		template<typename TCallable, bool HeapAllocated>
		struct TTaskDelegateImpl;

		template<typename TCallable>
		struct TTaskDelegateImpl<TCallable, false> final : TTaskDelegateBase
		{
			inline TTaskDelegateImpl(TCallable&& Callable, void* InlineData)
			{
				static_assert(TIsInvocable<TCallable>::Value, "TCallable is not invocable");
				static_assert(sizeof(TTaskDelegateImpl<TCallable, false>) == sizeof(TTaskDelegateBase), "Size must match the Baseclass");
				new (InlineData) TCallable(Forward<TCallable>(Callable));
			}

			inline void Move(TTaskDelegateBase& DstWrapper, void* DstData, void* SrcData, uint32 DestInlineSize) override
			{
				TCallable* SrcPtr = reinterpret_cast<TCallable*>(SrcData);
				if ((sizeof(TCallable) <= DestInlineSize) && (uintptr_t(DstData) % alignof(TCallable)) == 0)
				{
					new (&DstWrapper) TTaskDelegateImpl<TCallable, false>(MoveTemp(*SrcPtr), DstData);
				}
				else
				{
					new (&DstWrapper) TTaskDelegateImpl<TCallable, true>(MoveTemp(*SrcPtr), DstData);
				}
				new (this) TTaskDelegateDummy();
			}

			inline void Call(const void* InlineData) const override
			{
				const TCallable* LocalPtr = reinterpret_cast<const TCallable*>(InlineData);
				(*LocalPtr)();
			}

			void CallAndMove(ThisClass& Destination, void* InlineData, uint32 DestInlineSize) override
			{
				Call(InlineData);
				Move(Destination.CallableWrapper, Destination.InlineStorage, InlineData, DestInlineSize);
			}

			void Destroy(void* InlineData) override
			{
				TCallable* LocalPtr = reinterpret_cast<TCallable*>(InlineData);
				LocalPtr->~TCallable();
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
				return sizeof(TCallable); 
			}
		};

		template<typename TCallable>
		struct TTaskDelegateImpl<TCallable, true> final : TTaskDelegateBase
		{
		private:
			inline TTaskDelegateImpl(void* DstData, void* SrcData)
			{
				memcpy(DstData, SrcData, sizeof(TCallable*));
			}

		public:
			inline TTaskDelegateImpl(TCallable&& Callable, void* InlineData)
			{
				static_assert(TIsInvocable<TCallable>::Value, "TCallable is not invocable");
				static_assert(sizeof(TTaskDelegateImpl<TCallable, true>) == sizeof(TTaskDelegateBase), "Size must match the Baseclass");
				TCallable** HeapPtr = reinterpret_cast<TCallable**>(InlineData);
				*HeapPtr = new TCallable(Forward<TCallable>(Callable));
			}

			inline void Move(TTaskDelegateBase& DstWrapper, void* DstData, void* SrcData, uint32 DestInlineSize) override
			{
				new (&DstWrapper) TTaskDelegateImpl<TCallable, true>(DstData, SrcData);
				new (this) TTaskDelegateDummy();
			}

			inline void Call(const void* InlineData) const override
			{
				const TCallable* HeapPtr = reinterpret_cast<const TCallable*>(*reinterpret_cast<const void* const*>(InlineData));
				(*HeapPtr)();
			}

			void CallAndMove(ThisClass& Destination, void* InlineData, uint32 DestInlineSize) override
			{
				Call(InlineData);
				Move(Destination.CallableWrapper, Destination.InlineStorage, InlineData, DestInlineSize);
			}

			void Destroy(void* InlineData) override
			{
				TCallable* HeapPtr = reinterpret_cast<TCallable*>(*reinterpret_cast<void**>(InlineData));
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
				return sizeof(TCallable); 
			}
		};

	public:
		TTaskDelegate() 
		{
			static_assert(TotalSize % 8 == 0,  "Totalsize must be dividable by 8");
			static_assert(TotalSize >= 16,  "Totalsize must be larger or equal than 16");
			new (&CallableWrapper) TTaskDelegateDummy();
		}

		template<uint32 SourceTotalSize>
		TTaskDelegate(const TTaskDelegate<SourceTotalSize>&) = delete;

		template<uint32 SourceTotalSize>
		TTaskDelegate(TTaskDelegate<SourceTotalSize>&& Other)
		{
			static_cast<TTaskDelegateBase*>(&Other.CallableWrapper)->Move(CallableWrapper, InlineStorage, Other.InlineStorage, InlineStorageSize);
		}

		template<typename TCallable>
		TTaskDelegate(TCallable&& Callable)
		{
			if ((sizeof(TCallable) <= InlineStorageSize) && ((uintptr_t(InlineStorageSize) % alignof(TCallable)) == 0))
			{
				new (&CallableWrapper) TTaskDelegateImpl<TCallable, false>(Forward<TCallable>(Callable), InlineStorage);
			}
			else
			{
				new (&CallableWrapper) TTaskDelegateImpl<TCallable, true>(Forward<TCallable>(Callable), InlineStorage);
			}
		}

		~TTaskDelegate()
		{
			static_cast<TTaskDelegateBase*>(&CallableWrapper)->Destroy(InlineStorage);
		}

		void operator()() const
		{
			static_cast<const TTaskDelegateBase*>(&CallableWrapper)->Call(InlineStorage);
		}

		template<uint32 DestTotalSize>
		void CallAndMove(TTaskDelegate<DestTotalSize>& Destination)
		{
			checkSlow(!Destination.IsSet());
			static_cast<TTaskDelegateBase*>(&CallableWrapper)->CallAndMove(Destination, InlineStorage, TTaskDelegate<DestTotalSize>::InlineStorageSize);
		}

		template<uint32 SourceTotalSize>
		ThisClass& operator= (const TTaskDelegate<SourceTotalSize>&) = delete;

		template<uint32 SourceTotalSize>
		ThisClass& operator= (TTaskDelegate<SourceTotalSize>&& Other)
		{
			static_cast<TTaskDelegateBase*>(&CallableWrapper)->Destroy(InlineStorage);
			static_cast<TTaskDelegateBase*>(&Other.CallableWrapper)->Move(CallableWrapper, InlineStorage, Other.InlineStorage, InlineStorageSize);
			return *this;
		}

		template<typename TCallable>
		ThisClass& operator= (TCallable&& Callable)
		{
			static_cast<TTaskDelegateBase*>(&CallableWrapper)->Destroy(InlineStorage);
			if ((sizeof(TCallable) <= InlineStorageSize) && ((uintptr_t(InlineStorageSize) % alignof(TCallable)) == 0))
			{
				new (&CallableWrapper) TTaskDelegateImpl<TCallable, false>(Forward<TCallable>(Callable), InlineStorage);
			}
			else
			{
				new (&CallableWrapper) TTaskDelegateImpl<TCallable, true>(Forward<TCallable>(Callable), InlineStorage);
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
		char InlineStorage[InlineStorageSize];
		TTaskDelegateBase CallableWrapper;
	};
}