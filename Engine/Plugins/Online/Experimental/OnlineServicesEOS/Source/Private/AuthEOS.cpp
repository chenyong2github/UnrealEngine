// Copyright Epic Games, Inc. All Rights Reserved.

#include "AuthEOS.h"
#include "OnlineServicesEOS.h"
#include "OnlineServicesEOSTypes.h"
#include "Online/AuthErrors.h"

#if WITH_EOS_SDK
#include "eos_auth.h"
#include "eos_types.h"
#include "eos_init.h"
#include "eos_sdk.h"
#include "eos_logging.h"

void EOS_CALL EOSFree(void* Ptr)
{
	FMemory::Free(Ptr);
}

void* EOS_CALL EOSMalloc(size_t Size, size_t Alignment)
{
	return FMemory::Malloc(Size, Alignment);
}

void* EOS_CALL EOSRealloc(void* Ptr, size_t Size, size_t Alignment)
{
	return FMemory::Realloc(Ptr, Size, Alignment);
}

void EOS_CALL EOSLog(const EOS_LogMessage* InMsg)
{
	if (GLog == nullptr)
	{
		return;
	}

	switch (InMsg->Level)
	{
		case EOS_ELogLevel::EOS_LOG_Fatal:
		{
			UE_LOG(LogTemp, Warning, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_Error:
		{
			UE_LOG(LogTemp, Warning, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_Warning:
		{
			UE_LOG(LogTemp, Warning, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_Verbose:
		{
			UE_LOG(LogTemp, Warning, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_VeryVerbose:
		{
			UE_LOG(LogTemp, Warning, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
		case EOS_ELogLevel::EOS_LOG_Info:
		default:
		{
			UE_LOG(LogTemp, Warning, TEXT("EOSSDK-%s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));
			break;
		}
	}
}

EOS_HPlatform SetupEOSSdk()
{
	static EOS_HPlatform PlatformHandle = nullptr;
	if (PlatformHandle == nullptr)
	{
		// Init EOS SDK
		EOS_InitializeOptions SDKOptions = { };
		SDKOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;

		SDKOptions.ProductName = "My Product";
		SDKOptions.ProductVersion = "Unknown";
		SDKOptions.AllocateMemoryFunction = &EOSMalloc;
		SDKOptions.ReallocateMemoryFunction = &EOSRealloc;
		SDKOptions.ReleaseMemoryFunction = &EOSFree;

		EOS_EResult InitResult = EOS_Initialize(&SDKOptions);

		EOS_Platform_Options PlatformOptions = {};
		PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
		PlatformOptions.bIsServer = EOS_FALSE;
		PlatformOptions.EncryptionKey = nullptr;
		PlatformOptions.OverrideCountryCode = nullptr;
		PlatformOptions.OverrideLocaleCode = nullptr;
		PlatformOptions.Flags = EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D9 | EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D10 | EOS_PF_WINDOWS_ENABLE_OVERLAY_OPENGL; // Enable overlay support for D3D9/10 and OpenGL. This sample uses D3D11 or SDL.

		char CacheDirectory[512];
		FCStringAnsi::Strncpy(CacheDirectory, TCHAR_TO_UTF8(*(FPlatformProcess::UserDir() / FString(TEXT("CacheDir")))), 512);
		PlatformOptions.CacheDirectory = CacheDirectory;

		PlatformOptions.ProductId = "";
		PlatformOptions.SandboxId = "";
		PlatformOptions.DeploymentId = "";

		PlatformOptions.ClientCredentials.ClientId = "";
		PlatformOptions.ClientCredentials.ClientSecret = "";

		PlatformHandle = EOS_Platform_Create(&PlatformOptions);

		EOS_Logging_SetCallback(&EOSLog);
	}
	return PlatformHandle;
}

namespace UE::Online {

// TEMP until Net Id Registry is done
TMap<EOS_EpicAccountId, int32> EOSAccountIdMap;

// Copied from OSS EOS

#define EOS_OSS_STRING_BUFFER_LENGTH 256
// Chose arbitrarily since the SDK doesn't define it
#define EOS_MAX_TOKEN_SIZE 4096

struct FEosAuthCredentials :
	public EOS_Auth_Credentials
{
	FEosAuthCredentials() :
		EOS_Auth_Credentials()
	{
		ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
		Id = IdAnsi;
		Token = TokenAnsi;
	}

	FEosAuthCredentials(EOS_EExternalCredentialType InExternalType, const TArray<uint8>& InToken) :
		EOS_Auth_Credentials()
	{
		ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
		Type = EOS_ELoginCredentialType::EOS_LCT_ExternalAuth;
		ExternalType = InExternalType;
		Id = IdAnsi;
		Token = TokenAnsi;

		uint32_t InOutBufferLength = EOS_OSS_STRING_BUFFER_LENGTH;
		EOS_ByteArray_ToString(InToken.GetData(), InToken.Num(), TokenAnsi, &InOutBufferLength);
	}

	char IdAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
	char TokenAnsi[EOS_MAX_TOKEN_SIZE];
};

FAuthEOS::FAuthEOS(FOnlineServicesEOS& InOwningSubsystem)
	: FAuthCommon(InOwningSubsystem)
{
}

void FAuthEOS::Tick(float DeltaTime)
{
	EOS_HPlatform PlatformHandle = SetupEOSSdk();
	if (PlatformHandle)
	{
		EOS_Platform_Tick(PlatformHandle);
	}
}

bool LexFromString(EOS_EAuthScopeFlags& OutEnum, const TCHAR* InString)
{
	if (FCString::Stricmp(InString, TEXT("BasicProfile")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_BasicProfile;
	}
	else if (FCString::Stricmp(InString, TEXT("FriendsList")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_FriendsList;
	}
	else if (FCString::Stricmp(InString, TEXT("Presence")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_Presence;
	}
	else if (FCString::Stricmp(InString, TEXT("FriendsManagement")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_FriendsManagement;
	}
	else if (FCString::Stricmp(InString, TEXT("Email")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_Email;
	}
	else if (FCString::Stricmp(InString, TEXT("NoFlags")) == 0 || FCString::Stricmp(InString, TEXT("None")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_NoFlags;
	}
	else
	{
		return false;
	}
	return true;
}

bool LexFromString(EOS_ELoginCredentialType& OutEnum, const TCHAR* const InString)
{
	if (FCString::Stricmp(InString, TEXT("ExchangeCode")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
	}
	else if (FCString::Stricmp(InString, TEXT("PersistentAuth")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
	}
	//else if (FCString::Stricmp(InString, TEXT("DeviceCode")) == 0) // DeviceCode is deprecated
	//{
	//	OutEnum = EOS_ELoginCredentialType::EOS_LCT_DeviceCode;
	//}
	else if (FCString::Stricmp(InString, TEXT("Developer")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_Developer;
	}
	else if (FCString::Stricmp(InString, TEXT("RefreshToken")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_RefreshToken;
	}
	else if (FCString::Stricmp(InString, TEXT("AccountPortal")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
	}
	else if (FCString::Stricmp(InString, TEXT("ExternalAuth")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_ExternalAuth;
	}
	else
	{
		return false;
	}
	return true;
}

TOnlineAsyncOpHandle<FAuthLogin> FAuthEOS::Login(FAuthLogin::Params&& Params)
{
	EOS_HPlatform PlatformHandle = SetupEOSSdk();
	EOS_HAuth AuthHandle = EOS_Platform_GetAuthInterface(PlatformHandle);
	check(AuthHandle != nullptr);

	TOnlineAsyncOp<FAuthLogin>& Op = Services.OpCache.GetOp<FAuthLogin>(MoveTemp(Params));

	EOS_Auth_LoginOptions LoginOptions = { };
	LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
	bool bContainsFlagsNone = false;
	for (const FString& Scope : Op.GetParams().Scopes)
	{
		EOS_EAuthScopeFlags ScopeFlag;
		if (LexFromString(ScopeFlag, *Scope))
		{
			if (ScopeFlag == EOS_EAuthScopeFlags::EOS_AS_NoFlags)
			{
				bContainsFlagsNone = true;
			}
			LoginOptions.ScopeFlags |= ScopeFlag;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Invalid ScopeFlag=[%s]"), *Scope);
			Op.SetError(Errors::UnknownError());
			return Op.GetHandle();
		}
	}
	// TODO:  Where to put default scopes?
	if (!bContainsFlagsNone && LoginOptions.ScopeFlags == EOS_EAuthScopeFlags::EOS_AS_NoFlags)
	{
		LoginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile | EOS_EAuthScopeFlags::EOS_AS_FriendsList | EOS_EAuthScopeFlags::EOS_AS_Presence;
	}	

	FEosAuthCredentials Credentials;
	if (LexFromString(Credentials.Type, *Op.GetParams().CredentialsType))
	{
		switch (Credentials.Type)
		{
		case EOS_ELoginCredentialType::EOS_LCT_ExchangeCode:
			// This is how the Epic launcher will pass credentials to you
			Credentials.IdAnsi[0] = '\0';
			FCStringAnsi::Strncpy(Credentials.TokenAnsi, TCHAR_TO_UTF8(*Op.GetParams().CredentialsToken), EOS_MAX_TOKEN_SIZE);
			break;
		case EOS_ELoginCredentialType::EOS_LCT_Developer:
			// This is auth via the EOS auth tool
			FCStringAnsi::Strncpy(Credentials.IdAnsi, TCHAR_TO_UTF8(*Op.GetParams().CredentialsId), EOS_OSS_STRING_BUFFER_LENGTH);
			FCStringAnsi::Strncpy(Credentials.TokenAnsi, TCHAR_TO_UTF8(*Op.GetParams().CredentialsToken), EOS_MAX_TOKEN_SIZE);
			break;
		case EOS_ELoginCredentialType::EOS_LCT_AccountPortal:
			// This is auth via the EOS Account Portal
			Credentials.IdAnsi[0] = '\0';
			Credentials.TokenAnsi[0] = '\0';
			break;
		case EOS_ELoginCredentialType::EOS_LCT_PersistentAuth:
			// This is auth via stored credentials in EOS
			Credentials.Id = nullptr;
			Credentials.Token = nullptr;
			break;
		default:
			UE_LOG(LogTemp, Warning, TEXT("Unsupported CredentialsType=[%s]"), *Op.GetParams().CredentialsType);
			Op.SetError(Errors::UnknownError()); // TODO
			return Op.GetHandle();
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid CredentialsType=[%s]"), *Op.GetParams().CredentialsType);
		Op.SetError(Errors::UnknownError()); // TODO
		return Op.GetHandle();
	}

	Op.Then([this, AuthHandle, LoginOptions, Credentials](TOnlineAsyncOp<FAuthLogin>& InAsyncOp) mutable
		{
			LoginOptions.Credentials = &Credentials;
			return EOS_Async<EOS_Auth_LoginCallbackInfo>(InAsyncOp, EOS_Auth_Login, AuthHandle, LoginOptions);
		})
		.Then([this](TOnlineAsyncOp<FAuthLogin>& InAsyncOp, const EOS_Auth_LoginCallbackInfo* Data)
		{
			UE_LOG(LogTemp, Warning, TEXT("LoginResult: [%s]"), ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));

			if (Data->ResultCode == EOS_EResult::EOS_Success)
			{
				// Success
				TSharedRef<FAccountInfoEOS> AccountInfo = MakeShared<FAccountInfoEOS>();
				AccountInfo->LocalUserNum = InAsyncOp.GetParams().LocalUserNum;
				AccountInfo->UserId = MakeEOSAccountId(Data->LocalUserId);

				check(!AccountInfos.Contains(AccountInfo->UserId));
				AccountInfos.Emplace(AccountInfo->UserId, AccountInfo);

				FAuthLogin::Result Result = { AccountInfo };
				InAsyncOp.SetResult(MoveTemp(Result));
			}
			else if (Data->ResultCode == EOS_EResult::EOS_InvalidUser && Data->ContinuanceToken != nullptr)
			{
				// 
			}
			else
			{
				FOnlineError Error;
				if (Data->ResultCode == EOS_EResult::EOS_InvalidAuth)
				{
					Error = Errors::InvalidCredentials();
				}
				else
				{
					Error = Errors::Unknown();
				}

				InAsyncOp.SetError(MoveTemp(Error));
			}
		})
		.Enqueue();
	return Op.GetHandle();
}

TOnlineAsyncOpHandle<FAuthLogout> FAuthEOS::Logout(FAuthLogout::Params&& Params)
{
	EOS_HPlatform PlatformHandle = SetupEOSSdk();
	EOS_HAuth AuthHandle = EOS_Platform_GetAuthInterface(PlatformHandle);
	check(AuthHandle != nullptr);
	
	FAccountId ParamLocalUserId = Params.LocalUserId;
	TOptional<EOS_EpicAccountId> AccountId = EOSAccountIdFromOnlineServiceAccountId(ParamLocalUserId);
	TOnlineAsyncOp<FAuthLogout>& Op = Services.OpCache.GetOp<FAuthLogout>(MoveTemp(Params));
	if (AccountId.IsSet() && AccountInfos.Contains(ParamLocalUserId))
	{
		// Should we destroy persistent auth first?
		TOnlineChainableAsyncOp<FAuthLogout, void> NextOp = Op.Then([](TOnlineAsyncOp<FAuthLogout>& InAsyncOp){ return; });
		if (Params.bDestroyAuth)
		{
			EOS_Auth_DeletePersistentAuthOptions DeletePersistentAuthOptions = {0};
			DeletePersistentAuthOptions.ApiVersion = EOS_AUTH_DELETEPERSISTENTAUTH_API_LATEST;
			DeletePersistentAuthOptions.RefreshToken = nullptr; // Is this needed?  Docs say it's needed for consoles
			NextOp = NextOp.Then([this, AuthHandle, DeletePersistentAuthOptions](TOnlineAsyncOp<FAuthLogout>& InAsyncOp)
				{
					return EOS_Async<EOS_Auth_DeletePersistentAuthCallbackInfo>(InAsyncOp, EOS_Auth_DeletePersistentAuth, AuthHandle, DeletePersistentAuthOptions);
				})
				.Then([](TOnlineAsyncOp<FAuthLogout>& InAsyncOp, const EOS_Auth_DeletePersistentAuthCallbackInfo* Data)
				{
					UE_LOG(LogTemp, Warning, TEXT("DeletePersistentAuthResult: [%s]"), ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
					// Regardless of success/failure, continue
				});
		}
		// Logout
		NextOp.Then([this, AuthHandle, AccountId](TOnlineAsyncOp<FAuthLogout>& InAsyncOp)
			{
				EOS_Auth_LogoutOptions LogoutOptions = { };
				LogoutOptions.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST;
				LogoutOptions.LocalUserId = AccountId.GetValue();
				return EOS_Async<EOS_Auth_LogoutCallbackInfo>(InAsyncOp, EOS_Auth_Logout, AuthHandle, LogoutOptions);
			})
			.Then([](TOnlineAsyncOp<FAuthLogout>& InAsyncOp, const EOS_Auth_LogoutCallbackInfo* Data)
			{
				UE_LOG(LogTemp, Warning, TEXT("LogoutResult: [%s]"), ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));

				if (Data->ResultCode == EOS_EResult::EOS_Success)
				{
					// Success
					InAsyncOp.SetResult(FAuthLogout::Result());
				}
				else
				{
					// TODO: Error codes
					FOnlineError Error;
					InAsyncOp.SetError(MoveTemp(Error));
				}
			}).Enqueue();
	}
	else
	{
		// TODO: Error codes
		Op.SetError(Errors::UnknownError());
	}
	
	return Op.GetHandle();
}

TOnlineAsyncOpHandle<FAuthGenerateAuth> FAuthEOS::GenerateAuth(FAuthGenerateAuth::Params&& Params)
{
	TSharedRef<TOnlineAsyncOp<FAuthGenerateAuth>> AsyncOperation = MakeShared<TOnlineAsyncOp<FAuthGenerateAuth>>(Services, MoveTemp(Params));
	return AsyncOperation->GetHandle();
}

TOnlineResult<FAuthGetAccountByLocalUserNum::Result> FAuthEOS::GetAccountByLocalUserNum(FAuthGetAccountByLocalUserNum::Params&& Params)
{
	TOnlineResult<FAccountId> LocalUserIdResult = GetAccountIdByLocalUserNum(Params.LocalUserNum);
	if (LocalUserIdResult.IsOk())
	{
		FAuthGetAccountByLocalUserNum::Result Result;
		Result.AccountInfo = AccountInfos.FindChecked(LocalUserIdResult.GetOkValue());
		return TOnlineResult<FAuthGetAccountByLocalUserNum::Result>(Result);
	}
	else
	{
		return TOnlineResult<FAuthGetAccountByLocalUserNum::Result>(LocalUserIdResult.GetErrorValue());
	}
}

TOnlineResult<FAuthGetAccountByAccountId::Result> FAuthEOS::GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params)
{
	if (TSharedRef<FAccountInfoEOS>* const FoundAccount = AccountInfos.Find(Params.LocalUserId))
	{
		FAuthGetAccountByAccountId::Result Result;
		Result.AccountInfo = *FoundAccount;
		return TOnlineResult<FAuthGetAccountByAccountId::Result>(Result);
	}
	else
	{
		// TODO: proper error
		return TOnlineResult<FAuthGetAccountByAccountId::Result>(Errors::UnknownError());
	}
}

TOnlineResult<FAccountId> FAuthEOS::GetAccountIdByLocalUserNum(int32 LocalUserNum) const
{
	for (const TPair<FAccountId, TSharedRef<FAccountInfoEOS>>& AccountPair : AccountInfos)
	{
		if (AccountPair.Value->LocalUserNum == LocalUserNum)
		{
			TOnlineResult<FAccountId> Result(AccountPair.Key);
			return Result;
		}
	}
	TOnlineResult<FAccountId> Result(Errors::UnknownError()); // TODO: error code
	return Result;
}

/* UE::Online */ }

#endif // WITH_EOS_SDK
