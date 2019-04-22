// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

package com.epicgames.ue4;

import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.os.Build;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.Random;
import android.util.Log;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import java.nio.ByteBuffer;

/*
	Custom media player that renders video to a frame buffer. This
	variation is for API 14 and above.
*/
public class AudioDecoder
{
	private MediaCodec mAudioCodec = null;
	private boolean bIsInitialized = false;
	private int mSampleRate;
	private int mChannelCount;
	private int mBitsPerSample; 
	private byte[] mAacSpecificConfig;

	public AudioDecoder()
	{
	}
	
	public byte[] DecodeAudio(byte[] rawBuffer)
	{
		byte[] data = null;
		if( mAudioCodec != null)
		{
			int inIndex = mAudioCodec.dequeueInputBuffer(-1);
			if (inIndex >= 0) 
			{
				int sampleSize = rawBuffer.length;
				ByteBuffer buffer = mAudioCodec.getInputBuffer(inIndex);
				buffer.clear();
				buffer.put(rawBuffer);
				buffer.clear();

				mAudioCodec.queueInputBuffer(inIndex, 0, sampleSize, 0, 0);
			}

			MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();

			int outIndex = mAudioCodec.dequeueOutputBuffer(info, 10000);

			if (outIndex >= 0) 
			{
				ByteBuffer outBuffer = mAudioCodec.getOutputBuffer(outIndex);
				data = new byte[info.size];
			
				outBuffer.get(data);
				outBuffer.clear();
				
				mAudioCodec.releaseOutputBuffer(outIndex, true);
			}
		}
		return data;
	}

	public void reset()
	{
		release();
	}

	public void release()
	{
		if (null != mAudioCodec)
		{
			mAudioCodec.stop();
			mAudioCodec.release();
		}
	}

	private boolean CreateCodec()
	{
		try 
		{
			// Create the decoder
			mAudioCodec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_AUDIO_AAC);

			// Media settings
			MediaFormat format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, mSampleRate, mChannelCount);

			// PPS and SPS headers
			format.setInteger(MediaFormat.KEY_BIT_RATE, mBitsPerSample);

			byte[] bytes = new byte[]{(byte) 0x12, (byte)0x12};
			ByteBuffer bb = ByteBuffer.wrap(bytes);
			format.setByteBuffer("csd-0", bb);
			
			// Configure the decoder
			mAudioCodec.configure(format, null, null, 0);

			// Start the decoder
			mAudioCodec.start();
			bIsInitialized = true;
		}
		catch(Exception e)
		{
			GameActivity.Log.warn("Android Audio Decoder: CreateCodec failed!");		
			e.printStackTrace();
			return false;
		}
		
		return true;
	}

	public boolean updateConfig(int sampleRate, int channelCount, int BitsPerSample, byte[] AacSpecificConfig)
	{
		bIsInitialized = false;
		
		mSampleRate = sampleRate;
		mChannelCount = channelCount;
		mBitsPerSample = BitsPerSample;
		mAacSpecificConfig = AacSpecificConfig;

		if(mAudioCodec != null)
		{
			try 
			{
				mAudioCodec.stop();
				mAudioCodec.release();
			}
			catch(Exception e)
			{
				e.printStackTrace();
			}
		}
		return CreateCodec();
	}

}
