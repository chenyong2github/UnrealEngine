// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RemoteSessionTypes.h"

class IRemoteSessionChannel;


class REMOTESESSION_API IRemoteSessionRole
{
public:
	virtual~IRemoteSessionRole() {}

	virtual bool IsConnected() const = 0;
	
	virtual bool HasError() const = 0;
	
	virtual FString GetErrorMessage() const = 0;

	virtual TSharedPtr<IRemoteSessionChannel> GetChannel(const TCHAR* Type) = 0;

	virtual void RegisterChannelChangeDelegate(FOnRemoteSessionChannelChange InDelegate) = 0;
	virtual void UnregisterChannelChangeDelegate(void* UserObject) = 0;

	template<class T>
	TSharedPtr<T> GetChannel()
	{
		TSharedPtr<IRemoteSessionChannel> Channel = GetChannel(T::StaticType());

		if (Channel.IsValid())
		{
			return StaticCastSharedPtr<T>(Channel);
		}

		return TSharedPtr<T>();
	}
};

class REMOTESESSION_API IRemoteSessionUnmanagedRole : public IRemoteSessionRole
{
public:
	virtual void Tick(float DeltaTime) = 0;
	virtual void Close() = 0;
	virtual void CloseWithError(const FString& Message) = 0;
};
