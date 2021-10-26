// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using HordeCommon.Rpc.Tasks;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Health of a particular server
	/// </summary>
	public enum PerforceServerStatus
	{
		/// <summary>
		/// Server could not be reached
		/// </summary>
		Unknown,

		/// <summary>
		/// Bad. Do not use.
		/// </summary>
		Unhealthy,

		/// <summary>
		/// Degraded, but functioning
		/// </summary>
		Degraded,

		/// <summary>
		/// Good
		/// </summary>
		Healthy,
	}

	/// <summary>
	/// Perforce server infomation
	/// </summary>
	public interface IPerforceServer
	{
		/// <summary>
		/// The server and port
		/// </summary>
		public string ServerAndPort { get; }

		/// <summary>
		/// The server and port before doing DNS resolution
		/// </summary>
		public string BaseServerAndPort { get; }

		/// <summary>
		/// The cluster this server belongs to
		/// </summary>
		public string Cluster { get; }

		/// <summary>
		/// Current status
		/// </summary>
		public PerforceServerStatus Status { get; }

		/// <summary>
		/// Number of leases using this server
		/// </summary>
		public int NumLeases { get; }

		/// <summary>
		/// Error message related to this server
		/// </summary>
		public string? Detail { get; }

		/// <summary>
		/// Last update time for this server
		/// </summary>
		public DateTime? LastUpdateTime { get; }
	}

	/// <summary>
	/// Load balancer for Perforce edge servers
	/// </summary>
	public sealed class PerforceLoadBalancer : IHostedService, IDisposable
	{
		/// <summary>
		/// Information about a resolved Perforce server
		/// </summary>
		class PerforceServerEntry : IPerforceServer
		{
			public string ServerAndPort { get; set; } = String.Empty;
			public string BaseServerAndPort { get; set; } = String.Empty;
			public string? HealthCheckUrl { get; set; }
			public string Cluster { get; set; } = String.Empty;
			public PerforceServerStatus Status { get; set; }
			public string? Detail { get; set; }
			public int NumLeases { get; set; }
			public DateTime? LastUpdateTime { get; set; }

			[BsonConstructor]
			private PerforceServerEntry()
			{
			}

			public PerforceServerEntry(string ServerAndPort, string BaseServerAndPort, string? HealthCheckUrl, string Cluster, PerforceServerStatus Status, string? Detail, DateTime? LastUpdateTime)
			{
				this.ServerAndPort = ServerAndPort;
				this.BaseServerAndPort = BaseServerAndPort;
				this.HealthCheckUrl = HealthCheckUrl;
				this.Cluster = Cluster;
				this.Status = Status;
				this.Detail = Detail;
				this.LastUpdateTime = LastUpdateTime;
			}
		}

		/// <summary>
		/// 
		/// </summary>
		[SingletonDocument("6046aec374a9283100967ee7")]
		class PerforceServerList : SingletonBase
		{
			public List<PerforceServerEntry> Servers { get; set; } = new List<PerforceServerEntry>();
		}

		DatabaseService DatabaseService;
		ILeaseCollection LeaseCollection;
		SingletonDocument<PerforceServerList> ServerListSingleton;
		Random Random = new Random();
		ILogger Logger;
		ElectedTick Ticker;
		Task InternalTickTask = Task.CompletedTask;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="LeaseCollection"></param>
		/// <param name="Logger"></param>
		public PerforceLoadBalancer(DatabaseService DatabaseService, ILeaseCollection LeaseCollection, ILogger<PerforceLoadBalancer> Logger)
		{
			this.DatabaseService = DatabaseService;
			this.LeaseCollection = LeaseCollection;
			this.ServerListSingleton = new SingletonDocument<PerforceServerList>(DatabaseService);
			this.Logger = Logger;
			this.Ticker = new ElectedTick(DatabaseService, new ObjectId("603fb20536e999974fd6ff6b"), TickAsync, TimeSpan.FromMinutes(1.0), Logger);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Ticker.Dispose();
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken cancellationToken)
		{
			Ticker.Start();
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken cancellationToken)
		{
			await Ticker.StopAsync();
			await InternalTickTask;
		}

		/// <summary>
		/// Get the current server list
		/// </summary>
		/// <returns></returns>
		async Task<PerforceServerList> GetServerListAsync()
		{
			PerforceServerList ServerList = await ServerListSingleton.GetAsync();

			DateTime MinLastUpdateTime = DateTime.UtcNow - TimeSpan.FromMinutes(2.5);
			foreach (PerforceServerEntry Server in ServerList.Servers)
			{
				if (Server.Status == PerforceServerStatus.Healthy && Server.LastUpdateTime != null && Server.LastUpdateTime.Value < MinLastUpdateTime)
				{
					Server.Status = PerforceServerStatus.Degraded;
					Server.Detail = "Server has not responded to health check";
				}
			}

			return ServerList;
		}

		/// <summary>
		/// Get the perforce servers
		/// </summary>
		/// <returns></returns>
		public async Task<List<IPerforceServer>> GetServersAsync()
		{
			PerforceServerList ServerList = await GetServerListAsync();
			return ServerList.Servers.ConvertAll<IPerforceServer>(x => x);
		}

		/// <summary>
		/// Allocates a server for use by a lease
		/// </summary>
		/// <returns>The server to use. Null if there is no healthy server available.</returns>
		public async Task<IPerforceServer?> GetServer(string Cluster)
		{
			PerforceServerList ServerList = await GetServerListAsync();

			List<PerforceServerEntry> Candidates = ServerList.Servers.Where(x => x.Cluster == Cluster && x.Status >= PerforceServerStatus.Healthy).ToList();
			if(Candidates.Count == 0)
			{
				int Idx = Random.Next(0, Candidates.Count);
				return Candidates[Idx];
			}
			return null;
		}

		/// <summary>
		/// Select a Perforce server to use by the Horde server
		/// </summary>
		/// <param name="Cluster"></param>
		/// <returns></returns>
		public Task<IPerforceServer?> SelectServerAsync(PerforceCluster Cluster)
		{
			List<string> Properties = new List<string>{ "HordeServer=1" };
			return SelectServerAsync(Cluster, Properties);
		}

		/// <summary>
		/// Select a Perforce server to use
		/// </summary>
		/// <param name="Cluster"></param>
		/// <param name="Agent"></param>
		/// <returns></returns>
		public Task<IPerforceServer?> SelectServerAsync(PerforceCluster Cluster, IAgent Agent)
		{
			return SelectServerAsync(Cluster, Agent.Properties);
		}

		/// <summary>
		/// Select a Perforce server to use
		/// </summary>
		/// <param name="Cluster"></param>
		/// <param name="Properties"></param>
		/// <returns></returns>
		public async Task<IPerforceServer?> SelectServerAsync(PerforceCluster Cluster, IReadOnlyList<string> Properties)
		{
			// Find all the valid servers for this agent
			List<PerforceServer> ValidServers = new List<PerforceServer>();
			if (Properties != null)
			{
				ValidServers.AddRange(Cluster.Servers.Where(x => x.Properties != null && x.Properties.Count > 0 && x.Properties.All(y => Properties.Contains(y))));
			}
			if (ValidServers.Count == 0)
			{
				ValidServers.AddRange(Cluster.Servers.Where(x => x.Properties == null || x.Properties.Count == 0));
			}

			HashSet<string> ValidServerNames = new HashSet<string>(ValidServers.Select(x => x.ServerAndPort), StringComparer.OrdinalIgnoreCase);

			// Find all the matching servers.
			PerforceServerList ServerList = await GetServerListAsync();

			List<PerforceServerEntry> Candidates = ServerList.Servers.Where(x => x.Cluster == Cluster.Name && ValidServerNames.Contains(x.BaseServerAndPort)).ToList();
			if (Candidates.Count == 0)
			{
				foreach (PerforceServer ValidServer in ValidServers)
				{
					Logger.LogDebug("Fetching server info for {ServerAndPort}", ValidServer.ServerAndPort);
					await UpdateServerAsync(Cluster, ValidServer, Candidates);
				}
				if (Candidates.Count == 0)
				{
					Logger.LogWarning("Unable to resolve any Perforce servers from valid list");
					return null; 
				}
			}

			// Remove any servers that are unhealthy
			if (Candidates.Any(x => x.Status == PerforceServerStatus.Healthy))
			{
				Candidates.RemoveAll(x => x.Status != PerforceServerStatus.Healthy);
			}
			else
			{
				Candidates.RemoveAll(x => x.Status != PerforceServerStatus.Unknown);
			}

			if (Candidates.Count == 0)
			{
				Logger.LogWarning("Unable to find any healthy Perforce server in cluster {ClusterName}", Cluster.Name);
				return null;
			}

			// Select which server to use with a weighted average of the number of active leases
			int Index = 0;
			if (Candidates.Count > 1)
			{
				const int BaseWeight = 20;

				int TotalLeases = Candidates.Sum(x => x.NumLeases);
				int TotalWeight = Candidates.Sum(x => (TotalLeases + BaseWeight) - x.NumLeases);

				int Weight = Random.Next(TotalWeight);
				for (; Index + 1 < Candidates.Count; Index++)
				{
					Weight -= (TotalLeases + BaseWeight) - Candidates[Index].NumLeases;
					if(Weight < 0)
					{
						break;
					}
				}
			}

			return Candidates[Index];
		}

		/// <inheritdoc/>
		Task TickAsync(CancellationToken StoppingToken)
		{
			if (InternalTickTask.IsCompleted)
			{
				InternalTickTask = Task.Run(() => TickInternalGuardedAsync());
			}
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		async Task TickInternalGuardedAsync()
		{
			try
			{
				await TickInternalAsync();
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Error updating server state");
			}
		}

		/// <inheritdoc/>
		async Task TickInternalAsync()
		{
			Globals Globals = await DatabaseService.GetGlobalsAsync();

			// Set of new server entries
			List<PerforceServerEntry> NewServers = new List<PerforceServerEntry>();

			// Update the state of all the valid servers
			foreach (PerforceCluster Cluster in Globals.PerforceClusters)
			{
				foreach (PerforceServer Server in Cluster.Servers)
				{
					try
					{
						await UpdateServerAsync(Cluster, Server, NewServers);
					}
					catch (Exception Ex)
					{
						Logger.LogError(Ex, "Exception while updating Perforce server status");
					}
				}
			}

			// Update the number of leases for each entry
			List<PerforceServerEntry> NewEntries = NewServers.OrderBy(x => x.Cluster).ThenBy(x => x.BaseServerAndPort).ThenBy(x => x.ServerAndPort).ToList();
			await UpdateLeaseCounts(NewEntries);
			PerforceServerList List = await ServerListSingleton.UpdateAsync(List => MergeServerList(List, NewEntries));

			// Now update the health of each entry
			List<Task> Tasks = new List<Task>();
			foreach (PerforceServerEntry Entry in List.Servers)
			{
				Tasks.Add(Task.Run(() => UpdateHealthAsync(Entry)));
			}
			await Task.WhenAll(Tasks);
		}

		static void MergeServerList(PerforceServerList ServerList, List<PerforceServerEntry> NewEntries)
		{
			Dictionary<string, PerforceServerEntry> ExistingEntries = ServerList.Servers.ToDictionary(x => x.ServerAndPort, x => x, StringComparer.OrdinalIgnoreCase);
			foreach (PerforceServerEntry NewEntry in NewEntries)
			{
				PerforceServerEntry? ExistingEntry;
				if (ExistingEntries.TryGetValue(NewEntry.ServerAndPort, out ExistingEntry))
				{
					NewEntry.Status = ExistingEntry.Status;
					NewEntry.Detail = ExistingEntry.Detail;
					NewEntry.LastUpdateTime = ExistingEntry.LastUpdateTime;
				}
			}
			ServerList.Servers = NewEntries;
		}

		async Task UpdateLeaseCounts(IEnumerable<PerforceServerEntry> NewServerEntries)
		{
			Dictionary<string, Dictionary<string, PerforceServerEntry>> NewServerLookup = new Dictionary<string, Dictionary<string, PerforceServerEntry>>();
			foreach (PerforceServerEntry NewServerEntry in NewServerEntries)
			{
				if (NewServerEntry.Cluster != null)
				{
					Dictionary<string, PerforceServerEntry>? ClusterServers;
					if (!NewServerLookup.TryGetValue(NewServerEntry.Cluster, out ClusterServers))
					{
						ClusterServers = new Dictionary<string, PerforceServerEntry>();
						NewServerLookup.Add(NewServerEntry.Cluster, ClusterServers);
					}
					ClusterServers[NewServerEntry.ServerAndPort] = NewServerEntry;
				}
			}

			List<ILease> Leases = await LeaseCollection.FindActiveLeasesAsync();
			foreach (ILease Lease in Leases)
			{
				Any Any = Any.Parser.ParseFrom(Lease.Payload.ToArray());

				HashSet<(string, string)> Servers = new HashSet<(string, string)>();
				if (Any.TryUnpack(out ConformTask ConformTask))
				{
					foreach (HordeCommon.Rpc.Messages.AgentWorkspace Workspace in ConformTask.Workspaces)
					{
						Servers.Add((Workspace.Cluster, Workspace.ServerAndPort));
					}
				}
				if (Any.TryUnpack(out ExecuteJobTask ExecuteJobTask))
				{
					if (ExecuteJobTask.AutoSdkWorkspace != null)
					{
						Servers.Add((ExecuteJobTask.AutoSdkWorkspace.Cluster, ExecuteJobTask.AutoSdkWorkspace.ServerAndPort));
					}
					if (ExecuteJobTask.Workspace != null)
					{
						Servers.Add((ExecuteJobTask.Workspace.Cluster, ExecuteJobTask.Workspace.ServerAndPort));
					}
				}

				foreach ((string Cluster, string ServerAndPort) in Servers)
				{
					IncrementLeaseCount(Cluster, ServerAndPort, NewServerLookup);
				}
			}
		}

		static void IncrementLeaseCount(string Cluster, string ServerAndPort, Dictionary<string, Dictionary<string, PerforceServerEntry>> NewServers)
		{
			Dictionary<string, PerforceServerEntry>? ClusterServers;
			if (Cluster != null && NewServers.TryGetValue(Cluster, out ClusterServers))
			{
				PerforceServerEntry? Entry;
				if (ClusterServers.TryGetValue(ServerAndPort, out Entry))
				{
					Entry.NumLeases++;
				}
			}
		}

		async Task UpdateServerAsync(PerforceCluster Cluster, PerforceServer Server, List<PerforceServerEntry> NewServers)
		{
			string InitialHostName = Server.ServerAndPort;
			int Port = 1666;

			int PortIdx = InitialHostName.LastIndexOf(':');
			if (PortIdx != -1)
			{
				Port = int.Parse(InitialHostName.Substring(PortIdx + 1), NumberStyles.Integer, CultureInfo.InvariantCulture);
				InitialHostName = InitialHostName.Substring(0, PortIdx);
			}

			List<string> HostNames = new List<string>();
			if (!Server.ResolveDns || IPAddress.TryParse(InitialHostName, out IPAddress? HostAddress))
			{
				HostNames.Add(InitialHostName);
			}
			else
			{
				await ResolveServersAsync(InitialHostName, HostNames);
			}

			foreach (string HostName in HostNames)
			{
				string? HealthCheckUrl = null;
				if (Server.HealthCheck)
				{
					HealthCheckUrl = $"http://{HostName}:5000/healthcheck";
				}
				NewServers.Add(new PerforceServerEntry($"{HostName}:{Port}", Server.ServerAndPort, HealthCheckUrl, Cluster.Name, PerforceServerStatus.Unknown, "", DateTime.UtcNow));
			}
		}

		async Task ResolveServersAsync(string HostName, List<string> HostNames)
		{
			// Find all the addresses of the hosts
			IPHostEntry Entry = await Dns.GetHostEntryAsync(HostName);
			foreach (IPAddress Address in Entry.AddressList)
			{
				try
				{
					HostNames.Add(Address.ToString());
				}
				catch (Exception Ex)
				{
					Logger.LogDebug(Ex, "Unable to resolve host name for Perforce server {Address}", Address);
				}
			}
		}

		/// <summary>
		/// Updates 
		/// </summary>
		/// <param name="Entry"></param>
		/// <returns></returns>
		async Task UpdateHealthAsync(PerforceServerEntry Entry)
		{
			DateTime? UpdateTime = null;
			string Detail = "Health check disabled";

			// Get the health of the server
			PerforceServerStatus Health = PerforceServerStatus.Healthy;
			if (Entry.HealthCheckUrl != null)
			{
				UpdateTime = DateTime.UtcNow;
				Uri HealthCheckUrl = new Uri(Entry.HealthCheckUrl);
				try
				{
					(Health, Detail) = await GetServerHealthAsync(HealthCheckUrl);
				}
				catch
				{
					(Health, Detail) = (PerforceServerStatus.Unhealthy, $"Failed to query status at {HealthCheckUrl}");
				}
			}

			// Update the server record
			if (Health != Entry.Status || Detail != Entry.Detail || UpdateTime != Entry.LastUpdateTime)
			{
				await ServerListSingleton.UpdateAsync(x => UpdateHealth(x, Entry.ServerAndPort, Health, Detail, UpdateTime));
			}
		}

		static void UpdateHealth(PerforceServerList ServerList, string ServerAndPort, PerforceServerStatus Status, string Detail, DateTime? UpdateTime)
		{
			PerforceServerEntry? Entry = ServerList.Servers.FirstOrDefault(x => x.ServerAndPort.Equals(ServerAndPort, StringComparison.Ordinal));
			if (Entry != null)
			{
				if (Entry.LastUpdateTime == null || UpdateTime == null || Entry.LastUpdateTime.Value < UpdateTime.Value)
				{
					Entry.Status = Status;
					Entry.Detail = Detail;
					Entry.LastUpdateTime = UpdateTime;
				}
			}
		}

		static async Task<(PerforceServerStatus, string)> GetServerHealthAsync(Uri HealthCheckUrl)
		{
			using HttpClient Client = new HttpClient();
			HttpResponseMessage Response = await Client.GetAsync(HealthCheckUrl);

			byte[] Data = await Response.Content.ReadAsByteArrayAsync();
			JsonDocument Document = JsonDocument.Parse(Data);

			foreach (JsonElement Element in Document.RootElement.GetProperty("results").EnumerateArray())
			{
				if (Element.TryGetProperty("checker", out JsonElement Checker) && Checker.ValueEquals("edge_traffic_lights"))
				{
					if (Element.TryGetProperty("output", out JsonElement Output))
					{
						string Status = Output.GetString() ?? String.Empty;
						switch(Status)
						{
							case "green":
								return (PerforceServerStatus.Healthy, "Server is healthy");
							case "yellow":
								return (PerforceServerStatus.Degraded, "Degraded service");
							case "red":
								return (PerforceServerStatus.Unhealthy, "Server is being drained");
							default:
								return (PerforceServerStatus.Unknown, $"Expected state for health check ({Status})");
						}
					}
				}
			}

			return (PerforceServerStatus.Unknown, "Unable to parse health check output");
		}
	}
}
