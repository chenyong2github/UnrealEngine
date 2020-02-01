// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "CoreTypes.h"
#include "Windows/WindowsHWrapper.h"

namespace thread
{
	typedef CONTEXT Context;
	typedef HANDLE Handle;
	typedef unsigned int(__stdcall *Function)(void*);

	// returns the thread ID of the calling thread
	unsigned int GetId(void);

	// returns the thread ID of the given thread
	unsigned int GetId(Handle handle);

	// raw thread creation function
	Handle Create(unsigned int stackSize, Function function, void* context);

	// creates a thread from a given function and provided arguments
	template <typename F, typename... Args>
	inline Handle Create(const char* threadName, unsigned int stackSize, F ptrToFunction, Args&&... args);

	// creates a thread from a given member function, instance and provided arguments
	template <class C, typename F, typename... Args>
	inline Handle Create(const char* threadName, unsigned int stackSize, F ptrToMemberFunction, C* instance, Args&&... args);


	void Join(Handle handle);
	void Terminate(Handle handle);

	void Yield(void);
	void Sleep(unsigned int milliSeconds);

	void CancelIO(Handle handle);


	// opens a thread
	Handle Open(unsigned int threadId);

	// closes a thread
	void Close(Handle& handle);

	// suspends a thread
	void Suspend(Handle handle);

	// resumes a thread
	void Resume(Handle handle);

	// returns a thread's priority
	int GetPriority(Handle handle);

	// sets a thread's priority
	void SetPriority(Handle handle, int priority);

	// returns a thread's context.
	// NOTE: only use on suspended threads!
	Context GetContext(Handle handle);

	// sets a thread's context.
	// NOTE: only use on suspended threads!
	void SetContext(Handle handle, const Context& context);

	// reads a context' instruction pointer
	const void* ReadInstructionPointer(const Context& context);

	// writes a context' instruction pointer
	void WriteInstructionPointer(Context& context, const void* ip);


	// sets the name of the calling thread
	void SetName(const char* name);
}


template <typename F, typename... Args>
inline thread::Handle thread::Create(const char* threadName, unsigned int stackSize, F ptrToFunction, Args&&... args)
{
	// a helper lambda that calls the given functions with the provided arguments.
	// because this lambda needs to capture, it cannot be converted to a function pointer implicitly,
	// and therefore cannot be used as a thread function directly.
	// however, we can solve this by another indirection.
	auto captureLambda = [threadName, ptrToFunction, args...]() -> unsigned int
	{
		thread::SetName(threadName);

		return (*ptrToFunction)(args...);
	};

	// helper to make the following easier to read
	typedef decltype(captureLambda) CaptureLambdaType;

	// the lambda with captures will be called in the thread about to be created, hence it
	// must be allocated on the heap.
	CaptureLambdaType* lambdaOnHeap = new CaptureLambdaType(captureLambda);

	// here's the trick: we generate another capture-less lambda that has the same signature as a thread function,
	// and internally cast the given object to its original type, calling the lambda with captures from within
	// this capture-less lambda.
	auto capturelessLambda = [](void* lambdaContext) -> unsigned int
	{
		CaptureLambdaType* lambdaOnHeap = static_cast<CaptureLambdaType*>(lambdaContext);
		const unsigned int result = (*lambdaOnHeap)();
		delete lambdaOnHeap;

		return result;
	};

	unsigned int(__stdcall *threadFunction)(void*) = capturelessLambda;

	thread::Handle handle = thread::Create(stackSize, threadFunction, lambdaOnHeap);

	return handle;
}


template <class C, typename F, typename... Args>
inline thread::Handle thread::Create(const char* threadName, unsigned int stackSize, F ptrToMemberFunction, C* instance, Args&&... args)
{
	// a helper lambda that calls the given functions with the provided arguments.
	// because this lambda needs to capture, it cannot be converted to a function pointer implicitly,
	// and therefore cannot be used as a thread function directly.
	// however, we can solve this by another indirection.
	auto captureLambda = [threadName, ptrToMemberFunction, instance, args...]() -> unsigned int
	{
		thread::SetName(threadName);

		return (instance->*ptrToMemberFunction)(args...);
	};

	// helper to make the following easier to read
	typedef decltype(captureLambda) CaptureLambdaType;

	// the lambda with captures will be called in the thread about to be created, hence it
	// must be allocated on the heap.
	CaptureLambdaType* lambdaOnHeap = new CaptureLambdaType(captureLambda);

	// here's the trick: we generate another capture-less lambda that has the same signature as a thread function,
	// and internally cast the given object to its original type, calling the lambda with captures from within
	// this capture-less lambda.
	auto capturelessLambda = [](void* lambdaContext) -> unsigned int
	{
		CaptureLambdaType* lambdaOnHeap = static_cast<CaptureLambdaType*>(lambdaContext);
		const unsigned int result = (*lambdaOnHeap)();
		delete lambdaOnHeap;

		return result;
	};

	unsigned int (__stdcall *threadFunction)(void*) = capturelessLambda;

	thread::Handle handle = thread::Create(stackSize, threadFunction, lambdaOnHeap);

	return handle;
}
