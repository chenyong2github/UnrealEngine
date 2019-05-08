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
				// Get the list of tracked builds for this stream
				List<TrackedBuild> StreamBuilds;
				if (!State.Streams.TryGetValue(InputJob.Stream, out StreamBuilds))
				{
					StreamBuilds = new List<TrackedBuild>();
					State.Streams.Add(InputJob.Stream, StreamBuilds);
				}

				// Create a new tracked build for this input build. Even if we discard this, we can use it for searching the existing build list for the place to insert.
				TrackedBuild NewBuild = new TrackedBuild(InputJob.Name, InputJob.Change, InputJob.Url, InputJob.Url);

				// Add this build to the tracked state data
				int StreamBuildIndex = StreamBuilds.BinarySearch(NewBuild);
				if (StreamBuildIndex < 0)
				{
					StreamBuildIndex = ~StreamBuildIndex;
					StreamBuilds.Insert(StreamBuildIndex, NewBuild);
				}

				// Get the build for this changelist
				StreamBuilds[StreamBuildIndex].StepNames.UnionWith(InputJob.Steps.Select(x => x.Name));

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
					TrackedIssueHistory IssueHistory = Issue.Streams[Stream];
					if(IssueHistory.FailedBuilds.Count > 0 && IssueHistory.NextSuccessfulBuild == null)
					{
						// Figure out all the step names that have to be 
						HashSet<string> RequiredStepNames = new HashSet<string>(IssueHistory.FailedBuilds.SelectMany(x => x.StepNames));

						// Find the successful build after this change
						TrackedBuild LastFailedBuild = IssueHistory.FailedBuilds[IssueHistory.FailedBuilds.Count - 1];
						IssueHistory.NextSuccessfulBuild = DuplicateSentinelBuild(State.FindBuildAfter(Stream, LastFailedBuild.Change, LastFailedBuild.StepNames));
					}
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
					foreach(KeyValuePair<string, TrackedIssueHistory> StreamPair in Issue.Streams)
					{
						TrackedIssueHistory StreamHistory = StreamPair.Value;
						foreach(TrackedBuild Build in StreamHistory.Builds)
						{
							if(!Build.bPostedToServer)
							{
								Log.TraceInformation("Adding {0} to issue {1}", Build.UniqueId, Issue.Id);

								CommandTypes.AddBuild AddBuild = new CommandTypes.AddBuild();
								AddBuild.Change = Build.Change;
								AddBuild.Stream = StreamPair.Key;
								AddBuild.Name = Build.Name;
								AddBuild.Outcome = (Build == StreamHistory.PrevSuccessfulBuild || Build == StreamHistory.NextSuccessfulBuild)? CommandTypes.Outcome.Success : CommandTypes.Outcome.Error;
								AddBuild.Url = Build.Url;

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
		/// Finds the matcher for a particular category
		/// </summary>
		/// <param name="Category">The category to find</param>
		/// <returns>Pattern matcher for this category</returns>
		bool TryMergeFingerprint(TrackedIssueFingerprint Source, TrackedIssueFingerprint Target)
		{
			if(Source.Category != Target.Category)
			{
				return false;
			}

			PatternMatcher Matcher = Matchers.First(x => x.Category == Source.Category);
			if(!Matcher.TryMerge(Source, Target))
			{
				return false;
			}

			return true;
		}

		/// <summary>
		/// Duplicates a build for use as a sentinel value
		/// </summary>
		/// <param name="Build">The build to duplicate</param>
		/// <returns>The new build instance</returns>
		static TrackedBuild DuplicateSentinelBuild(TrackedBuild Build)
		{
			if(Build == null)
			{
				return null;
			}
			else
			{
				return new TrackedBuild(Build.Name, Build.Change, Build.UniqueId, Build.Url);
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

			// Early out if there are no remaining fingerprints to add
			if (Fingerprints.Count == 0)
			{
				return;
			}

			// Merge any fingerprints with open issues
			for (int Idx = 0; Idx < Fingerprints.Count; Idx++)
			{
				TrackedIssueFingerprint Fingerprint = Fingerprints[Idx];
				foreach (TrackedIssue Issue in State.Issues)
				{
					// Check that this issue is present in the current stream, and that this build is not either side of a successful build
					TrackedIssueHistory History;
					if (!Issue.Streams.TryGetValue(InputJob.Stream, out History))
					{
						continue;
					}
					if(!History.CanAddFailedBuild(InputJob.Change))
					{
						continue;
					}
					if(!TryMergeFingerprint(Fingerprint, Issue.Fingerprint))
					{
						continue;
					}

					// Merge the issue
					History.AddFailedBuild(CreateBuildForJobStep(InputJob, InputJobStep, Fingerprint));
					Fingerprints.RemoveAt(Idx--);
					break;
				}
			}

			// Early out if there are no remaining issues
			if (Fingerprints.Count == 0)
			{
				return;
			}

			// List of changes since the last successful build in this stream
			List<ChangeInfo> Changes = null;

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
						for (int Idx = 0; Idx < Fingerprints.Count; Idx++)
						{
							TrackedIssueFingerprint Fingerprint = Fingerprints[Idx];
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
								if (!TryMergeFingerprint(Fingerprint, Issue.Fingerprint))
								{
									continue;
								}

								// Merge the issue
								AddFailureToIssue(Issue, Fingerprint, InputJob, InputJobStep, State);
								Fingerprints.RemoveAt(Idx--);
								break;
							}
						}
					}
				}
			}

			// Early out if there are no remaining issues
			if (Fingerprints.Count == 0)
			{
				return;
			}

			// Create new issues for everything else in this stream
			List<TrackedIssue> NewIssues = new List<TrackedIssue>();
			foreach(TrackedIssueFingerprint Fingerprint in Fingerprints)
			{
				TrackedIssue Issue = NewIssues.FirstOrDefault(NewIssue => TryMergeFingerprint(Fingerprint, NewIssue.Fingerprint));
				if(Issue == null)
				{
					Issue = new TrackedIssue(Fingerprint, InputJob.Change);
					if(Changes != null)
					{
						PatternMatcher Matcher = Matchers.First(x => x.Category == Fingerprint.Category);
						List<ChangeInfo> Causers = Matcher.FindCausers(Perforce, Fingerprint, Changes);
						foreach(ChangeInfo Causer in Causers)
						{
							Issue.SourceChanges.UnionWith(Causer.SourceChanges);
							Issue.PendingWatchers.Add(Causer.Record.User);
						}
					}
					NewIssues.Add(Issue);
				}
				AddFailureToIssue(Issue, Fingerprint, InputJob, InputJobStep, State);
			}
			State.Issues.AddRange(NewIssues);
		}

		/// <summary>
		/// Creates a TrackedBuild instance for the given jobstep
		/// </summary>
		/// <param name="InputJob">The job to create a build for</param>
		/// <param name="InputJobStep">The step to create a build for</param>
		/// <returns>New build instance</returns>
		TrackedBuild CreateBuildForJobStep(InputJob InputJob, InputJobStep InputJobStep, TrackedIssueFingerprint Fingerprint)
		{
			TrackedBuild FailedBuild = new TrackedBuild(InputJob.Name, InputJob.Change, InputJob.Url, Fingerprint.Url);
			FailedBuild.StepNames.Add(InputJobStep.Name);
			return FailedBuild;
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
			TrackedIssueHistory History;
			if(Issue.Streams.TryGetValue(InputJob.Stream, out History))
			{
				History.AddFailedBuild(CreateBuildForJobStep(InputJob, InputJobStep, Fingerprint));
			}
			else
			{
				TrackedBuild LastSuccessfulBuild = DuplicateSentinelBuild(State.FindBuildBefore(InputJob.Stream, InputJob.Change, new HashSet<string>{ InputJobStep.Name }));
				History = new TrackedIssueHistory(LastSuccessfulBuild, CreateBuildForJobStep(InputJob, InputJobStep, Fingerprint));
				Issue.Streams.Add(InputJob.Stream, History);
			}
		}

		/// <summary>
		/// Find all changes PLUS all robomerge source changes
		/// </summary>
		/// <param name="Perforce">The Perforce connection to use</param>
		/// <param name="Stream">The stream to query changes from</param>
		/// <param name="PrevChange">The first change in the range to query</param>
		/// <param name="NextChange">The last change in the range to query</param>
		/// <returns>Set of changelist numbers</returns>
		List<ChangeInfo> FindChanges(PerforceConnection Perforce, string Stream, int PrevChange, int NextChange)
		{
			// Query for all the changes since then
			List<ChangesRecord> ChangeRecords = Perforce.Changes(ChangesOptions.LongOutput, null, -1, ChangeStatus.Submitted, null, String.Format("{0}/...@{1},{2}", Stream, PrevChange, NextChange)).Data.ToList();

			// Figure out all the original changelists that these were merged from, and see if any of those matches with an existing issue
			List<ChangeInfo> Changes = new List<ChangeInfo>();
			foreach(ChangesRecord ChangeRecord in ChangeRecords)
			{
				ChangeInfo Change = new ChangeInfo();
				Change.Record = ChangeRecord;
				Change.SourceChanges.Add(ChangeRecord.Number);
				Changes.Add(Change);

				Match SourceMatch = Regex.Match(ChangeRecord.Description, "^#ROBOMERGE-SOURCE: (.*)$", RegexOptions.Multiline);
				if(SourceMatch.Success)
				{
					string SourceText = SourceMatch.Groups[1].Value;
					foreach(Match ChangeMatch in Regex.Matches(SourceText, "CL\\s*(\\d+)"))
					{
						int SourceChange;
						if(int.TryParse(ChangeMatch.Groups[1].Value, out SourceChange))
						{
							Change.SourceChanges.Add(SourceChange);
						}
					}
				}
			}
			return Changes;
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
