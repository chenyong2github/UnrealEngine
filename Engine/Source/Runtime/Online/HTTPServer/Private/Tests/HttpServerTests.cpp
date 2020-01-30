// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpRouteHandle.h"
#include "HttpRequestHandler.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHttpServerIntegrationTest, "System.Online.HttpServer.Integration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHttpServerIntegrationTest::RunTest(const FString& Parameters)
{
	const uint32 HttpRouterPort = 8888;
	const FHttpPath HttpPath(TEXT("/TestHttpServer"));

	// Ensure router creation
	TSharedPtr<IHttpRouter> HttpRouter = FHttpServerModule::Get().GetHttpRouter(HttpRouterPort);
	TestTrue(TEXT("HttpRouter.IsValid()"), HttpRouter.IsValid());

	// Ensure unique routers per-port
	TSharedPtr<IHttpRouter> DuplicateHttpRouter = FHttpServerModule::Get().GetHttpRouter(HttpRouterPort);
	TestEqual(TEXT("HttpRouter Duplicates"), HttpRouter, DuplicateHttpRouter);

	// Ensure we can create route bindings
	const FHttpRequestHandler RequestHandler = [this]
	(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		return true;
	};
	FHttpRouteHandle HttpRouteHandle = HttpRouter->BindRoute(HttpPath, EHttpServerRequestVerbs::VERB_GET, RequestHandler);
	TestTrue(TEXT("HttpRouteHandle.IsValid()"), HttpRouteHandle.IsValid());

	// Disallow duplicate route bindings
	FHttpRouteHandle DuplicateHandle = HttpRouter->BindRoute(HttpPath, EHttpServerRequestVerbs::VERB_GET, RequestHandler);
	TestFalse(TEXT("HttpRouteHandle Duplicated"), DuplicateHandle.IsValid());

	// Make a request
	/*
	ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("HTTP TEST 1 http://localhost:8888/TestHttpServer")));
	*/

	HttpRouter->UnbindRoute(HttpRouteHandle);
	return true;
}