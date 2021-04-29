// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Stores settings for communicating with a Perforce server.
	/// </summary>
	public class PerforceConnection
	{
		#region Plumbing

		/// <summary>
		/// The current server and port
		/// </summary>
		public string? ServerAndPort
		{
			get;
			set;
		}

		/// <summary>
		/// The current user name
		/// </summary>
		public string? UserName
		{
			get;
			set;
		}

		/// <summary>
		/// The current client name
		/// </summary>
		public string? ClientName
		{
			get;
			set;
		}

		/// <summary>
		/// Name of this application, reported to server through -zprog arguments
		/// </summary>
		public string? AppName
		{
			get;
			set;
		}

		/// <summary>
		/// Version of this application, reported to server through -zversion arguments
		/// </summary>
		public string? AppVersion
		{
			get;
			set;
		}

		/// <summary>
		/// Additional options to append to the command line
		/// </summary>
		public List<string> GlobalOptions { get; } = new List<string>();

		/// <summary>
		/// The logging interface
		/// </summary>
		public ILogger Logger
		{
			get;
			set;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ServerAndPort">The server address and port</param>
		/// <param name="UserName">The user name</param>
		/// <param name="Logger">Interface for logging</param>
		public PerforceConnection(string? ServerAndPort, string? UserName, ILogger Logger)
			: this(ServerAndPort, UserName, null, Logger)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ServerAndPort">The server address and port</param>
		/// <param name="UserName">The user name</param>
		/// <param name="ClientName">The client name</param>
		/// <param name="Logger">Interface for logging</param>
		public PerforceConnection(string? ServerAndPort, string? UserName, string? ClientName, ILogger Logger)
			: this(ServerAndPort, UserName, ClientName, null, null, Logger)
		{
			AssemblyName EntryAssemblyName = Assembly.GetEntryAssembly()!.GetName();
			if (EntryAssemblyName.Name != null)
			{
				AppName = EntryAssemblyName.Name;
				AppVersion = EntryAssemblyName.Version?.ToString() ?? String.Empty;
			}
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ServerAndPort">The server address and port</param>
		/// <param name="UserName">The user name</param>
		/// <param name="ClientName">The client name</param>
		/// <param name="Logger">Interface for logging</param>
		public PerforceConnection(string? ServerAndPort, string? UserName, string? ClientName, string? AppName, string? AppVersion, ILogger Logger)
		{
			this.ServerAndPort = ServerAndPort;
			this.UserName = UserName;
			this.ClientName = ClientName;
			this.AppName = AppName;
			this.AppVersion = AppVersion;
			this.Logger = Logger;
		}

		/// <summary>
		/// Constructor 
		/// </summary>
		/// <param name="Other">Connection to copy settings from</param>
		/// <param name="ClientName">The client name</param>
		public PerforceConnection(PerforceConnection Other)
			: this(Other.ServerAndPort, Other.UserName, Other.ClientName, Other.AppName, Other.AppVersion, Other.Logger)
		{
			GlobalOptions.AddRange(Other.GlobalOptions);
		}

		/// <summary>
		/// Gets all the arguments
		/// </summary>
		/// <returns></returns>
		string GetFullCommandLine(string CommandLine)
		{
			StringBuilder Result = new StringBuilder();
			if (ServerAndPort != null)
			{
				Result.AppendFormat("-p {0} ", ServerAndPort);
			}
			if (UserName != null)
			{
				Result.AppendFormat("-u {0} ", UserName);
			}
			if (ClientName != null)
			{
				Result.AppendFormat("-c {0} ", ClientName);
			}
			if (AppName != null)
			{
				Result.AppendFormat("-zprog={0} ", AppName);
			}
			if (AppVersion != null)
			{
				Result.AppendFormat("-zversion={0} ", AppVersion);
			}

			foreach (string GlobalOption in GlobalOptions)
			{
				Result.AppendFormat("{0} ", GlobalOption);
			}
			Result.Append(CommandLine);
			return Result.ToString();
		}

		/// <summary>
		/// Serializes a list of key/value pairs into binary format.
		/// </summary>
		/// <param name="KeyValuePairs">List of key value pairs</param>
		/// <returns>Serialized record data</returns>
		static byte[] SerializeRecord(List<KeyValuePair<string, object>> KeyValuePairs)
		{
			MemoryStream Stream = new MemoryStream();
			using (BinaryWriter Writer = new BinaryWriter(Stream))
			{
				Writer.Write((byte)'{');
				foreach (KeyValuePair<string, object> KeyValuePair in KeyValuePairs)
				{
					Writer.Write('s');
					byte[] KeyBytes = Encoding.UTF8.GetBytes(KeyValuePair.Key);
					Writer.Write((int)KeyBytes.Length);
					Writer.Write(KeyBytes);

					if (KeyValuePair.Value is string)
					{
						Writer.Write('s');
						byte[] ValueBytes = Encoding.UTF8.GetBytes((string)KeyValuePair.Value);
						Writer.Write((int)ValueBytes.Length);
						Writer.Write(ValueBytes);
					}
					else
					{
						throw new PerforceException("Unsupported formatting type for {0}", KeyValuePair.Key);
					}
				}
				Writer.Write((byte)'0');
			}
			return Stream.ToArray();
		}

		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="CommandLine">Arguments for the command</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="StatRecordType">The type of records to return for "stat" responses</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public async Task<List<PerforceResponse>> CommandAsync(string CommandLine, byte[]? InputData, Type? StatRecordType, CancellationToken CancellationToken)
		{
			using (PerforceChildProcess Process = new PerforceChildProcess(InputData, GetFullCommandLine(CommandLine), Logger))
			{
				CachedRecordInfo? StatRecordInfo = (StatRecordType == null) ? null : PerforceReflection.GetCachedRecordInfo(StatRecordType);
				return await Process.ReadResponsesAsync(StatRecordInfo, CancellationToken);
			}
		}

		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="Arguments">Arguments for the command</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public async Task<PerforceResponseList<T>> CommandAsync<T>(string Arguments, byte[]? InputData, CancellationToken CancellationToken) where T : class
		{
			List<PerforceResponse> Responses = await CommandAsync(Arguments, InputData, typeof(T), CancellationToken);

			PerforceResponseList<T> TypedResponses = new PerforceResponseList<T>();
			foreach (PerforceResponse Response in Responses)
			{
				TypedResponses.Add(new PerforceResponse<T>(Response));
			}
			return TypedResponses;
		}

		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="CommonArguments">Arguments for the command</param>
		/// <param name="BatchedArguments">Arguments to pass to the command in batches</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public async Task<PerforceResponseList<T>> BatchedCommandAsync<T>(string CommonArguments, string[] BatchedArguments, byte[]? InputData, CancellationToken CancellationToken) where T : class
		{
			PerforceResponseList<T> Responses = new PerforceResponseList<T>();
			for (int FileSpecIdx = 0; FileSpecIdx < BatchedArguments.Length;)
			{
				StringBuilder Arguments = new StringBuilder(CommonArguments);
				for (; FileSpecIdx < BatchedArguments.Length && Arguments.Length < 4096; FileSpecIdx++)
				{
					Arguments.AppendFormat(" \"{0}\"", BatchedArguments[FileSpecIdx]);
				}
				Responses.AddRange(await CommandAsync<T>(Arguments.ToString(), null, CancellationToken));
			}
			return Responses;
		}

		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="CommandLine">Arguments for the command</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="HandleRecord">Delegate used to handle each record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public async Task RecordCommandAsync(string CommandLine, byte[]? InputData, Action<PerforceRecord> HandleRecord, CancellationToken CancellationToken)
		{
			using (PerforceChildProcess Process = new PerforceChildProcess(InputData, GetFullCommandLine(CommandLine), Logger))
			{
				await Process.ReadRecordsAsync(HandleRecord, CancellationToken);
			}
		}

		/// <summary>
		/// Attempts to execute the given command, returning the results from the server or the first PerforceResponse object.
		/// </summary>
		/// <param name="Arguments">Arguments for the command.</param>
		/// <param name="InputData">Input data for the command.</param>
		/// <param name="StatRecordType">Type of element to return in the response</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either an object of type T or error.</returns>
		public async Task<PerforceResponse> SingleResponseCommandAsync(string Arguments, byte[]? InputData, Type? StatRecordType, CancellationToken CancellationToken)
		{
			List<PerforceResponse> Responses = await CommandAsync(Arguments, InputData, StatRecordType, CancellationToken);
			if (Responses.Count != 1)
			{
				throw new PerforceException("Expected one result from 'p4 {0}', got {1}", Arguments, Responses.Count);
			}
			return Responses[0];
		}

		/// <summary>
		/// Attempts to execute the given command, returning the results from the server or the first PerforceResponse object.
		/// </summary>
		/// <typeparam name="T">Type of record to parse</typeparam>
		/// <param name="Arguments">Arguments for the command.</param>
		/// <param name="InputData">Input data for the command.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either an object of type T or error.</returns>
		public async Task<PerforceResponse<T>> SingleResponseCommandAsync<T>(string Arguments, byte[]? InputData, CancellationToken CancellationToken) where T : class
		{
			return new PerforceResponse<T>(await SingleResponseCommandAsync(Arguments, InputData, typeof(T), CancellationToken));
		}

		#endregion

		#region p4 add

		/// <summary>
		/// Adds files to a pending changelist.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileNames">Files to be added</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<AddRecord>> AddAsync(int ChangeNumber, string[] FileNames, CancellationToken CancellationToken)
		{
			return (await TryAddAsync(ChangeNumber, null, AddOptions.None, FileNames, CancellationToken)).Data;
		}

		/// <summary>
		/// Adds files to a pending changelist.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileNames">Files to be added</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<AddRecord>> AddAsync(int ChangeNumber, string? FileType, AddOptions Options, string[] FileNames, CancellationToken CancellationToken)
		{
			return (await TryAddAsync(ChangeNumber, FileType, Options, FileNames, CancellationToken)).Data;
		}

		/// <summary>
		/// Adds files to a pending changelist.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileNames">Files to be added</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<AddRecord>> TryAddAsync(int ChangeNumber, string[] FileNames, CancellationToken CancellationToken)
		{
			return TryAddAsync(ChangeNumber, null, AddOptions.None, FileNames, CancellationToken);
		}

		/// <summary>
		/// Adds files to a pending changelist.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileNames">Files to be added</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<AddRecord>> TryAddAsync(int ChangeNumber, string? FileType, AddOptions Options, string[] FileNames, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("add");
			if (ChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			if ((Options & AddOptions.DowngradeToAdd) != 0)
			{
				Arguments.Append(" -d");
			}
			if ((Options & AddOptions.IncludeWildcards) != 0)
			{
				Arguments.Append(" -f");
			}
			if ((Options & AddOptions.NoIgnore) != 0)
			{
				Arguments.Append(" -I");
			}
			if ((Options & AddOptions.PreviewOnly) != 0)
			{
				Arguments.Append(" -n");
			}
			if (FileType != null)
			{
				Arguments.AppendFormat(" -t \"{0}\"", FileType);
			}

			return BatchedCommandAsync<AddRecord>(Arguments.ToString(), FileNames, null, CancellationToken);
		}

		#endregion

		#region p4 change

		/// <summary>
		/// Creates a changelist with the p4 change command. 
		/// </summary>
		/// <param name="Record">Information for the change to create. The number field should be left set to -1.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The changelist number, or an error.</returns>
		public async Task<ChangeRecord> CreateChangeAsync(ChangeRecord Record, CancellationToken CancellationToken)
		{
			return (await TryCreateChangeAsync(Record, CancellationToken)).Data;
		}

		/// <summary>
		/// Creates a changelist with the p4 change command. 
		/// </summary>
		/// <param name="Record">Information for the change to create. The number field should be left set to -1.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The changelist number, or an error.</returns>
		public async Task<PerforceResponse<ChangeRecord>> TryCreateChangeAsync(ChangeRecord Record, CancellationToken CancellationToken)
		{
			if (Record.Number != -1)
			{
				throw new PerforceException("'Number' field should be set to -1 to create a new changelist.");
			}

			PerforceResponse Response = await SingleResponseCommandAsync("change -i", SerializeRecord(Record), null, CancellationToken);

			PerforceError? Error = Response.Error;
			if (Error != null)
			{
				return new PerforceResponse<ChangeRecord>(Error);
			}

			PerforceInfo? Info = Response.Info;
			if (Info == null)
			{
				throw new PerforceException("Unexpected info response from change command: {0}", Response);
			}

			string[] Tokens = Info.Data.Split(' ');
			if (Tokens.Length != 3)
			{
				throw new PerforceException("Unexpected info response from change command: {0}", Response);
			}

			Record.Number = int.Parse(Tokens[1]);
			return new PerforceResponse<ChangeRecord>(Record);
		}

		/// <summary>
		/// Updates an existing changelist.
		/// </summary>
		/// <param name="Options">Options for this command</param>
		/// <param name="Record">Information for the change to create. The number field should be left set to zero.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The changelist number, or an error.</returns>
		public async Task UpdateChangeAsync(UpdateChangeOptions Options, ChangeRecord Record, CancellationToken CancellationToken)
		{
			(await TryUpdateChangeAsync(Options, Record, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Updates an existing changelist.
		/// </summary>
		/// <param name="Options">Options for this command</param>
		/// <param name="Record">Information for the change to create. The number field should be left set to zero.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The changelist number, or an error.</returns>
		public Task<PerforceResponse> TryUpdateChangeAsync(UpdateChangeOptions Options, ChangeRecord Record, CancellationToken CancellationToken)
		{
			if (Record.Number == -1)
			{
				throw new PerforceException("'Number' field must be set to update a changelist.");
			}

			StringBuilder Arguments = new StringBuilder("change -i");
			if ((Options & UpdateChangeOptions.Force) != 0)
			{
				Arguments.Append(" -f");
			}
			if ((Options & UpdateChangeOptions.Submitted) != 0)
			{
				Arguments.Append(" -u");
			}

			return SingleResponseCommandAsync(Arguments.ToString(), SerializeRecord(Record), null, CancellationToken);
		}

		/// <summary>
		/// Deletes a changelist (p4 change -d)
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">Changelist number to delete</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task DeleteChangeAsync(DeleteChangeOptions Options, int ChangeNumber, CancellationToken CancellationToken)
		{
			(await TryDeleteChangeAsync(Options, ChangeNumber, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Deletes a changelist (p4 change -d)
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">Changelist number to delete</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponse> TryDeleteChangeAsync(DeleteChangeOptions Options, int ChangeNumber, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("change -d");
			if ((Options & DeleteChangeOptions.Submitted) != 0)
			{
				Arguments.Append(" -f");
			}
			if ((Options & DeleteChangeOptions.BeforeRenumber) != 0)
			{
				Arguments.Append(" -O");
			}
			Arguments.AppendFormat(" {0}", ChangeNumber);

			return SingleResponseCommandAsync(Arguments.ToString(), null, null, CancellationToken);
		}

		/// <summary>
		/// Gets a changelist
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">Changelist number to retrieve. -1 is the default changelist for this workspace.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<ChangeRecord> GetChangeAsync(GetChangeOptions Options, int ChangeNumber, CancellationToken CancellationToken)
		{
			return (await TryGetChange(Options, ChangeNumber, CancellationToken)).Data;
		}

		/// <summary>
		/// Gets a changelist
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">Changelist number to retrieve. -1 is the default changelist for this workspace.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponse<ChangeRecord>> TryGetChange(GetChangeOptions Options, int ChangeNumber, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("change -o");
			if ((Options & GetChangeOptions.BeforeRenumber) != 0)
			{
				Arguments.Append(" -O");
			}
			if (ChangeNumber != -1)
			{
				Arguments.AppendFormat(" {0}", ChangeNumber);
			}

			return SingleResponseCommandAsync<ChangeRecord>(Arguments.ToString(), null, CancellationToken);
		}

		/// <summary>
		/// Serializes a change record to a byte array
		/// </summary>
		/// <param name="Input">The record to serialize</param>
		/// <returns>Serialized record</returns>
		byte[] SerializeRecord(ChangeRecord Input)
		{
			List<KeyValuePair<string, object>> NameToValue = new List<KeyValuePair<string, object>>();
			if (Input.Number == -1)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Change", "new"));
			}
			else
			{
				NameToValue.Add(new KeyValuePair<string, object>("Change", Input.Number.ToString()));
			}
			if (Input.Type != ChangeType.Unspecified)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Type", Input.Type.ToString()));
			}
			if (Input.User != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("User", Input.User));
			}
			if (Input.Client != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Client", Input.Client));
			}
			if (Input.Description != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Description", Input.Description));
			}
			return SerializeRecord(NameToValue);
		}

		#endregion

		#region p4 changes

		/// <summary>
		/// Enumerates changes on the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public async Task<List<ChangesRecord>> GetChangesAsync(ChangesOptions Options, int MaxChanges, ChangeStatus Status, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryGetChangesAsync(Options, MaxChanges, Status, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Enumerates changes on the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="ClientName">List only changes made from the named client workspace.</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="UserName">List only changes made by the named user</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public async Task<List<ChangesRecord>> GetChangesAsync(ChangesOptions Options, string? ClientName, int MaxChanges, ChangeStatus Status, string? UserName, string[]? FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryGetChangesAsync(Options, ClientName, MaxChanges, Status, UserName, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Enumerates changes on the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public Task<PerforceResponseList<ChangesRecord>> TryGetChangesAsync(ChangesOptions Options, int MaxChanges, ChangeStatus Status, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return TryGetChangesAsync(Options, null, MaxChanges, Status, null, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Enumerates changes on the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="ClientName">List only changes made from the named client workspace.</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="UserName">List only changes made by the named user</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public Task<PerforceResponseList<ChangesRecord>> TryGetChangesAsync(ChangesOptions Options, string? ClientName, int MaxChanges, ChangeStatus Status, string? UserName, string[]? FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("changes");
			if ((Options & ChangesOptions.IncludeIntegrations) != 0)
			{
				Arguments.Append(" -i");
			}
			if ((Options & ChangesOptions.IncludeTimes) != 0)
			{
				Arguments.Append(" -t");
			}
			if ((Options & ChangesOptions.LongOutput) != 0)
			{
				Arguments.Append(" -l");
			}
			if ((Options & ChangesOptions.TruncatedLongOutput) != 0)
			{
				Arguments.Append(" -L");
			}
			if ((Options & ChangesOptions.IncludeRestricted) != 0)
			{
				Arguments.Append(" -f");
			}
			if (ClientName != null)
			{
				Arguments.AppendFormat(" -c \"{0}\"", ClientName);
			}
			if (MaxChanges != -1)
			{
				Arguments.AppendFormat(" -m {0}", MaxChanges);
			}
			if (Status != ChangeStatus.All)
			{
				Arguments.AppendFormat(" -s {0}", PerforceReflection.GetEnumText(typeof(ChangeStatus), Status));
			}
			if (UserName != null)
			{
				Arguments.AppendFormat(" -u {0}", UserName);
			}
			if (FileSpecs != null)
			{
				foreach (string FileSpec in FileSpecs)
				{
					Arguments.AppendFormat(" \"{0}\"", FileSpec);
				}
			}
			return CommandAsync<ChangesRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 clean

		/// <summary>
		/// Cleans the workspace
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<CleanRecord>> CleanAsync(CleanOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryCleanAsync(Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Cleans the workspace
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<PerforceResponseList<CleanRecord>> TryCleanAsync(CleanOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("clean");
			if ((Options & CleanOptions.Edited) != 0)
			{
				Arguments.Append(" -e");
			}
			if ((Options & CleanOptions.Added) != 0)
			{
				Arguments.Append(" -a");
			}
			if ((Options & CleanOptions.Deleted) != 0)
			{
				Arguments.Append(" -d");
			}
			if ((Options & CleanOptions.Preview) != 0)
			{
				Arguments.Append(" -n");
			}
			if ((Options & CleanOptions.NoIgnoreChecking) != 0)
			{
				Arguments.Append(" -I");
			}
			if ((Options & CleanOptions.LocalSyntax) != 0)
			{
				Arguments.Append(" -l");
			}
			if ((Options & CleanOptions.ModifiedTimes) != 0)
			{
				Arguments.Append(" -m");
			}
			foreach (string FileSpec in FileSpecs)
			{
				Arguments.AppendFormat(" \"{0}\"", FileSpec);
			}

			PerforceResponseList<CleanRecord> Records = await CommandAsync<CleanRecord>(Arguments.ToString(), null, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		#endregion

		#region p4 client

		/// <summary>
		/// Creates a client
		/// </summary>
		/// <param name="Record">The client record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task CreateClientAsync(ClientRecord Record, CancellationToken CancellationToken)
		{
			(await TryCreateClientAsync(Record, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Creates a client
		/// </summary>
		/// <param name="Record">The client record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponse> TryCreateClientAsync(ClientRecord Record, CancellationToken CancellationToken)
		{
			return TryUpdateClientAsync(Record, CancellationToken);
		}

		/// <summary>
		/// Creates a client
		/// </summary>
		/// <param name="Record">The client record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task UpdateClientAsync(ClientRecord Record, CancellationToken CancellationToken)
		{
			(await TryUpdateClientAsync(Record, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Update a client
		/// </summary>
		/// <param name="Record">The client record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponse> TryUpdateClientAsync(ClientRecord Record, CancellationToken CancellationToken)
		{
			return SingleResponseCommandAsync("client -i", SerializeRecord(Record), null, CancellationToken);
		}

		/// <summary>
		/// Deletes a client
		/// </summary>
		/// <param name="Options">Options for this command</param>
		/// <param name="ClientName">Name of the client</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task DeleteClientAsync(DeleteClientOptions Options, string ClientName, CancellationToken CancellationToken)
		{
			(await TryDeleteClientAsync(Options, ClientName, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Deletes a client
		/// </summary>
		/// <param name="Options">Options for this command</param>
		/// <param name="ClientName">Name of the client</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponse> TryDeleteClientAsync(DeleteClientOptions Options, string ClientName, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("client -d");
			if ((Options & DeleteClientOptions.Force) != 0)
			{
				Arguments.Append(" -f");
			}
			if ((Options & DeleteClientOptions.DeleteShelved) != 0)
			{
				Arguments.Append(" -Fs");
			}
			Arguments.AppendFormat(" \"{0}\"", ClientName);
			return SingleResponseCommandAsync(Arguments.ToString(), null, null, CancellationToken);
		}

		/// <summary>
		/// Changes the stream associated with a client
		/// </summary>
		/// <param name="ClientName">The client name</param>
		/// <param name="StreamName">The new stream to be associated with the client</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task SwitchClientToStreamAsync(string ClientName, string StreamName, SwitchClientOptions Options, CancellationToken CancellationToken)
		{
			(await TrySwitchClientToStreamAsync(ClientName, StreamName, Options, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Changes the stream associated with a client
		/// </summary>
		/// <param name="ClientName">The client name</param>
		/// <param name="StreamName">The new stream to be associated with the client</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponse> TrySwitchClientToStreamAsync(string ClientName, string StreamName, SwitchClientOptions Options, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("client -s");
			if ((Options & SwitchClientOptions.IgnoreOpenFiles) != 0)
			{
				Arguments.Append(" -f");
			}
			Arguments.AppendFormat(" -S \"{0}\"", StreamName);

			return SingleResponseCommandAsync(Arguments.ToString(), null, null, CancellationToken);
		}

		/// <summary>
		/// Changes a client to mirror a template
		/// </summary>
		/// <param name="ClientName">The client name</param>
		/// <param name="TemplateName">The new stream to be associated with the client</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task SwitchClientToTemplateAsync(string ClientName, string TemplateName, CancellationToken CancellationToken)
		{
			(await TrySwitchClientToTemplateAsync(ClientName, TemplateName, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Changes a client to mirror a template
		/// </summary>
		/// <param name="ClientName">The client name</param>
		/// <param name="TemplateName">The new stream to be associated with the client</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponse> TrySwitchClientToTemplateAsync(string ClientName, string TemplateName, CancellationToken CancellationToken)
		{
			string Arguments = String.Format("client -s -t \"{0}\"", TemplateName);
			return SingleResponseCommandAsync(Arguments, null, null, CancellationToken);
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="ClientName">Name of the client. Specify null for the current client.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public async Task<ClientRecord> GetClientAsync(string? ClientName, CancellationToken CancellationToken)
		{
			return (await TryGetClientAsync(ClientName, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="ClientName">Name of the client. Specify null for the current client.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public Task<PerforceResponse<ClientRecord>> TryGetClientAsync(string? ClientName, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("client -o");
			if (ClientName != null)
			{
				Arguments.AppendFormat(" \"{0}\"", ClientName);
			}
			return SingleResponseCommandAsync<ClientRecord>(Arguments.ToString(), null, CancellationToken);
		}

		/// <summary>
		/// Queries information about a stream
		/// </summary>
		/// <param name="StreamName">Name of the stream to query</param>
		/// <param name="bIncludeView">Whether to include the stream view in the output</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Stream information record</returns>
		public async Task<StreamRecord> GetStreamAsync(string StreamName, bool bIncludeView, CancellationToken CancellationToken)
		{
			return (await TryGetStreamAsync(StreamName, bIncludeView, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries information about a stream
		/// </summary>
		/// <param name="StreamName">Name of the stream to query</param>
		/// <param name="bIncludeView">Whether to include the stream view in the output</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Stream information record</returns>
		public Task<PerforceResponse<StreamRecord>> TryGetStreamAsync(string StreamName, bool bIncludeView, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("stream -o");
			if (bIncludeView)
			{
				Arguments.Append(" -v");
			}
			Arguments.AppendFormat(" {0}", StreamName);

			return SingleResponseCommandAsync<StreamRecord>(Arguments.ToString(), null, CancellationToken);
		}

		/// <summary>
		/// Queries the view for a stream
		/// </summary>
		/// <param name="StreamName">Name of the stream.</param>
		/// <param name="ChangeNumber">Changelist at which to query the stream view</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public async Task<ClientRecord> GetStreamViewAsync(string StreamName, int ChangeNumber, CancellationToken CancellationToken)
		{
			return (await TryGetStreamViewAsync(StreamName, ChangeNumber, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries the view for a stream
		/// </summary>
		/// <param name="StreamName">Name of the stream.</param>
		/// <param name="ChangeNumber">Changelist at which to query the stream view</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public Task<PerforceResponse<ClientRecord>> TryGetStreamViewAsync(string StreamName, int ChangeNumber, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("client -o");
			Arguments.AppendFormat(" -S \"{0}\"", StreamName);
			if (ChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			return SingleResponseCommandAsync<ClientRecord>(Arguments.ToString(), null, CancellationToken);
		}

		/// <summary>
		/// Serializes a client record to a byte array
		/// </summary>
		/// <param name="Input">The input record</param>
		/// <returns>Serialized record data</returns>
		byte[] SerializeRecord(ClientRecord Input)
		{
			List<KeyValuePair<string, object>> NameToValue = new List<KeyValuePair<string, object>>();
			if (Input.Name != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Client", Input.Name));
			}
			if (Input.Owner != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Owner", Input.Owner));
			}
			if (Input.Host != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Host", Input.Host));
			}
			if (Input.Description != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Description", Input.Description));
			}
			if (Input.Root != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Root", Input.Root));
			}
			if (Input.Options != ClientOptions.None)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Options", PerforceReflection.GetEnumText(typeof(ClientOptions), Input.Options)));
			}
			if (Input.SubmitOptions != ClientSubmitOptions.Unspecified)
			{
				NameToValue.Add(new KeyValuePair<string, object>("SubmitOptions", PerforceReflection.GetEnumText(typeof(ClientSubmitOptions), Input.SubmitOptions)));
			}
			if (Input.LineEnd != ClientLineEndings.Unspecified)
			{
				NameToValue.Add(new KeyValuePair<string, object>("LineEnd", PerforceReflection.GetEnumText(typeof(ClientLineEndings), Input.LineEnd)));
			}
			if (Input.Type != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Type", Input.Type));
			}
			if (Input.Stream != null)
			{
				NameToValue.Add(new KeyValuePair<string, object>("Stream", Input.Stream));
			}
			for (int Idx = 0; Idx < Input.View.Count; Idx++)
			{
				NameToValue.Add(new KeyValuePair<string, object>(String.Format("View{0}", Idx), Input.View[Idx]));
			}
			return SerializeRecord(NameToValue);
		}

		#endregion

		#region p4 clients

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="Options">Options for this command</param>
		/// <param name="UserName">List only client workspaces owned by this user.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public async Task<List<ClientsRecord>> GetClientsAsync(ClientsOptions Options, string? UserName, CancellationToken CancellationToken)
		{
			return (await TryGetClientsAsync(Options, UserName, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="Options">Options for this command</param>
		/// <param name="UserName">List only client workspaces owned by this user.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public Task<PerforceResponseList<ClientsRecord>> TryGetClientsAsync(ClientsOptions Options, string? UserName, CancellationToken CancellationToken)
		{
			return TryGetClientsAsync(Options, null, -1, null, UserName, CancellationToken);
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="Options">Options for this command</param>
		/// <param name="Filter">List only client workspaces matching filter. Treated as case sensitive if <ref>ClientsOptions.CaseSensitiveFilter</ref> is set.</param>
		/// <param name="MaxResults">Limit the number of results to return. -1 for all.</param>
		/// <param name="Stream">List client workspaces associated with the specified stream.</param>
		/// <param name="UserName">List only client workspaces owned by this user.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public async Task<List<ClientsRecord>> GetClientsAsync(ClientsOptions Options, string? Filter, int MaxResults, string? Stream, string? UserName, CancellationToken CancellationToken)
		{
			return (await TryGetClientsAsync(Options, Filter, MaxResults, Stream, UserName, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="Options">Options for this command</param>
		/// <param name="Filter">List only client workspaces matching filter. Treated as case sensitive if <ref>ClientsOptions.CaseSensitiveFilter</ref> is set.</param>
		/// <param name="MaxResults">Limit the number of results to return. -1 for all.</param>
		/// <param name="Stream">List client workspaces associated with the specified stream.</param>
		/// <param name="UserName">List only client workspaces owned by this user.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public Task<PerforceResponseList<ClientsRecord>> TryGetClientsAsync(ClientsOptions Options, string? Filter, int MaxResults, string? Stream, string? UserName, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("clients");
			if ((Options & ClientsOptions.All) != 0)
			{
				Arguments.Append(" -a");
			}
			if (Filter != null)
			{
				if ((Options & ClientsOptions.CaseSensitiveFilter) != 0)
				{
					Arguments.AppendFormat(" -e \"{0}\"", Filter);
				}
				else
				{
					Arguments.AppendFormat(" -E \"{0}\"", Filter);
				}
			}
			if (MaxResults != -1)
			{
				Arguments.AppendFormat(" -m {0}", MaxResults);
			}
			if (Stream != null)
			{
				Arguments.AppendFormat(" -S \"{0}\"", Stream);
			}
			if ((Options & ClientsOptions.WithTimes) != 0)
			{
				Arguments.Append(" -t");
			}
			if (UserName != null)
			{
				Arguments.AppendFormat(" -u \"{0}\"", UserName);
			}
			if ((Options & ClientsOptions.Unloaded) != 0)
			{
				Arguments.Append(" -U");
			}
			return CommandAsync<ClientsRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 delete

		/// <summary>
		/// Execute the 'delete' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<DeleteRecord>> DeleteAsync(int ChangeNumber, DeleteOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryDeleteAsync(ChangeNumber, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'delete' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public Task<PerforceResponseList<DeleteRecord>> TryDeleteAsync(int ChangeNumber, DeleteOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("delete");
			if (ChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			if ((Options & DeleteOptions.PreviewOnly) != 0)
			{
				Arguments.Append(" -n");
			}
			if ((Options & DeleteOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Append(" -k");
			}
			if ((Options & DeleteOptions.WithoutSyncing) != 0)
			{
				Arguments.Append(" -v");
			}

			return BatchedCommandAsync<DeleteRecord>(Arguments.ToString(), FileSpecs, null, CancellationToken);
		}

		#endregion

		#region p4 describe

		/// <summary>
		/// Describes a single changelist
		/// </summary>
		/// <param name="ChangeNumber">The changelist number to retrieve description for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a describe record or error code</returns>
		public async Task<DescribeRecord> DescribeAsync(int ChangeNumber, CancellationToken CancellationToken)
		{
			return (await TryDescribeAsync(ChangeNumber, CancellationToken)).Data;
		}

		/// <summary>
		/// Describes a single changelist
		/// </summary>
		/// <param name="ChangeNumber">The changelist number to retrieve description for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a describe record or error code</returns>
		public async Task<PerforceResponse<DescribeRecord>> TryDescribeAsync(int ChangeNumber, CancellationToken CancellationToken)
		{
			PerforceResponseList<DescribeRecord> Records = await TryDescribeAsync(new int[] { ChangeNumber }, CancellationToken);
			if (Records.Count != 1)
			{
				throw new PerforceException("Expected only one record returned from p4 describe command, got {0}", Records.Count);
			}
			return Records[0];
		}

		/// <summary>
		/// Describes a set of changelists
		/// </summary>
		/// <param name="ChangeNumbers">The changelist numbers to retrieve descriptions for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<DescribeRecord>> DescribeAsync(int[] ChangeNumbers, CancellationToken CancellationToken)
		{
			return (await TryDescribeAsync(ChangeNumbers, CancellationToken)).Data;
		}

		/// <summary>
		/// Describes a set of changelists
		/// </summary>
		/// <param name="ChangeNumbers">The changelist numbers to retrieve descriptions for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public Task<PerforceResponseList<DescribeRecord>> TryDescribeAsync(int[] ChangeNumbers, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("describe -s");
			foreach (int ChangeNumber in ChangeNumbers)
			{
				Arguments.AppendFormat(" {0}", ChangeNumber);
			}
			return CommandAsync<DescribeRecord>(Arguments.ToString(), null, CancellationToken);
		}

		/// <summary>
		/// Describes a set of changelists
		/// </summary>
		/// <param name="ChangeNumbers">The changelist numbers to retrieve descriptions for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<DescribeRecord>> DescribeAsync(DescribeOptions Options, int MaxNumFiles, int[] ChangeNumbers, CancellationToken CancellationToken)
		{
			return (await TryDescribeAsync(Options, MaxNumFiles, ChangeNumbers, CancellationToken)).Data;
		}

		/// <summary>
		/// Describes a set of changelists
		/// </summary>
		/// <param name="ChangeNumbers">The changelist numbers to retrieve descriptions for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public Task<PerforceResponseList<DescribeRecord>> TryDescribeAsync(DescribeOptions Options, int MaxNumFiles, int[] ChangeNumbers, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("describe -s");
			if ((Options & DescribeOptions.ShowDescriptionForRestrictedChanges) != 0)
			{
				Arguments.Append(" -f");
			}
			if ((Options & DescribeOptions.Identity) != 0)
			{
				Arguments.Append(" -I");
			}
			if (MaxNumFiles != -1)
			{
				Arguments.AppendFormat(" -m{0}", MaxNumFiles);
			}
			if ((Options & DescribeOptions.OriginalChangeNumber) != 0)
			{
				Arguments.Append(" -O");
			}
			if ((Options & DescribeOptions.Shelved) != 0)
			{
				Arguments.Append(" -S");
			}
			foreach (int ChangeNumber in ChangeNumbers)
			{
				Arguments.AppendFormat(" {0}", ChangeNumber);
			}
			return CommandAsync<DescribeRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 edit

		/// <summary>
		/// Opens files for edit
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<EditRecord>> EditAsync(int ChangeNumber, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryEditAsync(ChangeNumber, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Opens files for edit
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<EditRecord>> TryEditAsync(int ChangeNumber, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return TryEditAsync(ChangeNumber, null, EditOptions.None, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Opens files for edit
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<EditRecord>> EditAsync(int ChangeNumber, string? FileType, EditOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryEditAsync(ChangeNumber, FileType, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Opens files for edit
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<EditRecord>> TryEditAsync(int ChangeNumber, string? FileType, EditOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("edit");
			if (ChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			if ((Options & EditOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Append(" -k");
			}
			if ((Options & EditOptions.PreviewOnly) != 0)
			{
				Arguments.Append(" -n");
			}
			if (FileType != null)
			{
				Arguments.AppendFormat(" -t \"{0}\"", FileType);
			}

			return BatchedCommandAsync<EditRecord>(Arguments.ToString(), FileSpecs, null, CancellationToken);
		}

		#endregion

		#region p4 filelog

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<FileLogRecord>> FileLogAsync(FileLogOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryFileLogAsync(Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public Task<PerforceResponseList<FileLogRecord>> TryFileLogAsync(FileLogOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return TryFileLogAsync(-1, -1, Options, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="MaxChanges">Number of changelists to show. Ignored if zero or negative.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<FileLogRecord>> FileLogAsync(int MaxChanges, FileLogOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryFileLogAsync(MaxChanges, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="MaxChanges">Number of changelists to show. Ignored if zero or negative.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public Task<PerforceResponseList<FileLogRecord>> TryFileLogAsync(int MaxChanges, FileLogOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return TryFileLogAsync(-1, MaxChanges, Options, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="ChangeNumber">Show only files modified by this changelist. Ignored if zero or negative.</param>
		/// <param name="MaxChanges">Number of changelists to show. Ignored if zero or negative.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<FileLogRecord>> FileLogAsync(int ChangeNumber, int MaxChanges, FileLogOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryFileLogAsync(ChangeNumber, MaxChanges, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="ChangeNumber">Show only files modified by this changelist. Ignored if zero or negative.</param>
		/// <param name="MaxChanges">Number of changelists to show. Ignored if zero or negative.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public Task<PerforceResponseList<FileLogRecord>> TryFileLogAsync(int ChangeNumber, int MaxChanges, FileLogOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			// Build the argument list
			StringBuilder Arguments = new StringBuilder("filelog");
			if (ChangeNumber > 0)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			if ((Options & FileLogOptions.ContentHistory) != 0)
			{
				Arguments.Append(" -h");
			}
			if ((Options & FileLogOptions.FollowAcrossBranches) != 0)
			{
				Arguments.Append(" -i");
			}
			if ((Options & FileLogOptions.FullDescriptions) != 0)
			{
				Arguments.Append(" -l");
			}
			if ((Options & FileLogOptions.LongDescriptions) != 0)
			{
				Arguments.Append(" -L");
			}
			if (MaxChanges > 0)
			{
				Arguments.AppendFormat(" -m {0}", MaxChanges);
			}
			if ((Options & FileLogOptions.DoNotFollowPromotedTaskStreams) != 0)
			{
				Arguments.Append(" -p");
			}
			if ((Options & FileLogOptions.IgnoreNonContributoryIntegrations) != 0)
			{
				Arguments.Append(" -s");
			}

			// Always include times to simplify parsing
			Arguments.Append(" -t");

			// Append all the arguments
			return BatchedCommandAsync<FileLogRecord>(Arguments.ToString(), FileSpecs, null, CancellationToken);
		}

		#endregion

		#region p4 fstat

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<FStatRecord>> FStatAsync(FStatOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryFStatAsync(Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public Task<PerforceResponseList<FStatRecord>> TryFStatAsync(FStatOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return TryFStatAsync(-1, Options, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="MaxFiles">Produce fstat output for only the first max files.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<FStatRecord>> FStatAsync(int MaxFiles, FStatOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryFStatAsync(MaxFiles, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="MaxFiles">Produce fstat output for only the first max files.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public Task<PerforceResponseList<FStatRecord>> TryFStatAsync(int MaxFiles, FStatOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return TryFStatAsync(-1, -1, null, MaxFiles, Options, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="AfterChangeNumber">Return only files affected after the given changelist number.</param>
		/// <param name="OnlyChangeNumber">Return only files affected by the given changelist number.</param>
		/// <param name="Filter">List only those files that match the criteria specified.</param>
		/// <param name="MaxFiles">Produce fstat output for only the first max files.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<FStatRecord>> FStatAsync(int AfterChangeNumber, int OnlyChangeNumber, string? Filter, int MaxFiles, FStatOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryFStatAsync(AfterChangeNumber, OnlyChangeNumber, Filter, MaxFiles, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="AfterChangeNumber">Return only files affected after the given changelist number.</param>
		/// <param name="OnlyChangeNumber">Return only files affected by the given changelist number.</param>
		/// <param name="Filter">List only those files that match the criteria specified.</param>
		/// <param name="MaxFiles">Produce fstat output for only the first max files.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public Task<PerforceResponseList<FStatRecord>> TryFStatAsync(int AfterChangeNumber, int OnlyChangeNumber, string? Filter, int MaxFiles, FStatOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			// Build the argument list
			StringBuilder Arguments = new StringBuilder("fstat");
			if (AfterChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", AfterChangeNumber);
			}
			if(OnlyChangeNumber != -1)
			{
				Arguments.AppendFormat(" -e {0}", OnlyChangeNumber);
			}
			if(Filter != null)
			{
				Arguments.AppendFormat(" -F \"{0}\"", Filter);
			}
			if((Options & FStatOptions.ReportDepotSyntax) != 0)
			{
				Arguments.Append(" -L");
			}
			if((Options & FStatOptions.AllRevisions) != 0)
			{
				Arguments.Append(" -Of");
			}
			if((Options & FStatOptions.IncludeFileSizes) != 0)
			{
				Arguments.Append(" -Ol");
			}
			if((Options & FStatOptions.ClientFileInPerforceSyntax) != 0)
			{
				Arguments.Append(" -Op");
			}
			if((Options & FStatOptions.ShowPendingIntegrations) != 0)
			{
				Arguments.Append(" -Or");
			}
			if((Options & FStatOptions.ShortenOutput) != 0)
			{
				Arguments.Append(" -Os");
			}
			if((Options & FStatOptions.ReverseOrder) != 0)
			{
				Arguments.Append(" -r");
			}
			if((Options & FStatOptions.OnlyMapped) != 0)
			{
				Arguments.Append(" -Rc");
			}
			if((Options & FStatOptions.OnlyHave) != 0)
			{
				Arguments.Append(" -Rh");
			}
			if((Options & FStatOptions.OnlyOpenedBeforeHead) != 0)
			{
				Arguments.Append(" -Rn");
			}
			if((Options & FStatOptions.OnlyOpenInWorkspace) != 0)
			{
				Arguments.Append(" -Ro");
			}
			if((Options & FStatOptions.OnlyOpenAndResolved) != 0)
			{
				Arguments.Append(" -Rr");
			}
			if((Options & FStatOptions.OnlyShelved) != 0)
			{
				Arguments.Append(" -Rs");
			}
			if((Options & FStatOptions.OnlyUnresolved) != 0)
			{
				Arguments.Append(" -Ru");
			}
			if((Options & FStatOptions.SortByDate) != 0)
			{
				Arguments.Append(" -Sd");
			}
			if((Options & FStatOptions.SortByHaveRevision) != 0)
			{
				Arguments.Append(" -Sh");
			}
			if((Options & FStatOptions.SortByHeadRevision) != 0)
			{
				Arguments.Append(" -Sr");
			}
			if((Options & FStatOptions.SortByFileSize) != 0)
			{
				Arguments.Append(" -Ss");
			}
			if((Options & FStatOptions.SortByFileType) != 0)
			{
				Arguments.Append(" -St");
			}
			if((Options & FStatOptions.IncludeFilesInUnloadDepot) != 0)
			{
				Arguments.Append(" -U");
			}

			// Execute the command
			return BatchedCommandAsync<FStatRecord>(Arguments.ToString(), FileSpecs, null, CancellationToken);
		}

		#endregion

		#region p4 info

		/// <summary>
		/// Execute the 'info' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; an InfoRecord or error code</returns>
		public async Task<InfoRecord> GetInfoAsync(InfoOptions Options, CancellationToken CancellationToken)
		{
			return (await TryGetInfoAsync(Options, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'info' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; an InfoRecord or error code</returns>
		public Task<PerforceResponse<InfoRecord>> TryGetInfoAsync(InfoOptions Options, CancellationToken CancellationToken)
		{
			// Build the argument list
			StringBuilder Arguments = new StringBuilder("info");
			if((Options & InfoOptions.ShortOutput) != 0)
			{
				Arguments.Append(" -s");
			}
			return SingleResponseCommandAsync<InfoRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 login

		/// <summary>
		/// Execute the 'login' command
		/// </summary>
		/// <param name="Password">Password to use to login</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task LoginAsync(string Password, CancellationToken CancellationToken)
		{
			// Some versions of P4.EXE do not support marshalled output for P4 login calls, so we only support this as a basic text query.
			byte[] PasswordBytes = Encoding.UTF8.GetBytes(Password);
			using (ManagedProcessGroup ChildProcessGroup = new ManagedProcessGroup())
			{
				string Executable = PerforceChildProcess.GetExecutable();
				string FullArgumentList = GetFullCommandLine("login");

				Logger.LogDebug("Running {0} {1}", Executable, FullArgumentList);
				using (ManagedProcess ChildProcess = new ManagedProcess(ChildProcessGroup, Executable, FullArgumentList, null, null, PasswordBytes, ProcessPriorityClass.Normal))
				{
					StringBuilder Lines = new StringBuilder("Unable to log in: ");
					for(; ;)
					{
						string? Line = await ChildProcess.ReadLineAsync();
						if(Line == null)
						{
							break;
						}

						Lines.Append($"\n  {Line}");

						if(Line.StartsWith("Enter password:", StringComparison.OrdinalIgnoreCase))
						{
							Lines.Append($" {new string('*', Password.Length)}");
						}
					}

					ChildProcess.WaitForExit();

					if (ChildProcess.ExitCode != 0)
					{
						throw new PerforceException($"{Lines}\n  (exit code {ChildProcess.ExitCode})");
					}
				}
			}
		}

		#endregion

		#region p4 merge

		/// <summary>
		/// Execute the 'merge' command
		/// </summary>
		/// <param name="Options">Options for the merge</param>
		/// <param name="Change"></param>
		/// <param name="MaxFiles">Maximum number of files to merge</param>
		/// <param name="SourceFileSpec">The source filespec and revision range</param>
		/// <param name="TargetFileSpec">The target filespec</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>List of records</returns>
		public Task<PerforceResponseList<MergeRecord>> MergeAsync(MergeOptions Options, int Change, int MaxFiles, string SourceFileSpec, string TargetFileSpec, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("merge");
			if ((Options & MergeOptions.Preview) != 0)
			{
				Arguments.AppendFormat(" -n");
			}
			if (Change != -1)
			{
				Arguments.AppendFormat(" -c {0}", Change);
			}
			if (MaxFiles != -1)
			{
				Arguments.AppendFormat(" -m {0}", MaxFiles);
			}
			Arguments.AppendFormat(" \"{0}\"", SourceFileSpec);
			Arguments.AppendFormat(" \"{0}\"", TargetFileSpec);

			return CommandAsync<MergeRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 move

		/// <summary>
		/// Opens files for move
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="SourceFileSpec">The source file(s)</param>
		/// <param name="TargetFileSpec">The target file(s)</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<MoveRecord>> MoveAsync(int ChangeNumber, string? FileType, MoveOptions Options, string SourceFileSpec, string TargetFileSpec, CancellationToken CancellationToken)
		{
			return (await TryMoveAsync(ChangeNumber, FileType, Options, SourceFileSpec, TargetFileSpec, CancellationToken)).Data;
		}

		/// <summary>
		/// Opens files for move
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="SourceFileSpec">The source file(s)</param>
		/// <param name="TargetFileSpec">The target file(s)</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<MoveRecord>> TryMoveAsync(int ChangeNumber, string? FileType, MoveOptions Options, string SourceFileSpec, string TargetFileSpec, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("move");
			if (ChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			if ((Options & MoveOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Append(" -k");
			}
			if ((Options & MoveOptions.RenameOnly) != 0)
			{
				Arguments.Append(" -r");
			}
			if ((Options & MoveOptions.PreviewOnly) != 0)
			{
				Arguments.Append(" -n");
			}
			if (FileType != null)
			{
				Arguments.AppendFormat(" -t \"{0}\"", FileType);
			}
			Arguments.AppendFormat(" \"{0}\" \"{1}\"", SourceFileSpec, TargetFileSpec);

			return CommandAsync<MoveRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 opened

		/// <summary>
		/// Execute the 'opened' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">List the files in pending changelist change. To list files in the default changelist, use DefaultChange.</param>
		/// <param name="ClientName">List only files that are open in the given client</param>
		/// <param name="UserName">List only files that are opened by the given user</param>
		/// <param name="MaxResults">Maximum number of results to return</param>
		/// <param name="FileSpecs">Specification for the files to list</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<FStatRecord>> GetOpenFilesAsync(OpenedOptions Options, int ChangeNumber, string? ClientName, string? UserName, int MaxResults, string[]? FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryGetOpenFilesAsync(Options, ChangeNumber, ClientName, UserName, MaxResults, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'opened' command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">List the files in pending changelist change. To list files in the default changelist, use DefaultChange.</param>
		/// <param name="ClientName">List only files that are open in the given client</param>
		/// <param name="UserName">List only files that are opened by the given user</param>
		/// <param name="MaxResults">Maximum number of results to return</param>
		/// <param name="FileSpecs">Specification for the files to list</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<FStatRecord>> TryGetOpenFilesAsync(OpenedOptions Options, int ChangeNumber, string? ClientName, string? UserName, int MaxResults, string[]? FileSpecs, CancellationToken CancellationToken)
		{
			// Build the argument list
			StringBuilder Arguments = new StringBuilder("opened");
			if((Options & OpenedOptions.AllWorkspaces) != 0)
			{
				Arguments.AppendFormat(" -a");
			}
			if (ChangeNumber == PerforceReflection.DefaultChange)
			{
				Arguments.AppendFormat(" -c default");
			}
			else if (ChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			if(ClientName != null)
			{
				Arguments.AppendFormat(" -C \"{0}\"", ClientName);
			}
			if(UserName != null)
			{
				Arguments.AppendFormat(" -u \"{0}\"", UserName);
			}
			if(MaxResults != -1)
			{
				Arguments.AppendFormat(" -m {0}", MaxResults);
			}
			if((Options & OpenedOptions.ShortOutput) != 0)
			{
				Arguments.AppendFormat(" -s");
			}
			if (FileSpecs != null)
			{
				foreach (string FileSpec in FileSpecs)
				{
					Arguments.AppendFormat(" \"{0}\"", FileSpec);
				}
			}
			return CommandAsync<FStatRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 print

		/// <summary>
		/// Execute the 'print' command
		/// </summary>
		/// <param name="OutputFile">Output file to redirect output to</param>
		/// <param name="FileSpec">Specification for the files to print</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<PrintRecord> PrintAsync(string OutputFile, string FileSpec, CancellationToken CancellationToken)
		{
			return (await TryPrintAsync(OutputFile, FileSpec, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'print' command
		/// </summary>
		/// <param name="OutputFile">Output file to redirect output to</param>
		/// <param name="FileSpec">Specification for the files to print</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponse<PrintRecord>> TryPrintAsync(string OutputFile, string FileSpec, CancellationToken CancellationToken)
		{
			// Build the argument list
			StringBuilder Arguments = new StringBuilder("print");
			Arguments.AppendFormat(" -o \"{0}\"", OutputFile);
			Arguments.AppendFormat(" \"{0}\"", FileSpec);
			return SingleResponseCommandAsync<PrintRecord>(Arguments.ToString(), null, CancellationToken);
		}

		class PrintHandler : IDisposable
		{
			Dictionary<string, string> DepotFileToLocalFile;
			FileStream? OutputStream;

			public PrintHandler(Dictionary<string, string> DepotFileToLocalFile)
			{
				this.DepotFileToLocalFile = DepotFileToLocalFile;
			}

			public void Dispose()
			{
				CloseStream();
			}

			private void OpenStream(string FileName)
			{
				CloseStream();
				Directory.CreateDirectory(Path.GetDirectoryName(FileName));
				OutputStream = File.Open(FileName, FileMode.Create, FileAccess.Write, FileShare.None);
			}

			private void CloseStream()
			{
				if(OutputStream != null)
				{
					OutputStream.Dispose();
					OutputStream = null;
				}
			}

			public void HandleRecord(List<KeyValuePair<string, object>> Fields)
			{
				if(Fields[0].Key != "code")
				{
					throw new Exception("Missing code field");
				}

				string Value = (string)Fields[0].Value;
				if(Value == "stat")
				{
					string DepotFile = Fields.First(x => x.Key == "depotFile").Value.ToString() ?? String.Empty;

					string? LocalFile;
					if(!DepotFileToLocalFile.TryGetValue(DepotFile, out LocalFile))
					{
						throw new PerforceException("Depot file '{0}' not found in input dictionary", DepotFile);
					}

					OpenStream(LocalFile);
				}
				else if(Value == "binary" || Value == "text")
				{
					byte[] Data = (byte[])Fields.First(x => x.Key == "data").Value;
					OutputStream!.Write(Data, 0, Data.Length);
				}
				else
				{
					throw new Exception("Unexpected record type");
				}
			}
		}

		#endregion

		#region p4 reconcile

		/// <summary>
		/// Open files for add, delete, and/or edit in order to reconcile a workspace with changes made outside of Perforce.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<ReconcileRecord>> ReconcileAsync(int ChangeNumber, ReconcileOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			PerforceResponseList<ReconcileRecord> Records = await TryReconcileAsync(ChangeNumber, Options, FileSpecs, CancellationToken);
			Records.RemoveAll(x => x.Info != null);
			return Records.Data;
		}

		/// <summary>
		/// Open files for add, delete, and/or edit in order to reconcile a workspace with changes made outside of Perforce.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<ReconcileRecord>> TryReconcileAsync(int ChangeNumber, ReconcileOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("reconcile");
			if(ChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			if((Options & ReconcileOptions.Edit) != 0)
			{
				Arguments.Append(" -e");
			}
			if((Options & ReconcileOptions.Add) != 0)
			{
				Arguments.Append(" -a");
			}
			if((Options & ReconcileOptions.Delete) != 0)
			{
				Arguments.Append(" -d");
			}
			if((Options & ReconcileOptions.PreviewOnly) != 0)
			{
				Arguments.Append(" -n");
			}
			if((Options & ReconcileOptions.AllowWildcards) != 0)
			{
				Arguments.Append(" -f");
			}
			if((Options & ReconcileOptions.NoIgnore) != 0)
			{
				Arguments.Append(" -I");
			}
			if((Options & ReconcileOptions.LocalFileSyntax) != 0)
			{
				Arguments.Append(" -l");
			}
			foreach(string FileSpec in FileSpecs)
			{
				Arguments.AppendFormat(" \"{0}\"", FileSpec);
			}

			return CommandAsync<ReconcileRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 reopen

		/// <summary>
		/// Reopen a file
		/// </summary>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="FileType">New filetype</param>
		/// <param name="FileSpec">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<ReopenRecord>> ReopenAsync(int? ChangeNumber, string? FileType, string FileSpec, CancellationToken CancellationToken)
		{
			return (await TryReopenAsync(ChangeNumber, FileType, FileSpec, CancellationToken)).Data;
		}

		/// <summary>
		/// Reopen a file
		/// </summary>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="FileType">New filetype</param>
		/// <param name="FileSpec">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<ReopenRecord>> TryReopenAsync(int? ChangeNumber, string? FileType, string FileSpec, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("reopen");
			if (ChangeNumber != null)
			{
				if (ChangeNumber == PerforceReflection.DefaultChange)
				{
					Arguments.Append(" -c default");
				}
				else
				{
					Arguments.AppendFormat(" -c {0}", ChangeNumber);
				}
			}
			if (FileType != null)
			{
				Arguments.AppendFormat(" -t \"{0}\"", FileType);
			}
			Arguments.AppendFormat(" \"{0}\"", FileSpec);

			return CommandAsync<ReopenRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 reload

		/// <summary>
		/// Reloads a client workspace
		/// </summary>
		/// <param name="ClientName">Name of the client to reload</param>
		/// <param name="SourceServerId">The source server id</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<List<PerforceResponse>> ReloadClient(string ClientName, string SourceServerId, CancellationToken CancellationToken)
		{
			return CommandAsync($"reload -c \"{ClientName}\" -p \"{SourceServerId}\"", null, null, CancellationToken);
		}

		#endregion p4 reload

		#region p4 resolve

		/// <summary>
		/// Resolve conflicts between file revisions.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<ResolveRecord>> ResolveAsync(int ChangeNumber, ResolveOptions Options, string[]? FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryResolveAsync(ChangeNumber, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Resolve conflicts between file revisions.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<PerforceResponseList<ResolveRecord>> TryResolveAsync(int ChangeNumber, ResolveOptions Options, string[]? FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("resolve");
			if ((Options & ResolveOptions.Automatic) != 0)
			{
				Arguments.Append(" -am");
			}
			if ((Options & ResolveOptions.AcceptYours) != 0)
			{
				Arguments.Append(" -ay");
			}
			if ((Options & ResolveOptions.AcceptTheirs) != 0)
			{
				Arguments.Append(" -at");
			}
			if ((Options & ResolveOptions.SafeAccept) != 0)
			{
				Arguments.Append(" -as");
			}
			if ((Options & ResolveOptions.ForceAccept) != 0)
			{
				Arguments.Append(" -af");
			}
			if((Options & ResolveOptions.IgnoreWhitespaceOnly) != 0)
			{
				Arguments.Append(" -db");
			}
			if((Options & ResolveOptions.IgnoreWhitespace) != 0)
			{
				Arguments.Append(" -dw");
			}
			if((Options & ResolveOptions.IgnoreLineEndings) != 0)
			{
				Arguments.Append(" -dl");
			}
			if ((Options & ResolveOptions.ResolveAgain) != 0)
			{
				Arguments.Append(" -f");
			}
			if ((Options & ResolveOptions.PreviewOnly) != 0)
			{
				Arguments.Append(" -n");
			}
			if (FileSpecs != null)
			{
				foreach (string FileSpec in FileSpecs)
				{
					Arguments.AppendFormat(" \"{0}\"", FileSpec);
				}
			}

			PerforceResponseList<ResolveRecord> Records = await CommandAsync<ResolveRecord>(Arguments.ToString(), null, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		#endregion

		#region p4 revert

		/// <summary>
		/// Reverts files that have been added to a pending changelist.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="ClientName">Revert another user’s open files. </param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<RevertRecord>> RevertAsync(int ChangeNumber, string? ClientName, RevertOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryRevertAsync(ChangeNumber, ClientName, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Reverts files that have been added to a pending changelist.
		/// </summary>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="ClientName">Revert another user’s open files. </param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<PerforceResponseList<RevertRecord>> TryRevertAsync(int ChangeNumber, string? ClientName, RevertOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("revert");
			if((Options & RevertOptions.Unchanged) != 0)
			{
				Arguments.Append(" -a");
			}
			if((Options & RevertOptions.PreviewOnly) != 0)
			{
				Arguments.Append(" -n");
			}
			if((Options & RevertOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Append(" -k");
			}
			if((Options & RevertOptions.DeleteAddedFiles) != 0)
			{
				Arguments.Append(" -w");
			}
			if(ChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			if(ClientName != null)
			{
				Arguments.AppendFormat(" -C \"{0}\"", ClientName);
			}

			PerforceResponseList<RevertRecord> Records = await BatchedCommandAsync<RevertRecord>(Arguments.ToString(), FileSpecs, null, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		#endregion

		#region p4 set

		/// <summary>
		/// Sets an environment variable
		/// </summary>
		/// <param name="Name">Name of the variable to set</param>
		/// <param name="Value">Value for the variable</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task SetAsync(string Name, string Value, CancellationToken CancellationToken)
		{
			Tuple<bool, string> Result = await TrySetAsync(Name, Value, CancellationToken);
			if (!Result.Item1)
			{
				throw new PerforceException(Result.Item2);
			}
		}

		/// <summary>
		/// Sets an environment variable
		/// </summary>
		/// <param name="Name">Name of the variable to set</param>
		/// <param name="Value">Value for the variable</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<Tuple<bool, string>> TrySetAsync(string Name, string Value, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("set");
			Arguments.Append($" {Name}=");
			Arguments.Append($"\"{Value}\"");

			using (PerforceChildProcess ChildProcess = new PerforceChildProcess(null, Arguments.ToString(), Logger))
			{
				return await ChildProcess.TryReadToEndAsync(CancellationToken);
			}
		}

		/// <summary>
		/// Gets the setting of a Perforce variable
		/// </summary>
		/// <param name="Name">Name of the variable to get</param>
		/// <param name="CancellationToken">Cancellation token for the request</param>
		/// <returns>Value of the variable</returns>
		public async Task<string?> TryGetSettingAsync(string Name, CancellationToken CancellationToken)
		{
			using (PerforceChildProcess ChildProcess = new PerforceChildProcess(null, $"set {Name}=", Logger))
			{
				Tuple<bool, string> Response = await ChildProcess.TryReadToEndAsync(CancellationToken);
				if (Response.Item1)
				{
					string TrimResponse = Response.Item2.Trim();
					if (TrimResponse.Length == 0)
					{
						return null;
					}

					string Prefix = Name + "=";
					if (TrimResponse.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase))
					{
						return TrimResponse.Substring(Prefix.Length);
					}
				}

				Logger.LogDebug($"Unable to get '{Name}' variable: {Response.Item2}");
				return null;
			}
		}

		#endregion

		#region p4 shelve

		/// <summary>
		/// Shelves a set of files
		/// </summary>
		/// <param name="ChangeNumber">The change number to receive the shelved files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<ShelveRecord>> ShelveAsync(int ChangeNumber, ShelveOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryShelveAsync(ChangeNumber, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Shelves a set of files
		/// </summary>
		/// <param name="ChangeNumber">The change number to receive the shelved files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<PerforceResponseList<ShelveRecord>> TryShelveAsync(int ChangeNumber, ShelveOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("shelve");
			Arguments.AppendFormat(" -c {0}", ChangeNumber);
			if((Options & ShelveOptions.OnlyChanged) != 0)
			{
				Arguments.Append(" -a leaveunchanged");
			}
			if((Options & ShelveOptions.Overwrite) != 0)
			{
				Arguments.Append(" -f");
			}
			foreach(string FileSpec in FileSpecs)
			{
				Arguments.AppendFormat(" \"{0}\"", FileSpec);
			}

			PerforceResponseList<ShelveRecord> Records = await CommandAsync<ShelveRecord>(Arguments.ToString(), null, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		/// <summary>
		/// Deletes files from a shelved changelist
		/// </summary>
		/// <param name="ChangeNumber">Changelist containing shelved files to be deleted</param>
		/// <param name="FileSpecs">Files to delete</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task DeleteShelvedFilesAsync(int ChangeNumber, string[] FileSpecs, CancellationToken CancellationToken)
		{
			List<PerforceResponse> Responses = await TryDeleteShelvedFilesAsync(ChangeNumber, FileSpecs, CancellationToken);

			PerforceResponse? ErrorResponse = Responses.FirstOrDefault(x => x.Error != null && x.Error.Generic != PerforceGenericCode.Empty);
			if(ErrorResponse != null)
			{
				throw new PerforceException(ErrorResponse.Error!);
			}
		}

		/// <summary>
		/// Deletes files from a shelved changelist
		/// </summary>
		/// <param name="ChangeNumber">Changelist containing shelved files to be deleted</param>
		/// <param name="FileSpecs">Files to delete</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<List<PerforceResponse>> TryDeleteShelvedFilesAsync(int ChangeNumber, string[] FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("shelve -d");
			if(ChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", ChangeNumber);
			}
			foreach (string FileSpec in FileSpecs)
			{
				Arguments.AppendFormat(" \"{0}\"", FileSpec);
			}

			return CommandAsync(Arguments.ToString(), null, null, CancellationToken);
		}

		#endregion

		#region p4 streams

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="StreamPath">The path for streams to enumerate (eg. "//UE4/...")</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of streams matching the given criteria</returns>
		public async Task<List<StreamsRecord>> GetStreamsAsync(string StreamPath, CancellationToken CancellationToken)
		{
			return (await TryGetStreamsAsync(StreamPath, CancellationToken)).Data;
		}

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="StreamPath">The path for streams to enumerate (eg. "//UE4/...")</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of streams matching the given criteria</returns>
		public Task<PerforceResponseList<StreamsRecord>> TryGetStreamsAsync(string StreamPath, CancellationToken CancellationToken)
		{
			return TryGetStreamsAsync(StreamPath, -1, null, false, CancellationToken);
		}

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="StreamPath">The path for streams to enumerate (eg. "//UE4/...")</param>
		/// <param name="MaxResults">Maximum number of results to return</param>
		/// <param name="Filter">Additional filter to be applied to the results</param>
		/// <param name="bUnloaded">Whether to enumerate unloaded workspaces</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of streams matching the given criteria</returns>
		public async Task<List<StreamsRecord>> GetStreamsAsync(string StreamPath, int MaxResults, string? Filter, bool bUnloaded, CancellationToken CancellationToken)
		{
			return (await TryGetStreamsAsync(StreamPath, MaxResults, Filter, bUnloaded, CancellationToken)).Data;
		}

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="StreamPath">The path for streams to enumerate (eg. "//UE4/...")</param>
		/// <param name="MaxResults">Maximum number of results to return</param>
		/// <param name="Filter">Additional filter to be applied to the results</param>
		/// <param name="bUnloaded">Whether to enumerate unloaded workspaces</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of streams matching the given criteria</returns>
		public Task<PerforceResponseList<StreamsRecord>> TryGetStreamsAsync(string StreamPath, int MaxResults, string? Filter, bool bUnloaded, CancellationToken CancellationToken)
		{
			// Build the command line
			StringBuilder Arguments = new StringBuilder("streams");
			if (bUnloaded)
			{
				Arguments.Append(" -U");
			}
			if (Filter != null)
			{
				Arguments.AppendFormat("-F \"{0}\"", Filter);
			}
			if (MaxResults > 0)
			{
				Arguments.AppendFormat("-m {0}", MaxResults);
			}
			Arguments.AppendFormat(" \"{0}\"", StreamPath);

			// Execute the command
			return CommandAsync<StreamsRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 submit

		/// <summary>
		/// Submits a pending changelist
		/// </summary>
		/// <param name="ChangeNumber">The changelist to submit</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<SubmitRecord> SubmitAsync(int ChangeNumber, SubmitOptions Options, CancellationToken CancellationToken)
		{
			return (await TrySubmitAsync(ChangeNumber, Options, CancellationToken)).Data;
		}

		/// <summary>
		/// Submits a pending changelist
		/// </summary>
		/// <param name="ChangeNumber">The changelist to submit</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<PerforceResponse<SubmitRecord>> TrySubmitAsync(int ChangeNumber, SubmitOptions Options, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("submit");
			if((Options & SubmitOptions.ReopenAsEdit) != 0)
			{
				Arguments.Append(" -r");
			}
			Arguments.AppendFormat(" -c {0}", ChangeNumber);

			return (await CommandAsync<SubmitRecord>(Arguments.ToString(), null, CancellationToken))[0];
		}

		/// <summary>
		/// Submits a shelved changelist
		/// </summary>
		/// <param name="ChangeNumber">The changelist to submit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<SubmitRecord> SubmitShelvedAsync(int ChangeNumber, CancellationToken CancellationToken)
		{
			return (await TrySubmitShelvedAsync(ChangeNumber, CancellationToken)).Data;
		}

		/// <summary>
		/// Submits a pending changelist
		/// </summary>
		/// <param name="ChangeNumber">The changelist to submit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<PerforceResponse<SubmitRecord>> TrySubmitShelvedAsync(int ChangeNumber, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("submit");
			Arguments.AppendFormat(" -e {0}", ChangeNumber);

			return (await CommandAsync<SubmitRecord>(Arguments.ToString(), null, CancellationToken))[0];
		}

		#endregion

		#region p4 sync

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<SyncRecord>> SyncAsync(string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TrySyncAsync(FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<SyncRecord>> TrySyncAsync(string[] FileSpecs, CancellationToken CancellationToken)
		{
			return TrySyncAsync(SyncOptions.None, -1, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified.</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<SyncRecord>> SyncAsync(SyncOptions Options, int MaxFiles, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TrySyncAsync(Options, MaxFiles, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified.</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<SyncRecord>> TrySyncAsync(SyncOptions Options, int MaxFiles, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return TrySyncAsync(Options, -1, -1, -1, -1, -1, -1, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified</param>
		/// <param name="NumThreads">Sync in parallel using the given number of threads</param>
		/// <param name="Batch">The number of files in a batch</param>
		/// <param name="BatchSize">The number of bytes in a batch</param>
		/// <param name="Min">Minimum number of files in a parallel sync</param>
		/// <param name="MinSize">Minimum number of bytes in a parallel sync</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<SyncRecord>> SyncAsync(SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TrySyncAsync(Options, MaxFiles, NumThreads, Batch, BatchSize, Min, MinSize, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified</param>
		/// <param name="NumThreads">Sync in parallel using the given number of threads</param>
		/// <param name="Batch">The number of files in a batch</param>
		/// <param name="BatchSize">The number of bytes in a batch</param>
		/// <param name="Min">Minimum number of files in a parallel sync</param>
		/// <param name="MinSize">Minimum number of bytes in a parallel sync</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<PerforceResponseList<SyncRecord>> TrySyncAsync(SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, string[] FileSpecs, CancellationToken CancellationToken)
		{
			// Perforce annoyingly returns 'up-to-date' as an error. Ignore it.
			PerforceResponseList<SyncRecord> Records = await SyncInternalAsync<SyncRecord>(Options, MaxFiles, NumThreads, Batch, BatchSize, Min, MinSize, FileSpecs, false, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified.</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<SyncSummaryRecord>> SyncQuietAsync(SyncOptions Options, int MaxFiles, string[] FileSpecs, CancellationToken CancellationToken)
		{
			PerforceResponseList<SyncSummaryRecord> Records = await TrySyncQuietAsync(Options, MaxFiles, FileSpecs, CancellationToken);
			Records.RemoveAll(x => (x.Error != null && x.Error.Generic == PerforceGenericCode.Empty) || x.Info != null);
			return Records.Data;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified.</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<SyncSummaryRecord>> TrySyncQuietAsync(SyncOptions Options, int MaxFiles, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return TrySyncQuietAsync(Options, MaxFiles, -1, -1, -1, -1, -1, FileSpecs, null, CancellationToken);
		}

		/// <summary>
		/// Syncs files from the server without returning detailed file info
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified</param>
		/// <param name="NumThreads">Sync in parallel using the given number of threads</param>
		/// <param name="Batch">The number of files in a batch</param>
		/// <param name="BatchSize">The number of bytes in a batch</param>
		/// <param name="Min">Minimum number of files in a parallel sync</param>
		/// <param name="MinSize">Minimum number of bytes in a parallel sync</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="ResponseFile">Response file for list of files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<SyncSummaryRecord>> SyncQuietAsync(SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, string[] FileSpecs, FileReference? ResponseFile, CancellationToken CancellationToken)
		{
			return (await TrySyncQuietAsync(Options, MaxFiles, NumThreads, Batch, BatchSize, Min, MinSize, FileSpecs, ResponseFile, CancellationToken)).Data;
		}

		/// <summary>
		/// Syncs files from the server without returning detailed file info
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified</param>
		/// <param name="NumThreads">Sync in parallel using the given number of threads</param>
		/// <param name="Batch">The number of files in a batch</param>
		/// <param name="BatchSize">The number of bytes in a batch</param>
		/// <param name="Min">Minimum number of files in a parallel sync</param>
		/// <param name="MinSize">Minimum number of bytes in a parallel sync</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="ResponseFile">Response file for list of files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<SyncSummaryRecord>> TrySyncQuietAsync(SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, string[] FileSpecs, FileReference? ResponseFile, CancellationToken CancellationToken)
		{
			return SyncInternalAsync<SyncSummaryRecord>(Options, MaxFiles, NumThreads, Batch, BatchSize, Min, MinSize, FileSpecs, true, CancellationToken);
		}

		/// <summary>
		/// Gets arguments for a sync command
		/// </summary>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified</param>
		/// <param name="NumThreads">Sync in parallel using the given number of threads</param>
		/// <param name="Batch">The number of files in a batch</param>
		/// <param name="BatchSize">The number of bytes in a batch</param>
		/// <param name="Min">Minimum number of files in a parallel sync</param>
		/// <param name="MinSize">Minimum number of bytes in a parallel sync</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="bQuiet">Whether to use quiet output</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Arguments for the command</returns>
		private async Task<PerforceResponseList<T>> SyncInternalAsync<T>(SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, string[] FileSpecs, bool bQuiet, CancellationToken CancellationToken) where T : class
		{
			StringBuilder Arguments = new StringBuilder("sync");
			if((Options & SyncOptions.Force) != 0)
			{
				Arguments.Append(" -f");
			}
			if((Options & SyncOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Append(" -k");
			}
			if((Options & SyncOptions.FullDepotSyntax) != 0)
			{
				Arguments.Append(" -L");
			}
			if((Options & SyncOptions.PreviewOnly) != 0)
			{
				Arguments.Append(" -n");
			}
			if((Options & SyncOptions.NetworkPreviewOnly) != 0)
			{
				Arguments.Append(" -N");
			}
			if((Options & SyncOptions.DoNotUpdateHaveList) != 0)
			{
				Arguments.Append(" -p");
			}
			if(bQuiet)
			{
				Arguments.Append(" -q");
			}
			if((Options & SyncOptions.ReopenMovedFiles) != 0)
			{
				Arguments.Append(" -r");
			}
			if((Options & SyncOptions.Safe) != 0)
			{
				Arguments.Append(" -s");
			}
			if(MaxFiles != -1)
			{
				Arguments.AppendFormat(" -m {0}", MaxFiles);
			}
			/*			if(NumThreads != -1)
						{
							Arguments.AppendFormat(" --parallel-threads={0}", NumThreads);
							if(Batch != -1)
							{
								Arguments.AppendFormat(",batch={0}", Batch);
							}
							if(BatchSize != -1)
							{
								Arguments.AppendFormat(",batchsize={0}", BatchSize);
							}
							if(Min != -1)
							{
								Arguments.AppendFormat(",min={0}", Min);
							}
							if(MinSize != -1)
							{
								Arguments.AppendFormat(",minsize={0}", MinSize);
							}
						}*/
			if (FileSpecs.Length < 10)
			{
				foreach (string FileSpec in FileSpecs)
				{
					Arguments.AppendFormat(" \"{0}\"", FileSpec);
				}
				return await CommandAsync<T>(Arguments.ToString(), null, CancellationToken);
			}
			else
			{
				string TempFileName = Path.GetTempFileName();
				try
				{
					Arguments.Insert(0, $"-x\"{TempFileName}\" ");
					return await CommandAsync<T>(Arguments.ToString(), null, CancellationToken);
				}
				finally
				{
					File.Delete(TempFileName);
				}
			}
		}

		#endregion

		#region p4 unshelve

		/// <summary>
		/// Restore shelved files from a pending change into a workspace
		/// </summary>
		/// <param name="ChangeNumber">The changelist containing shelved files</param>
		/// <param name="IntoChangeNumber">The changelist to receive the unshelved files</param>
		/// <param name="UsingBranchSpec">The branchspec to use when unshelving files</param>
		/// <param name="UsingStream">Specifies the use of a stream-derived branch view to map the shelved files between the specified stream and its parent stream.</param>
		/// <param name="ForceParentStream">Unshelve to the specified parent stream. Overrides the parent defined in the source stream specification.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to unshelve</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<List<UnshelveRecord>> UnshelveAsync(int ChangeNumber, int IntoChangeNumber, string UsingBranchSpec, string UsingStream, string ForceParentStream, UnshelveOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			return (await TryUnshelveAsync(ChangeNumber, IntoChangeNumber, UsingBranchSpec, UsingStream, ForceParentStream, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Restore shelved files from a pending change into a workspace
		/// </summary>
		/// <param name="ChangeNumber">The changelist containing shelved files</param>
		/// <param name="IntoChangeNumber">The changelist to receive the unshelved files</param>
		/// <param name="UsingBranchSpec">The branchspec to use when unshelving files</param>
		/// <param name="UsingStream">Specifies the use of a stream-derived branch view to map the shelved files between the specified stream and its parent stream.</param>
		/// <param name="ForceParentStream">Unshelve to the specified parent stream. Overrides the parent defined in the source stream specification.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to unshelve</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<UnshelveRecord>> TryUnshelveAsync(int ChangeNumber, int IntoChangeNumber, string UsingBranchSpec, string UsingStream, string ForceParentStream, UnshelveOptions Options, string[] FileSpecs, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("unshelve");
			Arguments.AppendFormat(" -s {0}", ChangeNumber);
			if((Options & UnshelveOptions.ForceOverwrite) != 0)
			{
				Arguments.Append(" -f");
			}
			if((Options & UnshelveOptions.PreviewOnly) != 0)
			{
				Arguments.Append(" -n");
			}
			if(IntoChangeNumber != -1)
			{
				Arguments.AppendFormat(" -c {0}", IntoChangeNumber);
			}
			if(UsingBranchSpec != null)
			{
				Arguments.AppendFormat(" -b \"{0}\"", UsingBranchSpec);
			}
			if(UsingStream != null)
			{
				Arguments.AppendFormat(" -S \"{0}\"", UsingStream);
			}
			if(ForceParentStream != null)
			{
				Arguments.AppendFormat(" -P \"{0}\"", ForceParentStream);
			}
			foreach(string FileSpec in FileSpecs)
			{
				Arguments.AppendFormat(" \"{0}\"", FileSpec);
			}

			return CommandAsync<UnshelveRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 user

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="UserName">Name of the user to fetch information for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task<UserRecord> GetUserAsync(string UserName, CancellationToken CancellationToken)
		{
			return (await TryGetUserAsync(UserName, CancellationToken))[0].Data;
		}

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="UserName">Name of the user to fetch information for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public Task<PerforceResponseList<UserRecord>> TryGetUserAsync(string UserName, CancellationToken CancellationToken)
		{
			StringBuilder Arguments = new StringBuilder("user");
			Arguments.AppendFormat(" -o \"{0}\"", UserName);
			return CommandAsync<UserRecord>(Arguments.ToString(), null, CancellationToken);
		}

		#endregion

		#region p4 where

		/// <summary>
		/// Retrieves the location of a file of set of files in the workspace
		/// </summary>
		/// <param name="FileSpecs">Patterns for the files to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<List<WhereRecord>> WhereAsync(string[] FileSpecs, CancellationToken CancellationToken)
		{
			List<WhereRecord> Records = (await TryWhereAsync(FileSpecs, CancellationToken)).Data;
			if (Records.Count != FileSpecs.Length)
			{
				throw new PerforceException($"Unexpected response count; expected {FileSpecs.Length}, got {Records.Count}.");
			}
			return Records;
		}

		/// <summary>
		/// Retrieves the location of a file of set of files in the workspace
		/// </summary>
		/// <param name="FileSpecs">Patterns for the files to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public async Task<PerforceResponseList<WhereRecord>> TryWhereAsync(string[] FileSpecs, CancellationToken CancellationToken)
		{
			PerforceResponseList<WhereRecord> Responses = new PerforceResponseList<WhereRecord>();
			for (int Idx = 0; Idx < FileSpecs.Length; )
			{
				StringBuilder Arguments = new StringBuilder("where");
				for (; Arguments.Length < 2048 && Idx < FileSpecs.Length; Idx++)
				{
					Arguments.AppendFormat(" \"{0}\"", FileSpecs[Idx]);
				}
				Responses.AddRange(await CommandAsync<WhereRecord>(Arguments.ToString(), null, CancellationToken));
			}
			return Responses;
		}

		#endregion
	}
}
