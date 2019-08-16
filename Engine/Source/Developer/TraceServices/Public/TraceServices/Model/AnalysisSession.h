// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

class FName;

namespace Trace
{

class ILinearAllocator;

class IProvider
{
public:
	virtual ~IProvider() = default;
};

class IAnalysisSession
{
public:
	virtual ~IAnalysisSession() = default;
	
	virtual const TCHAR* GetName() const = 0;
	virtual bool IsAnalysisComplete() const = 0;
	virtual double GetDurationSeconds() const = 0;
	virtual void UpdateDurationSeconds(double Duration) = 0;

	virtual void BeginRead() const = 0;
	virtual void EndRead() const = 0;
	virtual void ReadAccessCheck() const = 0;

	virtual void BeginEdit() = 0;
	virtual void EndEdit() = 0;
	virtual void WriteAccessCheck() = 0;
	
	virtual ILinearAllocator& GetLinearAllocator() = 0;
	virtual const TCHAR* StoreString(const TCHAR* String) = 0;
	
	virtual void AddProvider(const FName& Name, IProvider* Provider) = 0;
	template<typename ProviderType>
	const ProviderType* ReadProvider(const FName& Name) const
	{
		return static_cast<const ProviderType*>(ReadProviderPrivate(Name));
	}

private:
	virtual const IProvider* ReadProviderPrivate(const FName& Name) const = 0;
};

struct FAnalysisSessionReadScope
{
	FAnalysisSessionReadScope(const IAnalysisSession& InAnalysisSession)
		: AnalysisSession(InAnalysisSession)
	{
		AnalysisSession.BeginRead();
	}

	~FAnalysisSessionReadScope()
	{
		AnalysisSession.EndRead();
	}

private:
	const IAnalysisSession& AnalysisSession;
};
	
struct FAnalysisSessionEditScope
{
	FAnalysisSessionEditScope(IAnalysisSession& InAnalysisSession)
		: AnalysisSession(InAnalysisSession)
	{
		AnalysisSession.BeginEdit();
	}

	~FAnalysisSessionEditScope()
	{
		AnalysisSession.EndEdit();
	}

	IAnalysisSession& AnalysisSession;
};
	
}
