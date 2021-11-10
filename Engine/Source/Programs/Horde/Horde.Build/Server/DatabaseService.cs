// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.IdentityModel.Tokens;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Driver;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Linq.Expressions;
using System.Net;
using System.Net.Security;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading.Tasks;
using System.Net.Sockets;
using MongoDB.Driver.Core.Events;
using System.Threading;
using HordeServer.Collections;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Net.NetworkInformation;
using OpenTracing.Util;
using OpenTracing;
using HordeServer.Utiltiies;
using System.Globalization;

namespace HordeServer.Services
{
	using IStream = HordeServer.Models.IStream;
	using PoolId = StringId<IPool>;
	using ProjectId = StringId<IProject>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Singleton for accessing the database
	/// </summary>
	public sealed class DatabaseService : IDisposable
	{
		/// <summary>
		/// The database instance
		/// </summary>
		public IMongoDatabase Database { get; private set; }

		/// <summary>
		/// Collection of credential documents
		/// </summary>
		public IMongoCollection<Credential> Credentials { get; }

		/// <summary>
		/// Collection of singleton documents
		/// </summary>
		public IMongoCollection<BsonDocument> Singletons { get; }

		/// <summary>
		/// Settings for the application
		/// </summary>
		ServerSettings Settings { get; }

		/// <summary>
		/// Name of the JWT issuer
		/// </summary>
		public string JwtIssuer { get; }

		/// <summary>
		/// The signing key for this instance
		/// </summary>
		public SymmetricSecurityKey JwtSigningKey { get; }

		/// <summary>
		/// Logger for this instance
		/// </summary>
		ILogger<DatabaseService> Logger;
		
		/// <summary>
		/// Access the database in a read-only mode (don't create indices or modify content)
		/// </summary>
		public bool ReadOnlyMode { get; }

		/// <summary>
		/// The mongo process group
		/// </summary>
		ManagedProcessGroup? MongoProcessGroup;

		/// <summary>
		/// The mongo process
		/// </summary>
		ManagedProcess? MongoProcess;

		/// <summary>
		/// Task to read from the mongo process
		/// </summary>
		Task? MongoOutputTask;

		/// <summary>
		/// Factory for creating logger instances
		/// </summary>
		ILoggerFactory LoggerFactory;

		/// <summary>
		/// Default port for MongoDB connections
		/// </summary>
		const int DefaultMongoPort = 27017;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="SettingsSnapshot">The settings instance</param>
		/// <param name="LoggerFactory">Instance of the logger for this service</param>
		public DatabaseService(IOptions<ServerSettings> SettingsSnapshot, ILoggerFactory LoggerFactory)
		{
			this.Settings = SettingsSnapshot.Value;
			this.Logger = LoggerFactory.CreateLogger<DatabaseService>();
			this.LoggerFactory = LoggerFactory;

			try
			{
				ReadOnlyMode = Settings.DatabaseReadOnlyMode;
				if (Settings.DatabasePublicCert != null)
				{
					X509Store LocalTrustStore = new X509Store(StoreName.Root);
					try
					{
						LocalTrustStore.Open(OpenFlags.ReadWrite);

						X509Certificate2Collection Collection = ImportCertificateBundle(Settings.DatabasePublicCert);
						foreach (X509Certificate2 Certificate in Collection)
						{
							Logger.LogInformation("Importing certificate for {Subject}", Certificate.Subject);
						}

						LocalTrustStore.AddRange(Collection);
					}
					finally
					{
						LocalTrustStore.Close();
					}
				}

				string? ConnectionString = Settings.DatabaseConnectionString;
				if (ConnectionString == null)
				{
					if (IsPortInUse(DefaultMongoPort))
					{
						ConnectionString = "mongodb://localhost:27017";
					}
					else if (TryStartMongoServer(Logger))
					{
						ConnectionString = "mongodb://localhost:27017/?readPreference=primary&appname=Horde&ssl=false";
					}
					else
					{
						throw new Exception($"Unable to connect to MongoDB server. Setup a MongoDB server and set the connection string in {Program.UserConfigFile}");
					}
				}

				MongoClientSettings MongoSettings = MongoClientSettings.FromConnectionString(ConnectionString);
				MongoSettings.ClusterConfigurator = ClusterBuilder =>
				{
					if (Logger.IsEnabled(LogLevel.Trace))
					{
						ClusterBuilder.Subscribe<CommandStartedEvent>(Event => TraceMongoCommand(Event.Command));
					}
				};
				
				MongoSettings.SslSettings = new SslSettings();
				MongoSettings.SslSettings.ServerCertificateValidationCallback = CertificateValidationCallBack;
				MongoSettings.MaxConnectionPoolSize = 300; // Default is 100

				//TestSslConnection(MongoSettings.Server.Host, MongoSettings.Server.Port, Logger);

				MongoClient Client = new MongoClient(MongoSettings);
				Database = Client.GetDatabase(Settings.DatabaseName);

				Singletons = GetCollection<BsonDocument>("Singletons");

				Globals Globals = GetGlobalsAsync().Result;
				if (Globals.ForceReset)
				{
					Logger.LogInformation("Performing DB reset...");

					// Drop all the collections
					List<string> CollectionNames = Database.ListCollectionNames().ToList();
					foreach (string CollectionName in CollectionNames)
					{
						Logger.LogInformation("Dropping {CollectionName}", CollectionName);
						Database.DropCollection(CollectionName);
					}

					// Create the new singletons collection
					Singletons = GetCollection<BsonDocument>("Singletons");

					// Create the new globals object
					Globals NewGlobals = new Globals();
					NewGlobals.JwtSigningKey = Globals.JwtSigningKey; // Ensure agents can still register
					Singletons.InsertOne(NewGlobals.ToBsonDocument());
					Globals = NewGlobals;
				}

				while (Globals.JwtSigningKey == null)
				{
					Globals.RotateSigningKey();
					if (!TryUpdateSingletonAsync(Globals).Result)
					{
						Globals = GetGlobalsAsync().Result;
					}
				}

				JwtIssuer = Settings.JwtIssuer ?? Dns.GetHostName();
				if (String.IsNullOrEmpty(Settings.JwtSecret))
				{
					JwtSigningKey = new SymmetricSecurityKey(Globals.JwtSigningKey);
				}
				else
				{
					JwtSigningKey = new SymmetricSecurityKey(Convert.FromBase64String(Settings.JwtSecret));
				}

				Credentials = GetCollection<Credential>("Credentials");
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Exception while initializing DatabaseService");
				throw;
			}
		}

		internal const int CTRL_C_EVENT = 0;

		[DllImport("kernel32.dll")]
		internal static extern bool GenerateConsoleCtrlEvent(int Event, int ProcessGroupId);

		/// <inheritdoc/>
		public void Dispose()
		{
			if (MongoProcess != null)
			{
				GenerateConsoleCtrlEvent(CTRL_C_EVENT, MongoProcess.Id);

				MongoOutputTask?.Wait();
				MongoOutputTask = null;

				MongoProcess.WaitForExit();
				MongoProcess.Dispose();
				MongoProcess = null;
			}
			if(MongoProcessGroup != null)
			{
				MongoProcessGroup?.Dispose();
				MongoProcessGroup = null;
			}
		}

		/// <summary>
		/// Checks if the given port is in use
		/// </summary>
		/// <param name="PortNumber"></param>
		/// <returns></returns>
		static bool IsPortInUse(int PortNumber)
		{
			IPGlobalProperties IpGlobalProperties = IPGlobalProperties.GetIPGlobalProperties();

			IPEndPoint[] Listeners = IpGlobalProperties.GetActiveTcpListeners();
			if (Listeners.Any(x => x.Port == PortNumber))
			{
				return true;
			}

			return false;
		}

		/// <summary>
		/// Attempts to start a local instance of MongoDB
		/// </summary>
		/// <param name="Logger"></param>
		/// <returns></returns>
		bool TryStartMongoServer(ILogger Logger)
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				return false;
			}

			FileReference MongoExe = FileReference.Combine(Program.AppDir, "ThirdParty", "Mongo", "mongod.exe");
			if (!FileReference.Exists(MongoExe))
			{
				Logger.LogWarning("Unable to find Mongo executable.");
				return false;
			}

			DirectoryReference MongoDir = DirectoryReference.Combine(Program.DataDir, "Mongo");

			DirectoryReference MongoDataDir = DirectoryReference.Combine(MongoDir, "Data");
			DirectoryReference.CreateDirectory(MongoDataDir);

			FileReference MongoLogFile = FileReference.Combine(MongoDir, "mongod.log");

			FileReference ConfigFile = FileReference.Combine(MongoDir, "mongod.conf");
			if(!FileReference.Exists(ConfigFile))
			{
				DirectoryReference.CreateDirectory(ConfigFile.Directory);
				using (StreamWriter Writer = new StreamWriter(ConfigFile.FullName))
				{
					Writer.WriteLine("# mongod.conf");
					Writer.WriteLine();
					Writer.WriteLine("# for documentation of all options, see:");
					Writer.WriteLine("# http://docs.mongodb.org/manual/reference/configuration-options/");
					Writer.WriteLine();
					Writer.WriteLine("storage:");
					Writer.WriteLine("    dbPath: {0}", MongoDataDir.FullName);
					Writer.WriteLine();
					Writer.WriteLine("net:");
					Writer.WriteLine("    port: {0}", DefaultMongoPort);
					Writer.WriteLine("    bindIp: 127.0.0.1");
				}
			}

			MongoProcessGroup = new ManagedProcessGroup();
			try
			{
				MongoProcess = new ManagedProcess(MongoProcessGroup, MongoExe.FullName, $"--config \"{ConfigFile}\"", null, null, ProcessPriorityClass.Normal);
				MongoProcess.StdIn.Close();
				MongoOutputTask = Task.Run(() => RelayMongoOutput());
				return true;
			}
			catch (Exception Ex)
			{
				Logger.LogWarning(Ex, "Unable to start Mongo server process");
				return false;
			}
		}

		/// <summary>
		/// Copies output from the mongo process to the logger
		/// </summary>
		/// <returns></returns>
		async Task RelayMongoOutput()
		{
			ILogger MongoLogger = LoggerFactory.CreateLogger("MongoDB");

			Dictionary<string, ILogger> ChannelLoggers = new Dictionary<string, ILogger>();
			for (; ; )
			{
				string? Line = await MongoProcess!.ReadLineAsync();
				if (Line == null)
				{
					break;
				}
				if (Line.Length > 0)
				{
					Match Match = Regex.Match(Line, @"^\s*[^\s]+\s+([A-Z])\s+([^\s]+)\s+(.*)");
					if (Match.Success)
					{
						ILogger? ChannelLogger;
						if (!ChannelLoggers.TryGetValue(Match.Groups[2].Value, out ChannelLogger))
						{
							ChannelLogger = LoggerFactory.CreateLogger($"MongoDB.{Match.Groups[2].Value}");
							ChannelLoggers.Add(Match.Groups[2].Value, ChannelLogger);
						}
						ChannelLogger.Log(ParseMongoLogLevel(Match.Groups[1].Value), Match.Groups[3].Value.TrimEnd());
					}
					else
					{
						MongoLogger.Log(LogLevel.Information, Line);
					}
				}
			}
			MongoLogger.LogInformation("Exit code {ExitCode}", MongoProcess.ExitCode);
		}

		static LogLevel ParseMongoLogLevel(string Text)
		{
			if (Text.Equals("I", StringComparison.Ordinal))
			{
				return LogLevel.Information;
			}
			else if (Text.Equals("E", StringComparison.Ordinal))
			{
				return LogLevel.Error;
			}
			else
			{
				return LogLevel.Warning;
			}
		}

		/// <summary>
		/// Logs a mongodb command, removing any fields we don't care about
		/// </summary>
		/// <param name="Command">The command document</param>
		void TraceMongoCommand(BsonDocument Command)
		{
			List<string> Params = new List<string>();
			List<string> Values = new List<string>();

			foreach(BsonElement Element in Command)
			{
				if (Element.Value != null && !Element.Name.Equals("$db", StringComparison.Ordinal) && !Element.Name.Equals("lsid", StringComparison.Ordinal))
				{
					Params.Add($"{Element.Name}: {{{Values.Count}}}");
					Values.Add(Element.Value.ToString()!);
				}
			}

			Logger.LogTrace($"MongoDB: {String.Join(", ", Params)}", Values.ToArray());
		}

		/// <summary>
		/// Imports one or more certificates from a single PEM file
		/// </summary>
		/// <param name="FileName">File to import</param>
		/// <returns>Collection of certificates</returns>
		static X509Certificate2Collection ImportCertificateBundle(string FileName)
		{
			X509Certificate2Collection Collection = new X509Certificate2Collection();

			string Text = File.ReadAllText(FileName);
			for (int Offset = 0; Offset < Text.Length;)
			{
				int NextOffset = Text.IndexOf("-----BEGIN CERTIFICATE-----", Offset + 1, StringComparison.Ordinal);
				if (NextOffset == -1)
				{
					NextOffset = Text.Length;
				}

				string CertificateText = Text.Substring(Offset, NextOffset - Offset);
				Collection.Add(new X509Certificate2(Encoding.UTF8.GetBytes(CertificateText)));

				Offset = NextOffset;
			}

			return Collection;
		}

		/// <summary>
		/// Tests connection to the given server
		/// </summary>
		/// <param name="Host">Host name</param>
		/// <param name="Port">Port number to connect on</param>
		/// <param name="Logger">Logger for diagnostic messages</param>
		/// <returns>True if the connection was valid</returns>
		static bool TestSslConnection(string Host, int Port, ILogger Logger)
		{
#pragma warning disable CA1031 // Do not catch general exception types
			using (TcpClient Client = new TcpClient())
			{
				try
				{
					Client.Connect(Host, Port);
					Logger.LogInformation("Successfully connected to {Host} on port {Port}", Host, Port);
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Unable to connect to {Host} on port {Port}", Host, Port);
					return false;
				}

				using (SslStream Stream = new SslStream(Client.GetStream()))
				{
					try
					{
						Stream.AuthenticateAsClient(Host);
						Logger.LogInformation("Successfully authenticated host {Host}", Host);
					}
					catch (Exception Ex)
					{
						Logger.LogError(Ex, "Unable to authenticate as client");
						return false;
					}
				}
			}
			return true;
#pragma warning restore CA1031 // Do not catch general exception types
		}

		/// <summary>
		/// Provides additional diagnostic information for SSL certificate validation
		/// </summary>
		/// <param name="Sender"></param>
		/// <param name="Certificate"></param>
		/// <param name="Chain"></param>
		/// <param name="SslPolicyErrors"></param>
		/// <returns>True if the certificate is allowed, false otherwise</returns>
		bool CertificateValidationCallBack(object Sender, X509Certificate? Certificate, X509Chain? Chain, SslPolicyErrors SslPolicyErrors)
		{
			// If the certificate is a valid, signed certificate, return true.
			if (SslPolicyErrors == SslPolicyErrors.None)
			{
				return true;
			}

			// Generate diagnostic information
			StringBuilder Builder = new StringBuilder();
			if (Sender != null)
			{
				string SenderInfo = StringUtils.Indent(Sender.ToString() ?? String.Empty, "    ");
				Builder.Append(CultureInfo.InvariantCulture, $"\nSender:\n{SenderInfo}");
			}
			if (Certificate != null)
			{
				Builder.Append(CultureInfo.InvariantCulture, $"\nCertificate: {Certificate.Subject}");
			}
			if (Chain != null)
			{
				if (Chain.ChainStatus != null && Chain.ChainStatus.Length > 0)
				{
					Builder.Append("\nChain status:");
					foreach (X509ChainStatus Status in Chain.ChainStatus)
					{
						Builder.Append(CultureInfo.InvariantCulture, $"\n  {Status.StatusInformation}");
					}
				}
				if (Chain.ChainElements != null)
				{
					Builder.Append("\nChain elements:");
					for (int Idx = 0; Idx < Chain.ChainElements.Count; Idx++)
					{
						X509ChainElement Element = Chain.ChainElements[Idx];
						Builder.Append(CultureInfo.InvariantCulture, $"\n  {Idx,4} - Certificate: {Element.Certificate.Subject}");
						if (Element.ChainElementStatus != null && Element.ChainElementStatus.Length > 0)
						{
							foreach (X509ChainStatus Status in Element.ChainElementStatus)
							{
								Builder.Append(CultureInfo.InvariantCulture, $"\n         Status: {Status.StatusInformation} ({Status.Status})");
							}
						}
						if (!String.IsNullOrEmpty(Element.Information))
						{
							Builder.Append(CultureInfo.InvariantCulture, $"\n         Info: {Element.Information}");
						}
					}
				}
			}

			// Print out additional diagnostic information
			Logger.LogError("TLS certificate validation failed ({Errors}).{AdditionalInfo}", SslPolicyErrors, StringUtils.Indent(Builder.ToString(), "    "));
			return false;
		}

		/// <summary>
		/// Get a MongoDB collection from database
		/// </summary>
		/// <param name="Name">Name of collection</param>
		/// <typeparam name="T">A MongoDB document</typeparam>
		/// <returns></returns>
		public IMongoCollection<T> GetCollection<T>(string Name)
		{
			return new MongoTracingCollection<T>(Database.GetCollection<T>(Name));
		}

		/// <summary>
		/// Gets a singleton document by id
		/// </summary>
		/// <returns>The globals document</returns>
		public Task<Globals> GetGlobalsAsync()
		{
			return GetSingletonAsync<Globals>(Globals.StaticId, () => new Globals() { SchemaVersion = UpgradeService.LatestSchemaVersion });
		}

		/// <summary>
		/// Gets a singleton document by id
		/// </summary>
		/// <param name="Id">Id of the singleton document</param>
		/// <returns>The document</returns>
		public async Task<T> GetSingletonAsync<T>(ObjectId Id) where T : SingletonBase, new()
		{
			T Singleton = await GetSingletonAsync(Id, () => new T());
			Singleton.PostLoad();
			return Singleton;
		}

		/// <summary>
		/// Gets a singleton document by id
		/// </summary>
		/// <param name="Id">Id of the singleton document</param>
		/// <param name="Constructor">Method to use to construct a new object</param>
		/// <returns>The document</returns>
		public async Task<T> GetSingletonAsync<T>(ObjectId Id, Func<T> Constructor) where T : SingletonBase, new()
		{
			FilterDefinition<BsonDocument> Filter = new BsonDocument(new BsonElement("_id", Id));
			for (; ; )
			{
				BsonDocument? Object = await Singletons.Find<BsonDocument>(Filter).FirstOrDefaultAsync();
				if (Object != null)
				{
					T Item = BsonSerializer.Deserialize<T>(Object);
					Item.PostLoad();
					return Item;
				}

				T NewItem = Constructor();
				NewItem.Id = Id;
				await Singletons.InsertOneAsync(NewItem.ToBsonDocument());
			}
		}

		/// <summary>
		/// Updates a singleton
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Id"></param>
		/// <param name="Updater"></param>
		/// <returns></returns>
		public async Task UpdateSingletonAsync<T>(ObjectId Id, Action<T> Updater) where T : SingletonBase, new()
		{
			for (; ; )
			{
				T Object = await GetSingletonAsync(Id, () => new T());
				Updater(Object);
				
				if (await TryUpdateSingletonAsync(Object))
				{
					break;
				}
			}
		}

		/// <summary>
		/// Attempts to update a singleton object
		/// </summary>
		/// <param name="SingletonObject">The singleton object</param>
		/// <returns>True if the singleton document was updated</returns>
		public async Task<bool> TryUpdateSingletonAsync<T>(T SingletonObject) where T : SingletonBase
		{
			int PrevRevision = SingletonObject.Revision++;

			BsonDocument Filter = new BsonDocument { new BsonElement("_id", SingletonObject.Id), new BsonElement(nameof(SingletonBase.Revision), PrevRevision) };
			try
			{
				ReplaceOneResult Result = await Singletons.ReplaceOneAsync(Filter, SingletonObject.ToBsonDocument(), new ReplaceOptions { IsUpsert = true });
				return Result.MatchedCount > 0;
			}
			catch (MongoWriteException Ex)
			{
				// Duplicate key error occurs if filter fails to match because revision is not the same.
				if (Ex.WriteError != null && Ex.WriteError.Category == ServerErrorCategory.DuplicateKey)
				{
					return false;
				}
				else
				{
					throw;
				}
			}
		}
	}
}
