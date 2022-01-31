// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/CoreOnline.h"
#include "Online/OnlineUtils.h"
#include "Online/OnlineServicesLog.h"

#include "Templates/Invoke.h"
#include "Templates/Models.h"

class UWorld;
class FOutputDevice;

namespace UE::Online {

class IOnlineExecHandler
{
public:
	virtual ~IOnlineExecHandler() {}
	virtual bool Exec(UWorld* World, const TCHAR* Cmd, FOutputDevice& Ar) = 0;
	virtual bool Help(UWorld* World, const TCHAR* Cmd, FOutputDevice& Ar) = 0;
};

namespace Private {

template <typename T>
struct TOnlineInterfaceOperationMemberFunctionPtrTraits;

template <typename TInterface, typename TOp>
struct TOnlineInterfaceOperationMemberFunctionPtrTraits<TOnlineResult<TOp>(TInterface::*)(typename TOp::Params&&)>
{
	using InterfaceType = TInterface;
	using OpType = TOp;
	static constexpr bool bAsync = false;
};

template <typename TInterface, typename TOp>
struct TOnlineInterfaceOperationMemberFunctionPtrTraits<TOnlineResult<TOp>(TInterface::*)(typename TOp::Params&&) const>
{
	using InterfaceType = TInterface;
	using OpType = TOp;
	static constexpr bool bAsync = false;
};

template <typename TInterface, typename TOp>
struct TOnlineInterfaceOperationMemberFunctionPtrTraits<TOnlineAsyncOpHandle<TOp>(TInterface::*)(typename TOp::Params&&)>
{
	using InterfaceType = TInterface;
	using OpType = TOp;
	static constexpr bool bAsync = true;
};

template <typename TInterface, typename TOp>
struct TOnlineInterfaceOperationMemberFunctionPtrTraits<TOnlineAsyncOpHandle<TOp>(TInterface::*)(typename TOp::Params&&) const>
{
	using InterfaceType = TInterface;
	using OpType = TOp;
	static constexpr bool bAsync = true;
};

// Forward declarations for dependent type resolution
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, FString& Value);
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, FName& Value);
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, uint8& Value);
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, int32& Value);
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, uint32& Value);
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, int64& Value);
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, uint64& Value);
template <typename T> inline bool ParseOnlineExecParams(const TCHAR*& Cmd, TArray<T>& Array);
template <typename T, typename U> inline bool ParseOnlineExecParams(const TCHAR*& Cmd, TMap<T, U>& Map);
template <typename... Ts> inline bool ParseOnlineExecParams(const TCHAR*& Cmd, TVariant<Ts...>& Variant);
template <typename IdType> inline bool ParseOnlineExecParams(const TCHAR*& Cmd, TOnlineIdHandle<IdType>& Value);
template <typename T> std::enable_if_t<!TModels<Meta::COnlineMetadataAvailable, T>::Value, bool> ParseOnlineExecParams(const TCHAR*& Cmd, T& Value);
template <typename T> std::enable_if_t<TModels<Meta::COnlineMetadataAvailable, T>::Value, bool> ParseOnlineExecParams(const TCHAR*& Cmd, T& Value);

inline bool ParseOnlineExecParams(const TCHAR*& Cmd, FString& Value)
{
	if (!FParse::Token(Cmd, Value, true))
	{
		return false;
	}

	return true;
}

inline bool ParseOnlineExecParams(const TCHAR*& Cmd, FName& Value)
{
	FString StringValue;
	if (!ParseOnlineExecParams(Cmd, StringValue))
	{
		return false;
	}

	Value = FName(StringValue);
	return true;
}

inline bool ParseOnlineExecParams(const TCHAR*& Cmd, uint8& Value)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		Value = static_cast<uint8>(FCString::Strtoui64(*Token, nullptr, 10));
	}
	else
	{
		return false;
	}

	return true;
}

inline bool ParseOnlineExecParams(const TCHAR*& Cmd, int32& Value)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		Value = FCString::Strtoi(*Token, nullptr, 10);
	}
	else
	{
		return false;
	}

	return true;
}

inline bool ParseOnlineExecParams(const TCHAR*& Cmd, uint32& Value)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		Value = static_cast<uint32>(FCString::Strtoui64(*Token, nullptr, 10));
	}
	else
	{
		return false;
	}

	return true;
}

inline bool ParseOnlineExecParams(const TCHAR*& Cmd, int64& Value)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		Value = FCString::Strtoi64(*Token, nullptr, 10);
	}
	else
	{
		return false;
	}

	return true;
}

inline bool ParseOnlineExecParams(const TCHAR*& Cmd, uint64& Value)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		Value = FCString::Strtoui64(*Token, nullptr, 10);
	}
	else
	{
		return false;
	}

	return true;
}

inline bool ParseOnlineExecParams(const TCHAR*& Cmd, FPlatformUserId& Value)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		Value = FPlatformMisc::GetPlatformUserForUserIndex(FCString::Strtoi(Cmd, nullptr, 10));
	}
	else
	{
		return false;
	}

	return true;
}

template <typename T>
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, TArray<T>& Array)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		TArray<FString> TokenArray;
		Token.ParseIntoArray(TokenArray, TEXT(","));
		Array.Reserve(TokenArray.Num());
		for (const FString& ArrayToken : TokenArray)
		{
			T Value;
			const TCHAR* ArrayTokenTCHAR = *ArrayToken;
			if (ParseOnlineExecParams(ArrayTokenTCHAR, Value))
			{
				Array.Emplace(MoveTempIfPossible(Value));
			}
		}
	}
	else
	{
		return false;
	}
	return true;
}

template <typename T, typename U>
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, TMap<T, U>& Map)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		TArray<FString> TokenArray;
		Token.ParseIntoArray(TokenArray, TEXT(","));
		Map.Reserve(TokenArray.Num());
		for (const FString& ArrayToken : TokenArray)
		{
			TArray<FString> TokenValuePair;
			Token.ParseIntoArray(TokenValuePair, TEXT("="));

			if (TokenValuePair.Num() > 1)
			{
				T Key;
				U Value;

				const TCHAR* ArrayTokenKeyTCHAR = *TokenValuePair[0];
				if (ParseOnlineExecParams(ArrayTokenKeyTCHAR, Key))
				{
					if (TokenValuePair.Num() > 2)
					{
						const TCHAR* ArrayTokenValueTCHAR = *TokenValuePair[1];
						ParseOnlineExecParams(ArrayTokenValueTCHAR, Value);

						Map.Emplace(MoveTempIfPossible(Key), MoveTempIfPossible(Value));
					}
				}
			}
		}
	}
	else
	{
		return false;
	}
	return true;
}

template <typename... Ts>
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, TVariant<Ts...>& Variant)
{
	// TODO: This is a temporary stub implementation
	return true;
}

template<typename IdType>
inline bool ParseOnlineExecParams(const TCHAR*& Cmd, TOnlineIdHandle<IdType>& Value)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		FString ServicesString;
		FString HandleString;

		if (Token.Split(TEXT(":"), &ServicesString, &HandleString, ESearchCase::CaseSensitive))
		{
			EOnlineServices Services;
			LexFromString(Services, *ServicesString);
			uint32 Handle = static_cast<uint32>(FCString::Strtoui64(*HandleString, nullptr, 10));
			Value = TOnlineIdHandle<IdType>(Services, Handle);
			return true;
		}
	}

	return false;
}

template <typename T>
std::enable_if_t<!TModels<Meta::COnlineMetadataAvailable, T>::Value, bool> ParseOnlineExecParams(const TCHAR*& Cmd, T& Value)
{
	FString Token;
	if (FParse::Token(Cmd, Token, true))
	{
		LexFromString(Value, *Token);
	}
	else
	{
		return false;
	}

	return true;
}

template <typename T>
std::enable_if_t<TModels<Meta::COnlineMetadataAvailable, T>::Value, bool> ParseOnlineExecParams(const TCHAR*& Cmd, T& Value)
{
	bool bSuccess = true;
	Meta::VisitFields(Value, [&Cmd, &bSuccess](const TCHAR* Name, auto& Field)
		{
			bSuccess &= ParseOnlineExecParams(Cmd, Field);
		});

	return bSuccess;
}

/* Private */ }

template <typename MemberFunctionPtrType>
class TOnlineInterfaceOperationExecHandler : public IOnlineExecHandler
{
public:
	using InterfaceType = typename Private::TOnlineInterfaceOperationMemberFunctionPtrTraits<MemberFunctionPtrType>::InterfaceType;
	using OpType = typename Private::TOnlineInterfaceOperationMemberFunctionPtrTraits<MemberFunctionPtrType>::OpType;

	TOnlineInterfaceOperationExecHandler(InterfaceType* InInterface, MemberFunctionPtrType InFunction)
		: Interface(InInterface)
		, Function(InFunction)
	{
	}

	virtual bool Exec(UWorld* World, const TCHAR* Cmd, FOutputDevice& Ar) override
	{
		typename OpType::Params Params;
		if (!Private::ParseOnlineExecParams(Cmd, Params))
		{
			Ar.Log(TEXT("Failed to parse params"));
			return false;
		}

		if constexpr (Private::TOnlineInterfaceOperationMemberFunctionPtrTraits<MemberFunctionPtrType>::bAsync)
		{
			TOnlineAsyncOpHandle<OpType> AsyncOpHandle = Invoke(Function, Interface, MoveTemp(Params));
			AsyncOpHandle.OnComplete([&Ar](const TOnlineResult<OpType>& Result)
				{
					UE_LOG(LogOnlineServices, Log, TEXT("%s result: %s"), OpType::Name, *ToLogString(Result));
				});
		}
		else
		{
			TOnlineResult<OpType> Result = Invoke(Function, Interface, MoveTemp(Params));
			UE_LOG(LogOnlineServices, Log, TEXT("%s result: %s"), OpType::Name, *ToLogString(Result));
		}

		return true;
	}

	virtual bool Help(UWorld* World, const TCHAR* Cmd, FOutputDevice& Ar) override
	{
		FString HelpString = OpType::Name;
		Meta::VisitFields<typename OpType::Params>([&HelpString](const auto& Field)
			{
				HelpString += TEXT(" ");
				HelpString += Field.Name;
			});

		Ar.Log(HelpString);
		
		return true;
	}

private:
	InterfaceType* Interface;
	MemberFunctionPtrType Function;
};

template <typename T>
class TOnlineComponentExecHandler : public IOnlineExecHandler
{
public:
	TOnlineComponentExecHandler(T* InComponent)
		: Component(InComponent)
	{
	}

	virtual bool Exec(UWorld* World, const TCHAR* Cmd, FOutputDevice& Ar)
	{
		return Component->Exec(World, Cmd, Ar);
	}

	virtual bool Help(UWorld* World, const TCHAR* Cmd, FOutputDevice& Ar) override
	{
		return Component->Help(World, Cmd, Ar);
	}

private:
	T* Component;
};

/* UE::Online */ }
