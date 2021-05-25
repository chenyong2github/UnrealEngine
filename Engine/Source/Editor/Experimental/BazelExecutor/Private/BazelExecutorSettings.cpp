// Copyright Epic Games, Inc. All Rights Reserved.

#include "BazelExecutorSettings.h"
#include "Misc/ConfigCacheIni.h"

UBazelExecutorSettings::UBazelExecutorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ContentAddressableStorageTarget = TEXT("jupiter.devtools-dev.epicgames.com:8080");
	ExecutionTarget = TEXT("hordeserver-grpc.devtools-dev.epicgames.com:443");

	ContentAddressableStorageHeaders.Add("accept", "application/json");
	ContentAddressableStorageHeaders.Add("authorization", "ServiceAccount HordeREAPI");

	ExecutionHeaders.Add("accept", "application/json");
	ExecutionHeaders.Add("authorization", "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJodHRwOi8vc2NoZW1hcy54bWxzb2FwLm9yZy93cy8yMDA1LzA1L2lkZW50aXR5L2NsYWltcy9uYW1lIjoiQmVuIE1hcnNoIiwiaHR0cDovL2VwaWNnYW1lcy5jb20vdWUvaG9yZGUvcm9sZSI6ImFnZW50LXJlZ2lzdHJhdGlvbiIsImlzcyI6Imh0dHBzOi8vaG9yZGVzZXJ2ZXIuZGV2dG9vbHMtZGV2LmVwaWNnYW1lcy5jb20ifQ.qwGHN_BYqpJX2Y54nlJ6J_rinmmf5C1Srxymzk7hp2c");

	MaxSendMessageSize = 1024 * 1024 * 1024;
	MaxReceiveMessageSize = 1024 * 1024 * 1024;

	ContentAddressableStoragePemRootCertificates = TEXT(
		"-----BEGIN CERTIFICATE-----\n"
		"MIIEDzCCAvegAwIBAgIBADANBgkqhkiG9w0BAQUFADBoMQswCQYDVQQGEwJVUzEl\n"
		"MCMGA1UEChMcU3RhcmZpZWxkIFRlY2hub2xvZ2llcywgSW5jLjEyMDAGA1UECxMp\n"
		"U3RhcmZpZWxkIENsYXNzIDIgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMDQw\n"
		"NjI5MTczOTE2WhcNMzQwNjI5MTczOTE2WjBoMQswCQYDVQQGEwJVUzElMCMGA1UE\n"
		"ChMcU3RhcmZpZWxkIFRlY2hub2xvZ2llcywgSW5jLjEyMDAGA1UECxMpU3RhcmZp\n"
		"ZWxkIENsYXNzIDIgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwggEgMA0GCSqGSIb3\n"
		"DQEBAQUAA4IBDQAwggEIAoIBAQC3Msj+6XGmBIWtDBFk385N78gDGIc/oav7PKaf\n"
		"8MOh2tTYbitTkPskpD6E8J7oX+zlJ0T1KKY/e97gKvDIr1MvnsoFAZMej2YcOadN\n"
		"+lq2cwQlZut3f+dZxkqZJRRU6ybH838Z1TBwj6+wRir/resp7defqgSHo9T5iaU0\n"
		"X9tDkYI22WY8sbi5gv2cOj4QyDvvBmVmepsZGD3/cVE8MC5fvj13c7JdBmzDI1aa\n"
		"K4UmkhynArPkPw2vCHmCuDY96pzTNbO8acr1zJ3o/WSNF4Azbl5KXZnJHoe0nRrA\n"
		"1W4TNSNe35tfPe/W93bC6j67eA0cQmdrBNj41tpvi/JEoAGrAgEDo4HFMIHCMB0G\n"
		"A1UdDgQWBBS/X7fRzt0fhvRbVazc1xDCDqmI5zCBkgYDVR0jBIGKMIGHgBS/X7fR\n"
		"zt0fhvRbVazc1xDCDqmI56FspGowaDELMAkGA1UEBhMCVVMxJTAjBgNVBAoTHFN0\n"
		"YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4xMjAwBgNVBAsTKVN0YXJmaWVsZCBD\n"
		"bGFzcyAyIENlcnRpZmljYXRpb24gQXV0aG9yaXR5ggEAMAwGA1UdEwQFMAMBAf8w\n"
		"DQYJKoZIhvcNAQEFBQADggEBAAWdP4id0ckaVaGsafPzWdqbAYcaT1epoXkJKtv3\n"
		"L7IezMdeatiDh6GX70k1PncGQVhiv45YuApnP+yz3SFmH8lU+nLMPUxA2IGvd56D\n"
		"eruix/U0F47ZEUD0/CwqTRV/p2JdLiXTAAsgGh1o+Re49L2L7ShZ3U0WixeDyLJl\n"
		"xy16paq8U4Zt3VekyvggQQto8PT7dL5WXXp59fkdheMtlb71cZBDzI0fmgAKhynp\n"
		"VSJYACPq4xJDKVtHCN2MQWplBqjlIapBtJUhlbl90TSrE9atvNziPTnNvT51cKEY\n"
		"WQPJIrSPnNVeKtelttQKbfi3QBFGmh95DmK/D5fs4C8fF5Q=\n"
		"-----END CERTIFICATE-----"
	);

	ExecutionPemRootCertificates = TEXT(
		"-----BEGIN CERTIFICATE-----\n"
		"MIIDYTCCAkmgAwIBAgIIK9cc+F8gVfgwDQYJKoZIhvcNAQELBQAwNjE0MDIGA1UE\n"
		"AxMraG9yZGVzZXJ2ZXItZ3JwYy5kZXZ0b29scy1kZXYuZXBpY2dhbWVzLmNvbTAe\n"
		"Fw0yMDExMTAwMjE2MjZaFw0yMzAxMjAwMjE2MjZaMDYxNDAyBgNVBAMTK2hvcmRl\n"
		"c2VydmVyLWdycGMuZGV2dG9vbHMtZGV2LmVwaWNnYW1lcy5jb20wggEiMA0GCSqG\n"
		"SIb3DQEBAQUAA4IBDwAwggEKAoIBAQDErWwF1mzE7vM0xjBpgkHRuqiItJv+fs/5\n"
		"A7CniQad9wnTliPWPzMFpNYp1SubrZzMc55Q+f0MkF7LjTdOwYo5gUY+A6x9pt4B\n"
		"UNu4FP+XINI7j3WnK7mZgueh/JkQ28MGGYL1v7anaMCng/KXs6Mnlni1zirs81zZ\n"
		"SPniUCZXVIckesKd6zSbjCZ6YqcPDehXMeT1R2HKPwZOkCgxMJisxGL27gtH/2ya\n"
		"t3+x6ozUilENb5aBOsp3emK5+E7C+wamCMsipJT2mVa5ClddO09Ebc5KiUeBV6IN\n"
		"3dliqSZ65TwJChicNdDpvnbusfJaCklFd6bQKZzy4vFkfdLQmDXxAgMBAAGjczBx\n"
		"MAwGA1UdEwEB/wQCMAAwDgYDVR0PAQH/BAQDAgWgMBYGA1UdJQEB/wQMMAoGCCsG\n"
		"AQUFBwMBMDkGA1UdEQEB/wQvMC2CK2hvcmRlc2VydmVyLWdycGMuZGV2dG9vbHMt\n"
		"ZGV2LmVwaWNnYW1lcy5jb20wDQYJKoZIhvcNAQELBQADggEBAGv2cc+9rM0i7039\n"
		"YZ0qEFiqeDQx7gOLk19gBBLhCumP+8HQ5eK3qgiRnYARSHp8hgrQM18ELoQf8muS\n"
		"RYz5qG5cREumKki1BmfdStsxvWkFNMm1OyEBKHfCJoZ+r8D+/lVg17tkT/a65ewr\n"
		"TYrL71lqnX022WlRAVycthUnTwADw+kwYS1D4ZEs3WJ7qlk6OWFfWz7x1zfnjM8y\n"
		"T2Yoe1PkKTmDAaUw6OQa1X9vE8N9wd9zM9NDup5El6x0NoCIoT/ia8J+jhYRdeSc\n"
		"U/4ARFp/nH+y+fbuSZvmam4ac2OoBltSOMlaoOkNXNNpo1WlB3ZlCCaC5B83Gr2H\n"
		"IIoNLEM=\n"
		"-----END CERTIFICATE-----"
	);
}
