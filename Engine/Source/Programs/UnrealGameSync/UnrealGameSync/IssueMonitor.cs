// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	public enum IssueBuildOutcome
	{
		Unknown,
		Success,
		Error,
		Warning,
	}

	public class IssueBuildData
	{
		public long Id;
		public string Stream;
		public int Change;
		public string JobName;
		public string JobUrl;
		public string JobStepName;
		public string JobStepUrl;
		public string ErrorUrl;
		public IssueBuildOutcome Outcome;
	}

	public class IssueDiagnosticData
	{
		public long? BuildId;
		public string Message;
		public string Url;
	}

	public class IssueData
	{
		public long Id;
		public DateTime CreatedAt;
		public DateTime RetrievedAt;
		public string Project;
		public string Summary;
		public string Details;
		public string Owner;
		public string NominatedBy;
		public DateTime? AcknowledgedAt;
		public int FixChange;
		public DateTime? ResolvedAt;
		public bool bNotify;
		public List<IssueBuildData> Builds;
	}

	public class IssueUpdateData
	{
		public long Id;
		public string Owner;
		public string NominatedBy;
		public bool? Acknowledged;
		public int? FixChange;
		public bool? Resolved;
	}

	[Flags]
	public enum IssueAlertReason
	{
		Normal = 1,
		Owner = 2,
		UnassignedTimer = 4,
		UnacknowledgedTimer = 8,
		UnresolvedTimer = 16,
	}

	class IssueMonitor : IDisposable
	{
		public readonly string ApiUrl;
		public readonly string UserName;
		int RefCount = 1;
		Thread WorkerThread;
		BoundedLogWriter LogWriter;
		bool bDisposing;
		AutoResetEvent RefreshEvent;
		int UpdateIntervalMs;
		List<long> TrackingIssueIds = new List<long>();
		List<IssueData> Issues = new List<IssueData>();
		object LockObject = new object();
		List<IssueUpdateData> PendingUpdates = new List<IssueUpdateData>();

		public event Action OnIssuesChanged;

		// Only used by MainWindow, but easier to just store here
		public Dictionary<long, IssueAlertReason> IssueIdToAlertReason = new Dictionary<long, IssueAlertReason>();

		public IssueMonitor(string ApiUrl, string UserName, TimeSpan UpdateInterval, string LogFileName)
		{
			this.ApiUrl = ApiUrl;
			this.UserName = UserName;
			this.UpdateIntervalMs = (int)UpdateInterval.TotalMilliseconds;

			LogWriter = new BoundedLogWriter(LogFileName);
			if(ApiUrl == null)
			{
				LastStatusMessage = "Database functionality disabled due to empty ApiUrl.";
			}
			else
			{
				LogWriter.WriteLine("Using connection string: {0}", this.ApiUrl);
			}

			RefreshEvent = new AutoResetEvent(false);
		}

		public string LastStatusMessage
		{
			get;
			private set;
		}

		public List<IssueData> GetIssues()
		{
			return Issues;
		}

		public TimeSpan GetUpdateInterval()
		{
			return TimeSpan.FromMilliseconds(UpdateIntervalMs);
		}

		public void SetUpdateInterval(TimeSpan UpdateInterval)
		{
			UpdateIntervalMs = (int)UpdateInterval.TotalMilliseconds;
			RefreshEvent.Set();
		}

		public void StartTracking(long IssueId)
		{
			lock(LockObject)
			{
				TrackingIssueIds.Add(IssueId);
			}
			RefreshEvent.Set();
		}

		public void StopTracking(long IssueId)
		{
			lock(LockObject)
			{
				TrackingIssueIds.RemoveAt(TrackingIssueIds.IndexOf(IssueId));
			}
		}

		public bool HasPendingUpdate()
		{
			return PendingUpdates.Count > 0;
		}

		public void PostUpdate(IssueUpdateData Update)
		{
			bool bUpdatedIssues;
			lock(LockObject)
			{
				PendingUpdates.Add(Update);
				bUpdatedIssues = ApplyPendingUpdate(Issues, Update);
			}

			RefreshEvent.Set();

			if(bUpdatedIssues)
			{
				OnIssuesChanged?.Invoke();
			}
		}

		static bool ApplyPendingUpdate(List<IssueData> Issues, IssueUpdateData Update)
		{
			bool bUpdated = false;
			for(int Idx = 0; Idx < Issues.Count; Idx++)
			{
				IssueData Issue = Issues[Idx];
				if(Update.Id == Issue.Id)
				{
					if(Update.Owner != null && Update.Owner != Issue.Owner)
					{
						Issue.Owner = Update.Owner;
						bUpdated = true;
					}
					if(Update.NominatedBy != null && Update.NominatedBy != Issue.NominatedBy)
					{
						Issue.NominatedBy = Update.NominatedBy;
						bUpdated = true;
					}
					if(Update.Acknowledged.HasValue && Update.Acknowledged.Value != Issue.AcknowledgedAt.HasValue)
					{
						Issue.AcknowledgedAt = Update.Acknowledged.Value? (DateTime?)DateTime.UtcNow : null;
						bUpdated = true;
					}
					if(Update.FixChange.HasValue)
					{
						Issue.FixChange = Update.FixChange.Value;
						if(Issue.FixChange != 0)
						{
							Issues.RemoveAt(Idx);
						}
						bUpdated = true;
					}
					break;
				}
			}
			return bUpdated;
		}

		public void Start()
		{
			if(ApiUrl != null)
			{
				WorkerThread = new Thread(() => PollForUpdates());
				WorkerThread.Start();
			}
		}

		public void AddRef()
		{
			if(RefCount == 0)
			{
				throw new Exception("Invalid reference count for IssueMonitor (zero)");
			}
			RefCount++;
		}

		public void Release()
		{
			RefCount--;
			if(RefCount < 0)
			{
				throw new Exception("Invalid reference count for IssueMonitor (ltz)");
			}
			if(RefCount == 0)
			{
				DisposeInternal();
			}
		}

		void IDisposable.Dispose()
		{
			DisposeInternal();
		}

		void DisposeInternal()
		{
			bDisposing = true;
			if(WorkerThread != null)
			{
				RefreshEvent.Set();
				if(!WorkerThread.Join(100))
				{
					WorkerThread.Abort();
					WorkerThread.Join();
				}
				WorkerThread = null;
			}
			if(LogWriter != null)
			{
				LogWriter.Dispose();
				LogWriter = null;
			}
		}

		void PollForUpdates()
		{
			while (!bDisposing)
			{
				// Check if there's any pending update
				IssueUpdateData PendingUpdate;
				lock(LockObject)
				{
					if(PendingUpdates.Count > 0)
					{
						PendingUpdate = PendingUpdates[0];
					}
					else
					{
						PendingUpdate = null;
					}
				}

				// If we have an update, try to post it to the backend and check for another
				if(PendingUpdate != null)
				{
					if(SendUpdate(PendingUpdate))
					{
						lock (LockObject) { PendingUpdates.RemoveAt(0); }
					}
					else
					{
						RefreshEvent.WaitOne(5 * 1000);
					}
					continue;
				}

				// Read all the current issues
				ReadCurrentIssues();

				// Wait for something else to do
				RefreshEvent.WaitOne(UpdateIntervalMs);
			}
		}

		bool SendUpdate(IssueUpdateData Update)
		{
			try
			{
				RESTApi.PUT<IssueUpdateData>(ApiUrl, String.Format("issues/{0}", Update.Id), Update);
				return true;
			}
			catch(Exception Ex)
			{
				LogWriter.WriteException(Ex, "Failed with exception.");
				LastStatusMessage = String.Format("Failed to send update: ({0})", Ex.ToString());
				return false;
			}
		}

		bool ReadCurrentIssues()
		{
			try
			{
				Stopwatch Timer = Stopwatch.StartNew();
				LogWriter.WriteLine();
				LogWriter.WriteLine("Polling for notifications at {0}...", DateTime.Now.ToString());

				// Get the initial number of issues. We won't post updates if this stays at zero.
				int InitialNumIssues = Issues.Count;

				// Fetch the new issues
				List<IssueData> NewIssues = RESTApi.GET<List<IssueData>>(ApiUrl, String.Format("issues?user={0}", UserName));

				// Check if we're tracking a particular issue. If so, we want updates even when it's resolved.
				long[] LocalTrackingIssueIds;
				lock(LockObject)
				{
					LocalTrackingIssueIds = TrackingIssueIds.Distinct().ToArray();
				}
				foreach(long LocalTrackingIssueId in LocalTrackingIssueIds)
				{
					if(!NewIssues.Any(x => x.Id == LocalTrackingIssueId))
					{
						try
						{
							IssueData Issue = RESTApi.GET<IssueData>(ApiUrl, String.Format("issues/{0}", LocalTrackingIssueId));
							if(Issue != null)
							{
								NewIssues.Add(Issue);
							}
						}
						catch(Exception Ex)
						{
							LogWriter.WriteException(Ex, "Exception while fetching tracked issue");
						}
					}
				}

				// Update all the builds for each issue
				foreach(IssueData NewIssue in NewIssues)
				{
					NewIssue.Builds = RESTApi.GET<List<IssueBuildData>>(ApiUrl, String.Format("issues/{0}/builds", NewIssue.Id));
				}

				// Apply any pending updates to this issue list, and update it
				lock (LockObject)
				{
					foreach(IssueUpdateData PendingUpdate in PendingUpdates)
					{
						ApplyPendingUpdate(NewIssues, PendingUpdate);
					}
					Issues = NewIssues;
				}

				// Update the main thread
				if(InitialNumIssues > 0 || Issues.Count > 0)
				{
					if(OnIssuesChanged != null)
					{
						OnIssuesChanged();
					}
				}

				// Update the stats
				LastStatusMessage = String.Format("Last update took {0}ms", Timer.ElapsedMilliseconds);
				LogWriter.WriteLine("Done in {0}ms.", Timer.ElapsedMilliseconds);
				return true;
			}
			catch(Exception Ex)
			{
				LogWriter.WriteException(Ex, "Failed with exception.");
				LastStatusMessage = String.Format("Last update failed: ({0})", Ex.ToString());
				return false;
			}
		}
	}
}
