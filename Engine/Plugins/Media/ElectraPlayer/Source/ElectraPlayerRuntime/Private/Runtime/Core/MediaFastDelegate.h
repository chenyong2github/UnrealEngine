// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <Core/MediaTypes.h>
#include <Core/MediaMacros.h>
#include <Core/MediaNoncopyable.h>
#include <Core/MediaLock.h>

namespace Electra
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4191)
#endif

#include "3rdParty/FastDelegate/FastDelegate.h"
	using namespace fastdelegate;

#if defined(_MSC_VER)
#pragma warning( pop )
#endif





	/**
	 * Internal variables for the safe delegate.
	 * Those will be passed via TSharedPtr<> to the service functions that shall invoke the delegate.
	**/
	template <typename CB>
	struct TMediaSafeDelegateVars : private TMediaNoncopyable<TMediaSafeDelegateVars<CB> >
	{
		CB							Callback;
		FMediaLockCriticalSection	Lock;
	};

	/**
	 * Template functions to invoke the safe delegate for functions returning <void>.
	 * Used only by the caller.
	**/
	template<typename D>
	void TMediaSafeDelegateCall(D delegate)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback();
	}

	template<typename D, typename A1>
	void TMediaSafeDelegateCall(D delegate, A1 arg1)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback(arg1);
	}

	template<typename D, typename A1, typename A2>
	void TMediaSafeDelegateCall(D delegate, A1 arg1, A2 arg2)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback(arg1, arg2);
	}

	template<typename D, typename A1, typename A2, typename A3>
	void TMediaSafeDelegateCall(D delegate, A1 arg1, A2 arg2, A3 arg3)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback(arg1, arg2, arg3);
	}

	template<typename D, typename A1, typename A2, typename A3, typename A4>
	void TMediaSafeDelegateCall(D delegate, A1 arg1, A2 arg2, A3 arg3, A4 arg4)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback(arg1, arg2, arg3, arg4);
	}

	template<typename D, typename A1, typename A2, typename A3, typename A4, typename A5>
	void TMediaSafeDelegateCall(D delegate, A1 arg1, A2 arg2, A3 arg3, A4 arg4, A5 arg5)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback(arg1, arg2, arg3, arg4, arg5);
	}

	template<typename D, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
	void TMediaSafeDelegateCall(D delegate, A1 arg1, A2 arg2, A3 arg3, A4 arg4, A5 arg5, A6 arg6)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback(arg1, arg2, arg3, arg4, arg5, arg6);
	}

	template<typename D, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7>
	void TMediaSafeDelegateCall(D delegate, A1 arg1, A2 arg2, A3 arg3, A4 arg4, A5 arg5, A6 arg6, A7 arg7)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback(arg1, arg2, arg3, arg4, arg5, arg6, arg7);
	}

	template<typename D, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8>
	void TMediaSafeDelegateCall(D delegate, A1 arg1, A2 arg2, A3 arg3, A4 arg4, A5 arg5, A6 arg6, A7 arg7, A8 arg8)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
	}

	template<typename D, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9>
	void TMediaSafeDelegateCall(D delegate, A1 arg1, A2 arg2, A3 arg3, A4 arg4, A5 arg5, A6 arg6, A7 arg7, A8 arg8, A9 arg9)
	{
		FMediaLockCriticalSection::ScopedLock lock(delegate->Lock); if (!delegate->Callback.empty()) delegate->Callback(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
	}





	/**
	 * Safe delegate.
	 *
	 * Instantiate as a member in your class and set your callback function via Set().
	 * Pass the embedded Proxy object to any safe-delegate aware service function.
	 *
	 * If your class is destroyed before the callback is fired the following will happen:
	 *  - if the callback function has no return value nothing will happen
	 *  - if the callback function returns some value an assert will trigger because no meaningful
	 *    return value can be provided to the caller.
	**/
	template <typename CB>
	class TMediaSafeDelegate
	{
	public:
		//! Type of proxy to be passed into service functions.
		typedef Electra::TSharedPtrTS<TMediaSafeDelegateVars<CB> > Proxy;

		//! Empty callback, if none should be passed to a service function.
		static Proxy None(void)
		{
			Proxy none(new TMediaSafeDelegateVars<CB>); return(none);
		}


		//! Destructor. Clears the callback function in the proxy.
		~TMediaSafeDelegate()
		{
			if (Vars)
			{
				FMediaLockCriticalSection::ScopedLock lock(Vars->Lock);
				Vars->Callback.clear();
			}
		}

		//! Assigns the callback function to be invoked.
		void Set(CB callback)
		{
			if (Vars == nullptr)
			{
				Vars = MakeShareable(new TMediaSafeDelegateVars<CB>);
				Vars->Callback = callback;
			}
			else
			{
				FMediaLockCriticalSection::ScopedLock lock(Vars->Lock);
				Vars->Callback = callback;
			}
		}

		//! Cast operator to return the embedded proxy to a service function.
		operator Proxy()
		{
			checkf(Vars, TEXT("No callback function set!"));
			return(Vars);
		}

		//! Function operator to return the embedded proxy to a service function.
		Proxy operator() (void)
		{
			checkf(Vars, TEXT("No callback function set!"));
			return(Vars);
		}

	private:
		Proxy	Vars;			//!< Embedded proxy object.
	};


};



