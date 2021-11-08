// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#define LOCTEXT_NAMESPACE "OnlineErrors"

#include "Online/OnlineError.h"

namespace UE::Online::Errors {

UE_ONLINE_ERROR_CATEGORY(Common, Engine, 0x1, "Online Services")

UE_ONLINE_ERROR_COMMON(Common, NoConnection, 1, TEXT("no_connection"), LOCTEXT("NotConnected", "No valid connection"))
UE_ONLINE_ERROR_COMMON(Common, RequestFailure, 2, TEXT("request_failure"), LOCTEXT("RequestFailure", "Failed to send request"))
UE_ONLINE_ERROR_COMMON(Common, InvalidCreds, 3, TEXT("invalid_creds"), LOCTEXT("InvalidCreds", "Invalid credentials"))
UE_ONLINE_ERROR_COMMON(Common, InvalidUser, 4, TEXT("invalid_user"), LOCTEXT("InvalidUser", "No valid user"))
UE_ONLINE_ERROR_COMMON(Common, InvalidAuth, 5, TEXT("invalid_auth"), LOCTEXT("InvalidAuth", "No valid auth"))
UE_ONLINE_ERROR_COMMON(Common, AccessDenied, 6, TEXT("access_denied"), LOCTEXT("AccessDenied", "Access denied"))
UE_ONLINE_ERROR_COMMON(Common, TooManyRequests, 7, TEXT("too_many_requests"), LOCTEXT("TooManyRequests", "Too many requests"))
UE_ONLINE_ERROR_COMMON(Common, AlreadyPending, 8, TEXT("already_pending"), LOCTEXT("AlreadyPending", "Request already pending"))
UE_ONLINE_ERROR_COMMON(Common, InvalidParams, 9, TEXT("invalid_params"), LOCTEXT("InvalidParams", "Invalid params specified"))
UE_ONLINE_ERROR_COMMON(Common, CantParse, 10, TEXT("cant_parse"), LOCTEXT("CantParse", "Cannot parse results"))
UE_ONLINE_ERROR_COMMON(Common, InvalidResults, 11, TEXT("invalid_results"), LOCTEXT("InvalidResults", "Results were invalid"))
UE_ONLINE_ERROR_COMMON(Common, IncompatibleVersion, 12, TEXT("incompatible_version"), LOCTEXT("IncompatibleVersion", "Incompatible client version"))
UE_ONLINE_ERROR_COMMON(Common, NotConfigured, 13, TEXT("not_configured"), LOCTEXT("NotConfigured", "No valid configuration"))
UE_ONLINE_ERROR_COMMON(Common, NotImplemented, 14, TEXT("not_implemented"), LOCTEXT("NotImplemented", "Not implemented"))
UE_ONLINE_ERROR_COMMON(Common, MissingInterface, 15, TEXT("missing_interface"), LOCTEXT("MissingInterface", "Interface not found"))
UE_ONLINE_ERROR_COMMON(Common, Cancelled, 16, TEXT("cancelled"), LOCTEXT("Canceled", "Operation was cancelled"))
UE_ONLINE_ERROR_COMMON(Common, Unknown, 17, TEXT("unknown"), LOCTEXT("Unknown", "Unknown Error"))

} /* namespace UE::Online::Errors */

#undef LOCTEXT_NAMESPACE