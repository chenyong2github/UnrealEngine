// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#include "LC_EntryPoint.h"
#include "LC_ClientStartupThread.h"
#include "LC_API.h"


namespace
{
	// startup thread
	static ClientStartupThread* g_mainStartupThread = nullptr;
}


// BEGIN EPIC MOD - Manually trigger startup/shutdown code
void Startup(HINSTANCE instance)
{
	g_mainStartupThread = new ClientStartupThread(instance);
	api::Startup(g_mainStartupThread);
}


void Shutdown(void)
{
	api::Shutdown();

	// wait for the startup thread to finish its work and clean up
	g_mainStartupThread->Join();
	delete g_mainStartupThread;
}

#if 0
BOOL WINAPI DllMain(_In_ HINSTANCE hinstDLL, _In_ DWORD dwReason, _In_ LPVOID /* lpvReserved */)
{
	switch (dwReason)
	{
		case DLL_PROCESS_ATTACH:
			Startup(hinstDLL);
			break;

		case DLL_PROCESS_DETACH:
			Shutdown();
			break;

		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
			break;
	}

	return TRUE;
}
#endif
