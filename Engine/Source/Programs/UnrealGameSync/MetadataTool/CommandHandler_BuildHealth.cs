// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
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

namespace MetadataTool
{
	class CommandHandler_BuildHealth : CommandHandler
	{
		// Register all the pattern matchers
		static readonly List<PatternMatcher> Matchers = new List<PatternMatcher>()
		{ 
			new CompilePatternMatcher(),
			new UndefinedSymbolPatternMatcher()
		};

		static readonly Dictionary<string, PatternMatcher> CategoryToMatcher = Matchers.ToDictionary(x => x.Category, x => x);

		class CachedChangeInfo
		{
			public string Stream;
			public int PrevChange;
			public int NextChange;
			public IReadOnlyList<ChangeInfo> Changes;
		}

		List<CachedChangeInfo> CachedChanges = new List<CachedChangeInfo>();

		/// <summary>
		/// Constructor
		/// </summary>
		public CommandHandler_BuildHealth()
			: base("BuildHealth")
		{
		}

		/// <summary>
		/// Main command entry point
		/// </summary>
		/// <param name="Arguments">The command line arguments</param>
		public override void Exec(CommandLineArguments Arguments)
		{
			// Parse the arguments
			bool bClean = Arguments.HasOption("-Clean");
			string PerforcePort = Arguments.GetStringOrDefault("-P4Port=", null);
			string PerforceUser = Arguments.GetStringOrDefault("-P4User=", null);
			FileReference InputFile = Arguments.GetFileReference("-InputFile=");
			FileReference StateFile = Arguments.GetFileReference("-StateFile=");
			string ServerUrl = Arguments.GetStringOrDefault("-Server=", null);
			bool bKeepHistory = Arguments.HasOption("-KeepHistory");
			Arguments.CheckAllArgumentsUsed();

			// Build a mapping from category to matching
			Dictionary<string, PatternMatcher> CategoryNameToMatcher = new Dictionary<string, PatternMatcher>();
			foreach (PatternMatcher Matcher in Matchers)
			{
				CategoryNameToMatcher[Matcher.Category] = Matcher;
			}

			// Parse the input file
			Log.TraceInformation("Reading build results from {0}", InputFile);
			InputData InputData = DeserializeJson<InputData>(InputFile);

			// Read the persistent data file
			TrackedState State;
			if (!bClean && FileReference.Exists(StateFile))
			{
				Log.TraceInformation("Reading persistent data from {0}", StateFile);
				State = DeserializeJson<TrackedState>(StateFile);
			}
			else
			{
				Log.TraceInformation("Creating new persistent data");
				State = new TrackedState();
			}

			// Create the Perforce connection
			PerforceConnection Perforce = new PerforceConnection(PerforcePort, PerforceUser, null);

			// Parse all the builds and add them to the persistent data
			List<InputJob> InputJobs = InputData.Jobs.OrderBy(x => x.Change).ThenBy(x => x.Stream).ToList();
			foreach(InputJob InputJob in InputJobs)
			{
				// Add a new build for each job step
				foreach(InputJobStep InputJobStep in InputJob.Steps)
				{
					TrackedBuild NewBuild = new TrackedBuild(InputJob.Change, InputJob.Name, InputJob.Url, InputJobStep.Name, InputJobStep.Url, null);
					State.AddBuild(InputJob.Stream, NewBuild);
				}

				// Add all the job steps
				List<InputJobStep> InputJobSteps = InputJob.Steps.OrderBy(x => x.Name).ToList();
				foreach (InputJobStep InputJobStep in InputJobSteps)
				{
					AddStep(Perforce, State, InputJob, InputJobStep);
				}
			}

			// Try to find the next successful build for each stream, so we can close it as part of updating the server
			for (int Idx = 0; Idx < State.Issues.Count; Idx++)
			{
				TrackedIssue Issue = State.Issues[Idx];
				foreach(string Stream in Issue.Streams.Keys)
				{
					Dictionary<string, TrackedIssueHistory> StepNameToHistory = Issue.Streams[Stream];
					foreach(string StepName in StepNameToHistory.Keys)
					{
						TrackedIssueHistory IssueHistory = StepNameToHistory[StepName];
						if(IssueHistory.FailedBuilds.Count > 0 && IssueHistory.NextSuccessfulBuild == null)
						{
							// Find the successful build after this change
							TrackedBuild LastFailedBuild = IssueHistory.FailedBuilds[IssueHistory.FailedBuilds.Count - 1];
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
				foreach (List<TrackedBuild> Builds in State.Streams.Values)
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
				foreach (List<TrackedBuild> Builds in State.Streams.Values)
				{
					Builds.RemoveAll(x => x.Change <= DeleteChangeNumber);
				}
			}

			// Save the persistent data
			Log.TraceInformation("Writing persistent data to {0}", StateFile);
			DirectoryReference.CreateDirectory(StateFile.Directory);
			SerializeJson(StateFile, State);

			// Synchronize with the server
			if (ServerUrl != null)
			{
				// Post any issue updates
				foreach(TrackedIssue Issue in State.Issues)
				{
					PatternMatcher Matcher;
					if(!CategoryNameToMatcher.TryGetValue(Issue.Fingerprint.Category, out Matcher))
					{
						continue;
					}

					if(Issue.Id == -1)
					{
						Log.TraceInformation("Adding issue: {0}", Issue.Fingerprint);

						if(Issue.PendingWatchers.Count == 0)
						{
							Log.TraceWarning("(No possible causers)");
						}

						CommandTypes.AddIssue IssueBody = new CommandTypes.AddIssue();
						IssueBody.Project = "Fortnite";
						IssueBody.Summary = Issue.Fingerprint.Summary;
						IssueBody.Details = String.Join("\n", Issue.Fingerprint.Details);

						if(Issue.PendingWatchers.Count == 1)
						{
							IssueBody.Owner = Issue.PendingWatchers.First();
						}

						using(HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}", ServerUrl, Issue.Id), "POST", IssueBody))
						{
							int ResponseCode = (int)Response.StatusCode;
							if (!(ResponseCode >= 200 && ResponseCode <= 299))
							{
								throw new Exception("Unable to add issue");
							}
							Issue.Id = ParseHttpResponse<CommandTypes.AddIssueResponse>(Response).Id;
						}

						Issue.PostedSummary = Issue.Fingerprint.Summary;
						SerializeJson(StateFile, State);
					}
					else if(Issue.PostedSummary == null || !String.Equals(Issue.PostedSummary, Issue.Fingerprint.Summary, StringComparison.Ordinal))
					{
						Log.TraceInformation("Updating issue {0}", Issue.Id);

						CommandTypes.UpdateIssue IssueBody = new CommandTypes.UpdateIssue();
						IssueBody.Summary = Issue.Fingerprint.Summary;
						IssueBody.Details = String.Join("\n", Issue.Fingerprint.Details);

						using (HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}", ServerUrl, Issue.Id), "PUT", IssueBody))
						{
							int ResponseCode = (int)Response.StatusCode;
							if (!(ResponseCode >= 200 && ResponseCode <= 299))
							{
								throw new Exception("Unable to add issue");
							}
						}

						Issue.PostedSummary = Issue.Fingerprint.Summary;
						SerializeJson(StateFile, State);
					}
				}

				// Update the summary for any issues that are still open
				foreach (TrackedIssue Issue in State.Issues)
				{
					if (Issue.Id == -1)
					{
						Log.TraceInformation("Adding issue: {0}", Issue.Fingerprint);
					}
				}

				// Add any new builds associated with issues
				foreach (TrackedIssue Issue in State.Issues)
				{
					foreach(KeyValuePair<string, Dictionary<string, TrackedIssueHistory>> StreamPair in Issue.Streams)
					{
						foreach(TrackedIssueHistory StreamHistory in StreamPair.Value.Values)
						{
							foreach(TrackedBuild Build in StreamHistory.Builds)
							{
								if(!Build.bPostedToServer)
								{
									Log.TraceInformation("Adding {0} to issue {1}", Build.JobStepUrl, Issue.Id);

									CommandTypes.AddBuild AddBuild = new CommandTypes.AddBuild();
									AddBuild.Stream = StreamPair.Key;
									AddBuild.Change = Build.Change;
									AddBuild.JobName = Build.JobName;
									AddBuild.JobUrl = Build.JobUrl;
									AddBuild.JobStepName = Build.JobStepName;
									AddBuild.JobStepUrl = Build.JobStepUrl;
									AddBuild.ErrorUrl = Build.ErrorUrl;
									AddBuild.Outcome = (Build == StreamHistory.PrevSuccessfulBuild || Build == StreamHistory.NextSuccessfulBuild)? CommandTypes.Outcome.Success : CommandTypes.Outcome.Error;

									using (HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}/builds", ServerUrl, Issue.Id), "POST", AddBuild))
									{
										int ResponseCode = (int)Response.StatusCode;
										if (!(ResponseCode >= 200 && ResponseCode <= 299))
										{
											throw new Exception("Unable to add build");
										}
									}

									Build.bPostedToServer = true;
									SerializeJson(StateFile, State);
								}
							}
						}
					}
				}

				// Close any issues which are complete
				for (int Idx = 0; Idx < State.Issues.Count; Idx++)
				{
					TrackedIssue Issue = State.Issues[Idx];
					if (Issue.CanClose())
					{
						Log.TraceInformation("Marking issue {0} as resolved", Issue.Id);

						CommandTypes.UpdateIssue UpdateBody = new CommandTypes.UpdateIssue();
						UpdateBody.Resolved = true;

						using(HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}", ServerUrl, Issue.Id), "PUT", UpdateBody))
						{
							int ResponseCode = (int)Response.StatusCode;
							if (!(ResponseCode >= 200 && ResponseCode <= 299))
							{
								throw new Exception("Unable to delete issue");
							}
						}

						State.Issues.RemoveAt(Idx--);
						SerializeJson(StateFile, State);
					}
				}

				// Update watchers on any open builds
				foreach(TrackedIssue Issue in State.Issues)
				{
					while (Issue.PendingWatchers.Count > 0)
					{
						CommandTypes.Watcher Watcher = new CommandTypes.Watcher();
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

						SerializeJson(StateFile, State);
					}
				}
			}

			// TODO: VERIFY ISSUES ARE CLOSED
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
			if (BodyObject != null)
			{
				string BodyText = new JavaScriptSerializer().Serialize(BodyObject);
				byte[] BodyData = Encoding.UTF8.GetBytes(BodyText);
				using (Stream RequestStream = Request.GetRequestStream())
				{
					RequestStream.Write(BodyData, 0, BodyData.Length);
				}
			}

			// Read the response
			return (HttpWebResponse)Request.GetResponse();
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
		/// Print out help for this command
		/// </summary>
		public override void Help()
		{
			Log.TraceInformation("  -InputFile=<file>  Path to an input file containing completed jobs");
			Log.TraceInformation("  -StateFile=<file>  Path to a file containing persistent state");
			Log.TraceInformation("  -P4Port            Server and port for P4 commands");
			Log.TraceInformation("  -P4User            Username to use for P4 commands");
			Log.TraceInformation("  -Clean             Removes the existing state file and creates a new one");
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
		void AddStep(PerforceConnection Perforce, TrackedState State, InputJob InputJob, InputJobStep InputJobStep)
		{
			// Create all the fingerprints for failures in this step
			List<TrackedIssueFingerprint> Fingerprints = new List<TrackedIssueFingerprint>();
			if(InputJobStep.Diagnostics != null)
			{
				List<InputDiagnostic> Diagnostics = new List<InputDiagnostic>(InputJobStep.Diagnostics); 
				foreach(PatternMatcher PatternMatcher in Matchers)
				{
					PatternMatcher.Match(InputJob, InputJobStep, Diagnostics, Fingerprints);
				}
			}

			// Add all the fingerprints to issues
			foreach(TrackedIssueFingerprint Fingerprint in Fingerprints)
			{
				TrackedIssue Issue = FindOrAddIssueForFingerprint(Perforce, State, InputJob, InputJobStep, Fingerprint);
				AddFailureToIssue(Issue, Fingerprint, InputJob, InputJobStep, State);
			}
		}

		/// <summary>
		/// Finds or adds an issue for a particular fingerprint
		/// </summary>
		/// <param name="Perforce">Perforce connection used to find possible causers</param>
		/// <param name="State">The current set of tracked issues</param>
		/// <param name="Build">The new build</param>
		/// <param name="PreviousChange">The last changelist that was built before this one</param>
		/// <param name="InputJob">Job containing the step to add</param>
		/// <param name="InputJobStep">The job step to add</param>
		TrackedIssue FindOrAddIssueForFingerprint(PerforceConnection Perforce, TrackedState State, InputJob InputJob, InputJobStep InputJobStep, TrackedIssueFingerprint Fingerprint)
		{
			// Find the pattern matcher for this fingerprint
			PatternMatcher Matcher = CategoryToMatcher[Fingerprint.Category];

			// Check if it can be added to an existing open issue
			foreach (TrackedIssue Issue in State.Issues)
			{
				// Check this issue already exists in the current stream
				Dictionary<string, TrackedIssueHistory> StepNameToHistory;
				if(!Issue.Streams.TryGetValue(InputJob.Stream, out StepNameToHistory))
				{
					continue;
				}

				// Check that this issue has not already been closed
				TrackedIssueHistory History;
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
				if(!Matcher.CanMerge(Fingerprint, Issue.Fingerprint))
				{
					continue;
				}

				// Add the new build
				Matcher.Merge(Fingerprint, Issue.Fingerprint);
				return Issue;
			}

			// List of changes since the last successful build in this stream
			IReadOnlyList<ChangeInfo> Changes = null;

			// Find the previous changelist that was built in this stream
			List<TrackedBuild> StreamBuilds;
			if(State.Streams.TryGetValue(InputJob.Stream, out StreamBuilds))
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
					if (Changes.Count > 0)
					{
						SortedSet<int> SourceChanges = new SortedSet<int>(Changes.SelectMany(x => x.SourceChanges));
						foreach (TrackedIssue Issue in State.Issues)
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
							if (!Matcher.CanMerge(Fingerprint, Issue.Fingerprint))
							{
								continue;
							}

							// Merge the issue
							Matcher.Merge(Fingerprint, Issue.Fingerprint);
							return Issue;
						}
					}
				}
			}

			// Create new issues for everything else in this stream
			TrackedIssue NewIssue = new TrackedIssue(Fingerprint, InputJob.Change);
			if (Changes != null)
			{
				List<ChangeInfo> Causers = Matcher.FindCausers(Perforce, Fingerprint, Changes);
				foreach(ChangeInfo Causer in Causers)
				{
					NewIssue.SourceChanges.UnionWith(Causer.SourceChanges);
					NewIssue.PendingWatchers.Add(Causer.Record.User);
				}
			}
			State.Issues.Add(NewIssue);
			return NewIssue;
		}

		/// <summary>
		/// Creates a TrackedBuild instance for the given jobstep
		/// </summary>
		/// <param name="InputJob">The job to create a build for</param>
		/// <param name="InputJobStep">The step to create a build for</param>
		/// <returns>New build instance</returns>
		TrackedBuild CreateBuildForJobStep(InputJob InputJob, InputJobStep InputJobStep, TrackedIssueFingerprint Fingerprint)
		{
			return new TrackedBuild(InputJob.Change, InputJob.Name, InputJob.Url, InputJobStep.Name, InputJobStep.Url, Fingerprint.ErrorUrl);
		}

		/// <summary>
		/// Adds a new build history for a stream
		/// </summary>
		/// <param name="Issue">The issue to add a build to</param>
		/// <param name="Fingerprint">Fingerprint of the new issue being added</param>
		/// <param name="Stream">The new stream containing the issue</param>
		/// <param name="Build">The first failing build</param>
		/// <param name="State">Current persistent state. Used to find previous build history.</param>
		void AddFailureToIssue(TrackedIssue Issue, TrackedIssueFingerprint Fingerprint, InputJob InputJob, InputJobStep InputJobStep, TrackedState State)
		{
			// Find or add a step name to history mapping
			Dictionary<string, TrackedIssueHistory> StepNameToHistory;
			if(!Issue.Streams.TryGetValue(InputJob.Stream, out StepNameToHistory))
			{
				StepNameToHistory = new Dictionary<string, TrackedIssueHistory>();
				Issue.Streams.Add(InputJob.Stream, StepNameToHistory);
			}

			// Find or add a history for this step
			TrackedIssueHistory History;
			if(!StepNameToHistory.TryGetValue(InputJobStep.Name, out History))
			{
				History = new TrackedIssueHistory(State.FindBuildBefore(InputJob.Stream, InputJob.Change, InputJobStep.Name));
				StepNameToHistory.Add(InputJobStep.Name, History);
			}

			// Add the new build
			History.AddFailedBuild(CreateBuildForJobStep(InputJob, InputJobStep, Fingerprint));
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

				string Text = Encoding.UTF8.GetString(Stream.ToArray());
				FileReference.WriteAllText(Location, Json.Format(Text));
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
