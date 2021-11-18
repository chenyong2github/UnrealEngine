// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CookTypes.h"
#include "ShaderCompiler.h"

struct FODSCRequestPayload;
class ICookedPackageWriter;
class ITargetPlatform;

namespace UE { namespace Cook
{

using FCookRequestCompletedCallback = TFunction<void(const ECookResult)>;

using FRecompileShaderCompletedCallback = TFunction<void()>;

using FPrecookedFileList = TMap<FString, FDateTime>;

/**
 * Cook package request.
 */
struct FCookPackageRequest
{
	/** The platform to cook for. */
	FName PlatformName;
	/* Asset filename to cook. */
	FString Filename;
	/** Completion callback. */
	FCookRequestCompletedCallback CompletionCallback;
};

/**
 * Recompile shader(s) request.
 */
struct FRecompileShaderRequest
{
	/** The arguments to configure shader compilation for this request. */
	FShaderRecompileData RecompileArguments;
	/** Completion callback. */
	FRecompileShaderCompletedCallback CompletionCallback;
};

/**
 * Cook-on-the-fly server interface used by the request manager.
 */
class ICookOnTheFlyServer
{
public:
	virtual ~ICookOnTheFlyServer() {}

	/** Returns the cooker sandbox directory path. */
	virtual FString GetSandboxDirectory() const = 0;
	
	/**
	 * Add platform to the cook-on-the-fly session.
	 *
	 * @param PlatformName The platform name.
	 */
	virtual const ITargetPlatform* AddPlatform(const FName& PlatformName) = 0;

	/**
	 * Remove platform from the cook-on-the-fly session.
	 *
	 * @param PlatformName The platform name.
	 */
	virtual void RemovePlatform(const FName& PlatformName) = 0;

	/**
	 * Returns all unsolicited files that has been produced has a result of a cook request.
	 *
	 * @param PlatformName The platform name.
	 * @param Filename The filename.
	 * @param Filename Whether the filename is a cookable asset.
	 * @param OutUnsolicitedFiles All unsolicited filename(s) returned to the caller.
	 */
	virtual void GetUnsolicitedFiles(
		const FName& PlatformName,
		const FString& Filename,
		const bool bIsCookable,
		TArray<FString>& OutUnsolicitedFiles) = 0;

	/**
	 * Enqueue a new cook request.
	 *
	 * @param CookPackageRequest Cook package request parameters.
	 */
	virtual bool EnqueueCookRequest(FCookPackageRequest CookPackageRequest) = 0;

	/**
	 * Enqueue a new shader compile request.
	 *
	 * @param RecompileShaderRequest Recompile shader request parameters.
	 */
	virtual bool EnqueueRecompileShaderRequest(FRecompileShaderRequest RecompileShaderRequest) = 0;

	/**
	 * Returns the package store writer for the specified platform.
	 */
	virtual ICookedPackageWriter& GetPackageWriter(const ITargetPlatform* TargetPlatform) = 0;

	/**
	 * Wait until any pending flush request is completed.
	 *
	 * @return Duration in seconds.
	 */
	virtual double WaitForPendingFlush() = 0;
};

/**
 * The cook-on-the-fly request manager.
 *
 * Responsible for managing cook-on-the-fly requests from connected client(s).
 */
class ICookOnTheFlyRequestManager
{
public:
	virtual ~ICookOnTheFlyRequestManager() {}

	/** Initialze the request manager. */
	virtual bool Initialize() = 0;

	/** Shutdown the reques tmanager. */
	virtual void Shutdown() = 0;
};

}} // namespace UE::Cook
