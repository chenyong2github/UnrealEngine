// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Audio
{
	// COM RAII helper
	template<typename T>
	struct TScopeComObject final
	{
		using ThisType = TScopeComObject<T>;

		T* Obj = nullptr;

		T* Get() const
		{
			return static_cast<T*>(Obj);
		}

		T* operator->() const
		{
			check(Get());
			return Get();
		}

		explicit operator bool() const
		{
			return Get() != nullptr;
		}

		void Reset()
		{
			*this = ThisType();
		}

		// Returned from QueryInterface call, will already have AddRef called.
		TScopeComObject(T* InObj = nullptr)
			: Obj(InObj)
		{}
		TScopeComObject(const ThisType& InObj)
		{
			*this = InObj;
		}
		TScopeComObject(ThisType&& InObj)
		{
			*this = MoveTemp(InObj);
		}
		~TScopeComObject()
		{
			SafeRelease();
		}

		// Copy Assignment.
		ThisType& operator=(const ThisType& In)
		{
			// Remove any ref we currently hold
			SafeRelease();
			// Assign new state and add ref as we are copy
			Obj = In.Obj;
			SafeAddRef();
			return *this;
		}

		// Move assignment.
		ThisType& operator=(ThisType&& InObj)
		{
			SafeRelease();
			Obj = InObj.Obj;
			InObj.Obj = nullptr;
			return *this;
		}

		// Equality operator
		bool operator==(const ThisType& InObj) const
		{
			return InObj.Obj == Obj;
		}

	private:
		void SafeAddRef()
		{
			if (Obj)
			{
				Obj->AddRef();
			}
		}
		void SafeRelease()
		{
			if (Obj)
			{
				Obj->Release();
				Obj = nullptr;
			}
		}
	};

	struct FScopeComString final
	{
		LPTSTR StringPtr = nullptr;

		UE_NONCOPYABLE(FScopeComString)

			const LPTSTR Get() const
		{
			return StringPtr;
		}

		explicit operator bool() const
		{
			return Get() != nullptr;
		}

		FScopeComString(LPTSTR InStringPtr = nullptr)
			: StringPtr(InStringPtr)
		{}

		~FScopeComString()
		{
			if (StringPtr)
			{
				CoTaskMemFree(StringPtr);
			}
		}
	};
}