// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using HordeCommon.Rpc.Tasks;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
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
	}

	/// <summary>
	/// Load balancer for Perforce edge servers
	/// </summary>
	public class PerforceLoadBalancer : ElectedBackgroundService
	{
		/// <summary>
		/// Information about a resolved Perforce server
		/// </summary>
		class PerforceServerEntry : IPerforceServer
		{
			public string ServerAndPort { get; set; } = String.Empty;
			public string BaseServerAndPort { get; set; } = String.Empty;
			public string Cluster { get; set; } = String.Empty;
			public PerforceServerStatus Status { get; set; }
			public string? Detail { get; set; }
			public int NumLeases { get; set; }

			[BsonConstructor]
			private PerforceServerEntry()
			{
			}

			public PerforceServerEntry(string ServerAndPort, string BaseServerAndPort, string Cluster, PerforceServerStatus Status, string? Detail)
			{
				this.ServerAndPort = ServerAndPort;
				this.BaseServerAndPort = BaseServerAndPort;
				this.Cluster = Cluster;
				this.Status = Status;
				this.Detail = Detail;
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

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService"></param>
		/// <param name="LeaseCollection"></param>
		/// <param name="Logger"></param>
		public PerforceLoadBalancer(DatabaseService DatabaseService, ILeaseCollection LeaseCollection, ILogger<PerforceLoadBalancer> Logger)
			: base(DatabaseService, new ObjectId("603fb20536e999974fd6ff6b"), TimeSpan.FromMinutes(1.0), TimeSpan.FromMinutes(2.0), Logger)
		{
			this.DatabaseService = DatabaseService;
			this.LeaseCollection = LeaseCollection;
			this.ServerListSingleton = new SingletonDocument<PerforceServerList>(DatabaseService);
			this.Logger = Logger;
		}

		/// <summary>
		/// Get the perforce servers
		/// </summary>
		/// <returns></returns>
		public async Task<List<IPerforceServer>> GetServersAsync()
		{
			PerforceServerList ServerList = await ServerListSingleton.GetAsync();
			return ServerList.Servers.ConvertAll<IPerforceServer>(x => x);
		}

		/// <summary>
		/// Allocates a server for use by a lease
		/// </summary>
		/// <returns>The server to use. Null if there is no healthy server available.</returns>
		public async Task<IPerforceServer?> GetServer(string Cluster)
		{
			PerforceServerList ServerList = await ServerListSingleton.GetAsync();

			List<PerforceServerEntry> Candidates = ServerList.Servers.Where(x => x.Cluster == Cluster && x.Status >= PerforceServerStatus.Healthy).ToList();
			if(Candidates.Count == 0)
			{
				int Idx = Random.Next(0, Candidates.Count);
				return Candidates[Idx];
			}
			return null;
		}

		/// <summary>
		/// Select a Perforce server to use
		/// </summary>
		/// <param name="Cluster"></param>
		/// <param name="Agent"></param>
		/// <returns></returns>
		public async Task<IPerforceServer?> SelectServerAsync(PerforceCluster Cluster, IAgent Agent)
		{
			// Get the set of properties to use for checking server validity
			HashSet<string>? Properties = Agent.Capabilities.Devices[0].Properties;

			// Find all the valid servers for this agent
			HashSet<string> ValidServers = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			if (Properties != null)
			{
				ValidServers.UnionWith(Cluster.Servers.Where(x => x.Properties != null && x.Properties.Count > 0 && x.Properties.All(y => Properties.Contains(y))).Select(x => x.ServerAndPort));
			}
			if (ValidServers.Count == 0)
			{
				ValidServers.UnionWith(Cluster.Servers.Where(x => x.Properties == null || x.Properties.Count == 0).Select(x => x.ServerAndPort));
			}

			// Find all the matching servers
			PerforceServerList ServerList = await ServerListSingleton.GetAsync();
			List<PerforceServerEntry> Candidates = ServerList.Servers.Where(x => x.Cluster == Cluster.Name && x.Status == PerforceServerStatus.Healthy && ValidServers.Contains(x.BaseServerAndPort)).ToList();

			if (Candidates.Count == 0)
			{
				Logger.LogDebug("Unable to find Perforce server from cluster {ClusterName} matching {ValidServers}", Cluster.Name, String.Join(", ", ValidServers));
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
		protected override async Task<DateTime> TickLeaderAsync(CancellationToken StoppingToken)
		{
			Globals Globals = await DatabaseService.GetGlobalsAsync();

			// Set of new server entries
			ConcurrentBag<PerforceServerEntry> NewServers = new ConcurrentBag<PerforceServerEntry>();

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
			List<PerforceServerEntry> NewServersList = NewServers.OrderBy(x => x.Cluster).ThenBy(x => x.BaseServerAndPort).ThenBy(x => x.ServerAndPort).ToList();
			await UpdateLeaseCounts(NewServersList);

			// Mark any other servers as deleted
			PerforceServerList NewServerList = await ServerListSingleton.GetAsync();
			NewServerList.Servers = NewServers.OrderBy(x => x.Cluster).ThenBy(x => x.BaseServerAndPort).ThenBy(x => x.ServerAndPort).ToList();
			await ServerListSingleton.TryUpdateAsync(NewServerList);

			return DateTime.UtcNow.AddMinutes(1.0);
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

		async Task UpdateServerAsync(PerforceCluster Cluster, PerforceServer Server, ConcurrentBag<PerforceServerEntry> NewServers)
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
			if (!Server.ResolveDns || IPAddress.TryParse(InitialHostName, out IPAddress HostAddress))
			{
				HostNames.Add(InitialHostName);
			}
			else
			{
				await ResolveServersAsync(InitialHostName, HostNames);
			}

			List<Task> Tasks = new List<Task>();
			foreach (string HostName in HostNames)
			{
				Tasks.Add(Task.Run(() => UpdateHostAsync(Cluster, Server, HostName, Port, NewServers)));
			}
			await Task.WhenAll(Tasks);
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

		static async Task UpdateHostAsync(PerforceCluster Cluster, PerforceServer Server, string HostName, int Port, ConcurrentBag<PerforceServerEntry> NewServers)
		{
			string Detail = "Health check disabled";

			// Get the health of the server
			PerforceServerStatus Health = PerforceServerStatus.Healthy;
			if (Server.HealthCheck)
			{
				Uri HealthCheckUrl = new Uri($"http://{HostName}:5000/healthcheck");
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
			NewServers.Add(new PerforceServerEntry($"{HostName}:{Port}", Server.ServerAndPort, Cluster.Name, Health, Detail));
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
						string Status = Output.GetString();
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
