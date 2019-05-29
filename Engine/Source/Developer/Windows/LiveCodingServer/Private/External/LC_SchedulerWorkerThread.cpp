// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#include "LC_SchedulerWorkerThread.h"
#include "LC_SchedulerQueue.h"
#include "LC_SchedulerTask.h"


scheduler::WorkerThread::WorkerThread(TaskQueue* queue)
	: m_thread()
{
	m_thread = thread::Create("Live coding worker", 128u * 1024u, &scheduler::WorkerThread::ThreadFunction, this, queue);
}


scheduler::WorkerThread::~WorkerThread(void)
{
	thread::Join(m_thread);
}


unsigned int scheduler::WorkerThread::ThreadFunction(TaskQueue* queue)
{
	for (;;)
	{
		// get a task from the queue and execute it
		TaskBase* task = queue->PopTask();
		if (task == nullptr)
		{
			break;
		}

		task->Execute();
	}

	return 0u;
}
