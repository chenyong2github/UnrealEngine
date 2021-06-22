// Copyright Epic Games, Inc. All Rights Reserved.
package com.epicgames.ue4.download.fetch;

import android.content.Context;
import android.net.Uri;

import com.epicgames.ue4.Logger;
import com.epicgames.ue4.download.datastructs.DownloadDescription;
import com.epicgames.ue4.download.datastructs.DownloadQueueDescription;
import com.epicgames.ue4.download.fetch.FetchDownloadProgressOwner;
import com.epicgames.ue4.download.DownloadProgressListener;
import com.epicgames.ue4.download.fetch.FetchEnqueueResultListener;
import com.epicgames.ue4.download.fetch.FetchRequestProgressListener;

import com.epicgames.ue4.download.UEDownloadWorker.EDownloadCompleteReason;
import com.epicgames.ue4.download.fetch.FetchRequestProgressListener.ECompleteReason;

import com.tonyodev.fetch2.CompletedDownload;
import com.tonyodev.fetch2.Download;
import com.tonyodev.fetch2.Error;
import com.tonyodev.fetch2.Fetch;
import com.tonyodev.fetch2.FetchConfiguration;
import com.tonyodev.fetch2.FetchGroup;
import com.tonyodev.fetch2.FetchListener;
import com.tonyodev.fetch2.NetworkType;
import com.tonyodev.fetch2.Priority;
import com.tonyodev.fetch2.Request;
import com.tonyodev.fetch2.exception.FetchException;
import com.tonyodev.fetch2core.Func;
import com.tonyodev.fetch2core.Func2;

import androidx.annotation.Nullable;
import androidx.annotation.NonNull;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Set;

//Class that handles setting up and managing Fetch2 requests
public class FetchManager implements FetchDownloadProgressOwner, FetchEnqueueResultListener
{
	public FetchManager()
	{
		RequestedDownloads =  new HashMap<String,DownloadDescription>();
	}
	
	public void StopWork(String WorkID)
	{
		if (null != FetchInstance)
		{
			FetchInstance.freeze();
		}
	}
	
	public void EnqueueRequests(Context context, DownloadQueueDescription QueueDescription)
	{
		InitFetch(context);
		
		int NumDownloads = QueueDescription.DownloadDescriptions.size();
		Log.debug("EnqueueRequests called with " + NumDownloads + " DownloadDescriptions. Current RequestedDownloads Size: " + RequestedDownloads.size());

		SetVariablesFromQueueDescription(QueueDescription);
		ReconcileDownloadDescriptions(QueueDescription);
	}
	
	//Does any setup we need based on data in the QueueDescription
	public void SetVariablesFromQueueDescription(DownloadQueueDescription QueueDescription)
	{
		if (false == IsFetchInstanceValid())
		{
			Log.error("Call to SetVariablesFromQueueDescription after FetchInstance is invalidated!");
			return;
		}
		
		FetchInstance.setDownloadConcurrentLimit(QueueDescription.MaxConcurrentDownloads);		
	}
		
	//Goes through the DownloadDescriptions in our DownloadQueueDescription and finds DownloadDescription that
	//don't match our ActiveDownloadDescriptions entries and thus need to be reconciled (either added or modified in some way)
	private void ReconcileDownloadDescriptions(DownloadQueueDescription QueueDescription)
	{
		final int NumNewDownloadDescriptions = QueueDescription.DownloadDescriptions.size();
		for (int DescriptionIndex = 0; DescriptionIndex < NumNewDownloadDescriptions; ++DescriptionIndex)
		{
			DownloadDescription NewDownloadDescription = QueueDescription.DownloadDescriptions.get(DescriptionIndex);
			
			//Set any DownloadDescription settings that need to come from our QueueDescription
			NewDownloadDescription.ProgressListener = QueueDescription.ProgressListener;
							
			String RequestIDKey = NewDownloadDescription.RequestID;
			//Don't even have an entry for this RequestID, so it's completely new
			if (!RequestedDownloads.containsKey(RequestIDKey))
			{
				QueueNewDownloadDescription(NewDownloadDescription);
			}
			//Need to actually compare to determine if anything has changed
			else
			{
				DownloadDescription ActiveDescription = RequestedDownloads.get(RequestIDKey);
				final boolean bDescriptionHasChanged = ActiveDescription.equals(NewDownloadDescription);
				if (!bDescriptionHasChanged)
				{
					HandleChangedDownloadDescription(ActiveDescription, NewDownloadDescription);
				}
			}
		}
	}
	
	public void QueueNewDownloadDescription(DownloadDescription Description)
	{
		//We have hit this code after something else has already invalidated our FetchInstance
		if (false == IsFetchInstanceValid())
		{
			Log.error("Call to QueueNewDownloadDescription after FetchInstance is invalidated! RequestID:" + Description.RequestID);
			return;
		}
		
		RequestedDownloads.put(Description.RequestID, Description);
						
		FetchEnqueueResultListener.FetchEnqueueRequestCallback RequestCallback = new FetchEnqueueResultListener.FetchEnqueueRequestCallback(this);
		FetchEnqueueResultListener.FetchEnqueueErrorCallback ErrorCallback = new FetchEnqueueResultListener.FetchEnqueueErrorCallback(this, Description.RequestID);
		
		if (!Description.bIsCancelled)
		{
			Request FetchRequest = BuildFetchRequest(Description);
			Description.CachedFetchID = FetchRequest.getId();
			
			FetchInstance.enqueue(FetchRequest, RequestCallback, ErrorCallback);
		}		 
	}

	public void PauseDownload(String RequestID, boolean bPause)
	{
		DownloadDescription MatchedDesc = RequestedDownloads.get(RequestID);
		if (null == MatchedDesc)
		{
			Log.error("No DownloadDescription found for RequestID " + RequestID +" during PauseDownload!");
			return;
		}
		
		MatchedDesc.bIsPaused = bPause;
		if (bPause)
		{
			FetchInstance.pause(MatchedDesc.CachedFetchID);
		}
		else
		{
			FetchInstance.resume(MatchedDesc.CachedFetchID);
		}
	}

	public void CancelDownload(String RequestID)
	{
		DownloadDescription MatchedDesc = RequestedDownloads.get(RequestID);
		if (null == MatchedDesc)
		{
			Log.error("No DownloadDescription found for RequestID " + RequestID +" during CancelDownload!");
			return;
		}
		
		if (false == MatchedDesc.bIsCancelled)
		{
			MatchedDesc.bIsCancelled = true;
			FetchInstance.cancel(MatchedDesc.CachedFetchID);
		}
	}
	
	private Request BuildFetchRequest(DownloadDescription Description)
	{
		String URL = GetNextURL(Description);
		Uri DownloadUri = Uri.parse(Description.DestinationLocation);

		Request FetchRequest = new Request(URL, DownloadUri);
		FetchRequest.setPriority(GetFetchPriority(Description));
		FetchRequest.setTag(Description.RequestID);
		FetchRequest.setGroupId(Description.GroupID);
		FetchRequest.setAutoRetryMaxAttempts(Description.IndividualURLRetryCount);
		
		//For now we don't specify this on the fetch request because its already specified on the WorkManager worker, which should stop all our
		//downloading when it stops work anyway.
		FetchRequest.setNetworkType(NetworkType.ALL);
			
		return FetchRequest;
	}
	
	private void HandleChangedDownloadDescription(DownloadDescription OldDescription, DownloadDescription NewDescription)
	{
		CopyStateToNewDescription(OldDescription, NewDescription);
		
		//Handle the change by cancelling and recreating the fetch2 download
		RecreateDownloadByTagFunc RecreateFunc = new RecreateDownloadByTagFunc(this, NewDescription);
		FetchInstance.getDownloadsByTag(NewDescription.RequestID, RecreateFunc);
	}
	
	//Copies over the non-serialized values that track download state from our old descrition to our new description so that changing
	//serialized values doesn't wipe out existing downlod behavior
	private void CopyStateToNewDescription(DownloadDescription OldDescription, DownloadDescription NewDescription)
	{
		NewDescription.CurrentRetryCount = OldDescription.CurrentRetryCount;
		NewDescription.CachedFetchID = OldDescription.CachedFetchID;
		NewDescription.bIsPaused = OldDescription.bIsPaused;
		NewDescription.PreviousDownloadedBytes = OldDescription.PreviousDownloadedBytes;
		
		//Purposefully don't copy DownloadProgressListener as we set this based on the new QueueDescription already and likely
		//don't want to keep the old one (although in current code they match).
		//NewDescription.DownloadProgressListener ProgressListener = OldDescription.DownloadProgressListener;
	}
	
	private boolean IsFetchInstanceValid()
	{
		return ((null != FetchInstance)	&& (false == FetchInstance.isClosed()));
	}
	
	private String GetNextURL(DownloadDescription Description)
	{
		if (IsOutOfRetries(Description))
		{
			return null;
		}
		
		final int NumURLs = Description.URLs.size();
		final int URLIndexToUse = Description.CurrentRetryCount % NumURLs;
		
		return Description.URLs.get(URLIndexToUse);
	}
	
	private boolean IsOutOfRetries(DownloadDescription Description)
	{
		return ((Description.CurrentRetryCount >= Description.MaxRetryCount) && Description.MaxRetryCount >= 0);
	}
	
	private Priority GetFetchPriority(DownloadDescription Description)
	{
		switch (Description.RequestPriority)
		{
		case 0:
			return Priority.NORMAL;
		case 1:
			return Priority.HIGH; 
		case -1:
			return Priority.LOW;
		default:
			return Priority.NORMAL;
		}
	}
	
	private void InitFetch(Context context)
	{
		//Make sure any existing FetchInstance is in a correct state (either null and ready to be created, or open and unfrozen ready to do work
		if (FetchInstance != null)
		{
			//If we previously closed our FetchInstance just remove it and recreate it bellow
			if (FetchInstance.isClosed())
			{
				FetchInstance = null;
			}
			//If our FetchInstance exists and isn't closed, its very likely frozen and needs to be unfrozen
			else
			{
				FetchInstance.unfreeze();
			}
		}

		if (FetchInstance == null)
		{
			//TODO TRoss: Pull these values from the worker's getInputData
			FetchInstance = Fetch.Impl.getInstance(new FetchConfiguration.Builder(context)
				.setNamespace(context.getPackageName())
				.enableRetryOnNetworkGain(true)
				.setProgressReportingInterval(200)
				.build());

			//if we are creating our FetchInstance, make sure our FetchListener is also recreated and attached
			FetchListener = null;
		}
		
		if (null == FetchListener)
		{
			FetchListener = new FetchRequestProgressListener(this);
			FetchInstance.addListener(FetchListener);
		}
	}
	
	//Helper class to avoid use of Delegates as our current compile source target is 7 and thus delegates are not supported.
	//Cancels and then recreates downloads found by a given tag.
	private class RecreateDownloadByTagFunc extends CancelDownloadByTagFunc
	{
		@Override
		public void call(List<Download> MatchingDownloads)
		{
			if (IsValid()) 
			{
				//First cancel the download
				super.call(MatchingDownloads);
				
				//Now just have the fetch manager owner recreate the download with the supplied data
				Owner.QueueNewDownloadDescription(RecreateDescription);
			}
		}
		
		public RecreateDownloadByTagFunc(FetchManager Owner, DownloadDescription RecreateDescription)
		{
			this.Owner = Owner;
			this.RecreateDescription = RecreateDescription;
		}
		
		private boolean IsValid()
		{
			return ((null != Owner) && (null != RecreateDescription));
		}
		
		private FetchManager Owner;
		private	DownloadDescription RecreateDescription;
	}

	//Helper class to avoid use of Delegates as our current compile source target is 7 and thus delegates are not supported.
	//Cancels downloads found by a given tag.
	private class CancelDownloadByTagFunc implements Func<List<Download>>
	{
		@Override
		public void call(List<Download> MatchingDownloads)
		{
			if (MatchingDownloads != null)
			{
				for (int DownloadIndex = 0; DownloadIndex < MatchingDownloads.size(); ++DownloadIndex)
				{
					Download FoundDownload = MatchingDownloads.get(DownloadIndex);
					FetchInstance.cancel(FoundDownload.getId());
				}
			}
		}
	}
	
	//Helper Func to avoid delegates that returns a FetchGroup to the DownloadProgressOwner
	private class ReturnDownloadGroupProgress implements Func<FetchGroup>
	{
		@Override
		public void call(@NonNull FetchGroup Group)
		{
			if ((null != Group) && (null != Owner))
			{
				Owner.OnDownloadGroupProgress(Group, CachedDownloadProgressListener);
			}
		}

		ReturnDownloadGroupProgress(FetchDownloadProgressOwner Owner, DownloadProgressListener ProgressListener)
		{
			this.Owner = Owner;
			this.CachedDownloadProgressListener = ProgressListener;
		}
		
		private FetchDownloadProgressOwner Owner;
		private DownloadProgressListener CachedDownloadProgressListener;
	}
	
	public void RequestGroupProgressUpdate(int GroupID, DownloadProgressListener ListenerToUpdate)
	{
		//For now just assume each download is roughly the same size.
		//TODO TRoss, we should pass in the expected download amount potentially for each download and actually compute these values
		//so that we can do more accurate % rather then larger files "slowing down" the progress bar progression.
		
		int TotalProgress = 0;
		int TotalDownloadsInGroup = 0;
		
		ArrayList<String> DownloadKeys = new ArrayList<String>(RequestedDownloads.keySet());
		for (int DescIndex = 0; DescIndex < DownloadKeys.size(); ++DescIndex)
		{
			DownloadDescription FoundDesc = RequestedDownloads.get(DownloadKeys.get(DescIndex));
			
			if (FoundDesc.GroupID == GroupID)
			{
				++TotalDownloadsInGroup;
				TotalProgress += FoundDesc.PreviousDownloadPercent;
			}
		}
				
		//just get the raw average of this to send back
		int TotalToSend = TotalProgress / TotalDownloadsInGroup;
		boolean bIsIndeterminate = (TotalProgress == 0);
		
		//Make sure we cap at 100% progress
		if (TotalToSend > 100)
		{
			TotalToSend = 100;
		}
		ListenerToUpdate.OnDownloadGroupProgress(GroupID, TotalToSend, bIsIndeterminate);
	}

	//
	// FetchDownloadProgressOwner Implementation
	//
	@Override
	public void OnDownloadQueued(@NonNull Download download)
	{
		Log.verbose("OnDownloadChangePauseState");

		//Treat this as just an un-pause for now to make sure we handle cases where Fetch resumes downloads that we paused (This can happen when retrying for networkgain as an example)
		OnDownloadChangePauseState(download, false);
	}

	@Override
	public void OnDownloadProgress(@NonNull Download download, boolean indeterminate, long downloadedBytesPerSecond, long etaInMilliSeconds)
	{
		Log.verbose("OnDownloadProgress bytes/s:" + downloadedBytesPerSecond + " eta:" + etaInMilliSeconds);

		DownloadDescription MatchedDownload = RequestedDownloads.get(GetRequestID(download));
		if (null == MatchedDownload)
		{
			Log.error("OnDownloadProgress called from DownloadProgressOwner implementation with a download that doesn't match any DownloadDesc! Download's Tag: " + GetRequestID(download));
			return;
		}
		
		if (HasValidProgressCallback(MatchedDownload))
		{
			long TotalDownloadedSinceLastCall = 0;
			
			long TotalDownloaded = download.getDownloaded();
			//If our TotalDownload size has gone down, we likely had an error and restarted the download since the previous update.
			//Don't want to return a negative value, so just use 0 for now
			if (MatchedDownload.PreviousDownloadedBytes > TotalDownloaded)
			{
				TotalDownloadedSinceLastCall = 0;
			}
			else
			{
				TotalDownloadedSinceLastCall = (TotalDownloaded - MatchedDownload.PreviousDownloadedBytes);
			}

			MatchedDownload.PreviousDownloadPercent = download.getProgress();
			MatchedDownload.ProgressListener.OnDownloadProgress(GetRequestID(download), TotalDownloadedSinceLastCall, TotalDownloaded);
			
			//Disabled as apparently querying the fetch database during the progress listener functions is a bad idea for performance. ESPECIALLY the OnProgress
			//Kick off request for group progress update
			//ReturnDownloadGroupProgress GroupProgressFunc = new ReturnDownloadGroupProgress(this, MatchedDownload.ProgressListener);
			//FetchInstance.getFetchGroup(download.getGroup(), GroupProgressFunc);
		}
		else
		{
			Log.error("DownloadDescription tied to download does not have a valid ProgressListener callback! RequestID:" + MatchedDownload.RequestID);
		}
	}

	@Override
	public void OnDownloadChangePauseState(@NonNull Download download, boolean bIsPaused)
	{
		Log.debug("OnDownloadChangePauseState bIsPaused:" + bIsPaused);

		DownloadDescription MatchedDownload = RequestedDownloads.get(GetRequestID(download));
		if (null == MatchedDownload)
		{
			Log.error("OnDownloadComplete called from DownloadProgressOwner implementation with a download that doesn't match any DownloadDesc! Download's Tag: " + GetRequestID(download));
			return;
		}
		
		//the DownloadDescription is always definitive, so make sure the new download state matches our expectations
		if (bIsPaused != MatchedDownload.bIsPaused)
		{
			if (MatchedDownload.bIsPaused)
			{
				FetchInstance.pause(download.getId());
			}
			else
			{
				FetchInstance.resume(download.getId());
			}
		}
	}

	@Override
	public void OnDownloadGroupProgress(@NonNull FetchGroup Group, DownloadProgressListener ProgressListener)
	{
		if (null == ProgressListener)
		{
			Log.error("Call to OnDownloadGroupProgress with an invalid DownloadProgressListener!");
			return;
		}
		
		int Progress = Group.getGroupDownloadProgress();
		boolean bIsIndeterminate = (Progress > 0);
		
		ProgressListener.OnDownloadGroupProgress(Group.getId(), Progress, bIsIndeterminate);
	}
	
	//This version is the DownloadProgressListener version of this function! See CompleteDownload for FetchManager implementation
	@Override
	public void OnDownloadComplete(@NonNull Download download, ECompleteReason completeReason)
	{
		Log.debug("OnDownloadComplete Reason:" + completeReason);

		DownloadDescription MatchedDownload = RequestedDownloads.get(GetRequestID(download));
		if (null == MatchedDownload)
		{
			Log.error("OnDownloadComplete called from DownloadProgressOwner implementation with a download that doesn't match any DownloadDesc! Download's Tag: " + GetRequestID(download));
			return;
		}
		
		CompleteDownload(MatchedDownload, completeReason);
	}

	//
	// FetchEnqueueResultListener Implementation
	//
	@Override
	public void OnFetchEnqueueRequestCallback(@NonNull Request EnqueuedRequest)
	{
		Log.verbose("Enqueued Request Success. ID:" + EnqueuedRequest.getId() + " Tag:" + EnqueuedRequest.getTag());
	}

	@Override
	public void OnFetchEnqueueErrorCallback(@NonNull String RequestID, @NonNull Error EnqueueError)
	{
		Log.error("Error Enqueing Request! " + RequestID);
		RetryDownload(RequestID);
	}
	
	//if UEDOwnloadableWorker is calling into it, the only thing it should need is the RequestID
	public void RetryDownload(String RequestID)
	{
		RetryDownload(RequestID, null);
	}
	
	private void RetryDownload(String RequestID, @Nullable Download FetchDownload)
	{
		DownloadDescription MatchingDescription = RequestedDownloads.get(RequestID);
		if (null == MatchingDescription)
		{
			Log.error("RetryDownload called on invalid download that was never requested! RequestID:" + RequestID);
			return;
		}
		
		MatchingDescription.CurrentRetryCount++;

		if (IsOutOfRetries(MatchingDescription))
		{
			CompleteDownload(MatchingDescription, FetchRequestProgressListener.ECompleteReason.OutOfRetries);
			return;
		}
		
		//if we already know the associated FetchDownload we can just pass it through
		if (null != FetchDownload)
		{
			RetryDownload_Internal(MatchingDescription, FetchDownload);
		}
		//We don't know the associated Fetch download, so lets query Fetch for it first to see if it exists
		else
		{
			//Setup a callback to RetryDownload_Internal once we try and get our Download from Fetch
			FetchInstance.getDownload(MatchingDescription.CachedFetchID, new RetryDownloadFunc());
		}
	}
	
	private void RetryDownload_Internal(@NonNull DownloadDescription DownloadDesc, @Nullable Download RetryDownload)
	{
		if (null != RetryDownload)
		{
			//Remove existing download from Fetch
			FetchInstance.remove(RetryDownload.getId());
			
			//Remove partial download file when switching URLs and wait to recreate until the callback is finished for the delete
			FetchInstance.delete(RetryDownload.getId(),new RecreateCallbackWithDownload(DownloadDesc), new RecreateCallbackWithError(DownloadDesc));
		}
		else
		{
			QueueNewDownloadDescription(DownloadDesc);
		}		
	}
			
	private class RetryDownloadFunc implements Func2<Download>
	{
		@Override
		public void call(@Nullable Download MatchingDownload)
		{
			DownloadDescription MatchingDescription = RequestedDownloads.get(GetRequestID(MatchingDownload));
			RetryDownload_Internal(MatchingDescription, MatchingDownload);
		}
	}
	
	private class RecreateCallbackBase
	{
		public RecreateCallbackBase(DownloadDescription CachedDownloadDescription)
		{
			this.CachedDownloadDescription = CachedDownloadDescription;
		}
		private void call_internal()
		{
			if (null != CachedDownloadDescription) 
			{
				QueueNewDownloadDescription(CachedDownloadDescription);
			}
		}
		
		private DownloadDescription CachedDownloadDescription = null;
	}
	private class RecreateCallbackWithDownload extends RecreateCallbackBase implements Func<Download>
	{
		public RecreateCallbackWithDownload(DownloadDescription CachedDownloadDescription)
		{
			super(CachedDownloadDescription);
		}
		
		@Override
		public void call(@NonNull Download CancelledDownload)
		{
			super.call_internal();
		}
	}
	private class RecreateCallbackWithError extends RecreateCallbackBase implements Func<Error>
	{
		public RecreateCallbackWithError(DownloadDescription CachedDownloadDescription)
		{
			super(CachedDownloadDescription);
		}
		
		@Override
		public void call(@Nullable Error CancelError)
		{
			super.call_internal();
		}
	}
	
	private void CompleteDownload(DownloadDescription DownloadDesc, ECompleteReason CompleteReason)
	{
		if (false == HasValidProgressCallback(DownloadDesc))
		{
			Log.error("Call to CompleteDownload with an invalid DownloadDescription!");
			return;
		}
		
		//Only bubble up to the UEDownloadWorker non-intenional completes
		if (IsCompleteReasonIntentional(CompleteReason))
		{
			Log.verbose("Call to CompleteDownload taking no action as complete reason was intentional! RequestID:" + DownloadDesc.RequestID + " CompleteReason:" + CompleteReason);
			return;
		}

		CompletedDownloads.put(DownloadDesc.RequestID, DownloadDesc);
		if (CompleteReason != FetchRequestProgressListener.ECompleteReason.Success)
		{
			FailedDownloads.put(DownloadDesc.RequestID, DownloadDesc);
		}
		
		DownloadDesc.ProgressListener.OnDownloadComplete(DownloadDesc.RequestID, DownloadDesc.DestinationLocation, ConvertCompleteReasonForDownload(CompleteReason));		
		CheckForAllDownloadsComplete(DownloadDesc.ProgressListener);
	}
	
	private EDownloadCompleteReason ConvertCompleteReasonForDownload(ECompleteReason CompleteReason)
	{
		switch (CompleteReason)
		{
			case Error:
				return EDownloadCompleteReason.Error; 
			case Success:
				return EDownloadCompleteReason.Success;
			case OutOfRetries:
				return EDownloadCompleteReason.OutOfRetries;
			default:
				return EDownloadCompleteReason.Error;
		}
	}
	
	private void CheckForAllDownloadsComplete(DownloadProgressListener ProgressListener)
	{
		if (CompletedDownloads.size() < RequestedDownloads.size())
		{
			return;
		}

		if (CompletedDownloads.size() > RequestedDownloads.size())
		{
			Log.error("Error in CompleteDownload logic! CompletedDownloads.size:" + CompletedDownloads.size() + " RequstedDownloads.size():" + RequestedDownloads.size());
		}
		
		boolean bDidAllSucceed = (FailedDownloads.isEmpty());
		ProgressListener.OnAllDownloadsComplete(bDidAllSucceed);
	}
	
	private String GetRequestID(@NonNull Download download)
	{
		return download.getTag();
	}
	
	private boolean HasValidProgressCallback(DownloadDescription DownloadDesc)
	{
		return ((null != DownloadDesc) && (null != DownloadDesc.ProgressListener));
	}
	//Used to determine if the reason a download completed was because of action we took. IE: Cancelling, deleting, removing, etc that happened through calls to the FetchManager.
	//Or if it was a change of status by hitting an error, succeeding, etc.
	private boolean IsCompleteReasonIntentional(FetchRequestProgressListener.ECompleteReason CompleteReason)
	{
		switch (CompleteReason)
		{
			//All cases that are intentional should be here
			case Cancelled:
			case Deleted:
			case Removed:
				return false;
			//by default assume its not intentional
			default:
				return false;
		}
	}
	//private boolean ShouldRetryRequest(@NonNull Request 
	
	private Fetch FetchInstance = null;

	private FetchRequestProgressListener FetchListener = null;
	
	private HashMap<String, DownloadDescription> RequestedDownloads = new HashMap<String, DownloadDescription>();
	private HashMap<String, DownloadDescription> CompletedDownloads = new HashMap<String, DownloadDescription>();
	private HashMap<String, DownloadDescription> FailedDownloads = new HashMap<String, DownloadDescription>();
	
	public Logger Log = new Logger("UE4", "FetchManager");
}