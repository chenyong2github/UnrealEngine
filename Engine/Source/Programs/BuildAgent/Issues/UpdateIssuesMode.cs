// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Runtime.Serialization.Json;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Web;
using System.Web.Script.Serialization;
using Tools.DotNETCommon;
using Tools.DotNETCommon.Perforce;
using BuildAgent.Issues.Matchers;
using BuildAgent.WebApi;

namespace BuildAgent.Issues
{
	[ProgramMode("UpdateIssues", "Updates the UGS build health system with output from completed builds")]
	class UpdateIssuesMode : ProgramMode
	{
		// Register all the pattern matchers
		static readonly List<Matcher> Matchers = new List<Matcher>()
		{ 
			new CompileIssueMatcher(),
			new UndefinedSymbolMatcher(),
			new CopyrightNoticeMatcher(),
			new ContentIssueMatcher()
		};

		static readonly Dictionary<string, Matcher> CategoryToMatcher = Matchers.ToDictionary(x => x.Category, x => x);

		class CachedChangeInfo
		{
			public string Stream;
			public int PrevChange;
			public int NextChange;
			public IReadOnlyList<ChangeInfo> Changes;
		}

		List<CachedChangeInfo> CachedChanges = new List<CachedChangeInfo>();

		[CommandLine("-Server=")]
		[Description("Url of the UGS metadata service")]
		string ServerUrl = null;

		[CommandLine("-InputFile=")]
		[Description("Path to an input file containing completed jobs")]
		FileReference InputFile = null;

		[CommandLine("-StateFile=", Required = true)]
		[Description("Path to a file containing persistent state")]
		FileReference StateFile = null;

		[CommandLine("-P4Port=")]
		[Description("Server and port for P4 commands")]
		string PerforcePort = null;

		[CommandLine("-P4User=")]
		[Description("Username to use for P4 commands")]
		string PerforceUser = null;

		[CommandLine("-Clean")]
		[Description("Removes the existing state file and creates a new one")]
		bool bClean = false;

		[CommandLine("-KeepHistory")]
		[Description("Retain history of all completed jobs in the state file, rather than purging after a few days.")]
		bool bKeepHistory = false;

		[CommandLine("-ReadOnly")]
		[Description("Do not modify any files on disk.")]
		bool bReadOnly = false;

		[CommandLine("-SaveUnmatched")]
		[Description("Path to a directory to save any unmatched issues")]
		DirectoryReference SaveUnmatchedDir = null;

		/// <summary>
		/// Main command entry point
		/// </summary>
		/// <param name="Arguments">The command line arguments</param>
		public override int Execute()
		{
			// Build a mapping from category to matching
			Dictionary<string, Matcher> CategoryNameToMatcher = new Dictionary<string, Matcher>();
			foreach (Matcher Matcher in Matchers)
			{
				CategoryNameToMatcher[Matcher.Category] = Matcher;
			}

			// Complete any interrupted operation to update the state file
			CompleteStateTransaction(StateFile);

			// Read the persistent data file
			PersistentState State;
			if (!bClean && FileReference.Exists(StateFile))
			{
				Log.TraceInformation("Reading persistent data from {0}", StateFile);
				State = DeserializeJson<PersistentState>(StateFile);
			}
			else
			{
				Log.TraceInformation("Creating new persistent data");
				State = new PersistentState();
			}

			// Fixup any issues loaded from disk
			foreach(Issue Issue in State.Issues)
			{
				if(Issue.References == null)
				{
					Issue.References = new SortedSet<string>();
				}
			}

			// Create the Perforce connection
			PerforceConnection Perforce = new PerforceConnection(PerforcePort, PerforceUser, null);

			// Process the input data
			if(InputFile != null)
			{
				// Parse the input file
				Log.TraceInformation("Reading build results from {0}", InputFile);
				InputData InputData = DeserializeJson<InputData>(InputFile);

				// Parse all the builds and add them to the persistent data
				List<InputJob> InputJobs = InputData.Jobs.OrderBy(x => x.Change).ThenBy(x => x.Stream).ToList();
				Stopwatch Timer = Stopwatch.StartNew();
				foreach (InputJob InputJob in InputJobs)
				{
					// Add a new build for each job step
					foreach(InputJobStep InputJobStep in InputJob.Steps)
					{
						IssueBuild NewBuild = new IssueBuild(InputJob.Change, InputJob.Name, InputJob.Url, InputJobStep.Name, InputJobStep.Url, null);
						State.AddBuild(InputJob.Stream, NewBuild);
					}

					// Add all the job steps
					List<InputJobStep> InputJobSteps = InputJob.Steps.OrderBy(x => x.Name).ToList();
					foreach (InputJobStep InputJobStep in InputJobSteps)
					{
						if (InputJobStep.Diagnostics != null && InputJobStep.Diagnostics.Count > 0)
						{
							AddStep(Perforce, State, InputJob, InputJobStep);
						}
					}
					
					// Remove any steps which are empty
					InputJob.Steps.RemoveAll(x => x.Diagnostics == null || x.Diagnostics.Count == 0);
				}
				InputJobs.RemoveAll(x => x.Steps.Count == 0);
				Log.TraceInformation("Added jobs in {0}s", Timer.Elapsed.TotalSeconds);

				// If there are any unmatched issues, save out the current state and remaining input
				if(SaveUnmatchedDir != null && InputJobs.Count > 0)
				{
					DirectoryReference.CreateDirectory(SaveUnmatchedDir);
					if(FileReference.Exists(StateFile))
					{
						FileReference.Copy(StateFile, FileReference.Combine(SaveUnmatchedDir, "State.json"), true);
					}
					SerializeJson(FileReference.Combine(SaveUnmatchedDir, "Input.json"), InputData);
				}

				// Try to find the next successful build for each stream, so we can close it as part of updating the server
				for (int Idx = 0; Idx < State.Issues.Count; Idx++)
				{
					Issue Issue = State.Issues[Idx];
					foreach(string Stream in Issue.Streams.Keys)
					{
						Dictionary<string, IssueHistory> StepNameToHistory = Issue.Streams[Stream];
						foreach(string StepName in StepNameToHistory.Keys)
						{
							IssueHistory IssueHistory = StepNameToHistory[StepName];
							if(IssueHistory.FailedBuilds.Count > 0 && IssueHistory.NextSuccessfulBuild == null)
							{
								// Find the successful build after this change
								IssueBuild LastFailedBuild = IssueHistory.FailedBuilds[IssueHistory.FailedBuilds.Count - 1];
								IssueHistory.NextSuccessfulBuild = State.FindBuildAfter(Stream, LastFailedBuild.Change, StepName);
							}
						}
					}
				}

				// Find the change two days before the latest change being added
				if(InputData.Jobs.Count > 0 && !bKeepHistory)
				{
					// Find all the unique change numbers for each stream
					SortedSet<int> ChangeNumbers = new SortedSet<int>();
					foreach (List<IssueBuild> Builds in State.Streams.Values)
					{
						ChangeNumbers.UnionWith(Builds.Select(x => x.Change));
					}

					// Get the latest change record
					int LatestChangeNumber = InputData.Jobs.Min(x => x.Change);
					ChangeRecord LatestChangeRecord = Perforce.GetChange(GetChangeOptions.None, LatestChangeNumber).Data;

					// Step forward through all the changelists until we get to one we don't want to delete
					int DeleteChangeNumber = -1;
					foreach(int ChangeNumber in ChangeNumbers)
					{
						ChangeRecord ChangeRecord = Perforce.GetChange(GetChangeOptions.None, ChangeNumber).Data;
						if (ChangeRecord.Date > LatestChangeRecord.Date - TimeSpan.FromDays(2))
						{
							break;
						}
						DeleteChangeNumber = ChangeNumber;
					}

					// Remove any builds we no longer want to track
					foreach (List<IssueBuild> Builds in State.Streams.Values)
					{
						Builds.RemoveAll(x => x.Change <= DeleteChangeNumber);
					}
				}
			}

			// Mark any issues as resolved
			foreach(Issue Issue in State.Issues)
			{
				if(Issue.IsResolved())
				{
					if(!Issue.ResolvedAt.HasValue)
					{
						Issue.ResolvedAt = DateTime.UtcNow;
					}
				}
				else
				{
					if(Issue.ResolvedAt.HasValue)
					{
						Issue.ResolvedAt = null;
					}
				}
			}

			// If we're in read-only mode, don't write anything out
			if(bReadOnly)
			{
				return 0;
			}

			// Save the persistent data
			Log.TraceInformation("Writing persistent data to {0}", StateFile);
			DirectoryReference.CreateDirectory(StateFile.Directory);
			WriteState(StateFile, State);

			// Synchronize with the server
			if (ServerUrl != null)
			{
				// Post any issue updates
				foreach(Issue Issue in State.Issues)
				{
					Matcher Matcher;
					if(!CategoryNameToMatcher.TryGetValue(Issue.Category, out Matcher))
					{
						continue;
					}

					string Summary = Matcher.GetSummary(Issue);
					if (Issue.Id == -1)
					{
						Log.TraceInformation("Adding issue: {0}", Issue);

						if(Issue.PendingWatchers.Count == 0)
						{
							Log.TraceWarning("(No possible causers)");
						}

						ApiTypes.AddIssue IssueBody = new ApiTypes.AddIssue();
						IssueBody.Project = Issue.Project;
						IssueBody.Summary = Summary;

						if(Issue.PendingWatchers.Count == 1)
						{
							IssueBody.Owner = Issue.PendingWatchers.First();
						}

						using(HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues", ServerUrl), "POST", IssueBody))
						{
							int ResponseCode = (int)Response.StatusCode;
							if (!(ResponseCode >= 200 && ResponseCode <= 299))
							{
								throw new Exception("Unable to add issue");
							}
							Issue.Id = ParseHttpResponse<ApiTypes.AddIssueResponse>(Response).Id;
						}

						Issue.PostedSummary = Summary;
						WriteState(StateFile, State);
					}
					else if(Issue.PostedSummary == null || !String.Equals(Issue.PostedSummary, Summary, StringComparison.Ordinal))
					{
						Log.TraceInformation("Updating issue {0}", Issue.Id);

						ApiTypes.UpdateIssue IssueBody = new ApiTypes.UpdateIssue();
						IssueBody.Summary = Summary;

						using (HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}", ServerUrl, Issue.Id), "PUT", IssueBody))
						{
							int ResponseCode = (int)Response.StatusCode;
							if (!(ResponseCode >= 200 && ResponseCode <= 299))
							{
								throw new Exception("Unable to add issue");
							}
						}

						Issue.PostedSummary = Summary;
						WriteState(StateFile, State);
					}
				}

				// Add any new builds associated with issues
				Dictionary<string, long> JobStepUrlToId = new Dictionary<string, long>(StringComparer.Ordinal);
				foreach (Issue Issue in State.Issues)
				{
					foreach(KeyValuePair<string, Dictionary<string, IssueHistory>> StreamPair in Issue.Streams)
					{
						foreach(IssueHistory StreamHistory in StreamPair.Value.Values)
						{
							foreach(IssueBuild Build in StreamHistory.Builds)
							{
								if(!Build.bPostedToServer)
								{
									Log.TraceInformation("Adding {0} to issue {1}", Build.JobStepUrl, Issue.Id);

									ApiTypes.AddBuild AddBuild = new ApiTypes.AddBuild();
									AddBuild.Stream = StreamPair.Key;
									AddBuild.Change = Build.Change;
									AddBuild.JobName = Build.JobName;
									AddBuild.JobUrl = Build.JobUrl;
									AddBuild.JobStepName = Build.JobStepName;
									AddBuild.JobStepUrl = Build.JobStepUrl;
									AddBuild.ErrorUrl = Build.ErrorUrl;
									AddBuild.Outcome = (Build == StreamHistory.PrevSuccessfulBuild || Build == StreamHistory.NextSuccessfulBuild)? ApiTypes.Outcome.Success : ApiTypes.Outcome.Error;

									using (HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}/builds", ServerUrl, Issue.Id), "POST", AddBuild))
									{
										int ResponseCode = (int)Response.StatusCode;
										if (!(ResponseCode >= 200 && ResponseCode <= 299))
										{
											throw new Exception("Unable to add build");
										}
										Build.Id = ParseHttpResponse<ApiTypes.AddBuildResponse>(Response).Id;
									}

									Build.bPostedToServer = true;
									WriteState(StateFile, State);
								}
								if(Build.Id != -1)
								{
									JobStepUrlToId[Build.JobStepUrl] = Build.Id;
								}
							}
						}
					}
				}

				// Add any new diagnostics
				foreach(Issue Issue in State.Issues)
				{
					foreach(IssueDiagnostic Diagnostic in Issue.Diagnostics)
					{
						if(!Diagnostic.bPostedToServer)
						{
							string Summary = Diagnostic.Message;

							const int MaxLength = 40;
							if(Summary.Length > MaxLength)
							{
								Summary = Summary.Substring(0, MaxLength).TrimEnd();
							}

							Log.TraceInformation("Adding diagnostic '{0}' to issue {1}", Summary, Issue.Id);

							ApiTypes.AddDiagnostic AddDiagnostic = new ApiTypes.AddDiagnostic();

							long BuildId;
							if(Diagnostic.JobStepUrl != null && JobStepUrlToId.TryGetValue(Diagnostic.JobStepUrl, out BuildId))
							{
								AddDiagnostic.BuildId = BuildId;
							}
							else
							{
								Console.WriteLine("ERROR");
							}

							AddDiagnostic.Message = Diagnostic.Message;
							AddDiagnostic.Url = Diagnostic.ErrorUrl;

							using (HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}/diagnostics", ServerUrl, Issue.Id), "POST", AddDiagnostic))
							{
								int ResponseCode = (int)Response.StatusCode;
								if (!(ResponseCode >= 200 && ResponseCode <= 299))
								{
									throw new Exception("Unable to add build");
								}
							}

							Diagnostic.bPostedToServer = true;
							WriteState(StateFile, State);
						}
					}
				}

				// Close any issues which are complete
				for (int Idx = 0; Idx < State.Issues.Count; Idx++)
				{
					Issue Issue = State.Issues[Idx];
					if (Issue.ResolvedAt.HasValue != Issue.bPostedResolved)
					{
						Log.TraceInformation("Setting issue {0} resolved flag to {1}", Issue.Id, Issue.ResolvedAt.HasValue);

						ApiTypes.UpdateIssue UpdateBody = new ApiTypes.UpdateIssue();
						UpdateBody.Resolved = Issue.ResolvedAt.HasValue;

						using(HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}", ServerUrl, Issue.Id), "PUT", UpdateBody))
						{
							int ResponseCode = (int)Response.StatusCode;
							if (!(ResponseCode >= 200 && ResponseCode <= 299))
							{
								throw new Exception("Unable to delete issue");
							}
						}

						Issue.bPostedResolved = Issue.ResolvedAt.HasValue;
						WriteState(StateFile, State);
					}
				}

				// Update watchers on any open builds
				foreach(Issue Issue in State.Issues)
				{
					while (Issue.PendingWatchers.Count > 0)
					{
						ApiTypes.Watcher Watcher = new ApiTypes.Watcher();
						Watcher.UserName = Issue.PendingWatchers.First();

						using (HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}/watchers", ServerUrl, Issue.Id), "POST", Watcher))
						{
							int ResponseCode = (int)Response.StatusCode;
							if (!(ResponseCode >= 200 && ResponseCode <= 299))
							{
								throw new Exception("Unable to add watcher");
							}
						}

						Issue.PendingWatchers.Remove(Watcher.UserName);
						Issue.Watchers.Add(Watcher.UserName);

						WriteState(StateFile, State);
					}
				}
			}

			// Remove any issues which have been resolved for 24 hours. We have to keep information about issues that have been fixed for some time; we may be updating the same job 
			// multiple times while other steps are running, and we don't want to keep opening new issues for it. Also, it can take time for changes to propagate between streams.
			DateTime RemoveIssueTime = DateTime.UtcNow - TimeSpan.FromHours(24.0);
			for(int Idx = 0; Idx < State.Issues.Count; Idx++)
			{
				Issue Issue = State.Issues[Idx];
				if(Issue.ResolvedAt.HasValue && Issue.ResolvedAt.Value < RemoveIssueTime)
				{
					State.Issues.RemoveAt(Idx--);
					WriteState(StateFile, State);
					continue;
				}
			}

			// TODO: VERIFY ISSUES ARE CLOSED
			return 0;
		}

		/// <summary>
		/// Sends an arbitrary HTTP request
		/// </summary>
		/// <param name="Url">Endpoint to send to</param>
		/// <param name="Method">The method to use for sending the request</param>
		/// <param name="BodyObject">Object to be serialized as json in the body</param>
		/// <returns>HTTP response</returns>
		HttpWebResponse SendHttpRequest(string Url, string Method, object BodyObject)
		{
			// Create the request
			HttpWebRequest Request = (HttpWebRequest)WebRequest.Create(Url);
			Request.ContentType = "application/json";
			Request.Method = Method;

			string BodyText = null;
			if (BodyObject != null)
			{
				BodyText = new JavaScriptSerializer().Serialize(BodyObject);
				byte[] BodyData = Encoding.UTF8.GetBytes(BodyText);
				using (Stream RequestStream = Request.GetRequestStream())
				{
					RequestStream.Write(BodyData, 0, BodyData.Length);
				}
			}

			// Read the response
			try
			{
				return (HttpWebResponse)Request.GetResponse();
			}
			catch(Exception Ex)
			{
				ExceptionUtils.AddContext(Ex, String.Format("Url: {0}", Url));
				ExceptionUtils.AddContext(Ex, String.Format("Method: {0}", Method));
				ExceptionUtils.AddContext(Ex, String.Format("Body: {0}", BodyText));
				throw;
			}
		}

		/// <summary>
		/// Parses an HTTP response object as JSON
		/// </summary>
		/// <typeparam name="T">The type of object to parse</typeparam>
		/// <param name="Response">The web response instance</param>
		/// <returns>Response object</returns>
		T ParseHttpResponse<T>(HttpWebResponse Response)
		{
			using (StreamReader ResponseReader = new StreamReader(Response.GetResponseStream(), Encoding.Default))
			{
				string ResponseContent = ResponseReader.ReadToEnd();
				return new JavaScriptSerializer().Deserialize<T>(ResponseContent);
			}
		}

		/// <summary>
		/// Adds diagnostics from a job step into the issue database
		/// </summary>
		/// <param name="Perforce">Perforce connection used to find possible causers</param>
		/// <param name="State">The current set of tracked issues</param>
		/// <param name="Build">The new build</param>
		/// <param name="PreviousChange">The last changelist that was built before this one</param>
		/// <param name="InputJob">Job containing the step to add</param>
		/// <param name="InputJobStep">The job step to add</param>
		void AddStep(PerforceConnection Perforce, PersistentState State, InputJob InputJob, InputJobStep InputJobStep)
		{
			// Create a lazily evaluated list of changes that are responsible for any errors
			Lazy<IReadOnlyList<ChangeInfo>> LazyChanges = new Lazy<IReadOnlyList<ChangeInfo>>(() => FindChanges(Perforce, State, InputJob));

			// Create issues for any diagnostics in this step
			List<Issue> InputIssues = new List<Issue>();
			foreach(Matcher Matcher in Matchers)
			{
				Matcher.Match(InputJob, InputJobStep, InputJobStep.Diagnostics, InputIssues);
			}

			// Merge the issues together
			List<Issue> NewIssues = new List<Issue>();
			foreach(Issue InputIssue in InputIssues)
			{
				Issue OutputIssue = MergeIntoExistingIssue(Perforce, State, InputJob, InputJobStep, InputIssue, LazyChanges);
				if(OutputIssue == null)
				{
					NewIssues.Add(InputIssue);
					State.Issues.Add(InputIssue);
					OutputIssue = InputIssue;
				}
				AddFailureToIssue(OutputIssue, InputJob, InputJobStep, InputIssue.Diagnostics[0].ErrorUrl, State);
			}

			// Update the watchers for any new issues
			foreach(Issue NewIssue in NewIssues)
			{
				IReadOnlyList<ChangeInfo> Changes = LazyChanges.Value;
				if (Changes != null)
				{
					// Find the pattern matcher for this issue
					Matcher Matcher = CategoryToMatcher[NewIssue.Category];

					// Update the causers
					List<ChangeInfo> Causers = Matcher.FindCausers(Perforce, NewIssue, Changes);
					foreach (ChangeInfo Causer in Causers)
					{
						NewIssue.SourceChanges.UnionWith(Causer.SourceChanges);
						NewIssue.PendingWatchers.Add(Causer.Record.User);
					}
				}
			}
		}

		/// <summary>
		/// Finds or adds an issue for a particular issue
		/// </summary>
		/// <param name="Perforce">Perforce connection used to find possible causers</param>
		/// <param name="State">The current set of tracked issues</param>
		/// <param name="Build">The new build</param>
		/// <param name="PreviousChange">The last changelist that was built before this one</param>
		/// <param name="InputJob">Job containing the step to add</param>
		/// <param name="InputJobStep">The job step to add</param>
		Issue MergeIntoExistingIssue(PerforceConnection Perforce, PersistentState State, InputJob InputJob, InputJobStep InputJobStep, Issue InputIssue, Lazy<IReadOnlyList<ChangeInfo>> LazyChanges)
		{
			// Find the pattern matcher for this fingerprint
			Matcher Matcher = CategoryToMatcher[InputIssue.Category];

			// Check if it can be added to an existing open issue
			foreach (Issue Issue in State.Issues)
			{
				// Check this issue already exists in the current stream
				Dictionary<string, IssueHistory> StepNameToHistory;
				if(!Issue.Streams.TryGetValue(InputJob.Stream, out StepNameToHistory))
				{
					continue;
				}

				// Check that this issue has not already been closed
				IssueHistory History;
				if (StepNameToHistory.TryGetValue(InputJobStep.Name, out History))
				{
					if(!History.CanAddFailedBuild(InputJob.Change))
					{
						continue;
					}
				}
				else
				{
					if(!StepNameToHistory.Values.Any(x => x.CanAddFailedBuild(InputJob.Change)))
					{
						continue;
					}
				}

				// Try to merge the fingerprint
				if(!Matcher.CanMerge(InputIssue, Issue))
				{
					continue;
				}

				// Add the new build
				Matcher.Merge(InputIssue, Issue);
				return Issue;
			}

			// Check if this issue can be merged with an issue built in another stream
			IReadOnlyList<ChangeInfo> Changes = LazyChanges.Value;
			if(Changes != null && Changes.Count > 0)
			{
				SortedSet<int> SourceChanges = new SortedSet<int>(Changes.SelectMany(x => x.SourceChanges));
				foreach (Issue Issue in State.Issues)
				{
					// Check if this issue does not already contain this stream, but contains one of the causing changes
					if (Issue.Streams.ContainsKey(InputJob.Stream))
					{
						continue;
					}
					if(!SourceChanges.Any(x => Issue.SourceChanges.Contains(x)))
					{
						continue;
					}
					if (!Matcher.CanMerge(InputIssue, Issue))
					{
						continue;
					}

					// Merge the issue
					Matcher.Merge(InputIssue, Issue);
					return Issue;
				}
			}

			// Check if it can be merged into an issue that's been created for this job. We only do this after exhausting all other options.
			foreach (Issue Issue in State.Issues)
			{
				if(Issue.InitialJobUrl == InputIssue.InitialJobUrl && Matcher.CanMergeInitialJob(InputIssue, Issue))
				{
					Matcher.Merge(InputIssue, Issue);
					return Issue;
				}
			}

			return null;
		}

		/// <summary>
		/// Creates a TrackedBuild instance for the given jobstep
		/// </summary>
		/// <param name="InputJob">The job to create a build for</param>
		/// <param name="InputJobStep">The step to create a build for</param>
		/// <param name="InputErrorUrl">The error Url</param>
		/// <returns>New build instance</returns>
		IssueBuild CreateBuildForJobStep(InputJob InputJob, InputJobStep InputJobStep, string InputErrorUrl)
		{
			return new IssueBuild(InputJob.Change, InputJob.Name, InputJob.Url, InputJobStep.Name, InputJobStep.Url, InputErrorUrl);
		}

		/// <summary>
		/// Adds a new build history for a stream
		/// </summary>
		/// <param name="Issue">The issue to add a build to</param>
		/// <param name="InputJob">The job containing the error</param>
		/// <param name="InputJobStep">The job step containing the error</param>
		/// <param name="InputErrorUrl">Url of the error</param>
		/// <param name="State">Current persistent state. Used to find previous build history.</param>
		void AddFailureToIssue(Issue Issue, InputJob InputJob, InputJobStep InputJobStep, string InputErrorUrl, PersistentState State)
		{
			// Find or add a step name to history mapping
			Dictionary<string, IssueHistory> StepNameToHistory;
			if(!Issue.Streams.TryGetValue(InputJob.Stream, out StepNameToHistory))
			{
				StepNameToHistory = new Dictionary<string, IssueHistory>();
				Issue.Streams.Add(InputJob.Stream, StepNameToHistory);
			}

			// Find or add a history for this step
			IssueHistory History;
			if(!StepNameToHistory.TryGetValue(InputJobStep.Name, out History))
			{
				History = new IssueHistory(State.FindBuildBefore(InputJob.Stream, InputJob.Change, InputJobStep.Name));
				StepNameToHistory.Add(InputJobStep.Name, History);
			}

			// Add the new build
			History.AddFailedBuild(CreateBuildForJobStep(InputJob, InputJobStep, InputErrorUrl));
		}

		/// <summary>
		/// Find all changes PLUS all robomerge source changes
		/// </summary>
		/// <param name="Perforce">The Perforce connection to use</param>
		/// <param name="State">State of </param>
		/// <param name="InputJob">The job that failed</param>
		/// <returns>Set of changelist numbers</returns>
		IReadOnlyList<ChangeInfo> FindChanges(PerforceConnection Perforce, PersistentState State, InputJob InputJob)
		{
			// List of changes since the last successful build in this stream
			IReadOnlyList<ChangeInfo> Changes = null;

			// Find the previous changelist that was built in this stream
			List<IssueBuild> StreamBuilds;
			if (State.Streams.TryGetValue(InputJob.Stream, out StreamBuilds))
			{
				// Find the last change submitted to this stream before it started failing
				int LastChange = -1;
				for (int Idx = 0; Idx < StreamBuilds.Count && StreamBuilds[Idx].Change < InputJob.Change; Idx++)
				{
					LastChange = StreamBuilds[Idx].Change;
				}

				// Allow adding to any open issue that contains changes merged from other branches
				if (LastChange != -1)
				{
					// Query for all the changes since then
					Changes = FindChanges(Perforce, InputJob.Stream, LastChange, InputJob.Change);
				}
			}
			return Changes;
		}

		/// <summary>
		/// Find all changes PLUS all robomerge source changes
		/// </summary>
		/// <param name="Perforce">The Perforce connection to use</param>
		/// <param name="Stream">The stream to query changes from</param>
		/// <param name="PrevChange">The first change in the range to query</param>
		/// <param name="NextChange">The last change in the range to query</param>
		/// <returns>Set of changelist numbers</returns>
		IReadOnlyList<ChangeInfo> FindChanges(PerforceConnection Perforce, string Stream, int PrevChange, int NextChange)
		{
			CachedChangeInfo CachedInfo = CachedChanges.FirstOrDefault(x => x.Stream == Stream && x.PrevChange == PrevChange && x.NextChange == NextChange);
			if(CachedInfo == null)
			{
				// Query for all the changes since then
				List<ChangesRecord> ChangeRecords = Perforce.Changes(ChangesOptions.LongOutput, null, -1, ChangeStatus.Submitted, null, String.Format("{0}/...@{1},{2}", Stream, PrevChange, NextChange)).Data.ToList();

				// Figure out all the original changelists that these were merged from, and see if any of those matches with an existing issue
				List<ChangeInfo> Changes = new List<ChangeInfo>();
				foreach (ChangesRecord ChangeRecord in ChangeRecords)
				{
					ChangeInfo Change = new ChangeInfo();
					Change.Record = ChangeRecord;
					Change.SourceChanges.Add(ChangeRecord.Number);
					Changes.Add(Change);

					Match SourceMatch = Regex.Match(ChangeRecord.Description, "^#ROBOMERGE-SOURCE: (.*)$", RegexOptions.Multiline);
					if (SourceMatch.Success)
					{
						string SourceText = SourceMatch.Groups[1].Value;
						foreach (Match ChangeMatch in Regex.Matches(SourceText, "CL\\s*(\\d+)"))
						{
							int SourceChange;
							if (int.TryParse(ChangeMatch.Groups[1].Value, out SourceChange))
							{
								Change.SourceChanges.Add(SourceChange);
							}
						}
					}
				}

				// Create the new cached info
				CachedInfo = new CachedChangeInfo() { Stream = Stream, PrevChange = PrevChange, NextChange = NextChange, Changes = Changes };
				CachedChanges.Add(CachedInfo);
			}
			return CachedInfo.Changes;
		}

		/// <summary>
		/// Gets the path to a temporary file used for ensuring that serialization from the state file is transactional
		/// </summary>
		/// <param name="StateFile">Path to the state file</param>
		/// <returns>Path to the temporary state transaction file</returns>
		static FileReference GetStateTransactionFile(FileReference StateFile)
		{
			return new FileReference(StateFile.FullName + ".transaction");
		}

		/// <summary>
		/// Completes an interrupted transaction to write the state file
		/// </summary>
		/// <param name="StateFile">Path to the state file</param>
		static void CompleteStateTransaction(FileReference StateFile)
		{
			if(!FileReference.Exists(StateFile))
			{
				FileReference StateTransactionFile = GetStateTransactionFile(StateFile);
				if(FileReference.Exists(StateTransactionFile))
				{
					Log.TraceInformation("Completing partial transaction through {0}", StateTransactionFile);
					FileReference.Move(StateTransactionFile, StateFile);
				}
			}
		}

		/// <summary>
		/// Writes the state to disk in a way that can be recovered if the operation is interrupted
		/// </summary>
		/// <param name="StateFile">The file to write to</param>
		/// <param name="State">The state object</param>
		static void WriteState(FileReference StateFile, PersistentState State)
		{
			Stopwatch Timer = Stopwatch.StartNew();

			// Write out the state to the transaction file
			FileReference StateTransactionFile = GetStateTransactionFile(StateFile);
			SerializeJson(StateTransactionFile, State);

			// Remove the original file, then move the transaction file into place
			FileReference.Delete(StateFile);
			FileReference.Move(StateTransactionFile, StateFile);

			Log.TraceInformation("Took {0}s to write state", Timer.Elapsed.TotalSeconds);
		}

		/// <summary>
		/// Serializes an object to a file as JSON
		/// </summary>
		/// <param name="Location">Location of the file</param>
		/// <param name="Object">Object to serialize</param>
		static void SerializeJson(FileReference Location, object Object)
		{
			using (MemoryStream Stream = new MemoryStream())
			{
				DataContractJsonSerializer InputFileDataSerializer = new DataContractJsonSerializer(Object.GetType());
				InputFileDataSerializer.WriteObject(Stream, Object);
				FileReference.WriteAllBytes(Location, Stream.ToArray());
			}
		}

		/// <summary>
		/// Deserializes a file at the given location
		/// </summary>
		/// <param name="Location">Location of the file</param>
		/// <returns>Deserialized object</returns>
		static T DeserializeJson<T>(FileReference Location) where T : class
		{
			using (FileStream Stream = File.Open(Location.FullName, FileMode.Open, FileAccess.Read, FileShare.Read))
			{
				DataContractJsonSerializer InputFileDataSerializer = new DataContractJsonSerializer(typeof(T));
				return (T)InputFileDataSerializer.ReadObject(Stream);
			}
		}
	}
}
