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
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Stores settings for communicating with a Perforce server.
	/// </summary>
	public class PerforceConnection : IPerforceConnection
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
		/// <param name="AppName"></param>
		/// <param name="AppVersion"></param>
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
		public PerforceConnection(PerforceConnection Other)
			: this(Other.ServerAndPort, Other.UserName, Other.ClientName, Other.AppName, Other.AppVersion, Other.Logger)
		{
			GlobalOptions.AddRange(Other.GlobalOptions);
		}

		List<string> GetGlobalArguments()
		{
			List<string> Arguments = new List<string>();
			if (ServerAndPort != null)
			{
				Arguments.Add($"-p{ServerAndPort}");
			}
			if (UserName != null)
			{
				Arguments.Add($"-u{UserName}");
			}
			if (ClientName != null)
			{
				Arguments.Add($"-c{ClientName}");
			}
			if (AppName != null)
			{
				Arguments.Add($"-zprog={AppName}");
			}
			if (AppVersion != null)
			{
				Arguments.Add($"-zversion={AppVersion}");
			}
			Arguments.AddRange(GlobalOptions);
			return Arguments;
		}

		/// <inheritdoc/>
		public Task<IPerforceOutput> CommandAsync(string Command, IReadOnlyList<string> Arguments, IReadOnlyList<string>? FileArguments, byte[]? InputData)
		{
			return Task.FromResult<IPerforceOutput>(new PerforceChildProcess(Command, Arguments, FileArguments, InputData, GetGlobalArguments(), Logger));
		}

		#region p4 login

		/// <summary>
		/// Execute the 'login' command
		/// </summary>
		/// <param name="Password">Password to use to login</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task LoginAsync(string Password, CancellationToken CancellationToken = default)
		{
			// Some versions of P4.EXE do not support marshalled output for P4 login calls, so we only support this as a basic text query.
			byte[] PasswordBytes = Encoding.UTF8.GetBytes(Password);
			using (ManagedProcessGroup ChildProcessGroup = new ManagedProcessGroup())
			{
				string Executable = PerforceChildProcess.GetExecutable();

				List<string> Arguments = GetGlobalArguments();
				Arguments.Add("login");

				string FullArgumentList = CommandLineArguments.Join(Arguments);

				Logger.LogDebug("Running {0} {1}", Executable, FullArgumentList);
				using (ManagedProcess ChildProcess = new ManagedProcess(ChildProcessGroup, Executable, FullArgumentList, null, null, PasswordBytes, ProcessPriorityClass.Normal))
				{
					StringBuilder Lines = new StringBuilder("Unable to log in: ");
					for (; ; )
					{
						string? Line = await ChildProcess.ReadLineAsync();
						if (Line == null)
						{
							break;
						}

						Lines.Append($"\n  {Line}");

						if (Line.StartsWith("Enter password:", StringComparison.OrdinalIgnoreCase))
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

		#region p4 set

		/// <summary>
		/// Sets an environment variable
		/// </summary>
		/// <param name="Name">Name of the variable to set</param>
		/// <param name="Value">Value for the variable</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public async Task SetAsync(string Name, string Value, CancellationToken CancellationToken = default)
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
		public async Task<Tuple<bool, string>> TrySetAsync(string Name, string Value, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			Arguments.Append($"{Name}={Value}");

			using (PerforceChildProcess ChildProcess = new PerforceChildProcess("set", Arguments, null, null, GetGlobalArguments(), Logger))
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
		public async Task<string?> TryGetSettingAsync(string Name, CancellationToken CancellationToken = default)
		{
			using (PerforceChildProcess ChildProcess = new PerforceChildProcess("set", new List<string> { $"{Name}=" }, null, null, GetGlobalArguments(), Logger))
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
	}

	/// <summary>
	/// Extension methods for <see cref="PerforceConnection"/>
	/// </summary>
	public static class PerforceConnectionExtensions
	{
		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="Perforce">The Perforce connection</param>
		/// <param name="Command">Command to execute</param>
		/// <param name="Arguments">Arguments for the command</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="StatRecordType">The type of records to return for "stat" responses</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public static Task<List<PerforceResponse>> CommandAsync(this IPerforceConnection Perforce, string Command, IReadOnlyList<string> Arguments, byte[]? InputData, Type? StatRecordType, CancellationToken CancellationToken = default)
		{
			return CommandAsync(Perforce, Command, Arguments, null, InputData, StatRecordType, CancellationToken);
		}

		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="Perforce">The Perforce connection</param>
		/// <param name="Command">Command to execute</param>
		/// <param name="Arguments">Arguments for the command</param>
		/// <param name="FileArguments">File arguments for the command</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="StatRecordType">The type of records to return for "stat" responses</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public static async Task<List<PerforceResponse>> CommandAsync(this IPerforceConnection Perforce, string Command, IReadOnlyList<string> Arguments, IReadOnlyList<string>? FileArguments, byte[]? InputData, Type? StatRecordType, CancellationToken CancellationToken = default)
		{
			await using (IPerforceOutput Response = await Perforce.CommandAsync(Command, Arguments, FileArguments, InputData))
			{
				return await Response.ReadResponsesAsync(StatRecordType, CancellationToken);
			}
		}

		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="Perforce">The Perforce connection</param>
		/// <param name="Command">Command to execute</param>
		/// <param name="Arguments">Arguments for the command</param>
		/// <param name="FileArguments">File arguments for the command</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="StatRecordType">The type of records to return for "stat" responses</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public static async IAsyncEnumerable<PerforceResponse> StreamCommandAsync(this IPerforceConnection Perforce, string Command, IReadOnlyList<string> Arguments, IReadOnlyList<string>? FileArguments, byte[]? InputData, Type? StatRecordType, [EnumeratorCancellation] CancellationToken CancellationToken)
		{
			await using (IPerforceOutput Output = await Perforce.CommandAsync(Command, Arguments, FileArguments, InputData))
			{
				await foreach (PerforceResponse Response in Output.ReadStreamingResponsesAsync(StatRecordType, CancellationToken))
				{
					yield return Response;
				}
			}
		}

		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="Perforce">The Perforce connection</param>
		/// <param name="Command">Command to execute</param>
		/// <param name="Arguments">Arguments for the command</param>
		/// <param name="FileArguments">File arguments for the command</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public static async IAsyncEnumerable<PerforceResponse<T>> StreamCommandAsync<T>(this IPerforceConnection Perforce, string Command, IReadOnlyList<string> Arguments, IReadOnlyList<string>? FileArguments = null, byte[]? InputData = null, [EnumeratorCancellation] CancellationToken CancellationToken = default) where T : class
		{
			await using (IPerforceOutput Output = await Perforce.CommandAsync(Command, Arguments, FileArguments, InputData))
			{
				Type StatRecordType = typeof(T);
				await foreach (PerforceResponse Response in Output.ReadStreamingResponsesAsync(StatRecordType, CancellationToken))
				{
					yield return new PerforceResponse<T>(Response);
				}
			}
		}

		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="Perforce">The Perforce connection</param>
		/// <param name="Command">Command to execute</param>
		/// <param name="Arguments">Arguments for the command</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="HandleRecord">Delegate used to handle each record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public static async Task RecordCommandAsync(this IPerforceConnection Perforce, string Command, IReadOnlyList<string> Arguments, byte[]? InputData, Action<PerforceRecord> HandleRecord, CancellationToken CancellationToken = default)
		{
			await using (IPerforceOutput Response = await Perforce.CommandAsync(Command, Arguments, null, InputData))
			{
				await Response.ReadRecordsAsync(HandleRecord, CancellationToken);
			}
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
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Command">The command to execute</param>
		/// <param name="Arguments">Arguments for the command</param>
		/// <param name="FileArguments">Arguments which can be passed into a -x argument</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public static async Task<PerforceResponseList<T>> CommandAsync<T>(this IPerforceConnection Connection, string Command, IReadOnlyList<string> Arguments, IReadOnlyList<string>? FileArguments, byte[]? InputData, CancellationToken CancellationToken = default) where T : class
		{
			List<PerforceResponse> Responses = await Connection.CommandAsync(Command, Arguments, FileArguments, InputData, typeof(T), CancellationToken);

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
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Command">The command to execute</param>
		/// <param name="Arguments">Arguments for the command</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public static Task<PerforceResponseList<T>> CommandAsync<T>(this IPerforceConnection Connection, string Command, List<string> Arguments, byte[]? InputData, CancellationToken CancellationToken = default) where T : class
		{
			return CommandAsync<T>(Connection, Command, Arguments, null, InputData, CancellationToken);
		}

		/// <summary>
		/// Execute a command and parse the response
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Command">The command to execute</param>
		/// <param name="CommonArguments">Arguments for the command</param>
		/// <param name="BatchedArguments">Arguments to pass to the command in batches</param>
		/// <param name="InputData">Input data to pass to Perforce</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of objects returned by the server</returns>
		public static async Task<PerforceResponseList<T>> BatchedCommandAsync<T>(IPerforceConnection Connection, string Command, IReadOnlyList<string> CommonArguments, IReadOnlyList<string> BatchedArguments, byte[]? InputData, CancellationToken CancellationToken = default) where T : class
		{
			PerforceResponseList<T> Responses = new PerforceResponseList<T>();
			for (int FileSpecIdx = 0; FileSpecIdx < BatchedArguments.Count;)
			{
				List<string> Arguments = new List<string>();
				Arguments.AddRange(CommonArguments);

				const int PerArgumentExtra = 5;
				int Length = (Command.Length + PerArgumentExtra) + Arguments.Sum(x => x.Length + PerArgumentExtra);

				for (; FileSpecIdx < BatchedArguments.Count && Length < 4096; FileSpecIdx++)
				{
					Arguments.Add(BatchedArguments[FileSpecIdx]);
					Length += BatchedArguments[FileSpecIdx].Length + PerArgumentExtra;
				}
				Responses.AddRange(await CommandAsync<T>(Connection, Command, Arguments, null, CancellationToken));
			}
			return Responses;
		}

		/// <summary>
		/// Attempts to execute the given command, returning the results from the server or the first PerforceResponse object.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Command">The command to execute</param>
		/// <param name="Arguments">Arguments for the command.</param>
		/// <param name="InputData">Input data for the command.</param>
		/// <param name="StatRecordType">Type of element to return in the response</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either an object of type T or error.</returns>
		static async Task<PerforceResponse> SingleResponseCommandAsync(IPerforceConnection Connection, string Command, IReadOnlyList<string> Arguments, byte[]? InputData, Type? StatRecordType, CancellationToken CancellationToken = default)
		{
			List<PerforceResponse> Responses = await Connection.CommandAsync(Command, Arguments, InputData, StatRecordType, CancellationToken);
			if (Responses.Count != 1)
			{
				for (int Idx = 0; Idx < Responses.Count; Idx++)
				{
					Connection.Logger.LogDebug("Unexpected response {Idx}: {Text}", Idx, Responses[Idx].ToString());
				}
				throw new PerforceException("Expected one result from 'p4 {0}', got {1}", Command, Responses.Count);
			}
			return Responses[0];
		}

		/// <summary>
		/// Attempts to execute the given command, returning the results from the server or the first PerforceResponse object.
		/// </summary>
		/// <typeparam name="T">Type of record to parse</typeparam>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Command">The command to execute</param>
		/// <param name="Arguments">Arguments for the command.</param>
		/// <param name="InputData">Input data for the command.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either an object of type T or error.</returns>
		public static async Task<PerforceResponse<T>> SingleResponseCommandAsync<T>(IPerforceConnection Connection, string Command, List<string> Arguments, byte[]? InputData, CancellationToken CancellationToken = default) where T : class
		{
			return new PerforceResponse<T>(await SingleResponseCommandAsync(Connection, Command, Arguments, InputData, typeof(T), CancellationToken));
		}

		#endregion

		#region p4 add

		/// <summary>
		/// Adds files to a pending changelist.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileSpecList">Files to be added</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<AddRecord>> AddAsync(this IPerforceConnection Connection, int ChangeNumber, FileSpecList FileSpecList, CancellationToken CancellationToken = default)
		{
			return (await TryAddAsync(Connection, ChangeNumber, null, AddOptions.None, FileSpecList, CancellationToken)).Data;
		}

		/// <summary>
		/// Adds files to a pending changelist.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecList">Files to be added</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<AddRecord>> AddAsync(this IPerforceConnection Connection, int ChangeNumber, string? FileType, AddOptions Options, FileSpecList FileSpecList, CancellationToken CancellationToken = default)
		{
			return (await TryAddAsync(Connection, ChangeNumber, FileType, Options, FileSpecList, CancellationToken)).Data;
		}

		/// <summary>
		/// Adds files to a pending changelist.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileSpecList">Files to be added</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<AddRecord>> TryAddAsync(this IPerforceConnection Connection, int ChangeNumber, FileSpecList FileSpecList, CancellationToken CancellationToken = default)
		{
			return TryAddAsync(Connection, ChangeNumber, null, AddOptions.None, FileSpecList, CancellationToken);
		}

		/// <summary>
		/// Adds files to a pending changelist.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileNames">Files to be added</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<AddRecord>> TryAddAsync(this IPerforceConnection Connection, int ChangeNumber, string? FileType, AddOptions Options, FileSpecList FileNames, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if (ChangeNumber != -1)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}
			if ((Options & AddOptions.DowngradeToAdd) != 0)
			{
				Arguments.Add("-d");
			}
			if ((Options & AddOptions.IncludeWildcards) != 0)
			{
				Arguments.Add("-f");
			}
			if ((Options & AddOptions.NoIgnore) != 0)
			{
				Arguments.Add("-I");
			}
			if ((Options & AddOptions.PreviewOnly) != 0)
			{
				Arguments.Add("-n");
			}
			if (FileType != null)
			{
				Arguments.Add($"-t{FileType}");
			}

			return BatchedCommandAsync<AddRecord>(Connection, "add", Arguments, FileNames.List, null, CancellationToken);
		}

		#endregion

		#region p4 change

		/// <summary>
		/// Creates a changelist with the p4 change command. 
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Record">Information for the change to create. The number field should be left set to -1.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The changelist number, or an error.</returns>
		public static async Task<ChangeRecord> CreateChangeAsync(this IPerforceConnection Connection, ChangeRecord Record, CancellationToken CancellationToken = default)
		{
			return (await TryCreateChangeAsync(Connection, Record, CancellationToken)).Data;
		}

		/// <summary>
		/// Creates a changelist with the p4 change command. 
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Record">Information for the change to create. The number field should be left set to -1.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The changelist number, or an error.</returns>
		public static async Task<PerforceResponse<ChangeRecord>> TryCreateChangeAsync(this IPerforceConnection Connection, ChangeRecord Record, CancellationToken CancellationToken = default)
		{
			if (Record.Number != -1)
			{
				throw new PerforceException("'Number' field should be set to -1 to create a new changelist.");
			}

			PerforceResponse Response = await SingleResponseCommandAsync(Connection, "change", new List<string> { "-i" }, SerializeRecord(Record), null, CancellationToken);

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
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="Record">Information for the change to create. The number field should be left set to zero.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The changelist number, or an error.</returns>
		public static async Task UpdateChangeAsync(this IPerforceConnection Connection, UpdateChangeOptions Options, ChangeRecord Record, CancellationToken CancellationToken = default)
		{
			(await TryUpdateChangeAsync(Connection, Options, Record, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Updates an existing changelist.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="Record">Information for the change to create. The number field should be left set to zero.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The changelist number, or an error.</returns>
		public static Task<PerforceResponse> TryUpdateChangeAsync(this IPerforceConnection Connection, UpdateChangeOptions Options, ChangeRecord Record, CancellationToken CancellationToken = default)
		{
			if (Record.Number == -1)
			{
				throw new PerforceException("'Number' field must be set to update a changelist.");
			}

			List<string> Arguments = new List<string>();
			Arguments.Add("-i");
			if ((Options & UpdateChangeOptions.Force) != 0)
			{
				Arguments.Add("-f");
			}
			if ((Options & UpdateChangeOptions.Submitted) != 0)
			{
				Arguments.Add("-u");
			}

			return SingleResponseCommandAsync(Connection, "change", Arguments, SerializeRecord(Record), null, CancellationToken);
		}

		/// <summary>
		/// Deletes a changelist (p4 change -d)
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">Changelist number to delete</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task DeleteChangeAsync(this IPerforceConnection Connection, DeleteChangeOptions Options, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			(await TryDeleteChangeAsync(Connection, Options, ChangeNumber, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Deletes a changelist (p4 change -d)
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">Changelist number to delete</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponse> TryDeleteChangeAsync(this IPerforceConnection Connection, DeleteChangeOptions Options, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string> { "-d" };
			if ((Options & DeleteChangeOptions.Submitted) != 0)
			{
				Arguments.Add("-f");
			}
			if ((Options & DeleteChangeOptions.BeforeRenumber) != 0)
			{
				Arguments.Add("-O");
			}
			Arguments.Add($"{ChangeNumber}");

			return SingleResponseCommandAsync(Connection, "change", Arguments, null, null, CancellationToken);
		}

		/// <summary>
		/// Gets a changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">Changelist number to retrieve. -1 is the default changelist for this workspace.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<ChangeRecord> GetChangeAsync(this IPerforceConnection Connection, GetChangeOptions Options, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			return (await TryGetChange(Connection, Options, ChangeNumber, CancellationToken)).Data;
		}

		/// <summary>
		/// Gets a changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">Changelist number to retrieve. -1 is the default changelist for this workspace.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponse<ChangeRecord>> TryGetChange(this IPerforceConnection Connection, GetChangeOptions Options, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string> { "-o" };
			if ((Options & GetChangeOptions.BeforeRenumber) != 0)
			{
				Arguments.Add("-O");
			}
			if (ChangeNumber != -1)
			{
				Arguments.Add($"{ChangeNumber}");
			}

			return SingleResponseCommandAsync<ChangeRecord>(Connection, "change", Arguments, null, CancellationToken);
		}

		/// <summary>
		/// Serializes a change record to a byte array
		/// </summary>
		/// <param name="Input">The record to serialize</param>
		/// <returns>Serialized record</returns>
		static byte[] SerializeRecord(ChangeRecord Input)
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
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public static async Task<List<ChangesRecord>> GetChangesAsync(this IPerforceConnection Connection, ChangesOptions Options, int MaxChanges, ChangeStatus Status, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryGetChangesAsync(Connection, Options, MaxChanges, Status, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Enumerates changes on the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ClientName">List only changes made from the named client workspace.</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="UserName">List only changes made by the named user</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public static async Task<List<ChangesRecord>> GetChangesAsync(this IPerforceConnection Connection, ChangesOptions Options, string? ClientName, int MaxChanges, ChangeStatus Status, string? UserName, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryGetChangesAsync(Connection, Options, ClientName, MaxChanges, Status, UserName, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Enumerates changes on the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ClientName">List only changes made from the named client workspace.</param>
		/// <param name="MinChangeNumber">The minimum changelist number</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="UserName">List only changes made by the named user</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public static async Task<List<ChangesRecord>> GetChangesAsync(this IPerforceConnection Connection, ChangesOptions Options, string? ClientName, int MinChangeNumber, int MaxChanges, ChangeStatus Status, string? UserName, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryGetChangesAsync(Connection, Options, ClientName, MinChangeNumber, MaxChanges, Status, UserName, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Enumerates changes on the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public static Task<PerforceResponseList<ChangesRecord>> TryGetChangesAsync(this IPerforceConnection Connection, ChangesOptions Options, int MaxChanges, ChangeStatus Status, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TryGetChangesAsync(Connection, Options, null, MaxChanges, Status, null, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Enumerates changes on the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ClientName">List only changes made from the named client workspace.</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="UserName">List only changes made by the named user</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public static Task<PerforceResponseList<ChangesRecord>> TryGetChangesAsync(this IPerforceConnection Connection, ChangesOptions Options, string? ClientName, int MaxChanges, ChangeStatus Status, string? UserName, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TryGetChangesAsync(Connection, Options, ClientName, -1, MaxChanges, Status, UserName, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Enumerates changes on the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ClientName">List only changes made from the named client workspace.</param>
		/// <param name="MinChangeNumber">The minimum changelist number</param>
		/// <param name="MaxChanges">List only the highest numbered changes</param>
		/// <param name="Status">Limit the list to the changelists with the given status (pending, submitted or shelved)</param>
		/// <param name="UserName">List only changes made by the named user</param>
		/// <param name="FileSpecs">Paths to query changes for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server.</returns>
		public static Task<PerforceResponseList<ChangesRecord>> TryGetChangesAsync(this IPerforceConnection Connection, ChangesOptions Options, string? ClientName, int MinChangeNumber, int MaxChanges, ChangeStatus Status, string? UserName, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if ((Options & ChangesOptions.IncludeIntegrations) != 0)
			{
				Arguments.Add("-i");
			}
			if ((Options & ChangesOptions.IncludeTimes) != 0)
			{
				Arguments.Add("-t");
			}
			if ((Options & ChangesOptions.LongOutput) != 0)
			{
				Arguments.Add("-l");
			}
			if ((Options & ChangesOptions.TruncatedLongOutput) != 0)
			{
				Arguments.Add("-L");
			}
			if ((Options & ChangesOptions.IncludeRestricted) != 0)
			{
				Arguments.Add("-f");
			}
			if (ClientName != null)
			{
				Arguments.Add($"-c{ClientName}");
			}
			if (MinChangeNumber != -1)
			{
				Arguments.Add($"-e{MinChangeNumber}");
			}
			if (MaxChanges != -1)
			{
				Arguments.Add($"-m{MaxChanges}");
			}
			if (Status != ChangeStatus.All)
			{
				Arguments.Add($"-s{PerforceReflection.GetEnumText(typeof(ChangeStatus), Status)}");
			}
			if (UserName != null)
			{
				Arguments.Add($"-u{UserName}");
			}
			Arguments.AddRange(FileSpecs.List);

			return CommandAsync<ChangesRecord>(Connection, "changes", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 clean

		/// <summary>
		/// Cleans the workspace
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<CleanRecord>> CleanAsync(this IPerforceConnection Connection, CleanOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryCleanAsync(Connection, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Cleans the workspace
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<PerforceResponseList<CleanRecord>> TryCleanAsync(this IPerforceConnection Connection, CleanOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if ((Options & CleanOptions.Edited) != 0)
			{
				Arguments.Add("-e");
			}
			if ((Options & CleanOptions.Added) != 0)
			{
				Arguments.Add("-a");
			}
			if ((Options & CleanOptions.Deleted) != 0)
			{
				Arguments.Add("-d");
			}
			if ((Options & CleanOptions.Preview) != 0)
			{
				Arguments.Add("-n");
			}
			if ((Options & CleanOptions.NoIgnoreChecking) != 0)
			{
				Arguments.Add("-I");
			}
			if ((Options & CleanOptions.LocalSyntax) != 0)
			{
				Arguments.Add("-l");
			}
			if ((Options & CleanOptions.ModifiedTimes) != 0)
			{
				Arguments.Add("-m");
			}
			Arguments.AddRange(FileSpecs.List);

			PerforceResponseList<CleanRecord> Records = await CommandAsync<CleanRecord>(Connection, "clean", Arguments, null, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		#endregion

		#region p4 client

		/// <summary>
		/// Creates a client
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Record">The client record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task CreateClientAsync(this IPerforceConnection Connection, ClientRecord Record, CancellationToken CancellationToken = default)
		{
			(await TryCreateClientAsync(Connection, Record, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Creates a client
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Record">The client record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponse> TryCreateClientAsync(this IPerforceConnection Connection, ClientRecord Record, CancellationToken CancellationToken = default)
		{
			return TryUpdateClientAsync(Connection, Record, CancellationToken);
		}

		/// <summary>
		/// Creates a client
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Record">The client record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task UpdateClientAsync(this IPerforceConnection Connection, ClientRecord Record, CancellationToken CancellationToken = default)
		{
			(await TryUpdateClientAsync(Connection, Record, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Update a client
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Record">The client record</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponse> TryUpdateClientAsync(this IPerforceConnection Connection, ClientRecord Record, CancellationToken CancellationToken = default)
		{
			return SingleResponseCommandAsync(Connection, "client", new List<string> { "-i" }, SerializeRecord(Record), null, CancellationToken);
		}

		/// <summary>
		/// Deletes a client
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="ClientName">Name of the client</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task DeleteClientAsync(this IPerforceConnection Connection, DeleteClientOptions Options, string ClientName, CancellationToken CancellationToken = default)
		{
			(await TryDeleteClientAsync(Connection, Options, ClientName, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Deletes a client
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="ClientName">Name of the client</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponse> TryDeleteClientAsync(this IPerforceConnection Connection, DeleteClientOptions Options, string ClientName, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string> { "-d" };
			if ((Options & DeleteClientOptions.Force) != 0)
			{
				Arguments.Add("-f");
			}
			if ((Options & DeleteClientOptions.DeleteShelved) != 0)
			{
				Arguments.Add("-Fs");
			}
			Arguments.Add(ClientName);

			return SingleResponseCommandAsync(Connection, "client", Arguments, null, null, CancellationToken);
		}

		/// <summary>
		/// Changes the stream associated with a client
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ClientName">The client name</param>
		/// <param name="StreamName">The new stream to be associated with the client</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task SwitchClientToStreamAsync(this IPerforceConnection Connection, string ClientName, string StreamName, SwitchClientOptions Options, CancellationToken CancellationToken = default)
		{
			(await TrySwitchClientToStreamAsync(Connection, ClientName, StreamName, Options, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Changes the stream associated with a client
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ClientName">The client name</param>
		/// <param name="StreamName">The new stream to be associated with the client</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponse> TrySwitchClientToStreamAsync(this IPerforceConnection Connection, string ClientName, string StreamName, SwitchClientOptions Options, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string> { "-s" };
			if ((Options & SwitchClientOptions.IgnoreOpenFiles) != 0)
			{
				Arguments.Add("-f");
			}
			Arguments.Add($"-S{StreamName}");

			return SingleResponseCommandAsync(Connection, "client", Arguments, null, null, CancellationToken);
		}

		/// <summary>
		/// Changes a client to mirror a template
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ClientName">The client name</param>
		/// <param name="TemplateName">The new stream to be associated with the client</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task SwitchClientToTemplateAsync(this IPerforceConnection Connection, string ClientName, string TemplateName, CancellationToken CancellationToken = default)
		{
			(await TrySwitchClientToTemplateAsync(Connection, ClientName, TemplateName, CancellationToken)).EnsureSuccess();
		}

		/// <summary>
		/// Changes a client to mirror a template
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ClientName">The client name</param>
		/// <param name="TemplateName">The new stream to be associated with the client</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponse> TrySwitchClientToTemplateAsync(this IPerforceConnection Connection, string ClientName, string TemplateName, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			Arguments.Add("-s");
			Arguments.Add($"-t{TemplateName}");

			return SingleResponseCommandAsync(Connection, "client", Arguments, null, null, CancellationToken);
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ClientName">Name of the client. Specify null for the current client.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public static async Task<ClientRecord> GetClientAsync(this IPerforceConnection Connection, string? ClientName, CancellationToken CancellationToken = default)
		{
			return (await TryGetClientAsync(Connection, ClientName, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ClientName">Name of the client. Specify null for the current client.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public static Task<PerforceResponse<ClientRecord>> TryGetClientAsync(this IPerforceConnection Connection, string? ClientName, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string> { "-o" };
			if (ClientName != null)
			{
				Arguments.Add(ClientName);
			}
			return SingleResponseCommandAsync<ClientRecord>(Connection, "client", Arguments, null, CancellationToken);
		}

		/// <summary>
		/// Queries information about a stream
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="StreamName">Name of the stream to query</param>
		/// <param name="bIncludeView">Whether to include the stream view in the output</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Stream information record</returns>
		public static async Task<StreamRecord> GetStreamAsync(this IPerforceConnection Connection, string StreamName, bool bIncludeView, CancellationToken CancellationToken = default)
		{
			return (await TryGetStreamAsync(Connection, StreamName, bIncludeView, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries information about a stream
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="StreamName">Name of the stream to query</param>
		/// <param name="bIncludeView">Whether to include the stream view in the output</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Stream information record</returns>
		public static Task<PerforceResponse<StreamRecord>> TryGetStreamAsync(this IPerforceConnection Connection, string StreamName, bool bIncludeView, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string> { "-o" };
			if (bIncludeView)
			{
				Arguments.Add("-v");
			}
			Arguments.Add(StreamName);

			return SingleResponseCommandAsync<StreamRecord>(Connection, "stream", Arguments, null, CancellationToken);
		}

		/// <summary>
		/// Queries the view for a stream
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="StreamName">Name of the stream.</param>
		/// <param name="ChangeNumber">Changelist at which to query the stream view</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public static async Task<ClientRecord> GetStreamViewAsync(this IPerforceConnection Connection, string StreamName, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			return (await TryGetStreamViewAsync(Connection, StreamName, ChangeNumber, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries the view for a stream
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="StreamName">Name of the stream.</param>
		/// <param name="ChangeNumber">Changelist at which to query the stream view</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public static Task<PerforceResponse<ClientRecord>> TryGetStreamViewAsync(this IPerforceConnection Connection, string StreamName, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string> { "-o" };
			Arguments.Add($"-S{StreamName}");
			if (ChangeNumber != -1)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}

			return SingleResponseCommandAsync<ClientRecord>(Connection, "client", Arguments, null, CancellationToken);
		}

		/// <summary>
		/// Serializes a client record to a byte array
		/// </summary>
		/// <param name="Input">The input record</param>
		/// <returns>Serialized record data</returns>
		static byte[] SerializeRecord(ClientRecord Input)
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
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="UserName">List only client workspaces owned by this user.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public static async Task<List<ClientsRecord>> GetClientsAsync(this IPerforceConnection Connection, ClientsOptions Options, string? UserName, CancellationToken CancellationToken = default)
		{
			return (await TryGetClientsAsync(Connection, Options, UserName, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="UserName">List only client workspaces owned by this user.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public static Task<PerforceResponseList<ClientsRecord>> TryGetClientsAsync(this IPerforceConnection Connection, ClientsOptions Options, string? UserName, CancellationToken CancellationToken = default)
		{
			return TryGetClientsAsync(Connection, Options, null, -1, null, UserName, CancellationToken);
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="Filter">List only client workspaces matching filter. Treated as case sensitive if <ref>ClientsOptions.CaseSensitiveFilter</ref> is set.</param>
		/// <param name="MaxResults">Limit the number of results to return. -1 for all.</param>
		/// <param name="Stream">List client workspaces associated with the specified stream.</param>
		/// <param name="UserName">List only client workspaces owned by this user.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public static async Task<List<ClientsRecord>> GetClientsAsync(this IPerforceConnection Connection, ClientsOptions Options, string? Filter, int MaxResults, string? Stream, string? UserName, CancellationToken CancellationToken = default)
		{
			return (await TryGetClientsAsync(Connection, Options, Filter, MaxResults, Stream, UserName, CancellationToken)).Data;
		}

		/// <summary>
		/// Queries the current client definition
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for this command</param>
		/// <param name="Filter">List only client workspaces matching filter. Treated as case sensitive if <ref>ClientsOptions.CaseSensitiveFilter</ref> is set.</param>
		/// <param name="MaxResults">Limit the number of results to return. -1 for all.</param>
		/// <param name="Stream">List client workspaces associated with the specified stream.</param>
		/// <param name="UserName">List only client workspaces owned by this user.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a client record or error code</returns>
		public static Task<PerforceResponseList<ClientsRecord>> TryGetClientsAsync(this IPerforceConnection Connection, ClientsOptions Options, string? Filter, int MaxResults, string? Stream, string? UserName, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if ((Options & ClientsOptions.All) != 0)
			{
				Arguments.Add("-a");
			}
			if (Filter != null)
			{
				if ((Options & ClientsOptions.CaseSensitiveFilter) != 0)
				{
					Arguments.Add("-e");
					Arguments.Add(Filter);
				}
				else
				{
					Arguments.Add("-E");
					Arguments.Add(Filter);
				}
			}
			if (MaxResults != -1)
			{
				Arguments.Add($"-m{MaxResults}");
			}
			if (Stream != null)
			{
				Arguments.Add($"-S{Stream}");
			}
			if ((Options & ClientsOptions.WithTimes) != 0)
			{
				Arguments.Add("-t");
			}
			if (UserName != null)
			{
				Arguments.Add($"-u{UserName}");
			}
			if ((Options & ClientsOptions.Unloaded) != 0)
			{
				Arguments.Add("-U");
			}
			return CommandAsync<ClientsRecord>(Connection, "clients", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 delete

		/// <summary>
		/// Execute the 'delete' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The change to add deleted files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<DeleteRecord>> DeleteAsync(this IPerforceConnection Connection, int ChangeNumber, DeleteOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryDeleteAsync(Connection, ChangeNumber, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'delete' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The change to add deleted files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static Task<PerforceResponseList<DeleteRecord>> TryDeleteAsync(this IPerforceConnection Connection, int ChangeNumber, DeleteOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if (ChangeNumber != -1)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}
			if ((Options & DeleteOptions.PreviewOnly) != 0)
			{
				Arguments.Add("-n");
			}
			if ((Options & DeleteOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Add("-k");
			}
			if ((Options & DeleteOptions.WithoutSyncing) != 0)
			{
				Arguments.Add("-v");
			}

			return BatchedCommandAsync<DeleteRecord>(Connection, "delete", Arguments, FileSpecs.List, null, CancellationToken);
		}

		#endregion

		#region p4 describe

		/// <summary>
		/// Describes a single changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The changelist number to retrieve description for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a describe record or error code</returns>
		public static async Task<DescribeRecord> DescribeAsync(this IPerforceConnection Connection, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			return (await TryDescribeAsync(Connection, ChangeNumber, CancellationToken)).Data;
		}

		/// <summary>
		/// Describes a single changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The changelist number to retrieve description for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; either a describe record or error code</returns>
		public static async Task<PerforceResponse<DescribeRecord>> TryDescribeAsync(this IPerforceConnection Connection, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			PerforceResponseList<DescribeRecord> Records = await TryDescribeAsync(Connection, new int[] { ChangeNumber }, CancellationToken);
			if (Records.Count != 1)
			{
				throw new PerforceException("Expected only one record returned from p4 describe command, got {0}", Records.Count);
			}
			return Records[0];
		}

		/// <summary>
		/// Describes a set of changelists
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumbers">The changelist numbers to retrieve descriptions for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<DescribeRecord>> DescribeAsync(this IPerforceConnection Connection, int[] ChangeNumbers, CancellationToken CancellationToken = default)
		{
			return (await TryDescribeAsync(Connection, ChangeNumbers, CancellationToken)).Data;
		}

		/// <summary>
		/// Describes a set of changelists
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumbers">The changelist numbers to retrieve descriptions for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static Task<PerforceResponseList<DescribeRecord>> TryDescribeAsync(this IPerforceConnection Connection, int[] ChangeNumbers, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string> { "-s" };
			foreach (int ChangeNumber in ChangeNumbers)
			{
				Arguments.Add($"{ChangeNumber}");
			}
			return CommandAsync<DescribeRecord>(Connection, "describe", Arguments, null, CancellationToken);
		}

		/// <summary>
		/// Describes a set of changelists
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxNumFiles">Maximum number of files to return</param>
		/// <param name="ChangeNumbers">The changelist numbers to retrieve descriptions for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<DescribeRecord>> DescribeAsync(this IPerforceConnection Connection, DescribeOptions Options, int MaxNumFiles, int[] ChangeNumbers, CancellationToken CancellationToken = default)
		{
			return (await TryDescribeAsync(Connection, Options, MaxNumFiles, ChangeNumbers, CancellationToken)).Data;
		}

		/// <summary>
		/// Describes a set of changelists
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxNumFiles">Maximum number of files to return</param>
		/// <param name="ChangeNumbers">The changelist numbers to retrieve descriptions for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static Task<PerforceResponseList<DescribeRecord>> TryDescribeAsync(this IPerforceConnection Connection, DescribeOptions Options, int MaxNumFiles, int[] ChangeNumbers, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string> { "-s" };
			if ((Options & DescribeOptions.ShowDescriptionForRestrictedChanges) != 0)
			{
				Arguments.Add("-f");
			}
			if ((Options & DescribeOptions.Identity) != 0)
			{
				Arguments.Add("-I");
			}
			if (MaxNumFiles != -1)
			{
				Arguments.Add($"-m{MaxNumFiles}");
			}
			if ((Options & DescribeOptions.OriginalChangeNumber) != 0)
			{
				Arguments.Add("-O");
			}
			if ((Options & DescribeOptions.Shelved) != 0)
			{
				Arguments.Add("-S");
			}
			foreach (int ChangeNumber in ChangeNumbers)
			{
				Arguments.Add($"{ChangeNumber}");
			}
			return CommandAsync<DescribeRecord>(Connection, "describe", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 dirs

		/// <summary>
		/// List directories under a depot path
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="Stream">List directories mapped for the specified stream</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<DirsRecord>> GetDirsAsync(this IPerforceConnection Connection, DirsOptions Options, string? Stream, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryGetDirsAsync(Connection, Options, Stream, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// List directories under a depot path
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="Stream">List directories mapped for the specified stream</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<DirsRecord>> TryGetDirsAsync(this IPerforceConnection Connection, DirsOptions Options, string? Stream, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if ((Options & DirsOptions.OnlyMapped) != 0)
			{
				Arguments.Add("-C");
			}
			if ((Options & DirsOptions.IncludeDeleted) != 0)
			{
				Arguments.Add("-D");
			}
			if ((Options & DirsOptions.OnlyHave) != 0)
			{
				Arguments.Add("-H");
			}
			if (Stream != null)
			{
				Arguments.Add("-S");
				Arguments.Add(Stream);
			}
			if ((Options & DirsOptions.IgnoreCase) != 0)
			{
				Arguments.Add("-i");
			}

			return CommandAsync<DirsRecord>(Connection, "dirs", Arguments, FileSpecs.List, null, CancellationToken);
		}

		#endregion

		#region p4 edit

		/// <summary>
		/// Opens files for edit
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<EditRecord>> EditAsync(this IPerforceConnection Connection, int ChangeNumber, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryEditAsync(Connection, ChangeNumber, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Opens files for edit
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<EditRecord>> TryEditAsync(this IPerforceConnection Connection, int ChangeNumber, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TryEditAsync(Connection, ChangeNumber, null, EditOptions.None, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Opens files for edit
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<EditRecord>> EditAsync(this IPerforceConnection Connection, int ChangeNumber, string? FileType, EditOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryEditAsync(Connection, ChangeNumber, FileType, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Opens files for edit
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be opened for edit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<EditRecord>> TryEditAsync(this IPerforceConnection Connection, int ChangeNumber, string? FileType, EditOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if (ChangeNumber != -1)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}
			if ((Options & EditOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Add("-k");
			}
			if ((Options & EditOptions.PreviewOnly) != 0)
			{
				Arguments.Add("-n");
			}
			if (FileType != null)
			{
				Arguments.Add($"-t{FileType}");
			}

			return BatchedCommandAsync<EditRecord>(Connection, "edit", Arguments, FileSpecs.List, null, CancellationToken);
		}

		#endregion

		#region p4 filelog

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<FileLogRecord>> FileLogAsync(this IPerforceConnection Connection, FileLogOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryFileLogAsync(Connection, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static Task<PerforceResponseList<FileLogRecord>> TryFileLogAsync(this IPerforceConnection Connection, FileLogOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TryFileLogAsync(Connection, -1, -1, Options, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="MaxChanges">Number of changelists to show. Ignored if zero or negative.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<FileLogRecord>> FileLogAsync(this IPerforceConnection Connection, int MaxChanges, FileLogOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryFileLogAsync(Connection, MaxChanges, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="MaxChanges">Number of changelists to show. Ignored if zero or negative.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static Task<PerforceResponseList<FileLogRecord>> TryFileLogAsync(this IPerforceConnection Connection, int MaxChanges, FileLogOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TryFileLogAsync(Connection, -1, MaxChanges, Options, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Show only files modified by this changelist. Ignored if zero or negative.</param>
		/// <param name="MaxChanges">Number of changelists to show. Ignored if zero or negative.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<FileLogRecord>> FileLogAsync(this IPerforceConnection Connection, int ChangeNumber, int MaxChanges, FileLogOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryFileLogAsync(Connection, ChangeNumber, MaxChanges, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'filelog' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Show only files modified by this changelist. Ignored if zero or negative.</param>
		/// <param name="MaxChanges">Number of changelists to show. Ignored if zero or negative.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static Task<PerforceResponseList<FileLogRecord>> TryFileLogAsync(this IPerforceConnection Connection, int ChangeNumber, int MaxChanges, FileLogOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			// Build the argument list
			List<string> Arguments = new List<string>();
			if (ChangeNumber > 0)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}
			if ((Options & FileLogOptions.ContentHistory) != 0)
			{
				Arguments.Add("-h");
			}
			if ((Options & FileLogOptions.FollowAcrossBranches) != 0)
			{
				Arguments.Add("-i");
			}
			if ((Options & FileLogOptions.FullDescriptions) != 0)
			{
				Arguments.Add("-l");
			}
			if ((Options & FileLogOptions.LongDescriptions) != 0)
			{
				Arguments.Add("-L");
			}
			if (MaxChanges > 0)
			{
				Arguments.Add($"-m{MaxChanges}");
			}
			if ((Options & FileLogOptions.DoNotFollowPromotedTaskStreams) != 0)
			{
				Arguments.Add("-p");
			}
			if ((Options & FileLogOptions.IgnoreNonContributoryIntegrations) != 0)
			{
				Arguments.Add("-s");
			}

			// Always include times to simplify parsing
			Arguments.Add("-t");

			// Append all the arguments
			return BatchedCommandAsync<FileLogRecord>(Connection, "filelog", Arguments, FileSpecs.List, null, CancellationToken);
		}

		#endregion

		#region p4 fstat

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<FStatRecord>> FStatAsync(this IPerforceConnection Connection, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryFStatAsync(Connection, FStatOptions.None, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<FStatRecord>> FStatAsync(this IPerforceConnection Connection, FStatOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryFStatAsync(Connection, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static Task<PerforceResponseList<FStatRecord>> TryFStatAsync(this IPerforceConnection Connection, FStatOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TryFStatAsync(Connection, -1, Options, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="MaxFiles">Produce fstat output for only the first max files.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<FStatRecord>> FStatAsync(this IPerforceConnection Connection, int MaxFiles, FStatOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryFStatAsync(Connection, MaxFiles, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="MaxFiles">Produce fstat output for only the first max files.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static Task<PerforceResponseList<FStatRecord>> TryFStatAsync(this IPerforceConnection Connection, int MaxFiles, FStatOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TryFStatAsync(Connection, -1, -1, null, null, MaxFiles, Options, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="AfterChangeNumber">Return only files affected after the given changelist number.</param>
		/// <param name="OnlyChangeNumber">Return only files affected by the given changelist number.</param>
		/// <param name="Filter">List only those files that match the criteria specified.</param>
		/// <param name="Fields">Fields to return in the output</param>
		/// <param name="MaxFiles">Produce fstat output for only the first max files.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<FStatRecord>> FStatAsync(this IPerforceConnection Connection, int AfterChangeNumber, int OnlyChangeNumber, string? Filter, string? Fields, int MaxFiles, FStatOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryFStatAsync(Connection, AfterChangeNumber, OnlyChangeNumber, Filter, Fields, MaxFiles, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'fstat' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="AfterChangeNumber">Return only files affected after the given changelist number.</param>
		/// <param name="OnlyChangeNumber">Return only files affected by the given changelist number.</param>
		/// <param name="Filter">List only those files that match the criteria specified.</param>
		/// <param name="Fields">Fields to return in the output</param>
		/// <param name="MaxFiles">Produce fstat output for only the first max files.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">List of file specifications to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<PerforceResponseList<FStatRecord>> TryFStatAsync(this IPerforceConnection Connection, int AfterChangeNumber, int OnlyChangeNumber, string? Filter, string? Fields, int MaxFiles, FStatOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			// Build the argument list
			List<string> Arguments = new List<string>();
			if (AfterChangeNumber != -1)
			{
				Arguments.Add($"-c{AfterChangeNumber}");
			}
			if(OnlyChangeNumber != -1)
			{
				Arguments.Add($"-e{OnlyChangeNumber}");
			}
			if(Filter != null)
			{
				Arguments.Add("-F");
				Arguments.Add(Filter);
			}
			if (Fields != null)
			{
				Arguments.Add("-T");
				Arguments.Add(Fields);
			}
			if((Options & FStatOptions.ReportDepotSyntax) != 0)
			{
				Arguments.Add("-L");
			}
			if((Options & FStatOptions.AllRevisions) != 0)
			{
				Arguments.Add("-Of");
			}
			if((Options & FStatOptions.IncludeFileSizes) != 0)
			{
				Arguments.Add("-Ol");
			}
			if((Options & FStatOptions.ClientFileInPerforceSyntax) != 0)
			{
				Arguments.Add("-Op");
			}
			if((Options & FStatOptions.ShowPendingIntegrations) != 0)
			{
				Arguments.Add("-Or");
			}
			if((Options & FStatOptions.ShortenOutput) != 0)
			{
				Arguments.Add("-Os");
			}
			if((Options & FStatOptions.ReverseOrder) != 0)
			{
				Arguments.Add("-r");
			}
			if((Options & FStatOptions.OnlyMapped) != 0)
			{
				Arguments.Add("-Rc");
			}
			if((Options & FStatOptions.OnlyHave) != 0)
			{
				Arguments.Add("-Rh");
			}
			if((Options & FStatOptions.OnlyOpenedBeforeHead) != 0)
			{
				Arguments.Add("-Rn");
			}
			if((Options & FStatOptions.OnlyOpenInWorkspace) != 0)
			{
				Arguments.Add("-Ro");
			}
			if((Options & FStatOptions.OnlyOpenAndResolved) != 0)
			{
				Arguments.Add("-Rr");
			}
			if((Options & FStatOptions.OnlyShelved) != 0)
			{
				Arguments.Add("-Rs");
			}
			if((Options & FStatOptions.OnlyUnresolved) != 0)
			{
				Arguments.Add("-Ru");
			}
			if((Options & FStatOptions.SortByDate) != 0)
			{
				Arguments.Add("-Sd");
			}
			if((Options & FStatOptions.SortByHaveRevision) != 0)
			{
				Arguments.Add("-Sh");
			}
			if((Options & FStatOptions.SortByHeadRevision) != 0)
			{
				Arguments.Add("-Sr");
			}
			if((Options & FStatOptions.SortByFileSize) != 0)
			{
				Arguments.Add("-Ss");
			}
			if((Options & FStatOptions.SortByFileType) != 0)
			{
				Arguments.Add("-St");
			}
			if((Options & FStatOptions.IncludeFilesInUnloadDepot) != 0)
			{
				Arguments.Add("-U");
			}

			// Execute the command
			PerforceResponseList<FStatRecord> Records = await BatchedCommandAsync<FStatRecord>(Connection, "fstat", Arguments, FileSpecs.List, null, CancellationToken);
			if (OnlyChangeNumber != -1 && Records.Count > 0 && Records[Records.Count - 1].Succeeded && Records[Records.Count - 1].Data.Description != null)
			{
				Records.RemoveAt(Records.Count - 1);
			}
			return Records;
		}

		#endregion

		#region p4 info

		/// <summary>
		/// Execute the 'info' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; an InfoRecord or error code</returns>
		public static async Task<InfoRecord> GetInfoAsync(this IPerforceConnection Connection, InfoOptions Options, CancellationToken CancellationToken = default)
		{
			return (await TryGetInfoAsync(Connection, Options, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'info' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server; an InfoRecord or error code</returns>
		public static Task<PerforceResponse<InfoRecord>> TryGetInfoAsync(this IPerforceConnection Connection, InfoOptions Options, CancellationToken CancellationToken = default)
		{
			// Build the argument list
			List<string> Arguments = new List<string>();
			if((Options & InfoOptions.ShortOutput) != 0)
			{
				Arguments.Add("-s");
			}
			return SingleResponseCommandAsync<InfoRecord>(Connection, "info", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 merge

		/// <summary>
		/// Execute the 'merge' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the merge</param>
		/// <param name="Change"></param>
		/// <param name="MaxFiles">Maximum number of files to merge</param>
		/// <param name="SourceFileSpec">The source filespec and revision range</param>
		/// <param name="TargetFileSpec">The target filespec</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>List of records</returns>
		public static Task<PerforceResponseList<MergeRecord>> MergeAsync(this IPerforceConnection Connection, MergeOptions Options, int Change, int MaxFiles, string SourceFileSpec, string TargetFileSpec, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if ((Options & MergeOptions.Preview) != 0)
			{
				Arguments.Add($"-n");
			}
			if (Change != -1)
			{
				Arguments.Add($"-c{Change}");
			}
			if (MaxFiles != -1)
			{
				Arguments.Add($"-m{MaxFiles}");
			}
			Arguments.Add(SourceFileSpec);
			Arguments.Add(TargetFileSpec);

			return CommandAsync<MergeRecord>(Connection, "merge", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 move

		/// <summary>
		/// Opens files for move
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="SourceFileSpec">The source file(s)</param>
		/// <param name="TargetFileSpec">The target file(s)</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<MoveRecord>> MoveAsync(this IPerforceConnection Connection, int ChangeNumber, string? FileType, MoveOptions Options, string SourceFileSpec, string TargetFileSpec, CancellationToken CancellationToken = default)
		{
			return (await TryMoveAsync(Connection, ChangeNumber, FileType, Options, SourceFileSpec, TargetFileSpec, CancellationToken)).Data;
		}

		/// <summary>
		/// Opens files for move
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="FileType">Type for new files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="SourceFileSpec">The source file(s)</param>
		/// <param name="TargetFileSpec">The target file(s)</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<MoveRecord>> TryMoveAsync(this IPerforceConnection Connection, int ChangeNumber, string? FileType, MoveOptions Options, string SourceFileSpec, string TargetFileSpec, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if (ChangeNumber != -1)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}
			if ((Options & MoveOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Add("-k");
			}
			if ((Options & MoveOptions.RenameOnly) != 0)
			{
				Arguments.Add("-r");
			}
			if ((Options & MoveOptions.PreviewOnly) != 0)
			{
				Arguments.Add("-n");
			}
			if (FileType != null)
			{
				Arguments.Add($"-t{FileType}");
			}
			Arguments.Add(SourceFileSpec);
			Arguments.Add(TargetFileSpec);

			return CommandAsync<MoveRecord>(Connection, "move", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 opened

		/// <summary>
		/// Execute the 'opened' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">List the files in pending changelist change. To list files in the default changelist, use DefaultChange.</param>
		/// <param name="ClientName">List only files that are open in the given client</param>
		/// <param name="UserName">List only files that are opened by the given user</param>
		/// <param name="MaxResults">Maximum number of results to return</param>
		/// <param name="FileSpecs">Specification for the files to list</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<FStatRecord>> GetOpenFilesAsync(this IPerforceConnection Connection, OpenedOptions Options, int ChangeNumber, string? ClientName, string? UserName, int MaxResults, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryGetOpenFilesAsync(Connection, Options, ChangeNumber, ClientName, UserName, MaxResults, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'opened' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="ChangeNumber">List the files in pending changelist change. To list files in the default changelist, use DefaultChange.</param>
		/// <param name="ClientName">List only files that are open in the given client</param>
		/// <param name="UserName">List only files that are opened by the given user</param>
		/// <param name="MaxResults">Maximum number of results to return</param>
		/// <param name="FileSpecs">Specification for the files to list</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<FStatRecord>> TryGetOpenFilesAsync(this IPerforceConnection Connection, OpenedOptions Options, int ChangeNumber, string? ClientName, string? UserName, int MaxResults, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			// Build the argument list
			List<string> Arguments = new List<string>();
			if((Options & OpenedOptions.AllWorkspaces) != 0)
			{
				Arguments.Add($"-a");
			}
			if (ChangeNumber == PerforceReflection.DefaultChange)
			{
				Arguments.Add($"-cdefault");
			}
			else if (ChangeNumber != -1)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}
			if(ClientName != null)
			{
				Arguments.Add($"-C{ClientName}");
			}
			if(UserName != null)
			{
				Arguments.Add($"-u{UserName}");
			}
			if(MaxResults != -1)
			{
				Arguments.Add($"-m{MaxResults}");
			}
			if((Options & OpenedOptions.ShortOutput) != 0)
			{
				Arguments.Add($"-s");
			}
			Arguments.AddRange(FileSpecs.List);

			return CommandAsync<FStatRecord>(Connection, "opened", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 print

		/// <summary>
		/// Execute the 'print' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="OutputFile">Output file to redirect output to</param>
		/// <param name="FileSpec">Specification for the files to print</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<PrintRecord> PrintAsync(this IPerforceConnection Connection, string OutputFile, string FileSpec, CancellationToken CancellationToken = default)
		{
			return (await TryPrintAsync(Connection, OutputFile, FileSpec, CancellationToken)).Data;
		}

		/// <summary>
		/// Execute the 'print' command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="OutputFile">Output file to redirect output to</param>
		/// <param name="FileSpec">Specification for the files to print</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponse<PrintRecord>> TryPrintAsync(this IPerforceConnection Connection, string OutputFile, string FileSpec, CancellationToken CancellationToken = default)
		{
			// Build the argument list
			List<string> Arguments = new List<string>();
			Arguments.Add("-o");
			Arguments.Add(OutputFile);
			Arguments.Add(FileSpec);
			return SingleResponseCommandAsync<PrintRecord>(Connection, "print", Arguments, null, CancellationToken);
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
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<ReconcileRecord>> ReconcileAsync(this IPerforceConnection Connection, int ChangeNumber, ReconcileOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			PerforceResponseList<ReconcileRecord> Records = await TryReconcileAsync(Connection, ChangeNumber, Options, FileSpecs, CancellationToken);
			Records.RemoveAll(x => x.Info != null);
			return Records.Data;
		}

		/// <summary>
		/// Open files for add, delete, and/or edit in order to reconcile a workspace with changes made outside of Perforce.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<ReconcileRecord>> TryReconcileAsync(this IPerforceConnection Connection, int ChangeNumber, ReconcileOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if(ChangeNumber != -1)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}
			if((Options & ReconcileOptions.Edit) != 0)
			{
				Arguments.Add("-e");
			}
			if((Options & ReconcileOptions.Add) != 0)
			{
				Arguments.Add("-a");
			}
			if((Options & ReconcileOptions.Delete) != 0)
			{
				Arguments.Add("-d");
			}
			if((Options & ReconcileOptions.PreviewOnly) != 0)
			{
				Arguments.Add("-n");
			}
			if((Options & ReconcileOptions.AllowWildcards) != 0)
			{
				Arguments.Add("-f");
			}
			if((Options & ReconcileOptions.NoIgnore) != 0)
			{
				Arguments.Add("-I");
			}
			if((Options & ReconcileOptions.LocalFileSyntax) != 0)
			{
				Arguments.Add("-l");
			}
			if ((Options & ReconcileOptions.UseFileModification) != 0)
			{
				Arguments.Add("-m");
			}
			Arguments.AddRange(FileSpecs.List);

			return CommandAsync<ReconcileRecord>(Connection, "reconcile", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 reopen

		/// <summary>
		/// Reopen a file
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="FileType">New filetype</param>
		/// <param name="FileSpec">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<ReopenRecord>> ReopenAsync(this IPerforceConnection Connection, int? ChangeNumber, string? FileType, string FileSpec, CancellationToken CancellationToken = default)
		{
			return (await TryReopenAsync(Connection, ChangeNumber, FileType, FileSpec, CancellationToken)).Data;
		}

		/// <summary>
		/// Reopen a file
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="FileType">New filetype</param>
		/// <param name="FileSpec">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<ReopenRecord>> TryReopenAsync(this IPerforceConnection Connection, int? ChangeNumber, string? FileType, string FileSpec, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if (ChangeNumber != null)
			{
				if (ChangeNumber == PerforceReflection.DefaultChange)
				{
					Arguments.Add("-cdefault");
				}
				else
				{
					Arguments.Add($"-c{ChangeNumber}");
				}
			}
			if (FileType != null)
			{
				Arguments.Add($"-t{FileType}");
			}
			Arguments.Add(FileSpec);

			return CommandAsync<ReopenRecord>(Connection, "reopen", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 reload

		/// <summary>
		/// Reloads a client workspace
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ClientName">Name of the client to reload</param>
		/// <param name="SourceServerId">The source server id</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<List<PerforceResponse>> ReloadClient(this IPerforceConnection Connection, string ClientName, string SourceServerId, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			Arguments.Add($"-c{ClientName}");
			Arguments.Add($"-p{SourceServerId}");
			return Connection.CommandAsync("reload", Arguments, null, null, CancellationToken);
		}

		#endregion p4 reload

		#region p4 resolve

		/// <summary>
		/// Resolve conflicts between file revisions.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<ResolveRecord>> ResolveAsync(this IPerforceConnection Connection, int ChangeNumber, ResolveOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryResolveAsync(Connection, ChangeNumber, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Resolve conflicts between file revisions.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to open files to</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<PerforceResponseList<ResolveRecord>> TryResolveAsync(this IPerforceConnection Connection, int ChangeNumber, ResolveOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if ((Options & ResolveOptions.Automatic) != 0)
			{
				Arguments.Add("-am");
			}
			if ((Options & ResolveOptions.AcceptYours) != 0)
			{
				Arguments.Add("-ay");
			}
			if ((Options & ResolveOptions.AcceptTheirs) != 0)
			{
				Arguments.Add("-at");
			}
			if ((Options & ResolveOptions.SafeAccept) != 0)
			{
				Arguments.Add("-as");
			}
			if ((Options & ResolveOptions.ForceAccept) != 0)
			{
				Arguments.Add("-af");
			}
			if((Options & ResolveOptions.IgnoreWhitespaceOnly) != 0)
			{
				Arguments.Add("-db");
			}
			if((Options & ResolveOptions.IgnoreWhitespace) != 0)
			{
				Arguments.Add("-dw");
			}
			if((Options & ResolveOptions.IgnoreLineEndings) != 0)
			{
				Arguments.Add("-dl");
			}
			if ((Options & ResolveOptions.ResolveAgain) != 0)
			{
				Arguments.Add("-f");
			}
			if ((Options & ResolveOptions.PreviewOnly) != 0)
			{
				Arguments.Add("-n");
			}
			Arguments.AddRange(FileSpecs.List);

			PerforceResponseList<ResolveRecord> Records = await CommandAsync<ResolveRecord>(Connection, "resolve", Arguments, null, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		#endregion

		#region p4 revert

		/// <summary>
		/// Reverts files that have been added to a pending changelist.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="ClientName">Revert another user’s open files. </param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<RevertRecord>> RevertAsync(this IPerforceConnection Connection, int ChangeNumber, string? ClientName, RevertOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryRevertAsync(Connection, ChangeNumber, ClientName, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Reverts files that have been added to a pending changelist.
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist to add files to</param>
		/// <param name="ClientName">Revert another user’s open files. </param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to be reverted</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<PerforceResponseList<RevertRecord>> TryRevertAsync(this IPerforceConnection Connection, int ChangeNumber, string? ClientName, RevertOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if((Options & RevertOptions.Unchanged) != 0)
			{
				Arguments.Add("-a");
			}
			if((Options & RevertOptions.PreviewOnly) != 0)
			{
				Arguments.Add("-n");
			}
			if((Options & RevertOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Add("-k");
			}
			if((Options & RevertOptions.DeleteAddedFiles) != 0)
			{
				Arguments.Add("-w");
			}
			if(ChangeNumber != -1)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}
			if(ClientName != null)
			{
				Arguments.Add($"-C{ClientName}");
			}

			PerforceResponseList<RevertRecord> Records = await BatchedCommandAsync<RevertRecord>(Connection, "revert", Arguments, FileSpecs.List, null, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		#endregion

		#region p4 shelve

		/// <summary>
		/// Shelves a set of files
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The change number to receive the shelved files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<ShelveRecord>> ShelveAsync(this IPerforceConnection Connection, int ChangeNumber, ShelveOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryShelveAsync(Connection, ChangeNumber, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Shelves a set of files
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The change number to receive the shelved files</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<PerforceResponseList<ShelveRecord>> TryShelveAsync(this IPerforceConnection Connection, int ChangeNumber, ShelveOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			Arguments.Add($"-c{ChangeNumber}");
			if((Options & ShelveOptions.OnlyChanged) != 0)
			{
				Arguments.Add("-aleaveunchanged");
			}
			if((Options & ShelveOptions.Overwrite) != 0)
			{
				Arguments.Add("-f");
			}
			Arguments.AddRange(FileSpecs.List);

			PerforceResponseList<ShelveRecord> Records = await CommandAsync<ShelveRecord>(Connection, "shelve", Arguments, null, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		/// <summary>
		/// Deletes files from a shelved changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist containing shelved files to be deleted</param>
		/// <param name="FileSpecs">Files to delete</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task DeleteShelvedFilesAsync(this IPerforceConnection Connection, int ChangeNumber, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<PerforceResponse> Responses = await TryDeleteShelvedFilesAsync(Connection, ChangeNumber, FileSpecs, CancellationToken);

			PerforceResponse? ErrorResponse = Responses.FirstOrDefault(x => x.Error != null && x.Error.Generic != PerforceGenericCode.Empty);
			if(ErrorResponse != null)
			{
				throw new PerforceException(ErrorResponse.Error!);
			}
		}

		/// <summary>
		/// Deletes files from a shelved changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">Changelist containing shelved files to be deleted</param>
		/// <param name="FileSpecs">Files to delete</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<List<PerforceResponse>> TryDeleteShelvedFilesAsync(this IPerforceConnection Connection, int ChangeNumber, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			Arguments.Add("-d");
			if(ChangeNumber != -1)
			{
				Arguments.Add($"-c{ChangeNumber}");
			}
			Arguments.AddRange(FileSpecs.List);

			return Connection.CommandAsync("shelve", Arguments, null, null, CancellationToken);
		}

		#endregion

		#region p4 streams

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="StreamPath">The path for streams to enumerate (eg. "//UE4/...")</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of streams matching the given criteria</returns>
		public static async Task<List<StreamsRecord>> GetStreamsAsync(this IPerforceConnection Connection, string StreamPath, CancellationToken CancellationToken = default)
		{
			return (await TryGetStreamsAsync(Connection, StreamPath, CancellationToken)).Data;
		}

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="StreamPath">The path for streams to enumerate (eg. "//UE4/...")</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of streams matching the given criteria</returns>
		public static Task<PerforceResponseList<StreamsRecord>> TryGetStreamsAsync(this IPerforceConnection Connection, string StreamPath, CancellationToken CancellationToken = default)
		{
			return TryGetStreamsAsync(Connection, StreamPath, -1, null, false, CancellationToken);
		}

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="StreamPath">The path for streams to enumerate (eg. "//UE4/...")</param>
		/// <param name="MaxResults">Maximum number of results to return</param>
		/// <param name="Filter">Additional filter to be applied to the results</param>
		/// <param name="bUnloaded">Whether to enumerate unloaded workspaces</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of streams matching the given criteria</returns>
		public static async Task<List<StreamsRecord>> GetStreamsAsync(this IPerforceConnection Connection, string StreamPath, int MaxResults, string? Filter, bool bUnloaded, CancellationToken CancellationToken = default)
		{
			return (await TryGetStreamsAsync(Connection, StreamPath, MaxResults, Filter, bUnloaded, CancellationToken)).Data;
		}

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="StreamPath">The path for streams to enumerate (eg. "//UE4/...")</param>
		/// <param name="MaxResults">Maximum number of results to return</param>
		/// <param name="Filter">Additional filter to be applied to the results</param>
		/// <param name="bUnloaded">Whether to enumerate unloaded workspaces</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of streams matching the given criteria</returns>
		public static Task<PerforceResponseList<StreamsRecord>> TryGetStreamsAsync(this IPerforceConnection Connection, string StreamPath, int MaxResults, string? Filter, bool bUnloaded, CancellationToken CancellationToken = default)
		{
			// Build the command line
			List<string> Arguments = new List<string>();
			if (bUnloaded)
			{
				Arguments.Add("-U");
			}
			if (Filter != null)
			{
				Arguments.Add("-F");
				Arguments.Add(Filter);
			}
			if (MaxResults > 0)
			{
				Arguments.Add($"-m{MaxResults}");
			}
			Arguments.Add(StreamPath);

			// Execute the command
			return CommandAsync<StreamsRecord>(Connection, "streams", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 submit

		/// <summary>
		/// Submits a pending changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The changelist to submit</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<SubmitRecord> SubmitAsync(this IPerforceConnection Connection, int ChangeNumber, SubmitOptions Options, CancellationToken CancellationToken = default)
		{
			return (await TrySubmitAsync(Connection, ChangeNumber, Options, CancellationToken)).Data;
		}

		/// <summary>
		/// Submits a pending changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The changelist to submit</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<PerforceResponse<SubmitRecord>> TrySubmitAsync(this IPerforceConnection Connection, int ChangeNumber, SubmitOptions Options, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if((Options & SubmitOptions.ReopenAsEdit) != 0)
			{
				Arguments.Add("-r");
			}
			Arguments.Add($"-c{ChangeNumber}");

			return (await CommandAsync<SubmitRecord>(Connection, "submit", Arguments, null, CancellationToken))[0];
		}

		/// <summary>
		/// Submits a shelved changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The changelist to submit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<SubmitRecord> SubmitShelvedAsync(this IPerforceConnection Connection, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			return (await TrySubmitShelvedAsync(Connection, ChangeNumber, CancellationToken)).Data;
		}

		/// <summary>
		/// Submits a pending changelist
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The changelist to submit</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<PerforceResponse<SubmitRecord>> TrySubmitShelvedAsync(this IPerforceConnection Connection, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			Arguments.Add($"-e{ChangeNumber}");

			return (await CommandAsync<SubmitRecord>(Connection, "submit", Arguments, null, CancellationToken))[0];
		}

		#endregion

		#region p4 sync

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<SyncRecord>> SyncAsync(this IPerforceConnection Connection, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TrySyncAsync(Connection, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<SyncRecord>> TrySyncAsync(this IPerforceConnection Connection, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TrySyncAsync(Connection, SyncOptions.None, -1, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified.</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<SyncRecord>> SyncAsync(this IPerforceConnection Connection, SyncOptions Options, int MaxFiles, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TrySyncAsync(Connection, Options, MaxFiles, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified.</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<SyncRecord>> TrySyncAsync(this IPerforceConnection Connection, SyncOptions Options, int MaxFiles, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TrySyncAsync(Connection, Options, MaxFiles, -1, -1, -1, -1, -1, FileSpecs, CancellationToken);
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
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
		public static async Task<List<SyncRecord>> SyncAsync(this IPerforceConnection Connection, SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TrySyncAsync(Connection, Options, MaxFiles, NumThreads, Batch, BatchSize, Min, MinSize, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
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
		public static async Task<PerforceResponseList<SyncRecord>> TrySyncAsync(this IPerforceConnection Connection, SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			// Perforce annoyingly returns 'up-to-date' as an error. Ignore it.
			PerforceResponseList<SyncRecord> Records = await SyncInternalAsync<SyncRecord>(Connection, Options, MaxFiles, NumThreads, Batch, BatchSize, Min, MinSize, FileSpecs, false, CancellationToken);
			Records.RemoveAll(x => x.Error != null && x.Error.Generic == PerforceGenericCode.Empty);
			return Records;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified.</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<SyncSummaryRecord>> SyncQuietAsync(this IPerforceConnection Connection, SyncOptions Options, int MaxFiles, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			PerforceResponseList<SyncSummaryRecord> Records = await TrySyncQuietAsync(Connection, Options, MaxFiles, FileSpecs, CancellationToken);
			Records.RemoveAll(x => (x.Error != null && x.Error.Generic == PerforceGenericCode.Empty) || x.Info != null);
			return Records.Data;
		}

		/// <summary>
		/// Syncs files from the server
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="MaxFiles">Syncs only the first number of files specified.</param>
		/// <param name="FileSpecs">Files to sync</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<SyncSummaryRecord>> TrySyncQuietAsync(this IPerforceConnection Connection, SyncOptions Options, int MaxFiles, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return TrySyncQuietAsync(Connection, Options, MaxFiles, -1, -1, -1, -1, -1, FileSpecs, null, CancellationToken);
		}

		/// <summary>
		/// Syncs files from the server without returning detailed file info
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
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
		public static async Task<List<SyncSummaryRecord>> SyncQuietAsync(this IPerforceConnection Connection, SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, FileSpecList FileSpecs, FileReference? ResponseFile, CancellationToken CancellationToken = default)
		{
			return (await TrySyncQuietAsync(Connection, Options, MaxFiles, NumThreads, Batch, BatchSize, Min, MinSize, FileSpecs, ResponseFile, CancellationToken)).Data;
		}

		/// <summary>
		/// Syncs files from the server without returning detailed file info
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
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
		public static Task<PerforceResponseList<SyncSummaryRecord>> TrySyncQuietAsync(this IPerforceConnection Connection, SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, FileSpecList FileSpecs, FileReference? ResponseFile, CancellationToken CancellationToken = default)
		{
			return SyncInternalAsync<SyncSummaryRecord>(Connection, Options, MaxFiles, NumThreads, Batch, BatchSize, Min, MinSize, FileSpecs, true, CancellationToken);
		}

		/// <summary>
		/// Gets arguments for a sync command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
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
		private static async Task<PerforceResponseList<T>> SyncInternalAsync<T>(this IPerforceConnection Connection, SyncOptions Options, int MaxFiles, int NumThreads, int Batch, int BatchSize, int Min, int MinSize, FileSpecList FileSpecs, bool bQuiet, CancellationToken CancellationToken = default) where T : class
		{
			List<string> Arguments = new List<string>();
			if((Options & SyncOptions.Force) != 0)
			{
				Arguments.Add("-f");
			}
			if((Options & SyncOptions.KeepWorkspaceFiles) != 0)
			{
				Arguments.Add("-k");
			}
			if((Options & SyncOptions.FullDepotSyntax) != 0)
			{
				Arguments.Add("-L");
			}
			if((Options & SyncOptions.PreviewOnly) != 0)
			{
				Arguments.Add("-n");
			}
			if((Options & SyncOptions.NetworkPreviewOnly) != 0)
			{
				Arguments.Add("-N");
			}
			if((Options & SyncOptions.DoNotUpdateHaveList) != 0)
			{
				Arguments.Add("-p");
			}
			if(bQuiet)
			{
				Arguments.Add("-q");
			}
			if((Options & SyncOptions.ReopenMovedFiles) != 0)
			{
				Arguments.Add("-r");
			}
			if((Options & SyncOptions.Safe) != 0)
			{
				Arguments.Add("-s");
			}
			if(MaxFiles != -1)
			{
				Arguments.Add($"-m{MaxFiles}");
			}
			/*			if(NumThreads != -1)
						{
							Arguments.Add($"--parallel-threads={0}", NumThreads);
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
			return await CommandAsync<T>(Connection, "sync", Arguments, FileSpecs.List, null, CancellationToken);
		}

		#endregion

		#region p4 unshelve

		/// <summary>
		/// Restore shelved files from a pending change into a workspace
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The changelist containing shelved files</param>
		/// <param name="IntoChangeNumber">The changelist to receive the unshelved files</param>
		/// <param name="UsingBranchSpec">The branchspec to use when unshelving files</param>
		/// <param name="UsingStream">Specifies the use of a stream-derived branch view to map the shelved files between the specified stream and its parent stream.</param>
		/// <param name="ForceParentStream">Unshelve to the specified parent stream. Overrides the parent defined in the source stream specification.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to unshelve</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<List<UnshelveRecord>> UnshelveAsync(this IPerforceConnection Connection, int ChangeNumber, int IntoChangeNumber, string? UsingBranchSpec, string? UsingStream, string? ForceParentStream, UnshelveOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return (await TryUnshelveAsync(Connection, ChangeNumber, IntoChangeNumber, UsingBranchSpec, UsingStream, ForceParentStream, Options, FileSpecs, CancellationToken)).Data;
		}

		/// <summary>
		/// Restore shelved files from a pending change into a workspace
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumber">The changelist containing shelved files</param>
		/// <param name="IntoChangeNumber">The changelist to receive the unshelved files</param>
		/// <param name="UsingBranchSpec">The branchspec to use when unshelving files</param>
		/// <param name="UsingStream">Specifies the use of a stream-derived branch view to map the shelved files between the specified stream and its parent stream.</param>
		/// <param name="ForceParentStream">Unshelve to the specified parent stream. Overrides the parent defined in the source stream specification.</param>
		/// <param name="Options">Options for the command</param>
		/// <param name="FileSpecs">Files to unshelve</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<UnshelveRecord>> TryUnshelveAsync(this IPerforceConnection Connection, int ChangeNumber, int IntoChangeNumber, string? UsingBranchSpec, string? UsingStream, string? ForceParentStream, UnshelveOptions Options, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			Arguments.Add($"-s{ChangeNumber}");
			if((Options & UnshelveOptions.ForceOverwrite) != 0)
			{
				Arguments.Add("-f");
			}
			if((Options & UnshelveOptions.PreviewOnly) != 0)
			{
				Arguments.Add("-n");
			}
			if(IntoChangeNumber != -1)
			{
				Arguments.Add($"-c{IntoChangeNumber}");
			}
			if(UsingBranchSpec != null)
			{
				Arguments.Add($"-b{UsingBranchSpec}");
			}
			if(UsingStream != null)
			{
				Arguments.Add($"-S{UsingStream}");
			}
			if(ForceParentStream != null)
			{
				Arguments.Add($"-P{ForceParentStream}");
			}
			Arguments.AddRange(FileSpecs.List);

			return CommandAsync<UnshelveRecord>(Connection, "unshelve", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 user

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="UserName">Name of the user to fetch information for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task<UserRecord> GetUserAsync(this IPerforceConnection Connection, string UserName, CancellationToken CancellationToken = default)
		{
			return (await TryGetUserAsync(Connection, UserName, CancellationToken))[0].Data;
		}

		/// <summary>
		/// Enumerates all streams in a depot
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="UserName">Name of the user to fetch information for</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<UserRecord>> TryGetUserAsync(this IPerforceConnection Connection, string UserName, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			Arguments.Add("-o");
			Arguments.Add(UserName);
			return CommandAsync<UserRecord>(Connection, "user", Arguments, null, CancellationToken);
		}

		#endregion

		#region p4 where

		/// <summary>
		/// Retrieves the location of a file of set of files in the workspace
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="FileSpecs">Patterns for the files to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static async Task<List<WhereRecord>> WhereAsync(this IPerforceConnection Connection, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			List<WhereRecord> Records = (await TryWhereAsync(Connection, FileSpecs, CancellationToken)).Data;
			if (Records.Count != FileSpecs.List.Count)
			{
				throw new PerforceException($"Unexpected response count; expected {FileSpecs.List.Count}, got {Records.Count}.");
			}
			return Records;
		}

		/// <summary>
		/// Retrieves the location of a file of set of files in the workspace
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="FileSpecs">Patterns for the files to query</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of responses from the server</returns>
		public static Task<PerforceResponseList<WhereRecord>> TryWhereAsync(this IPerforceConnection Connection, FileSpecList FileSpecs, CancellationToken CancellationToken = default)
		{
			return BatchedCommandAsync<WhereRecord>(Connection, "where", new List<string>(), FileSpecs.List, null, CancellationToken);
		}

		#endregion

		#region p4 undo

		/// <summary>
		/// perform undo on a changelist (p4 undo -c [targetCL] //...@undoCL)
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumberToUndo">Changelist number to undo</param>
		/// <param name="ChangeNumber">Changelist number to receive the changes</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static async Task UndoChangeAsync(this IPerforceConnection Connection, int ChangeNumberToUndo, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			(await TryUndoChangeAsync(Connection, ChangeNumberToUndo, ChangeNumber, CancellationToken))[0].EnsureSuccess();
		}

		/// <summary>
		/// perform undo on a changelist (p4 undo -c [targetCL] //...@undoCL)
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="ChangeNumberToUndo">Changelist number to undo</param>
		/// <param name="ChangeNumber">Changelist number to receive the changes</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		public static Task<PerforceResponseList<UndoRecord>> TryUndoChangeAsync(this IPerforceConnection Connection, int ChangeNumberToUndo, int ChangeNumber, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			Arguments.Add($"-c{ChangeNumber}");
			Arguments.Add($"//...@{ChangeNumberToUndo}");
			return CommandAsync<UndoRecord>(Connection, "undo", Arguments, null, CancellationToken);
		}
		#endregion

		#region p4 annotate

		/// <summary>
		/// Runs the annotate p4 command
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="FileSpec">Depot path to the file</param>
		/// <param name="Options">Options for the anotate command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of annotate records</returns>
		public static async Task<List<AnnotateRecord>> AnnotateAsync(this IPerforceConnection Connection, string FileSpec, AnnotateOptions Options, CancellationToken CancellationToken = default)
		{
			return (await TryAnnotateAsync(Connection, FileSpec, Options, CancellationToken)).Data;
		}

		/// <summary>
		/// Runs the annotate p4 command 
		/// </summary>
		/// <param name="Connection">Connection to the Perforce server</param>
		/// <param name="FileSpec">Depot path to the file</param>
		/// <param name="Options">Options for the anotate command</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>List of annotate records</returns>
		public static Task<PerforceResponseList<AnnotateRecord>> TryAnnotateAsync(this IPerforceConnection Connection, string FileSpec, AnnotateOptions Options, CancellationToken CancellationToken = default)
		{
			List<string> Arguments = new List<string>();
			if ((Options & AnnotateOptions.IncludeDeletedFilesAndLines) != 0)
			{
				Arguments.Add("-a");
			}
			if ((Options & AnnotateOptions.IgnoreWhiteSpaceChanges) != 0)
			{
				Arguments.Add("-db");
			}
			if ((Options & AnnotateOptions.OutputUserAndDate) != 0)
			{
				Arguments.Add("-u");
			}
			if ((Options & AnnotateOptions.FollowIntegrations) != 0)
			{
				Arguments.Add("-I");
			}
			Arguments.Add(FileSpec);

			return CommandAsync<AnnotateRecord>(Connection, "annotate", Arguments, null, CancellationToken);
		}

		#endregion

	}
}
