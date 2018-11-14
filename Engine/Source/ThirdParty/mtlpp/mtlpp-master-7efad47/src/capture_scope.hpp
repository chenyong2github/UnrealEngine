// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once


#include "declare.hpp"
#include "imp_CaptureScope.hpp"
#include "ns.hpp"

MTLPP_BEGIN

namespace ue4
{
	template<>
	struct ITable<id<MTLCaptureScope>, void> : public IMPTable<id<MTLCaptureScope>, void>, public ITableCacheRef
	{
		ITable()
		{
		}
		
		ITable(Class C)
		: IMPTable<id<MTLCaptureScope>, void>(C)
		{
		}
	};
}

namespace mtlpp
{
	class Device;
	class CommandQueue;
	
	class MTLPP_EXPORT CaptureScope : public ns::Object<ns::Protocol<id<MTLCaptureScope>>::type>
	{
	public:
		CaptureScope(ns::Ownership const retain = ns::Ownership::Retain) : ns::Object<ns::Protocol<id<MTLCaptureScope>>::type>(retain) { }
		CaptureScope(ns::Protocol<id<MTLCaptureScope>>::type handle, ue4::ITableCache* cache = nullptr, ns::Ownership const retain = ns::Ownership::Retain) : ns::Object<ns::Protocol<id<MTLCaptureScope>>::type>(handle, retain, ue4::ITableCacheRef(cache).GetCaptureScope(handle)) { }
		
		void BeginScope();
		void EndScope();

		ns::AutoReleased<ns::String>   GetLabel() const;
		void SetLabel(const ns::String& label);
		
		ns::AutoReleased<Device> GetDevice() const;
		ns::AutoReleased<CommandQueue> GetCommandQueue() const;
	} MTLPP_AVAILABLE(10_13, 11_0);
	
}

MTLPP_END
