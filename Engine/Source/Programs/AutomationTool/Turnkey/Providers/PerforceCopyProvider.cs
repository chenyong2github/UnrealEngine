using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using AutomationTool;
using System.Text.RegularExpressions;

namespace Turnkey
{
	class PerforceCopyProvider : CopyProvider
	{
		public override string ProviderToken { get { return "perforce"; } }


		public override string Execute(string Operation, CopyExecuteSpecialMode SpecialMode, string SpecialModeHint)
		{
			if (!PrepareForOperation(Operation))
			{
				return null;
			}


			// calculate the output path
			string OutputPath = Operation.Replace(ConnectedStream, PerforceClient.RootPath);
			// find the first wildcard and that's the lowest down outputpath we can use
			int DotsLocation = OutputPath.IndexOf("...");
			int StarLocation = OutputPath.IndexOf("*");
			// hjandle various combos of -1 and non -1
			int WildcardLocation = (DotsLocation >= 0 && StarLocation >= 0) ? Math.Min(DotsLocation, StarLocation) : Math.Max(DotsLocation, StarLocation);
			if (WildcardLocation != -1)
			{
				// chop down to the last / before the wildcard
				int LastSlashLocation = OutputPath.Substring(0, WildcardLocation).LastIndexOf("/");
				OutputPath = OutputPath.Substring(0, LastSlashLocation);
			}
			OutputPath.Replace('/', System.IO.Path.DirectorySeparatorChar);

			TurnkeyUtils.Log("Syncing '{0}' to '{1}'", Operation, OutputPath);

			// now do the actual sync
			PerforceConnection.Sync(Operation, AllowSpew: false);

			return OutputPath;
		}

		public override string[] Enumerate(string Operation)
		{
			// if we have no wildcards, there's no need to waste time touching p4, just return the spec
			if (!Operation.Contains("*") && !Operation.Contains("..."))
			{
				return new string[] { ProviderToken + ":" + Operation };
			}

			// connect to the stream
			if (!PrepareForOperation(Operation))
			{
				return null;
			}

			// now get the file list that matches
			List<string> Results = PerforceConnection.Files(Operation);

			return Results.Select(x => ProviderToken + ":" + x).ToArray();
		}




		static private P4Connection PerforceConnection = null;
		static private P4ClientInfo PerforceClient = null;
		// only non-null if we are connected
		static private string ConnectedStream = null;

		private P4ClientInfo DetectClientForStream(string Stream, string Username, string Hostname)
		{

			// find clients for this user in the given stream
			P4ClientInfo[] StreamClients = PerforceConnection.GetClientsForUser(Username, null, Stream);

			// find the first one usable on this host
			foreach (P4ClientInfo Client in StreamClients)
			{
				if (string.IsNullOrEmpty(Client.Host) || string.Compare(Client.Host, Hostname) == 0)
				{
					return Client;
				}
			}

			// if no clients at all, return null
			return null;
		}

		private bool PrepareForOperation(string Operation)
		{
			Match StreamMatch = new Regex(@"(\/\/\w*\/\w*)\/.*").Match(Operation);
			if (!StreamMatch.Success)
			{
				throw new AutomationException("Unable to find stream spec in perforce operation {0}", Operation);
			}

			string Stream = StreamMatch.Groups[1].ToString();
			string Hostname = System.Net.Dns.GetHostName();

			TurnkeyUtils.Log("Stream: {0}", Stream);

			if (ConnectedStream != Stream || PerforceConnection == null)
			{
				PerforceConnection = new P4Connection(null, null);
				string Username = P4Environment.DetectUserName(PerforceConnection);
				PerforceClient = DetectClientForStream(Stream, Username, Hostname);
				if (PerforceClient == null)
				{
					TurnkeyUtils.Log("Unable to find a clientspec for the perforce stream {0}", Stream);

					string Response = TurnkeyUtils.ReadInput("Would you like to create one? [y/N]", "N");
					if (string.Compare(Response, "Y", true) != 0)
					{
						// make sure to try again next time
						PerforceConnection = null;
						TurnkeyUtils.Log("Skipping operation");
						return false;
					}

					// get clientspec name
					string ClientName = TurnkeyUtils.ReadInput("Enter clientspec name:", string.Format("{0}_sdks", Username));

					// get local pathname
					string LocalPath = TurnkeyUtils.ReadInput("Enter local path:", @"C:\Sdks");

					// create a client from the input settings
					P4ClientInfo NewClient = new P4ClientInfo();
					NewClient.Name = ClientName;
					NewClient.Owner = Username;
					NewClient.Host = Hostname;
					NewClient.RootPath = LocalPath;
					NewClient.Stream = Stream;

					PerforceClient = PerforceConnection.CreateClient(NewClient);
				}

				if (PerforceClient == null)
				{
					TurnkeyUtils.Log("Unable to find or create a client, will not perform perforce copy operation {0}", Operation);
					return false;
				}

				// now connect to that 
				PerforceConnection = new P4Connection(Username, PerforceClient.Name);

				// @todo turnkey: how to check that for errors?

				ConnectedStream = Stream;
			}

			return true;
		}


	}
}
