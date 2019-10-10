// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IRemoteSessionChannel;

enum class ERemoteSessionChannelMode
{
	Read,
	Write
};

DECLARE_DELEGATE_ThreeParams(FOnRemoteSessionChannelCreated, TWeakPtr<IRemoteSessionChannel> /*Instance*/, const FString& /*Type*/, ERemoteSessionChannelMode /*Mode*/);

struct REMOTESESSION_API FRemoteSessionChannelInfo
{
	FString Type;
	ERemoteSessionChannelMode Mode;
	FOnRemoteSessionChannelCreated OnCreated;

	FRemoteSessionChannelInfo() = default;
	FRemoteSessionChannelInfo(FString InType, ERemoteSessionChannelMode InMode, FOnRemoteSessionChannelCreated InOnCreated)
		: Type(InType), Mode(InMode), OnCreated(InOnCreated)
	{ }
};

class REMOTESESSION_API IRemoteSessionRole
{
public:
	virtual~IRemoteSessionRole() {}

	virtual bool IsConnected() const = 0;
	
	virtual bool HasError() const = 0;
	
	virtual FString GetErrorMessage() const = 0;

	virtual TSharedPtr<IRemoteSessionChannel> GetChannel(const TCHAR* Type) = 0;

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
