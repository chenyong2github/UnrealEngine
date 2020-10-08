// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Misc/SingleThreadRunnable.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Containers/Queue.h"
#include "ShaderCompiler.h"
#include "RHIDefinitions.h"

class FEvent;
class FRunnableThread;

class FODSCMessageHandler : public IPlatformFile::IFileServerMessageHandler
{
public:
	FODSCMessageHandler(EShaderPlatform InShaderPlatform);
	FODSCMessageHandler(const TArray<FString>& InMaterials, EShaderPlatform InShaderPlatform, bool InbCompileChangedShaders);
	/** Subclass fills out an archive to send to the server */
	virtual void FillPayload(FArchive& Payload) override;

	/** Subclass pulls data response from the server */
	virtual void ProcessResponse(FArchive& Response) override;

	void AddPayload(const FODSCRequestPayload& Payload);

	const TArray<FString>& GetMaterialsToLoad() const;
	const TArray<uint8>& GetMeshMaterialMaps() const;
	bool ReloadGlobalShaders() const;

private:
	/** The materials we send over the network and expect maps for on the return */
	TArray<FString> MaterialsToLoad;

	/** Which shader platform we are compiling for */
	EShaderPlatform ShaderPlatform;

	/** Whether or not to recompile changed shaders */
	bool bCompileChangedShaders = false;

	/** The payload for compiling a specific set of shaders. */
	TArray<FODSCRequestPayload> RequestBatch;

	/** The serialized shader maps from across the network */
	TArray<uint8> OutMeshMaterialMaps;
};

/**
 * Manages ODSC thread
 * Handles sending requests to the cook on the fly server and communicating results back to the Game Thread.
 */
class FODSCThread
	: FRunnable, FSingleThreadRunnable
{
public:

	FODSCThread();
	virtual ~FODSCThread();

	/**
	 * Start the ODSC thread.
	 */
	void StartThread();

	/**
	 * Stop the ODSC thread.  Blocks until thread has stopped.
	 */
	void StopThread();

	//~ Begin FSingleThreadRunnable Interface
	// Cannot be overriden to ensure identical behavior with the threaded tick
	virtual void Tick() override final;
	//~ End FSingleThreadRunnable Interface

	/**
	 * Add a shader compile request to be processed by this thread.
	 *
	 * @param MaterialsToCompile - List of material names to submit compiles for.
	 * @param ShaderPlatform - Which shader platform to compile for.
	 * @param bCompileChangedShaders - Whether or not we shouhld recompile shaders that have changed.
	 *
	 * @return false if no longer needs ticking
	 */
	void AddRequest(const TArray<FString>& MaterialsToCompile, EShaderPlatform ShaderPlatform, bool bCompileChangedShaders);

	/**
	 * Add a request to compile a pipeline (VS/PS) of shaders.  The results are submitted and processed in an async manner.
	 *
	 * @param ShaderPlatform - Which shader platform to compile for.
	 * @param MaterialName - The name of the material to compile.
	 * @param VertexFactoryName - The name of the vertex factory type we should compile.
	 * @param PipelineName - The name of the shader pipeline we should compile.
	 * @param ShaderTypeNames - The shader type names of all the shader stages in the pipeline.
	 *
	 * @return false if no longer needs ticking
	 */
	void AddShaderPipelineRequest(EShaderPlatform ShaderPlatform, const FString& MaterialName, const FString& VertexFactoryName, const FString& PipelineName, const TArray<FString>& ShaderTypeNames);

	/**
	 * Get completed requests.  Clears internal arrays.  Called on Game thread.
	 *
	 * @param OutCompletedRequests array of requests that have been completed
	 */
	void GetCompletedRequests(TArray<FODSCMessageHandler*>& OutCompletedRequests);

	/**
	* Wakeup the thread to process requests.
	*/
	void Wakeup();

protected:

	//~ Begin FRunnable Interface
	virtual bool Init() override;
	virtual uint32 Run() override final;
	virtual void Stop() override;
	virtual void Exit() override;
	//~ End FRunnable Interface

	/** signal request to stop and exit thread */
	FThreadSafeCounter ExitRequest;

private:

	/**
	 * Responsible for sending and waiting on compile requests with the cook on the fly server.
	 *
	 */
	void Process();

	/**
	 * Threaded requests that are waiting to be processed on the ODSC thread.
	 * Added to on (any) non-ODSC thread, processed then cleared on ODSC thread.
	 */
	TQueue<FODSCMessageHandler*, EQueueMode::Mpsc> PendingMaterialThreadedRequests;

	/**
	 * Threaded requests that are waiting to be processed on the ODSC thread.
	 * Added to on (any) non-ODSC thread, processed then cleared on ODSC thread.
	 */
	TQueue<FODSCRequestPayload, EQueueMode::Mpsc> PendingMeshMaterialThreadedRequests;

	/**
	 * Threaded requests that have completed and are waiting for the game thread to process.
	 * Added to on ODSC thread, processed then cleared on game thread (Single producer, single consumer)
	 */
	TQueue<FODSCMessageHandler*, EQueueMode::Spsc> CompletedThreadedRequests;

	/** Lock to access the RequestHashes TMap */
	FCriticalSection RequestHashCriticalSection;

	/** Hashes for all Pending or Completed requests.  This is so we avoid making the same request multiple times. */
	TArray<FString> RequestHashes;

	/** Pointer to Runnable Thread */
	FRunnableThread* Thread = nullptr;

	/** Holds an event signaling the thread to wake up. */
	FEvent* WakeupEvent;
};
