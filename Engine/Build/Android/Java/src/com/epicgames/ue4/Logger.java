package com.epicgames.ue4;

import android.util.Log;

public class Logger
{
	public interface ILoggerCallback
	{
		void LoggerCallback(String Level, String Tag, String Message);
	}

	private static ILoggerCallback mCallback = null;
	private String mTag;
	
	private static boolean bAllowLogging			= true;
	@SuppressWarnings({"FieldCanBeLocal", "unused"})
	private static boolean bAllowExceptionLogging	= true;

	@SuppressWarnings("unused")
	public static void RegisterCallback(ILoggerCallback callback)
	{
		mCallback = callback;
	}

	@SuppressWarnings("WeakerAccess")
	public static void SuppressLogs ()
	{
		bAllowLogging = bAllowExceptionLogging = false;
	}

	public Logger(String Tag)
	{
		mTag = Tag;
	}

	public void verbose(String Message)
	{
		if (bAllowLogging)
		{
			Log.v(mTag, Message);
		}
		if (mCallback != null)
		{
			mCallback.LoggerCallback("V/", mTag, Message);
		}
	}

	public void debug(String Message)
	{
		if (bAllowLogging)
		{
			Log.d(mTag, Message);
		}
		if (mCallback != null)
		{
			mCallback.LoggerCallback("D/", mTag, Message);
		}
	}
	
	public void warn(String Message)
	{
		if (bAllowLogging)
		{
			Log.w(mTag, Message);
		}
		if (mCallback != null)
		{
			mCallback.LoggerCallback("W/", mTag, Message);
		}
	}
	
	public void error(String Message)
	{
		if (bAllowLogging)
		{
			Log.e(mTag, Message);
		}
		if (mCallback != null)
		{
			mCallback.LoggerCallback("E/", mTag, Message);
		}
	}

	public void error(String Message, Throwable Throwable)
	{
		if (bAllowLogging)
		{
			Log.e(mTag, Message, Throwable);
		}
		if (mCallback != null)
		{
			mCallback.LoggerCallback("E/", mTag, Message);
		}
	}
}