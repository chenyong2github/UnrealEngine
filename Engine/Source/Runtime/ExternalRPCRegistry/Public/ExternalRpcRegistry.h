// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"
#include "ExternalRpcRegistry.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogExternalRpcRegistry, Log, All);

USTRUCT()
struct FExternalRouteInfo
{
	GENERATED_BODY()
public:
	FName RouteName;
	FHttpPath RoutePath;
	EHttpServerRequestVerbs RequestVerbs;
	FString InputContentType;
	FString InputExpectedFormat;	
	FString RpcCategory;
	bool bAlwaysOn;

	FExternalRouteInfo()
	{
		RouteName = FName(TEXT(""));
		RoutePath = FHttpPath();
		RequestVerbs = EHttpServerRequestVerbs::VERB_NONE;
		InputContentType = TEXT("");
		InputExpectedFormat = TEXT("");
		RpcCategory = TEXT("Unknown");
		bAlwaysOn = false;
	}

	FExternalRouteInfo(FName InRouteName, FHttpPath InRoutePath, EHttpServerRequestVerbs InRequestVerbs, FString InCategory = TEXT("Unknown"), bool bInAlwaysOn = false, FString InContentType = TEXT(""), FString InExpectedFormat = TEXT(""))
	{
		RouteName = InRouteName;
		RoutePath = InRoutePath;
		RequestVerbs = InRequestVerbs;
		InputContentType = InContentType;
		InputExpectedFormat = InExpectedFormat;
		RpcCategory = InCategory;
		bAlwaysOn = false;
	}
};

USTRUCT()
struct FExternalRouteDesc
{
	GENERATED_BODY()
public:
	FHttpRouteHandle Handle;
	FString InputContentType;
	FString InputExpectedFormat;
	FExternalRouteDesc()
	{
	
	}
	FExternalRouteDesc(FHttpRouteHandle InHandle, FString InContentType, FString InExpectedFormat)
	{
		Handle = InHandle;
		InputContentType = InContentType;
		InputExpectedFormat = InExpectedFormat;
	}
};

/**
 * This class is designed to be a singleton that handles registry, maintenance, and cleanup of any REST endpoints exposed on the process 
 * for use in communicating with the process externally. 
 */
UCLASS()
class EXTERNALRPCREGISTRY_API UExternalRpcRegistry : public UObject
{
	GENERATED_BODY()
protected:
	static UExternalRpcRegistry * ObjectInstance;
	TMap<FName, FExternalRouteDesc> RegisteredRoutes;
	TArray<FString> ActiveRpcCategories;
public:
	static UExternalRpcRegistry * GetInstance();

	int PortToUse = 11223;

	/**
	* Check if this Rpc is from a category that is meant to be enabled.
	*/
	bool IsActiveRpcCategory(FString InCategory);

	/**
	 * Try to get a route registered under given friendly name. Returns false if could not be found.
	 */
	bool GetRegisteredRoute(FName RouteName, FExternalRouteInfo& OutRouteInfo);

	void RegisterNewRoute(FExternalRouteInfo InRouteInfo, const FHttpRequestHandler& Handler, bool bOverrideIfBound = false);

	/**
	 * Register a new route.
	 * Will override existing routes if option is set, otherwise will error and fail to bind.
	 */
	void RegisterNewRoute(FName RouteName, const FHttpPath& HttpPath, const EHttpServerRequestVerbs& RequestVerbs, const FHttpRequestHandler& Handler, bool bOverrideIfBound = false, bool bIsAlwaysOn = false, FString OptionalCategory = TEXT("Unknown"), FString OptionalContentType = TEXT(""), FString OptionalExpectedFormat = TEXT(""));

	/**
	 * Clean up a route.
	 * Can be set to fail if trying to unbind an unbound route.
	 */
	void CleanUpRoute(FName RouteName, bool bFailIfUnbound = false);

	/**
	 * Default Route Listing http call. Spits out all registered routes and describes them via a REST API call.
	 * Always registered at /listrpcs GET by default
	 */
	bool HttpListOpenRoutes(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

};