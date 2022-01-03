// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

namespace PixelStreamingStatNames
{

	const FName JitterBufferDelay		  = FName(TEXT("jitterBufferDelay"));
	const FName FramesSent				  = FName(TEXT("framesSent"));
	const FName FramesPerSecond			  = FName(TEXT("framesPerSecond"));
	const FName FramesReceived			  = FName(TEXT("framesReceived"));
	const FName FramesDecoded			  = FName(TEXT("framesDecoded"));
	const FName FramesDropped			  = FName(TEXT("framesDropped"));
	const FName FramesCorrupted			  = FName(TEXT("framesCorrupted"));
	const FName PartialFramesLost		  = FName(TEXT("partialFramesLost"));
	const FName FullFramesLost			  = FName(TEXT("fullFramesLost"));
	const FName HugeFramesSent			  = FName(TEXT("hugeFramesSent"));
	const FName JitterBufferTargetDelay	  = FName(TEXT("jitterBufferTargetDelay"));
	const FName InterruptionCount		  = FName(TEXT("interruptionCount"));
	const FName TotalInterruptionDuration = FName(TEXT("totalInterruptionDuration"));
	const FName FreezeCount				  = FName(TEXT("freezeCount"));
	const FName PauseCount				  = FName(TEXT("pauseCount"));
	const FName TotalFreezesDuration	  = FName(TEXT("totalFreezesDuration"));
	const FName TotalPausesDuration		  = FName(TEXT("totalPausesDuration"));
	const FName FirCount				  = FName(TEXT("firCount"));
	const FName PliCount				  = FName(TEXT("pliCount"));
	const FName NackCount				  = FName(TEXT("nackCount"));
	const FName SliCount				  = FName(TEXT("sliCount"));
	const FName RetransmittedBytesSent	  = FName(TEXT("retransmittedBytesSent"));
	const FName TargetBitrate			  = FName(TEXT("targetBitrate"));
	const FName TotalEncodeBytesTarget	  = FName(TEXT("totalEncodedBytesTarget"));
	const FName KeyFramesEncoded		  = FName(TEXT("keyFramesEncoded"));
	const FName FrameWidth				  = FName(TEXT("frameWidth"));
	const FName FrameHeight				  = FName(TEXT("frameHeight"));
	const FName BytesSent				  = FName(TEXT("bytesSent"));
	const FName QPSum					  = FName(TEXT("qpSum"));
	const FName TotalEncodeTime			  = FName(TEXT("totalEncodeTime"));
	const FName TotalPacketSendDelay	  = FName(TEXT("totalPacketSendDelay"));
	const FName FramesEncoded			  = FName(TEXT("framesEncoded"));

	// Calculated stats
	const FName FramesSentPerSecond	   = FName(TEXT("transmit fps"));
	const FName Bitrate				   = FName(TEXT("bitrate (kb/s)"));
	const FName MeanQPPerSecond		   = FName(TEXT("qp"));
	const FName MeanEncodeTime		   = FName(TEXT("encode time (ms)"));
	const FName EncodedFramesPerSecond = FName(TEXT("encode fps"));
	const FName MeanSendDelay		   = FName(TEXT("capture to send (ms)"));
	const FName SourceFps			   = FName(TEXT("capture fps"));

} // namespace PixelStreamingStatNames