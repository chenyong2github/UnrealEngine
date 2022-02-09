// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Horde.Storage.Implementation;
using Jupiter;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using Newtonsoft.Json;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/status")]
    public class StatusController : Controller
    {
        private readonly VersionFile _versionFile;
        private readonly IOptionsMonitor<JupiterSettings> _jupiterSettings;
        private readonly IOptionsMonitor<ClusterSettings> _clusterSettings;

        public StatusController(VersionFile versionFile, IOptionsMonitor<JupiterSettings> jupiterSettings, IOptionsMonitor<ClusterSettings> clusterSettings)
        {
            _versionFile = versionFile;
            _jupiterSettings = jupiterSettings;
            _clusterSettings = clusterSettings;
        }

        /// <summary>
        /// Fetch information about Horde Storage
        /// </summary>
        /// <remarks>
        /// General information about the service, which version it is running and more.
        /// </remarks>
        /// <returns></returns>
        [HttpGet("")]
        [Authorize("Any")]
        [ProducesResponseType(type: typeof(StatusResponse), 200)]
        public IActionResult Status()
        {
            IEnumerable<AssemblyMetadataAttribute> attrs = typeof(StatusController).Assembly.GetCustomAttributes<AssemblyMetadataAttribute>();

            string srcControlIdentifier = "Unknown";
            AssemblyMetadataAttribute? gitHashAttribute = attrs.FirstOrDefault(attr => attr.Key == "GitHash");
            if (gitHashAttribute?.Value != null && !string.IsNullOrEmpty(gitHashAttribute.Value))
                srcControlIdentifier = gitHashAttribute.Value;

            AssemblyMetadataAttribute? p4ChangeAttribute = attrs.FirstOrDefault(attr => attr.Key == "PerforceChangelist");
            if (p4ChangeAttribute?.Value != null && !string.IsNullOrEmpty(p4ChangeAttribute.Value))
                srcControlIdentifier = p4ChangeAttribute.Value;
            return Ok(new StatusResponse(_versionFile.VersionString ?? "Unknown", srcControlIdentifier, GetCapabilities(), _jupiterSettings.CurrentValue.CurrentSite));
        }

        private string[] GetCapabilities()
        {
            return new string[]
            {
                "transactionlog",
                "ddc"
            };
        }

        /// <summary>
        /// Fetch information about other deployments
        /// </summary>
        /// <remarks>
        /// General information about the Jupiter service, which version it is running and more.
        /// </remarks>
        /// <returns></returns>
        [HttpGet("peers")]
        [Authorize("Any")]
        [ProducesResponseType(type: typeof(PeersResponse), 200)]
        public IActionResult Peers([FromQuery] bool includeInternalEndpoints = false)
        {
            return Ok(new PeersResponse(_jupiterSettings, _clusterSettings, includeInternalEndpoints));
        }
    }

    public class PeersResponse
    {
        [JsonConstructor]
        public PeersResponse(string currentSite, List<KnownPeer> peers)
        {
            CurrentSite = currentSite;
            Peers = peers;
        }

        public PeersResponse(IOptionsMonitor<JupiterSettings> jupiterSettings, IOptionsMonitor<ClusterSettings> clusterSettings, bool includeInternalEndpoints)
        {
            CurrentSite = jupiterSettings.CurrentValue.CurrentSite;
            Peers = clusterSettings.CurrentValue.Peers.Select(settings => new KnownPeer(settings, includeInternalEndpoints)).ToList();
        }

        public string CurrentSite { get; set; }

        public List<KnownPeer> Peers { get; set; } = new List<KnownPeer>();
    }

    public class KnownPeer
    {
        [JsonConstructor]
        public KnownPeer(string site, string fullName, List<string> endpoints)
        {
            Site = site;
            FullName = fullName;
            Endpoints = endpoints;
        }

        public KnownPeer(PeerSettings peerSettings, bool includeInternalEndpoints)
        {
            Site = peerSettings.Name;
            FullName = peerSettings.FullName;
            IEnumerable<PeerEndpoints> endpoints = peerSettings.Endpoints;
            if (!includeInternalEndpoints)
                endpoints = endpoints.Where(s => !s.IsInternal);
            Endpoints = endpoints.Select(e => e.Url).ToList();
        }

        public string Site { get; set; }
        public string FullName { get; set; }

        public List<string> Endpoints { get; set; }
    }

    public class StatusResponse
    {
        public StatusResponse(string version, string gitHash, string[] capabilities, string siteIdentifier)
        {
            Version = version;
            GitHash = gitHash;
            Capabilities = capabilities;
            SiteIdentifier = siteIdentifier;
        }

        public string Version { get; set; }
        public string GitHash { get; set; }
        public string[] Capabilities { get; set; }
        public string SiteIdentifier { get; set; }
    }
}
