// Copyright Epic Games, Inc. All Rights Reserved.
package com.epicgames.ue4.download;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.net.Uri;
import android.os.Build;

import androidx.annotation.Nullable;
import androidx.annotation.NonNull;

import androidx.core.app.NotificationCompat;
import androidx.work.Data;
import androidx.work.ForegroundInfo;
import androidx.work.WorkManager;
import androidx.work.WorkerParameters;

import java.io.File;

import com.epicgames.ue4.Logger;
import com.epicgames.ue4.workmanager.UEWorker;

import com.epicgames.ue4.download.datastructs.DownloadNotificationDescription;
import com.epicgames.ue4.download.DownloadProgressListener;
import com.epicgames.ue4.download.datastructs.DownloadQueueDescription;
import com.epicgames.ue4.download.datastructs.DownloadWorkerParameterKeys;
import com.epicgames.ue4.download.fetch.FetchManager;

import com.tonyodev.fetch2.Download;
import com.tonyodev.fetch2.Error;
import com.tonyodev.fetch2.Request;
import com.tonyodev.fetch2.exception.FetchException;
import static com.tonyodev.fetch2.util.FetchUtils.canRetryDownload;

import java.util.concurrent.TimeUnit;

import static android.content.Context.NOTIFICATION_SERVICE;

//Helper class to manage our different work requests and callbacks
public class UEDownloadWorker extends UEWorker implements DownloadProgressListener
{	
	public enum EDownloadCompleteReason
	{
		Success,
		Error,
		OutOfRetries
	}
	
	public UEDownloadWorker(Context context, WorkerParameters params)
	{
		super(context,params);
		
		//Overwrite the default log to have a more specific log identifier tag
		Log = new Logger("UE4", "UEDownloadWorker");
	}
	
	@Override
	public void InitWorker()
	{
		super.InitWorker();
		
		if (null == mFetchManager)
		{
			mFetchManager = new FetchManager();	
		}
		
		//Make sure we have a CancelIntent we can use to cancel this job (passed into notifications, etc)
		if (null == CancelIntent) 
		{
			CancelIntent = WorkManager.getInstance(getApplicationContext())
				.createCancelPendingIntent(getId());
		}
		
		//Generate our NotificationDescription so that we load important data from our InputData() to control notification content
		if (null == NotificationDescription)
		{
			NotificationDescription = new DownloadNotificationDescription(getInputData(), getApplicationContext(), Log);
		}
	}
	
	@Override
	public void OnWorkerStart(String WorkID)
	{
		super.OnWorkerStart(WorkID);
	
		//TODO: TRoss this should be based on some WorkerParameter and handled in UEWorker
		//Set this as an important task so that it continues even when the app closes, etc.
		setForegroundAsync(CreateForegroundInfo(NotificationDescription));
		
		if (mFetchManager == null)
		{
			Log.error("OnWorkerStart called without a valid FetchInstance! Failing Worker and completing!");
			SetWorkResult_Failure();
			return;
		}
		
		//Setup downloads in mFetchManager
		DownloadQueueDescription QueueDescription = new DownloadQueueDescription(getInputData(), getApplicationContext(), Log);
		QueueDescription.ProgressListener = this;
		mFetchManager.EnqueueRequests(getApplicationContext(),QueueDescription);

       
		//Enter actual loop until work is finished
		Log.verbose("Entering OnWorkerStart Loop waiting for Fetch2");
		try 
		{
			while (bReceivedResult == false)
			{
				Thread.sleep(500);
				Tick(QueueDescription);
			}
		} 
		catch (InterruptedException e) 
		{
			Log.error("Exception trying to sleep thread. Setting work result to retry and shutting down");
			e.printStackTrace();
			
			SetWorkResult_Retry();
		}
		finally
		{
			CleanUp(WorkID);
		}
	}
	
	private void Tick(DownloadQueueDescription QueueDescription)
	{
		mFetchManager.RequestGroupProgressUpdate(QueueDescription.DownloadGroupID,  this);
		
		nativeAndroidBackgroundDownloadOnTick();
	}
	
	@Override
	public void OnWorkerStopped(String WorkID)
	{	
		Log.debug("OnWorkerStopped called for " + WorkID);
		super.OnWorkerStopped(WorkID);
		
		CleanUp(WorkID);
	}
	
	public void CleanUp(String WorkID)
	{
		//Call stop work to make sure Fetch stops doing work while 
		mFetchManager.StopWork(WorkID);
		
		//Clean up our DownloadDescriptionList file if our work is not going to re-run ever
		if (ShouldCleanupDownloadDescriptorJSONFile())
		{
			Data data = getInputData();
			if (null != data)
			{
				String DownloadDescriptionListString = data.getString(DownloadWorkerParameterKeys.DOWNLOAD_DESCRIPTION_LIST_KEY);
				if (null != DownloadDescriptionListString)
				{
					File DeleteFile = new File(DownloadDescriptionListString);
					if (DeleteFile.exists())
					{
						DeleteFile.delete();
					}
				}
			}
		}
	}
	
	public void UpdateNotification(int CurrentProgress, boolean Indeterminate)
	{
		if (null != NotificationDescription)
		{
			NotificationDescription.CurrentProgress = CurrentProgress;
			NotificationDescription.Indeterminate = Indeterminate;

			setForegroundAsync(CreateForegroundInfo(NotificationDescription));
		}
		else
		{
			Log.error("Unexpected NULL NotificationDescripton during UpdateNotification!");
		}
	}
	
	@NonNull
	private ForegroundInfo CreateForegroundInfo(DownloadNotificationDescription Description) 
	{		
		Context context = getApplicationContext();
		NotificationManager notificationManager = GetNotificationManager(context);
		
		CreateNotificationChannel(context, notificationManager, Description);
		
		String NotificationTextToUse = null;
		if (Description.CurrentProgress < Description.MAX_PROGRESS)
		{
			NotificationTextToUse = Description.ContentText;
		}
		else
		{
			NotificationTextToUse = Description.ContentCompleteText;
		}
		
		Notification notification = new NotificationCompat.Builder(context, Description.NotificationChannelID)
			.setContentTitle(Description.TitleText)
			.setTicker(Description.TitleText)
			.setContentText(NotificationTextToUse)
			.setProgress(Description.MAX_PROGRESS,Description.CurrentProgress, Description.Indeterminate)
			.setOngoing(true)
			.setOnlyAlertOnce (true)
			.setSmallIcon(Description.SmallIconResourceID)
			.addAction(Description.CancelIconResourceID, Description.CancelText, CancelIntent)
			.build();	
		
		return new ForegroundInfo(Description.NotificationID,notification);
	}

	//Gets the Notification Manager through the appropriate method based on build version
	public NotificationManager GetNotificationManager(@NonNull Context context)
	{
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
		{
			return context.getSystemService(NotificationManager.class);
		}
		else
		{
			return (NotificationManager)context.getSystemService(NOTIFICATION_SERVICE);
		}
	}

	private void CreateNotificationChannel(Context context, NotificationManager notificationManager, DownloadNotificationDescription Description)
	{
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
		{
			if (notificationManager != null)
			{
				//Don't create if it already exists
				NotificationChannel Channel = notificationManager.getNotificationChannel(Description.NotificationChannelID);
				if (Channel == null)
				{
					Channel = new NotificationChannel(Description.NotificationChannelID, Description.NotificationChannelName, Description.NotificationChannelImportance);
					notificationManager.createNotificationChannel(Channel);
				}
			}
		}
	}
	
	public boolean ShouldCleanupDownloadDescriptorJSONFile()
	{
		return (IsWorkEndTerminal());
	}
	
	//
	// DownloadCompletionListener Implementation
	//
	@Override
	public void OnDownloadProgress(String RequestID, long BytesWrittenSinceLastCall, long TotalBytesWritten)
	{
		nativeAndroidBackgroundDownloadOnProgress(RequestID, BytesWrittenSinceLastCall, TotalBytesWritten);
	}
	
	@Override
	public void OnDownloadGroupProgress(int GroupID, int Progress, boolean Indeterminate)
	{
		//For now all downloads are in the same GroupID, but in the future we will want a notification for each group ID 
		//and to upgate them separately here.
		UpdateNotification(Progress, Indeterminate);
	}
	
	@Override
	public void OnDownloadComplete(String RequestID, String CompleteLocation, EDownloadCompleteReason CompleteReason)
	{
		boolean bWasSuccess = (CompleteReason == EDownloadCompleteReason.Success);
		nativeAndroidBackgroundDownloadOnComplete(RequestID, CompleteLocation, bWasSuccess);
	}
	
	@Override
	public void OnAllDownloadsComplete(boolean bDidAllRequestsSucceed)
	{	
		UpdateNotification(100, false);
		
		nativeAndroidBackgroundDownloadOnAllComplete(bDidAllRequestsSucceed);
		
		//If UE code didn't provide a result for the work in the above callback(IE: Engine isn't running yet, we are completely in background, etc.) 
		//then we need to still flag this Worker as completed and shutdown now that our task is finished
		if (!bReceivedResult)
		{
			if (bDidAllRequestsSucceed)
			{
				SetWorkResult_Success();
			}
			//by default if UE didn't give us a behavior, lets just retry the download through the worker if one of the downloads failed
			else
			{
				SetWorkResult_Retry();
			}
		}
	}
	
	//
	// Functions called by our UE c++ code on this object
	//
	public void PauseRequest(String RequestID)
	{
		mFetchManager.PauseDownload(RequestID, true);
	}
	
	public void ResumeRequest(String RequestID)
	{
		mFetchManager.PauseDownload(RequestID, false);
	}
	
	public void CancelRequest(String RequestID)
	{
		mFetchManager.CancelDownload(RequestID);
	}
	
	//Native functions used to bubble up progress to native UE code
	public native void nativeAndroidBackgroundDownloadOnProgress(String TaskID, long BytesWrittenSinceLastCall, long TotalBytesWritten);
	public native void nativeAndroidBackgroundDownloadOnComplete(String TaskID, String CompleteLocation, boolean bWasSuccess);
	public native void nativeAndroidBackgroundDownloadOnAllComplete(boolean bDidAllRequestsSucceed);
	public native void nativeAndroidBackgroundDownloadOnTick();
	
	static FetchManager mFetchManager;
	private PendingIntent CancelIntent = null;
	private DownloadNotificationDescription NotificationDescription = null;
}