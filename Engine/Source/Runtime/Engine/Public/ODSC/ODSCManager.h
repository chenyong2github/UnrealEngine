// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "RHIDefinitions.h"

class FODSCThread;

/**
 * Responsible for processing shader compile responses from the ODSC Thread.
 * Interface for submitting shader compile requests to the ODSC Thread.
 */
class ENGINE_API FODSCManager
	: public FTickerObjectBase
{
public:

	// FODSCManager

	/**
	 * Constructor
	 */
	FODSCManager();

	/**
	 * Destructor
	 */
	virtual ~FODSCManager();

	// FTickerObjectBase

	/**
	 * FTicker callback
	 *
	 * @param DeltaSeconds - time in seconds since the last tick
	 *
	 * @return false if no longer needs ticking
	 */
	bool Tick(float DeltaSeconds) override;

	/**
	 * Add a request to compile a shader.  The results are submitted and processed in an async manner.
	 *
	 * @param MaterialsToCompile - List of material names to submit compiles for.
	 * @param ShaderPlatform - Which shader platform to compile for.
	 * @param bCompileChangedShaders - Whether or not we should recompile shaders that have changed.
	 *
	 * @return false if no longer needs ticking
	 */
	void AddThreadedRequest(const TArray<FString>& MaterialsToCompile, EShaderPlatform ShaderPlatform, bool bCompileChangedShaders);

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
	void AddThreadedShaderPipelineRequest(EShaderPlatform ShaderPlatform, const FString& MaterialName, const FString& VertexFactoryName, const FString& PipelineName, const TArray<FString>& ShaderTypeNames);
private:

	/** Handles communicating directly with the cook on the fly server. */
	FODSCThread* Thread = nullptr;
};

/** The global shader ODSC manager. */
extern ENGINE_API FODSCManager* GODSCManager;