// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXRHIRenderQuery.h: AGX RHI Render Query Definitions.
=============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Private Query Buffer Forward Declarations


class FAGXContext;
class FAGXQueryBufferPool;
class FAGXQueryResult;
struct FAGXCommandBufferFence;


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Private Query Buffer Resource Class -


class FAGXQueryBuffer : public FRHIResource
{
public:
	FAGXQueryBuffer(FAGXContext* InContext, FAGXBuffer InBuffer);
	virtual ~FAGXQueryBuffer();

	uint64 GetResult(uint32 Offset);

	TWeakPtr<FAGXQueryBufferPool, ESPMode::ThreadSafe> Pool;
	FAGXBuffer Buffer;
	uint32 WriteOffset;
};


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Private Query Buffer Pool Class -


class FAGXQueryBufferPool
{
public:
	enum
	{
		EQueryBufferAlignment = 8,
		EQueryResultMaxSize   = 8,
		EQueryBufferMaxSize   = (1 << 16)
	};

	// Disallow a default constructor
	FAGXQueryBufferPool() = delete;

	FAGXQueryBufferPool(FAGXContext* InContext);
	~FAGXQueryBufferPool();

	void Allocate(FAGXQueryResult& NewQuery);
	FAGXQueryBuffer* GetCurrentQueryBuffer();
	void ReleaseCurrentQueryBuffer();
	void ReleaseQueryBuffer(FAGXBuffer& Buffer);

	TRefCountPtr<FAGXQueryBuffer> CurrentBuffer;
	TArray<FAGXBuffer> Buffers;
	FAGXContext* Context;
};


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Private Query Result Class -


class FAGXQueryResult
{
public:
	FAGXQueryResult() = default;
	~FAGXQueryResult() = default;

	bool Wait(uint64 Millis);
	uint64 GetResult();

	TRefCountPtr<FAGXQueryBuffer> SourceBuffer = nullptr;
	TSharedPtr<FAGXCommandBufferFence, ESPMode::ThreadSafe> CommandBufferFence = nullptr;
	uint32 Offset = 0;
	bool bCompleted = false;
	bool bBatchFence = false;
};


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Render Query Class -


class FAGXRHIRenderQuery : public FRHIRenderQuery
{
public:
	FAGXRHIRenderQuery(ERenderQueryType InQueryType);
	virtual ~FAGXRHIRenderQuery();

	/**
	 * Kick off an occlusion test
	 */
	void Begin(FAGXContext* Context, TSharedPtr<FAGXCommandBufferFence, ESPMode::ThreadSafe> const& BatchFence);

	/**
	 * Finish up an occlusion test
	 */
	void End(FAGXContext* Context);

	/**
	 * Get the query result
	 */
	bool GetResult(uint64& OutNumPixels, bool bWait, uint32 GPUIndex);

private:
	// The type of query
	ERenderQueryType Type;

	// Query buffer allocation details as the buffer is already set on the command-encoder
	FAGXQueryResult Buffer;

	// Query result.
	volatile uint64 Result;

	// Result availability - if not set the first call to acquire it will read the buffer & cache
	volatile bool bAvailable;

	// Timer event completion signal
	FEvent* QueryWrittenEvent;
};
