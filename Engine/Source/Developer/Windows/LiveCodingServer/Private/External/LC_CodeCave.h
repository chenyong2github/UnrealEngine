// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "CoreTypes.h"
#include "LC_Process.h"
#include "LC_Types.h"

// nifty helper to let all threads of a process except one make "progress" by being held inside a jump-to-self cave.
class CodeCave
{
public:
	// the jump-to-self code needs to be available in the address space of the given process
	CodeCave(process::Handle processHandle, unsigned int processId, unsigned int commandThreadId, const void* jumpToSelf);

	void Install(void);
	void Uninstall(void);

private:
	process::Handle m_processHandle;
	unsigned int m_processId;
	unsigned int m_commandThreadId;
	const void* m_jumpToSelf;

	struct PerThreadData
	{
		unsigned int id;
		const void* originalIp;
		int priority;
	};

	types::vector<PerThreadData> m_perThreadData;
};
