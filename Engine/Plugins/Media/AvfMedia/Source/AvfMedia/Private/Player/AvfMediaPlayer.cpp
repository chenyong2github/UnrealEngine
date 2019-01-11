// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AvfMediaPlayer.h"
#include "AvfMediaPrivate.h"

#include "HAL/PlatformProcess.h"
#include "IMediaEventSink.h"
#include "MediaSamples.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/CoreDelegates.h"

#if PLATFORM_MAC
	#include "Mac/CocoaThread.h"
#else
	#include "IOS/IOSAsyncTask.h"
    #include "HAL/FileManager.h"
#endif

#include "AvfMediaTracks.h"
#include "AvfMediaUtils.h"
#include "IMediaAudioSample.h"


/* FAVPlayerDelegate
 *****************************************************************************/

/**
 * Cocoa class that can help us with reading player item information.
 */
@interface FAVPlayerDelegate : NSObject
{
};

/** We should only initiate a helper with a media player */
-(FAVPlayerDelegate*) initWithMediaPlayer:(FAvfMediaPlayer*)InPlayer;

/** Destructor */
-(void)dealloc;

/** Notification called when player item reaches the end of playback. */
-(void)playerItemPlaybackEndReached:(NSNotification*)Notification;

/** Reference to the media player which will be responsible for this media session */
@property FAvfMediaPlayer* MediaPlayer;

/** Flag indicating whether the media player item has reached the end of playback */
@property bool bHasPlayerReachedEnd;

@end


@implementation FAVPlayerDelegate
@synthesize MediaPlayer;


-(FAVPlayerDelegate*) initWithMediaPlayer:(FAvfMediaPlayer*)InPlayer
{
	id Self = [super init];
	if (Self)
	{
		MediaPlayer = InPlayer;
	}	
	return Self;
}


/** Listener for changes in our media classes properties. */
- (void) observeValueForKeyPath:(NSString*)keyPath
		ofObject:	(id)object
		change:		(NSDictionary*)change
		context:	(void*)context
{
	if ([keyPath isEqualToString:@"status"])
	{
		if (object == (id)context)
		{
			MediaPlayer->OnStatusNotification();
		}
	}
}


- (void)dealloc
{
	[super dealloc];
}


-(void)playerItemPlaybackEndReached:(NSNotification*)Notification
{
	MediaPlayer->OnEndReached();
}

@end

/* Sync Control Class for consumed samples  */
class FAvfMediaSamples : public FMediaSamples
{
public:
	FAvfMediaSamples()
	: FMediaSamples()
	, AudioSyncSampleTime(FTimespan::MinValue())
	, VideoSyncSampleTime(FTimespan::MinValue())
	{}
	
	virtual ~FAvfMediaSamples()
	{}
	
	virtual bool FetchAudio(TRange<FTimespan> TimeRange, TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>& OutSample) override
	{
		bool bResult = FMediaSamples::FetchAudio(TimeRange, OutSample);
		
		if(FTimespan::MinValue() == AudioSyncSampleTime && bResult && OutSample.IsValid())
		{
			AudioSyncSampleTime = OutSample->GetTime() + OutSample->GetDuration();
		}

		return bResult;
	}
	
	virtual bool FetchVideo(TRange<FTimespan> TimeRange, TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& OutSample)
	{
		bool bResult = FMediaSamples::FetchVideo(TimeRange, OutSample);
		
		if(FTimespan::MinValue() == VideoSyncSampleTime && bResult && OutSample.IsValid())
		{
			VideoSyncSampleTime = OutSample->GetTime() + OutSample->GetDuration();
		}

		return bResult;
	}
	
	void ClearSyncSampleTimes ()			 { AudioSyncSampleTime = VideoSyncSampleTime = FTimespan::MinValue(); }
	FTimespan GetAudioSyncSampleTime() const { return AudioSyncSampleTime; }
	FTimespan GetVideoSyncSampleTime() const { return VideoSyncSampleTime; }
	
private:

	TAtomic<FTimespan> AudioSyncSampleTime;
	TAtomic<FTimespan> VideoSyncSampleTime;
};

/* FAvfMediaPlayer structors
 *****************************************************************************/

FAvfMediaPlayer::FAvfMediaPlayer(IMediaEventSink& InEventSink)
	: EventSink(InEventSink)
{
	CurrentRate = 0.0f;
	CurrentState = EMediaState::Closed;
    CurrentTime = FTimespan::Zero();

	Duration = FTimespan::Zero();
	MediaUrl = FString();
	ShouldLoop = false;
    
	MediaHelper = nil;
    MediaPlayer = nil;
	PlayerItem = nil;
		
	bPrerolled = false;
	bTimeSynced = false;
	bSeeking = false;

	Samples = new FAvfMediaSamples;
	Tracks = new FAvfMediaTracks(*Samples);
}


FAvfMediaPlayer::~FAvfMediaPlayer()
{
	Close();

	delete Samples;
	Samples = nullptr;
}

/* FAvfMediaPlayer Sample Sync helpers
 *****************************************************************************/
void FAvfMediaPlayer::ClearTimeSync()
{
	bTimeSynced = false;
	if(Samples != nullptr)
	{
		Samples->ClearSyncSampleTimes();
	}
}

FTimespan FAvfMediaPlayer::GetAudioTimeSync() const
{
	FTimespan Sync = FTimespan::MinValue();
	if(Samples != nullptr)
	{
		Sync = Samples->GetAudioSyncSampleTime();
	}
	return Sync;
}

FTimespan FAvfMediaPlayer::GetVideoTimeSync() const
{
	FTimespan Sync = FTimespan::MinValue();
	if(Samples != nullptr)
	{
		Sync = Samples->GetVideoSyncSampleTime();
	}
	return Sync;
}


/* FAvfMediaPlayer interface
 *****************************************************************************/

void FAvfMediaPlayer::OnEndReached()
{
	if (ShouldLoop)
	{
		PlayerTasks.Enqueue([=]()
		{
			EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackEndReached);
			Seek(CurrentRate < 0.f ? Duration : FTimespan::Zero());
		});
	}
	else
	{
		CurrentState = EMediaState::Paused;
		CurrentRate = 0.0f;

		PlayerTasks.Enqueue([=]()
		{
			Seek(FTimespan::Zero());
			EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackEndReached);
			EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackSuspended);
		});
	}
}


void FAvfMediaPlayer::OnStatusNotification()
{
	PlayerTasks.Enqueue([=]()
	{
		switch(PlayerItem.status)
		{
			case AVPlayerItemStatusReadyToPlay:
			{
				if (Duration == FTimespan::Zero() || CurrentState == EMediaState::Closed)
				{
					Tracks->Initialize(PlayerItem, Info);
					EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
					
					Duration = FTimespan::FromSeconds(CMTimeGetSeconds(PlayerItem.asset.duration));
					CurrentState = (CurrentState == EMediaState::Closed) ? EMediaState::Stopped : CurrentState;

					if (!bPrerolled)
					{
						// Preroll for playback.
						[MediaPlayer prerollAtRate:1.0f completionHandler:^(BOOL bFinished)
						{
							if (bFinished)
							{
								PlayerTasks.Enqueue([=]()
								{
									if(PlayerItem.status == AVPlayerItemStatusReadyToPlay)
									{
										bPrerolled = true;
										CurrentState = EMediaState::Stopped;
										
										EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpened);
										if (CurrentTime != FTimespan::Zero())
										{
											Seek(CurrentTime);
										}
										if(CurrentRate != 0.0f)
										{
											SetRate(CurrentRate);
										}
									}
								});
							}
							else
							{
								PlayerTasks.Enqueue([=]()
								{
									CurrentState = EMediaState::Error;
									EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
								});
							}
						}];
					}
				}

				break;
			}
			case AVPlayerItemStatusFailed:
			{
				if (Duration == FTimespan::Zero() || CurrentState == EMediaState::Closed)
				{
					CurrentState = EMediaState::Error;
					EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
				}
				else
				{
					CurrentState = EMediaState::Error;
					EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackSuspended);
				}
				break;
			}
			case AVPlayerItemStatusUnknown:
			default:
			{
				break;
			}
		}
	});
}


/* IMediaPlayer interface
 *****************************************************************************/

void FAvfMediaPlayer::Close()
{
	if (CurrentState == EMediaState::Closed)
	{
		return;
	}

    if (EnteredForegroundHandle.IsValid())
    {
        FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(EnteredForegroundHandle);
        EnteredForegroundHandle.Reset();
    }

    if (HasReactivatedHandle.IsValid())
    {
        FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(HasReactivatedHandle);
        HasReactivatedHandle.Reset();
    }

    if (EnteredBackgroundHandle.IsValid())
    {
        FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(EnteredBackgroundHandle);
        EnteredBackgroundHandle.Reset();
    }

    if (WillDeactivateHandle.IsValid())
    {
        FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(WillDeactivateHandle);
        WillDeactivateHandle.Reset();
    }

	CurrentTime = FTimespan::Zero();
	MediaUrl = FString();
	
	if (PlayerItem != nil)
	{
		if (MediaHelper != nil)
		{
			[[NSNotificationCenter defaultCenter] removeObserver:MediaHelper name:AVPlayerItemDidPlayToEndTimeNotification object:PlayerItem];
			[PlayerItem removeObserver:MediaHelper forKeyPath:@"status"];
		}

		[PlayerItem release];
		PlayerItem = nil;
	}
	
	if (MediaHelper != nil)
	{
		[MediaHelper release];
		MediaHelper = nil;
	}

	if (MediaPlayer != nil)
	{
		// If we don't remove the current player item then the retain count is > 1 for the MediaPlayer then on it's release then the MetalPlayer stays around forever
		[MediaPlayer replaceCurrentItemWithPlayerItem:nil];
		[MediaPlayer release];
		MediaPlayer = nil;
	}
	
	Tracks->Reset();
	EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);

	CurrentState = EMediaState::Closed;
	Duration = CurrentTime = FTimespan::Zero();
	Info.Empty();
	
	EventSink.ReceiveMediaEvent(EMediaEvent::MediaClosed);
		
	bPrerolled = false;
	bSeeking = false;

	CurrentRate = 0.f;
	
	ClearTimeSync();
}


IMediaCache& FAvfMediaPlayer::GetCache()
{
	return *this;
}


IMediaControls& FAvfMediaPlayer::GetControls()
{
	return *this;
}


FString FAvfMediaPlayer::GetInfo() const
{
	return Info;
}


FName FAvfMediaPlayer::GetPlayerName() const
{
	static FName PlayerName(TEXT("AvfMedia"));
	return PlayerName;
}


IMediaSamples& FAvfMediaPlayer::GetSamples()
{
	return *Samples;
}


FString FAvfMediaPlayer::GetStats() const
{
	FString Result;

	Tracks->AppendStats(Result);

	return Result;
}


IMediaTracks& FAvfMediaPlayer::GetTracks()
{
	return *Tracks;
}


FString FAvfMediaPlayer::GetUrl() const
{
	return MediaUrl;
}


IMediaView& FAvfMediaPlayer::GetView()
{
	return *this;
}


bool FAvfMediaPlayer::Open(const FString& Url, const IMediaOptions* /*Options*/)
{
	Close();

	NSURL* nsMediaUrl = nil;
	FString Path;

	if (Url.StartsWith(TEXT("file://")))
	{
		// Media Framework doesn't percent encode the URL, so the path portion is just a native file path.
		// Extract it and then use it create a proper URL.
		Path = Url.Mid(7);
		nsMediaUrl = [NSURL fileURLWithPath:Path.GetNSString() isDirectory:NO];
	}
	else
	{
		// Assume that this has been percent encoded for now - when we support HTTP Live Streaming we will need to check for that.
		nsMediaUrl = [NSURL URLWithString: Url.GetNSString()];
	}

	// open media file
	if (nsMediaUrl == nil)
	{
		UE_LOG(LogAvfMedia, Error, TEXT("Failed to open Media file:"), *Url);

		return false;
	}

	// On non-Mac Apple OSes the path is:
	//	a) case-sensitive
	//	b) relative to the 'cookeddata' directory, not the notional GameContentDirectory which is 'virtual' and resolved by the FIOSPlatformFile calls
#if !PLATFORM_MAC
	if ([[nsMediaUrl scheme] isEqualToString:@"file"])
	{
		FString FullPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Path);
		nsMediaUrl = [NSURL fileURLWithPath: FullPath.GetNSString() isDirectory:NO];
	}
#endif
	
	// create player instance
	MediaUrl = FPaths::GetCleanFilename(Url);
	MediaPlayer = [[AVPlayer alloc] init];

	if (!MediaPlayer)
	{
		UE_LOG(LogAvfMedia, Error, TEXT("Failed to create instance of an AVPlayer"));

		return false;
	}
	
	MediaPlayer.actionAtItemEnd = AVPlayerActionAtItemEndPause;

	// create player item
	MediaHelper = [[FAVPlayerDelegate alloc] initWithMediaPlayer:this];
	check(MediaHelper != nil);

	PlayerItem = [[AVPlayerItem playerItemWithURL:nsMediaUrl] retain];

	if (PlayerItem == nil)
	{
		UE_LOG(LogAvfMedia, Error, TEXT("Failed to open player item with Url:"), *Url);

		return false;
	}

	CurrentState = EMediaState::Preparing;

	// load tracks
	[[PlayerItem asset] loadValuesAsynchronouslyForKeys:@[@"tracks"] completionHandler:^
	{
		NSError* Error = nil;

		if ([[PlayerItem asset] statusOfValueForKey:@"tracks" error : &Error] == AVKeyValueStatusLoaded)
		{
			// File movies will be ready now
			if (PlayerItem.status == AVPlayerItemStatusReadyToPlay)
			{
				PlayerTasks.Enqueue([=]()
				{
					OnStatusNotification();
				});
			}
		}
		else if (Error != nullptr)
		{
			NSDictionary *userInfo = [Error userInfo];
			NSString *errstr = [[userInfo objectForKey : NSUnderlyingErrorKey] localizedDescription];

			UE_LOG(LogAvfMedia, Warning, TEXT("Failed to load video tracks. [%s]"), *FString(errstr));
	 
			PlayerTasks.Enqueue([=]()
			{
				CurrentState = EMediaState::Error;
				EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
			});
		}
	}];

	[[NSNotificationCenter defaultCenter] addObserver:MediaHelper selector:@selector(playerItemPlaybackEndReached:) name:AVPlayerItemDidPlayToEndTimeNotification object:PlayerItem];
	[PlayerItem addObserver:MediaHelper forKeyPath:@"status" options:0 context:PlayerItem];

	[MediaPlayer replaceCurrentItemWithPlayerItem : PlayerItem];
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	[[MediaPlayer currentItem] seekToTime:kCMTimeZero];
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	MediaPlayer.rate = 0.0;
	CurrentTime = FTimespan::Zero();

	if (!EnteredForegroundHandle.IsValid())
    {
        EnteredForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FAvfMediaPlayer::HandleApplicationHasEnteredForeground);
    }
    if (!HasReactivatedHandle.IsValid())
    {
        HasReactivatedHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddRaw(this, &FAvfMediaPlayer::HandleApplicationActivate);
    }

    if (!EnteredBackgroundHandle.IsValid())
    {
        EnteredBackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FAvfMediaPlayer::HandleApplicationWillEnterBackground);
    }
    if (!WillDeactivateHandle.IsValid())
    {
        WillDeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddRaw(this, &FAvfMediaPlayer::HandleApplicationDeactivate);
    }

	return true;
}


bool FAvfMediaPlayer::Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& /*Archive*/, const FString& /*OriginalUrl*/, const IMediaOptions* /*Options*/)
{
	return false; // not supported
}


void FAvfMediaPlayer::TickAudio()
{
	// NOP
}


void FAvfMediaPlayer::TickFetch(FTimespan DeltaTime, FTimespan /*Timecode*/)
{
	if ((CurrentState > EMediaState::Error) && (Duration > FTimespan::Zero()))
	{
		Tracks->ProcessVideo();
	}
}


void FAvfMediaPlayer::TickInput(FTimespan DeltaTime, FTimespan /*Timecode*/)
{
	if ((CurrentState > EMediaState::Error) && (Duration > FTimespan::Zero()))
	{
		switch(CurrentState)
		{
			case EMediaState::Playing:
			{
				if(bSeeking)
				{
					ClearTimeSync();
				}
				else
				{
					if(!bTimeSynced)
					{
						FTimespan SyncTime = FTimespan::MinValue();
#if AUDIO_PLAYBACK_VIA_ENGINE
						if(Tracks->GetSelectedTrack(EMediaTrackType::Audio) != INDEX_NONE)
						{
							SyncTime = GetAudioTimeSync();
						}
						else if(Tracks->GetSelectedTrack(EMediaTrackType::Video) != INDEX_NONE)
						{
							SyncTime = GetVideoTimeSync();
						}
						else /* Default Use AVPlayer time*/
#endif
						{
							SyncTime = FTimespan::FromSeconds(CMTimeGetSeconds(MediaPlayer.currentTime));
						}
						
						if(SyncTime != FTimespan::MinValue())
						{
							bTimeSynced = true;
							CurrentTime = SyncTime;
						}
					}
					else
					{
						CurrentTime += DeltaTime * CurrentRate;
					}
				}
				break;
			}
			case EMediaState::Stopped:
			case EMediaState::Closed:
			case EMediaState::Error:
			case EMediaState::Preparing:
			{
				CurrentTime = FTimespan::Zero();
				break;
			}
			case EMediaState::Paused:
			default:
			{
				break;
			}
		}
	}
	
	// process deferred tasks
	TFunction<void()> Task;

	while (PlayerTasks.Dequeue(Task))
	{
		Task();
	}
}


/* IMediaControls interface
 *****************************************************************************/

bool FAvfMediaPlayer::CanControl(EMediaControl Control) const
{
	if (!bPrerolled)
	{
		return false;
	}

	if (Control == EMediaControl::Pause)
	{
		return (CurrentState == EMediaState::Playing);
	}

	if (Control == EMediaControl::Resume)
	{
		return (CurrentState != EMediaState::Playing);
	}

	if ((Control == EMediaControl::Scrub) || (Control == EMediaControl::Seek))
	{
		return true;
	}

	return false;
}


FTimespan FAvfMediaPlayer::GetDuration() const
{
	return Duration;
}


float FAvfMediaPlayer::GetRate() const
{
	return CurrentRate;
}


EMediaState FAvfMediaPlayer::GetState() const
{
	return CurrentState;
}


EMediaStatus FAvfMediaPlayer::GetStatus() const
{
	return EMediaStatus::None;
}


TRangeSet<float> FAvfMediaPlayer::GetSupportedRates(EMediaRateThinning Thinning) const
{
	TRangeSet<float> Result;

	Result.Add(TRange<float>(PlayerItem.canPlayFastReverse ? -8.0f : -1.0f, 0.0f));
	Result.Add(TRange<float>(0.0f, PlayerItem.canPlayFastForward ? 8.0f : 0.0f));

	return Result;
}


FTimespan FAvfMediaPlayer::GetTime() const
{
	return CurrentTime;
}


bool FAvfMediaPlayer::IsLooping() const
{
	return ShouldLoop;
}

bool FAvfMediaPlayer::Seek(const FTimespan& Time)
{
	if (bPrerolled)
	{
		bSeeking = true;
		ClearTimeSync();

		CurrentTime = Time;
		
		double TotalSeconds = Time.GetTotalSeconds();
		CMTime CurrentTimeInSeconds = CMTimeMakeWithSeconds(TotalSeconds, 1000);
		
		static CMTime Tolerance = CMTimeMakeWithSeconds(0.01, 1000);
		[MediaPlayer seekToTime:CurrentTimeInSeconds toleranceBefore:Tolerance toleranceAfter:Tolerance completionHandler:^(BOOL bFinished)
		{
			if(bFinished)
			{
				PlayerTasks.Enqueue([=]()
				{
					bSeeking = false;
					EventSink.ReceiveMediaEvent(EMediaEvent::SeekCompleted);
				});
			}
		}];
	}

	return true;
}


bool FAvfMediaPlayer::SetLooping(bool Looping)
{
	ShouldLoop = Looping;
	
	if (ShouldLoop)
	{
		MediaPlayer.actionAtItemEnd = AVPlayerActionAtItemEndNone;
	}
	else
	{
		MediaPlayer.actionAtItemEnd = AVPlayerActionAtItemEndPause;
	}

	return true;
}


bool FAvfMediaPlayer::SetRate(float Rate)
{
	CurrentRate = Rate;
	
	if (bPrerolled)
	{
		[MediaPlayer setRate : CurrentRate];
		
		if (FMath::IsNearlyZero(CurrentRate) && CurrentState != EMediaState::Paused)
		{
			CurrentState = EMediaState::Paused;
			EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackSuspended);
		}
		else
		{
			if(CurrentState != EMediaState::Playing)
			{
				ClearTimeSync();
				
				CurrentState = EMediaState::Playing;
				EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackResumed);
			}
		}
		
		// Use AVPlayer Mute to control reverse playback audio playback
		// Only needed if !AUDIO_PLAYBACK_VIA_ENGINE - however - keep all platforms the same
		bool bMuteAudio = Rate < 0.f;
		if(bMuteAudio)
		{
			MediaPlayer.muted = YES;
		}
		else
		{
			MediaPlayer.muted = NO;
		}

#if AUDIO_PLAYBACK_VIA_ENGINE
		Tracks->ApplyMuteState(bMuteAudio);
#endif
	}

	return true;
}


#if PLATFORM_IOS || PLATFORM_TVOS
bool FAvfMediaPlayer::SetNativeVolume(float Volume)
{
	if (MediaPlayer != nil)
	{
		MediaPlayer.volume = Volume < 0.0f ? 0.0f : (Volume < 1.0f ? Volume : 1.0f);
		return true;
	}
	return false;
}
#endif


void FAvfMediaPlayer::HandleApplicationHasEnteredForeground()
{
    // check the state to ensure we are still playing
    if ((CurrentState == EMediaState::Playing) && MediaPlayer != nil)
    {
        [MediaPlayer play];
    }
}

void FAvfMediaPlayer::HandleApplicationWillEnterBackground()
{
    // check the state to ensure we are still playing
    if ((CurrentState == EMediaState::Playing) && MediaPlayer != nil)
    {
        [MediaPlayer pause];
    }
}

void FAvfMediaPlayer::HandleApplicationActivate()
{
	// check the state to ensure we are still playing
	if ((CurrentState == EMediaState::Playing) && MediaPlayer != nil)
	{
		[MediaPlayer play];
	}
}

void FAvfMediaPlayer::HandleApplicationDeactivate()
{
	// check the state to ensure we are still playing
	if ((CurrentState == EMediaState::Playing) && MediaPlayer != nil)
	{
		[MediaPlayer pause];
	}
}
