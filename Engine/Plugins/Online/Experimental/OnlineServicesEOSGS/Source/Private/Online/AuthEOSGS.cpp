// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/AuthEOSGS.h"

#include "Algo/Transform.h"
#include "Misc/CommandLine.h"
#include "Online/AuthErrors.h"
#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineErrorDefinitions.h"
#include "Online/OnlineIdEOSGS.h"
#include "Online/OnlineServicesEOSGS.h"
#include "Online/OnlineServicesEOSGSTypes.h"

#include "eos_auth.h"
#include "eos_common.h"
#include "eos_connect.h"
#include "eos_init.h"
#include "eos_logging.h"
#include "eos_sdk.h"
#include "eos_types.h"
#include "eos_userinfo.h"

namespace UE::Online {

struct FAuthEOSConfig
{
	FString DefaultExternalCredentialTypeStr;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FAuthEOSConfig)
	ONLINE_STRUCT_FIELD(FAuthEOSConfig, DefaultExternalCredentialTypeStr)
END_ONLINE_STRUCT_META()

/* Meta*/ }

ELoginStatus ToELoginStatus(EOS_ELoginStatus InStatus)
{
	switch (InStatus)
	{
	case EOS_ELoginStatus::EOS_LS_NotLoggedIn:
	{
		return ELoginStatus::NotLoggedIn;
	}
	case EOS_ELoginStatus::EOS_LS_UsingLocalProfile:
	{
		return ELoginStatus::UsingLocalProfile;
	}
	case EOS_ELoginStatus::EOS_LS_LoggedIn:
	{
		return ELoginStatus::LoggedIn;
	}
	}
	return ELoginStatus::NotLoggedIn;
}

FEOSAuthCredentials::FEOSAuthCredentials() :
	EOS_Auth_Credentials()
{
	ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
	Id = IdAnsi;
	Token = TokenAnsi;
}


FEOSAuthCredentials::FEOSAuthCredentials(const FEOSAuthCredentials& Other)
{
	ApiVersion = Other.ApiVersion;
	Id = IdAnsi;
	Token = TokenAnsi;
	Type = Other.Type;
	SystemAuthCredentialsOptions = Other.SystemAuthCredentialsOptions;
	ExternalType = Other.ExternalType;

	FCStringAnsi::Strncpy(IdAnsi, Other.IdAnsi, EOS_STRING_BUFFER_LENGTH);
	FCStringAnsi::Strncpy(TokenAnsi, Other.TokenAnsi, EOS_MAX_TOKEN_SIZE);
}

FEOSAuthCredentials::FEOSAuthCredentials(EOS_EExternalCredentialType InExternalType, const TArray<uint8>& InToken) :
	EOS_Auth_Credentials()
{
	ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
	Id = IdAnsi;
	Token = TokenAnsi;

	Type = EOS_ELoginCredentialType::EOS_LCT_ExternalAuth;
	ExternalType = InExternalType;

	uint32_t InOutBufferLength = EOS_MAX_TOKEN_SIZE;
	EOS_ByteArray_ToString(InToken.GetData(), InToken.Num(), TokenAnsi, &InOutBufferLength);
}

FEOSAuthCredentials& FEOSAuthCredentials::operator=(FEOSAuthCredentials& Other)
{
	ApiVersion = Other.ApiVersion;
	Type = Other.Type;
	SystemAuthCredentialsOptions = Other.SystemAuthCredentialsOptions;
	ExternalType = Other.ExternalType;

	FCStringAnsi::Strncpy(IdAnsi, Other.IdAnsi, EOS_STRING_BUFFER_LENGTH);
	FCStringAnsi::Strncpy(TokenAnsi, Other.TokenAnsi, EOS_MAX_TOKEN_SIZE);

	return *this;
}

void FEOSAuthCredentials::SetToken(const FCredentialsToken& InToken)
{
	if (InToken.IsType<TArray<uint8>>())
	{
		const TArray<uint8>& TokenData = InToken.Get<TArray<uint8>>();
		uint32_t InOutBufferLength = EOS_MAX_TOKEN_SIZE;
		EOS_ByteArray_ToString(TokenData.GetData(), TokenData.Num(), TokenAnsi, &InOutBufferLength);
	}
	else if (InToken.IsType<FString>())
	{
		const FString& TokenString = InToken.Get<FString>();
		FCStringAnsi::Strncpy(TokenAnsi, TCHAR_TO_UTF8(*TokenString), EOS_MAX_TOKEN_SIZE);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SetToken cannot be set with an invalid credentials token parameter. Please ensure there is valid data in the credentials token."));
	}
}

FEOSConnectLoginCredentials::FEOSConnectLoginCredentials()
{
	ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
}

class FEOSConnectLoginCredentialsEOS : public FEOSConnectLoginCredentials
{
public:
	FEOSConnectLoginCredentialsEOS()
	{
		Token = TokenAnsi;
		TokenAnsi[0] = '\0';
	}
	void SetToken(const FCredentialsToken& InToken)
	{
		if (InToken.IsType<TArray<uint8>>())
		{
			const TArray<uint8>& TokenData = InToken.Get<TArray<uint8>>();
			uint32_t InOutBufferLength = EOS_MAX_TOKEN_SIZE;
			EOS_ByteArray_ToString(TokenData.GetData(), TokenData.Num(), TokenAnsi, &InOutBufferLength);
		}
		else if (InToken.IsType<FString>())
		{
			const FString& TokenString = InToken.Get<FString>();
			FCStringAnsi::Strncpy(TokenAnsi, TCHAR_TO_UTF8(*TokenString), EOS_MAX_TOKEN_SIZE);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SetToken cannot be set with an invalid credentials token parameter. Please ensure there is valid data in the credentials token."));
		}
	}

	char TokenAnsi[EOS_MAX_TOKEN_SIZE];
};

FEOSConnectLoginCredentialsEAS::FEOSConnectLoginCredentialsEAS(EOS_Auth_Token* InEASToken) : EASToken(InEASToken)
{
	check(EASToken);
	Type = EOS_EExternalCredentialType::EOS_ECT_EPIC;
	Token = EASToken->AccessToken;
}

FEOSConnectLoginCredentialsEAS::~FEOSConnectLoginCredentialsEAS()
{
	if (EASToken)
	{
		EOS_Auth_Token_Release(EASToken);
	}
}

FAuthEOSGS::FAuthEOSGS(FOnlineServicesEOSGS& InServices)
	: Super(InServices)
{
}

void FAuthEOSGS::Initialize()
{
	Super::Initialize();

	AuthHandle = EOS_Platform_GetAuthInterface(static_cast<FOnlineServicesEOSGS&>(GetServices()).GetEOSPlatformHandle());
	check(AuthHandle != nullptr);

	ConnectHandle = EOS_Platform_GetConnectInterface(static_cast<FOnlineServicesEOSGS&>(GetServices()).GetEOSPlatformHandle());
	check(ConnectHandle != nullptr);
}

void SplitCredentialsType(const FString& InStr, FString& OutCredentialTypeStr, FString& OutExternalCredentialTypeStr)
{
	InStr.Split(FString(TEXT(":")), &OutCredentialTypeStr, &OutExternalCredentialTypeStr);
	if (OutCredentialTypeStr.IsEmpty())
	{
		OutCredentialTypeStr = InStr;
	}
}

TSharedPtr<FEOSConnectLoginCredentials> MakeConnectLoginCredentials(TOnlineAsyncOp<FAuthLogin>& InAsyncOp)
{
	const FAuthLogin::Params& LoginParams = InAsyncOp.GetParams();

	TSharedRef<FEOSConnectLoginCredentialsEOS> Credentials = MakeShared<FEOSConnectLoginCredentialsEOS>();
	LexFromString(Credentials->Type, *LoginParams.CredentialsType);
	Credentials->SetToken(LoginParams.CredentialsToken);
	return Credentials;
}

TOnlineAsyncOpHandle<FAuthLogin> FAuthEOSGS::Login(FAuthLogin::Params&& Params)
{
#if (WITH_EDITOR || !UE_BUILD_SHIPPING)
	// Handle developer login
	if (Params.CredentialsId.IsEmpty() && Params.CredentialsType.IsEmpty() && Params.CredentialsToken.IsType<FString>() && Params.CredentialsToken.Get<FString>().IsEmpty())
	{
		FString	CommandLineAuthId;
		FString	CommandLineAuthToken;
		FString	CommandLineAuthType;
		FParse::Value(FCommandLine::Get(), TEXT("AUTH_LOGIN="), CommandLineAuthId);
		FParse::Value(FCommandLine::Get(), TEXT("AUTH_PASSWORD="), CommandLineAuthToken);
		FParse::Value(FCommandLine::Get(), TEXT("AUTH_TYPE="), CommandLineAuthType);
		if (!CommandLineAuthId.IsEmpty() && !CommandLineAuthToken.IsEmpty() && !CommandLineAuthType.IsEmpty())
		{
			Params.CredentialsId = MoveTemp(CommandLineAuthId);
			Params.CredentialsToken.Emplace<FString>(MoveTemp(CommandLineAuthToken));
			Params.CredentialsType = MoveTemp(CommandLineAuthType);
		}
	}
#endif

	TOnlineAsyncOpRef<FAuthLogin> Op = GetOp<FAuthLogin>(MoveTemp(Params));
	// Are we already logged in?
	if (GetAccountIdByPlatformUserId(Op->GetParams().PlatformUserId).IsOk())
	{
		Op->SetError(Errors::Auth::AlreadyLoggedIn());
		return Op->GetHandle();
	}

	TOnlineChainableAsyncOp<FAuthLogin, TSharedPtr<FEOSConnectLoginCredentials>> ConnectLoginOp = [this, &Op]()
	{
		FString LoginCredentialTypeStr, Unused;
		SplitCredentialsType(Op->GetParams().CredentialsType, LoginCredentialTypeStr, Unused);

		// If the Credentials type is an EAS Login credential type, then login to EOS_Auth with that and get an EAS token to login to EOS_Connect with
		EOS_ELoginCredentialType EasLoginType;
		if (LexFromString(EasLoginType, *LoginCredentialTypeStr))
		{
			return LoginEAS(*Op);
		}

		// The credentials type is presumably an EOS_EExternalCredentialType, so just login to connect with that
		return Op->Then([this](TOnlineAsyncOp<FAuthLogin>& InAsyncOp)
		{
			return MakeConnectLoginCredentials(InAsyncOp);
		});
	}();
	// Check if the above steps completed (failed) the operation
	if (ConnectLoginOp.GetOwningOperation().IsComplete())
	{
		return ConnectLoginOp.GetOwningOperation().GetHandle();
	}
	ConnectLoginOp.Then([this](TOnlineAsyncOp<FAuthLogin>& InAsyncOp, TSharedPtr<FEOSConnectLoginCredentials>&& InConnectLoginCredentials, TPromise<const EOS_Connect_LoginCallbackInfo*>&& Promise)
	{
		EOS_Connect_LoginOptions ConnectLoginOptions = { };
		ConnectLoginOptions.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
		ConnectLoginOptions.Credentials = InConnectLoginCredentials.Get();

		EOS_Async(EOS_Connect_Login, ConnectHandle, ConnectLoginOptions, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FAuthLogin>& InAsyncOp, const EOS_Connect_LoginCallbackInfo* Data, TPromise<const EOS_Connect_CreateUserCallbackInfo*>&& Promise)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[FAuthEOSGS::Login] EOS_Connect_Login Result: [%s]"), *LexToString(Data->ResultCode));

		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			ProcessSuccessfulLogin(InAsyncOp, Data->LocalUserId);
		}
		else if (Data->ResultCode == EOS_EResult::EOS_InvalidUser && Data->ContinuanceToken != nullptr)
		{
			EOS_Connect_CreateUserOptions ConnectCreateUserOptions = { };
			ConnectCreateUserOptions.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
			ConnectCreateUserOptions.ContinuanceToken = Data->ContinuanceToken;

			EOS_Async(EOS_Connect_CreateUser, ConnectHandle, ConnectCreateUserOptions, MoveTemp(Promise));
			return;
		}
		else
		{
			// TODO: EAS Logout
			InAsyncOp.SetError(Errors::Unknown()); // TODO
		}

		Promise.EmplaceValue();
	})
	.Then([this](TOnlineAsyncOp<FAuthLogin>& InAsyncOp, const EOS_Connect_CreateUserCallbackInfo* Data)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[FAuthEOSGS::Login] EOS_Connect_CreateUser Result: [%s]"), *LexToString(Data->ResultCode));

		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			ProcessSuccessfulLogin(InAsyncOp, Data->LocalUserId);
		}
		else
		{
			// TODO: EAS Logout

			InAsyncOp.SetError(Errors::Unknown()); // TODO
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineChainableAsyncOp<FAuthLogin, TSharedPtr<FEOSConnectLoginCredentials>> FAuthEOSGS::LoginEAS(TOnlineAsyncOp<FAuthLogin>& InAsyncOp)
{
	const FAuthLogin::Params& Params = InAsyncOp.GetParams();
	auto FailureLambda = [](TOnlineAsyncOp<FAuthLogin>& Op) -> TSharedPtr<FEOSConnectLoginCredentials>
	{
		return nullptr;
	};

	EOS_Auth_LoginOptions LoginOptions = { };
	LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;

	FString CredentialTypeStr;
	FString ExternalCredentialTypeStr;
	SplitCredentialsType(Params.CredentialsType, CredentialTypeStr, ExternalCredentialTypeStr);

	FEOSAuthCredentials Credentials;
	if (LexFromString(Credentials.Type, *CredentialTypeStr))
	{
		switch (Credentials.Type)
		{
		case EOS_ELoginCredentialType::EOS_LCT_ExternalAuth:
			// If an external credential type wasn't specified, we'll grab the platform default from the configuration values
			if (ExternalCredentialTypeStr.IsEmpty())
			{
				FAuthEOSConfig AuthEOSConfig;
				LoadConfig(AuthEOSConfig);
				ExternalCredentialTypeStr = AuthEOSConfig.DefaultExternalCredentialTypeStr;
			}

			EOS_EExternalCredentialType ExternalType;
			if (LexFromString(ExternalType, *ExternalCredentialTypeStr))
			{
				Credentials.ExternalType = ExternalType;
			}

			Credentials.SetToken(Params.CredentialsToken);
			break;
		case EOS_ELoginCredentialType::EOS_LCT_ExchangeCode:
			// This is how the Epic launcher will pass credentials to you
			Credentials.IdAnsi[0] = '\0';
			FCStringAnsi::Strncpy(Credentials.TokenAnsi, TCHAR_TO_UTF8(*Params.CredentialsToken.Get<FString>()), EOS_MAX_TOKEN_SIZE);
			break;
		case EOS_ELoginCredentialType::EOS_LCT_Password:
			FCStringAnsi::Strncpy(Credentials.IdAnsi, TCHAR_TO_UTF8(*Params.CredentialsId), EOS_STRING_BUFFER_LENGTH);
			FCStringAnsi::Strncpy(Credentials.TokenAnsi, TCHAR_TO_UTF8(*Params.CredentialsToken.Get<FString>()), EOS_MAX_TOKEN_SIZE);
			break;
		case EOS_ELoginCredentialType::EOS_LCT_Developer:
			// This is auth via the EOS auth tool
			FCStringAnsi::Strncpy(Credentials.IdAnsi, TCHAR_TO_UTF8(*Params.CredentialsId), EOS_STRING_BUFFER_LENGTH);
			FCStringAnsi::Strncpy(Credentials.TokenAnsi, TCHAR_TO_UTF8(*Params.CredentialsToken.Get<FString>()), EOS_MAX_TOKEN_SIZE);
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
			UE_LOG(LogTemp, Warning, TEXT("Unsupported CredentialsType=[%s]"), *Params.CredentialsType);
			InAsyncOp.SetError(Errors::Unknown()); // TODO
			return InAsyncOp.Then(FailureLambda);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid CredentialsType=[%s]"), *Params.CredentialsType);
		InAsyncOp.SetError(Errors::Unknown()); // TODO
		return InAsyncOp.Then(FailureLambda);
	}

	return InAsyncOp.Then([this, LoginOptions, Credentials](TOnlineAsyncOp<FAuthLogin>& InAsyncOp, TPromise<const EOS_Auth_LoginCallbackInfo*>&& Promise) mutable
	{
		LoginOptions.Credentials = &Credentials;
		EOS_Async(EOS_Auth_Login, AuthHandle, LoginOptions, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FAuthLogin>& InAsyncOp, const EOS_Auth_LoginCallbackInfo* Data, TPromise<const EOS_Auth_LinkAccountCallbackInfo*>&& Promise)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[FAuthEOS::Login] EOS_Auth_Login Result: [%s]"), *LexToString(Data->ResultCode));

		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			// We cache the Epic Account Id to use it in later stages of the login process
			InAsyncOp.Data.Set(TEXT("EpicAccountId"), Data->LocalUserId);

			// A success means the account is already linked, we'll process the NULL result in the next step
		}
		else if (Data->ResultCode == EOS_EResult::EOS_InvalidUser && Data->ContinuanceToken != nullptr)
		{
			EOS_Auth_LinkAccountOptions LinkAccountOptions = {};
			LinkAccountOptions.ApiVersion = EOS_AUTH_LINKACCOUNT_API_LATEST;
			LinkAccountOptions.ContinuanceToken = Data->ContinuanceToken;

			EOS_Async(EOS_Auth_LinkAccount, AuthHandle, LinkAccountOptions, MoveTemp(Promise));
			return;
		}
		else
		{
			FOnlineError Error = Errors::Unknown();
			if (Data->ResultCode == EOS_EResult::EOS_InvalidAuth)
			{
				Error = Errors::InvalidCreds();
			}

			InAsyncOp.SetError(MoveTemp(Error));
		}

		Promise.EmplaceValue();
	})
	.Then([this](TOnlineAsyncOp<FAuthLogin>& InAsyncOp, const EOS_Auth_LinkAccountCallbackInfo* Data) -> TFuture<TSharedPtr<FEOSConnectLoginCredentials>>
	{
		UE_LOG(LogTemp, Verbose, TEXT("[FAuthEOS::Login] EOS_Auth_LinkAccount Result: [%s]"), (Data == nullptr) ? TEXT("Null") : *LexToString(Data->ResultCode));

		// If Data is NULL, it means the account was already linked
		if (Data == nullptr || Data->ResultCode == EOS_EResult::EOS_Success)
		{
			EOS_EpicAccountId EpicAccountId = nullptr;
			if (Data == nullptr)
			{
				EpicAccountId = *InAsyncOp.Data.Get<EOS_EpicAccountId>(TEXT("EpicAccountId"));
			}
			else
			{
				// We cache the Epic Account Id to use it in later stages of the login process
				InAsyncOp.Data.Set(TEXT("EpicAccountId"), Data->LocalUserId);
				EpicAccountId = Data->LocalUserId;
			}

			// On success, attempt Connect Login
			EOS_Auth_Token* AuthToken = nullptr;
			EOS_Auth_CopyUserAuthTokenOptions CopyOptions = { };
			CopyOptions.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;

			EOS_EResult CopyResult = EOS_Auth_CopyUserAuthToken(AuthHandle, &CopyOptions, EpicAccountId, &AuthToken);

			UE_LOG(LogTemp, Verbose, TEXT("[FAuthEOS::Login] EOS_Auth_CopyUserAuthToken Result: [%s]"), *LexToString(CopyResult));

			if (CopyResult == EOS_EResult::EOS_Success)
			{
				TSharedPtr<FEOSConnectLoginCredentials> ConnectLoginCredentials = MakeShared<FEOSConnectLoginCredentialsEAS>(AuthToken);
				return MakeFulfilledPromise<TSharedPtr<FEOSConnectLoginCredentials>>(MoveTemp(ConnectLoginCredentials)).GetFuture();
			}
			else
			{
				// TODO: EAS Logout
				InAsyncOp.SetError(Errors::Unknown()); // TODO
			}
		}
		else
		{
			InAsyncOp.SetError(Errors::Unknown()); // TODO
		}

		return MakeFulfilledPromise<TSharedPtr<FEOSConnectLoginCredentials>>(nullptr).GetFuture();
	});
}

void FAuthEOSGS::ProcessSuccessfulLogin(TOnlineAsyncOp<FAuthLogin>& InAsyncOp, const EOS_ProductUserId ProductUserId)
{
	const FOnlineAccountIdHandle LocalUserId = CreateAccountId(ProductUserId);

	UE_LOG(LogTemp, Verbose, TEXT("[FAuthEOSGS::Login] Successfully logged in as [%s]"), *ToLogString(LocalUserId));

	TSharedRef<FAccountInfoEOS> AccountInfo = MakeShared<FAccountInfoEOS>();
	AccountInfo->PlatformUserId = InAsyncOp.GetParams().PlatformUserId;
	AccountInfo->UserId = LocalUserId;
	AccountInfo->LoginStatus = ELoginStatus::LoggedIn;

	check(!AccountInfos.Contains(LocalUserId));
	AccountInfos.Emplace(LocalUserId, AccountInfo);

	FAuthLogin::Result Result = { AccountInfo };
	InAsyncOp.SetResult(MoveTemp(Result));

	FLoginStatusChanged EventParameters;
	EventParameters.LocalUserId = LocalUserId;
	EventParameters.PreviousStatus = ELoginStatus::NotLoggedIn;
	EventParameters.CurrentStatus = ELoginStatus::LoggedIn;
	OnLoginStatusChangedEvent.Broadcast(EventParameters);
}

TOnlineAsyncOpHandle<FAuthLogout> FAuthEOSGS::Logout(FAuthLogout::Params&& Params)
{
	const FOnlineAccountIdHandle LocalUserId = Params.LocalUserId;
	TOnlineAsyncOpRef<FAuthLogout> Op = GetOp<FAuthLogout>(MoveTemp(Params));

	if (!ValidateOnlineId(LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	Op->SetResult(FAuthLogout::Result());
	return Op->GetHandle();
}

TOnlineResult<FAuthGetAccountByPlatformUserId> FAuthEOSGS::GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params)
{
	TResult<FOnlineAccountIdHandle, FOnlineError> PlatformUserIdResult = GetAccountIdByPlatformUserId(Params.PlatformUserId);
	if (PlatformUserIdResult.IsOk())
	{
		FAuthGetAccountByPlatformUserId::Result Result = { AccountInfos.FindChecked(PlatformUserIdResult.GetOkValue()) };
		return TOnlineResult<FAuthGetAccountByPlatformUserId>(Result);
	}
	else
	{
		return TOnlineResult<FAuthGetAccountByPlatformUserId>(PlatformUserIdResult.GetErrorValue());
	}
}

TOnlineResult<FAuthGetAccountByAccountId> FAuthEOSGS::GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params)
{
	if (TSharedRef<FAccountInfoEOS>* const FoundAccount = AccountInfos.Find(Params.LocalUserId))
	{
		FAuthGetAccountByAccountId::Result Result = { *FoundAccount };
		return TOnlineResult<FAuthGetAccountByAccountId>(Result);
	}
	else
	{
		// TODO: proper error
		return TOnlineResult<FAuthGetAccountByAccountId>(Errors::Unknown());
	}
}

bool FAuthEOSGS::IsLoggedIn(const FOnlineAccountIdHandle& AccountId) const
{
	// TODO:  More logic?
	return AccountInfos.Contains(AccountId);
}

TResult<FOnlineAccountIdHandle, FOnlineError> FAuthEOSGS::GetAccountIdByPlatformUserId(FPlatformUserId PlatformUserId) const
{
	for (const TPair<FOnlineAccountIdHandle, TSharedRef<FAccountInfoEOS>>& AccountPair : AccountInfos)
	{
		if (AccountPair.Value->PlatformUserId == PlatformUserId)
		{
			TResult<FOnlineAccountIdHandle, FOnlineError> Result(AccountPair.Key);
			return Result;
		}
	}
	TResult<FOnlineAccountIdHandle, FOnlineError> Result(Errors::Unknown()); // TODO: error code
	return Result;
}

TFuture<FOnlineAccountIdHandle> FAuthEOSGS::ResolveAccountId(const FOnlineAccountIdHandle& LocalUserId, const EOS_ProductUserId ProductUserId)
{
	return MakeFulfilledPromise<FOnlineAccountIdHandle>(CreateAccountId(ProductUserId)).GetFuture();
}

TFuture<TArray<FOnlineAccountIdHandle>> FAuthEOSGS::ResolveAccountIds(const FOnlineAccountIdHandle& LocalUserId, const TArray<EOS_ProductUserId>& InProductUserIds)
{
	TArray<FOnlineAccountIdHandle> AccountIds;
	AccountIds.Reserve(InProductUserIds.Num());
	for (const EOS_ProductUserId ProductUserId : InProductUserIds)
	{
		AccountIds.Emplace(CreateAccountId(ProductUserId));
	}
	return MakeFulfilledPromise<TArray<FOnlineAccountIdHandle>>(MoveTemp(AccountIds)).GetFuture();
}

TFunction<TFuture<FOnlineAccountIdHandle>(FOnlineAsyncOp& InAsyncOp, const EOS_ProductUserId& ProductUserId)> FAuthEOSGS::ResolveProductIdFn()
{
	return [this](FOnlineAsyncOp& InAsyncOp, const EOS_ProductUserId& ProductUserId)
	{
		const FOnlineAccountIdHandle* LocalUserIdPtr = InAsyncOp.Data.Get<FOnlineAccountIdHandle>(TEXT("LocalUserId"));
		if (!ensure(LocalUserIdPtr))
		{
			return MakeFulfilledPromise<FOnlineAccountIdHandle>().GetFuture();
		}
		return ResolveAccountId(*LocalUserIdPtr, ProductUserId);
	};
}

TFunction<TFuture<TArray<FOnlineAccountIdHandle>>(FOnlineAsyncOp& InAsyncOp, const TArray<EOS_ProductUserId>& ProductUserIds)> FAuthEOSGS::ResolveProductIdsFn()
{
	return [this](FOnlineAsyncOp& InAsyncOp, const TArray<EOS_ProductUserId>& ProductUserIds)
	{
		const FOnlineAccountIdHandle* LocalUserIdPtr = InAsyncOp.Data.Get<FOnlineAccountIdHandle>(TEXT("LocalUserId"));
		if (!ensure(LocalUserIdPtr))
		{
			return MakeFulfilledPromise<TArray<FOnlineAccountIdHandle>>().GetFuture();
		}
		return ResolveAccountIds(*LocalUserIdPtr, ProductUserIds);
	};
}

FOnlineAccountIdHandle FAuthEOSGS::CreateAccountId(const EOS_ProductUserId ProductUserId)
{
	return FOnlineAccountIdRegistryEOSGS::Get().FindOrAddAccountId(ProductUserId);
}

/* UE::Online */ }
