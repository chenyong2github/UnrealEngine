// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MemPro/MemProProfiler.h"

#if MEMPRO_ENABLED
#include "Misc/CString.h"
#include "Misc/CoreDelegates.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMemPro, Log, All);

/* NB. you can enable MemPro tracking after engine init by adding something like this to the command line:
 *    -execcmds="MemPro.LLMTag RHIMisc, MemPro.Enabled 1"
 */



/* 
 * Main runtime switch for MemPro support
 */
int32 GMemProEnabled = 0; //edit this and set to 1 to track from startup. You probably want to edit GMemProTrackTag too
static FAutoConsoleVariableRef CVarMemProEnable(
	TEXT("MemPro.Enabled"),
	GMemProEnabled,
	TEXT("Enable MemPro memory tracking.\n"),
	ECVF_Default
);


/* 
 * the LLM tag to track in MemPro, or ELLMTag::GenericTagCount to track all
 */
#if ENABLE_LOW_LEVEL_MEM_TRACKER

	ELLMTag GMemProTrackTag = ELLMTag::EngineMisc;

#endif //ENABLE_LOW_LEVEL_MEM_TRACKER



/* 
 * helper function to track a tag
 */
#if ENABLE_LOW_LEVEL_MEM_TRACKER
void FMemProProfiler::TrackTag( ELLMTag Tag )
{
	GMemProTrackTag = Tag;
}
#endif //ENABLE_LOW_LEVEL_MEM_TRACKER


/* 
 * helper function to track a tag given its name
 */
#if ENABLE_LOW_LEVEL_MEM_TRACKER
void FMemProProfiler::TrackTagByName( const TCHAR* TagName )
{
	//sanity check
	if( TagName == nullptr || FCString::Strlen(TagName) == 0 )
	{
		UE_LOG( LogMemPro, Display, TEXT("please specify an LLM tag or * to track all") );
		return;
	}

	//check whether they want to track all tags
	if( FCString::Strcmp(TagName, TEXT("*") ) == 0 )
	{
		TrackTag( ELLMTag::GenericTagCount );
		UE_LOG( LogMemPro, Display, TEXT("tracking all LLM tags" ) );
	}
	else
	{
		//find the specific tag to track
		uint64 TagIndex = (uint64)ELLMTag::Paused;
		if ( FLowLevelMemTracker::Get().FindTagByName(TagName, TagIndex) && TagIndex < LLM_TAG_COUNT )
		{
			TrackTag( (ELLMTag)TagIndex );
			UE_LOG( LogMemPro, Display, TEXT("tracking LLM tag \'%s\'" ), TagName );
		}
		else
		{
			UE_LOG( LogMemPro, Display, TEXT("Unknown LLM tag \'%s\'" ), TagName );
		}
	}
}
#endif //ENABLE_LOW_LEVEL_MEM_TRACKER


/*
 * console command to get mempro to track a specific LLM tag
 */
#if ENABLE_LOW_LEVEL_MEM_TRACKER
static FAutoConsoleCommand MemProTrackLLMTag(
	TEXT("MemPro.LLMTag"),
	TEXT("Capture a specific LLM tag with MemPro"),
	FConsoleCommandWithArgsDelegate::CreateLambda( [](const TArray<FString>& Args )
	{
		FMemProProfiler::TrackTagByName( (Args.Num() == 0) ? nullptr : *Args[0] );
	})
);
#endif //ENABLE_LOW_LEVEL_MEM_TRACKER


/*
 * query the port that MemPro might be using so other development tools can steer clear if necessary
 */
bool FMemProProfiler::IsUsingPort( uint32 Port )
{
#if defined(MEMPRO_WRITE_DUMP)
	return false;
#else
	return Port == FCStringAnsi::Atoi(MEMPRO_PORT);
#endif
}


/*
 * initialisation for MemPro. IT may have
 */

void FMemProProfiler::PostInit()
{
	//shutdown MemPro when the engine is shutting down so that the send thread terminates cleanly
	FCoreDelegates::OnPreExit.AddLambda( []()
	{
		MemPro::SetPaused(true);
		GMemProEnabled = 0;
		MemPro::Disconnect();
		//MemPro::Shutdown(); ...disabled for now as I was getting hangs on shutdown.
	});

}

#endif //MEMPRO_ENABLED
