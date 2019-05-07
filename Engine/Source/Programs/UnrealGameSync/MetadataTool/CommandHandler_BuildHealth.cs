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
		/// <summary>
		/// List of extensions for source files
		/// </summary>
		static string[] SourceFileExtensions =
		{
			".cpp",
			".h",
			".cc",
			".hh",
			".m",
			".mm",
			".rc",
			".inl",
			".inc"
		};

		/// <summary>
		/// List of extensions for content
		/// </summary>
		static string[] ContentFileExtensions =
		{
			".umap",
			".uasset"
		};

		/// <summary>
		/// Stores information about a change and its merge history
		/// </summary>
		class ChangeInfo
		{
			public ChangesRecord Record;
			public List<int> SourceChanges = new List<int>();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public CommandHandler_BuildHealth()
			: base("TickBuildHealth")
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
				TrackedBuild NewBuild = new TrackedBuild(InputJob.Name, InputJob.Change, InputJob.Url);

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
					if(Issue.Id == -1)
					{
						Log.TraceInformation("Adding issue: {0}", Issue.Fingerprint);

						if(Issue.PendingWatchers.Count == 0)
						{
							Log.TraceWarning("(No possible causers)");
						}

						CommandTypes.AddIssue IssueBody = new CommandTypes.AddIssue();
						IssueBody.Project = "Fortnite";
						IssueBody.Summary = Issue.GetSummary();

						using(HttpWebResponse Response = SendHttpRequest(String.Format("{0}/api/issues/{1}", ServerUrl, Issue.Id), "POST", IssueBody))
						{
							int ResponseCode = (int)Response.StatusCode;
							if (!(ResponseCode >= 200 && ResponseCode <= 299))
							{
								throw new Exception("Unable to add issue");
							}
							Issue.Id = ParseHttpResponse<CommandTypes.AddIssueResponse>(Response).Id;
						}

						SerializeJson(StateFile, State);
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
								Log.TraceInformation("Adding {0} to issue {1}", Build.Url, Issue.Id);

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
					while(Issue.PendingWatchers.Count > 0)
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

			// TODO: ASSIGN TO APPROPRIATE CAUSERS
			// TODO: CONFIRM ISSUES ARE CLOSED
		}

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
				return new TrackedBuild(Build.Name, Build.Change, Build.Url);
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
			CreateFingerprints(InputJob, InputJobStep, Fingerprints);

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
					if (Issue.Streams.TryGetValue(InputJob.Stream, out History) && History.CanAddFailedBuild(InputJob.Change) && Issue.Fingerprint.CanMerge(Fingerprint))
					{
						Issue.Fingerprint.Merge(Fingerprint);
						History.AddFailedBuild(CreateBuildForJobStep(InputJob, InputJobStep));
						Fingerprints.RemoveAt(Idx--);
						break;
					}
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
						for (int Idx = 0; Idx < Fingerprints.Count; Idx++)
						{
							TrackedIssueFingerprint Fingerprint = Fingerprints[Idx];
							foreach (TrackedIssue Issue in State.Issues)
							{
								// Check if this issue does not already contain this stream, but contains one of the causing changes
								if (!Issue.Streams.ContainsKey(InputJob.Stream) && Changes.SelectMany(x => x.SourceChanges).Any(x => Issue.SourceChanges.Contains(x)) && Issue.Fingerprint.CanMerge(Fingerprint))
								{
									Issue.Fingerprint.Merge(Fingerprint);
									AddFailureToIssue(Issue, InputJob, InputJobStep, State);
									Fingerprints.RemoveAt(Idx--);
									break;
								}
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
				TrackedIssue Issue = NewIssues.FirstOrDefault(NewIssue => NewIssue.Fingerprint.TryMerge(Fingerprint));
				if(Issue == null)
				{
					Issue = new TrackedIssue(Fingerprint, InputJob.Change);
					if(Changes != null)
					{
						List<ChangeInfo> Causers = FilterCausers(Perforce, Changes);
						foreach(ChangeInfo Causer in Causers)
						{
							Issue.SourceChanges.UnionWith(Causer.SourceChanges);
							Issue.PendingWatchers.Add(Causer.Record.User);
						}
					}
					NewIssues.Add(Issue);
				}
				AddFailureToIssue(Issue, InputJob, InputJobStep, State);
			}
			State.Issues.AddRange(NewIssues);
		}

		/// <summary>
		/// Filters the list of causers for a possible issue
		/// </summary>
		/// <param name="Perforce">The Perforce connection</param>
		/// <param name="Changes">List of changes</param>
		/// <param name="Causers">Receives a possible list of causers</param>
		List<ChangeInfo> FilterCausers(PerforceConnection Perforce, List<ChangeInfo> Changes)
		{
			List<ChangeInfo> LikelyCausers = new List<ChangeInfo>();
			List<ChangeInfo> PossibleCausers = new List<ChangeInfo>();
			foreach(ChangeInfo Change in Changes)
			{
				DescribeRecord Description = Perforce.Describe(Change.Record.Number).Data;
				PossibleCausers.Add(Change);
			}

			if(LikelyCausers.Count > 0)
			{
				return LikelyCausers;
			}
			else
			{
				return PossibleCausers;
			}
		}

		/// <summary>
		/// Creates a TrackedBuild instance for the given jobstep
		/// </summary>
		/// <param name="InputJob">The job to create a build for</param>
		/// <param name="InputJobStep">The step to create a build for</param>
		/// <returns>New build instance</returns>
		TrackedBuild CreateBuildForJobStep(InputJob InputJob, InputJobStep InputJobStep)
		{
			TrackedBuild FailedBuild = new TrackedBuild(InputJob.Name, InputJob.Change, InputJob.Url);
			FailedBuild.StepNames.Add(InputJobStep.Name);
			return FailedBuild;
		}

		/// <summary>
		/// Adds a new build history for a stream
		/// </summary>
		/// <param name="Issue">The issue to add a build to</param>
		/// <param name="Stream">The new stream containing the issue</param>
		/// <param name="Build">The first failing build</param>
		/// <param name="State">Current persistent state. Used to find previous build history.</param>
		void AddFailureToIssue(TrackedIssue Issue, InputJob InputJob, InputJobStep InputJobStep, TrackedState State)
		{
			TrackedIssueHistory History;
			if(Issue.Streams.TryGetValue(InputJob.Stream, out History))
			{
				History.AddFailedBuild(CreateBuildForJobStep(InputJob, InputJobStep));
			}
			else
			{
				TrackedBuild LastSuccessfulBuild = DuplicateSentinelBuild(State.FindBuildBefore(InputJob.Stream, InputJob.Change, new HashSet<string>{ InputJobStep.Name }));
				History = new TrackedIssueHistory(LastSuccessfulBuild, CreateBuildForJobStep(InputJob, InputJobStep));
				Issue.Streams.Add(InputJob.Stream, History);
			}
		}

		/// <summary>
		/// Parse a list of fingerprints from the build output
		/// </summary>
		/// <param name="Job">The job to generate fingerprints for</param>
		/// <param name="JobStep">The jobstep to create fingerprints for</param>
		/// <param name="Fingerprints">List which receives the created fingerprints</param>
		void CreateFingerprints(InputJob Job, InputJobStep JobStep, List<TrackedIssueFingerprint> Fingerprints)
		{
			if(JobStep.Diagnostics != null)
			{
				foreach(InputDiagnostic Diagnostic in JobStep.Diagnostics)
				{
					// List of files that are of unknown type
					HashSet<string> FileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

					// Find any files in compiler output format
					HashSet<string> SourceFileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
					foreach (Match FileMatch in Regex.Matches(Diagnostic.Message, @"^\s*(?:In file included from\s*)?((?:[A-Za-z]:)?[^\s(:]+)[\(:]\d", RegexOptions.Multiline))
					{
						if (FileMatch.Success)
						{
							string FileName = GetNormalizedFileName(FileMatch.Groups[1].Value, JobStep.BaseDirectory);
							if(SourceFileExtensions.Any(x => FileName.EndsWith(x, StringComparison.OrdinalIgnoreCase)))
							{
								SourceFileNames.Add(FileName);
							}
							else
							{
								FileNames.Add(FileName);
							}
						}
					}

					// Find any content files
					HashSet<string> ContentFileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
					foreach (Match FileMatch in Regex.Matches(Diagnostic.Message, @"(?<=\W)(/(?:Game|Engine)/[^ :,]+[A-Za-z])(?=\W)"))
					{
						if (FileMatch.Success)
						{
							string FileName = FileMatch.Groups[1].Value;
							ContentFileNames.Add(FileName);
						}
					}

					// If we found any source files, create a diagnostic category for them
					if (SourceFileNames.Count > 0)
					{
						TrackedIssueFingerprint Fingerprint = new TrackedIssueFingerprint(TrackedIssueFingerprintCategory.Code, Job.Change);
						Fingerprint.FileNames.UnionWith(SourceFileNames);
						Fingerprints.Add(Fingerprint);
					}

					// If we found any content files, create a diagnostic category for them
					if (ContentFileNames.Count > 0)
					{
						TrackedIssueFingerprint Fingerprint = new TrackedIssueFingerprint(TrackedIssueFingerprintCategory.Content, Job.Change);
						Fingerprint.FileNames.UnionWith(ContentFileNames);
						Fingerprints.Add(Fingerprint);
					}

					// If there are any other files, create an unknown issue for it
					if (FileNames.Count > 0)
					{
						TrackedIssueFingerprint Fingerprint = new TrackedIssueFingerprint(TrackedIssueFingerprintCategory.Unknown, Job.Change);
						Fingerprint.FileNames.UnionWith(FileNames);
						Fingerprints.Add(Fingerprint);
					}

					// If we didn't find any files, add a distinct issue
					if(SourceFileNames.Count == 0 && ContentFileNames.Count == 0 && FileNames.Count == 0)
					{
						TrackedIssueFingerprint Fingerprint = new TrackedIssueFingerprint(TrackedIssueFingerprintCategory.Unknown, Job.Change);
						Fingerprint.Messages.Add(Diagnostic.Message);
						Fingerprints.Add(Fingerprint);
					}
				}
			}
		}

		/// <summary>
		/// Normalizes a filename to a path within the workspace
		/// </summary>
		/// <param name="FileName">Filename to normalize</param>
		/// <param name="BaseDirectory">Base directory containing the workspace</param>
		/// <returns>Normalized filename</returns>
		protected string GetNormalizedFileName(string FileName, string BaseDirectory)
		{
			string NormalizedFileName = FileName.Replace('\\', '/');
			if (!String.IsNullOrEmpty(BaseDirectory))
			{
				// Normalize the expected base directory for errors in this build, and attempt to strip it from the file name
				string NormalizedBaseDirectory = BaseDirectory;
				if (NormalizedBaseDirectory != null && NormalizedBaseDirectory.Length > 0)
				{
					NormalizedBaseDirectory = NormalizedBaseDirectory.Replace('\\', '/').TrimEnd('/') + "/";
				}
				if (NormalizedFileName.StartsWith(NormalizedBaseDirectory, StringComparison.OrdinalIgnoreCase))
				{
					NormalizedFileName = NormalizedFileName.Substring(NormalizedBaseDirectory.Length);
				}
			}
			else
			{
				// Try to match anything under a 'Sync' folder.
				Match FallbackRegex = Regex.Match(NormalizedFileName, "/Sync/(.*)");
				if (FallbackRegex.Success)
				{
					NormalizedFileName = FallbackRegex.Groups[1].Value;
				}
			}
			return NormalizedFileName;
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
