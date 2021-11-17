// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Datadog.Trace.DuckTyping;
using Horde.Storage.Implementation;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using StatsdClient;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/status")]
    public class StatusController : Controller
    {
        private readonly VersionFile _versionFile;
        private readonly IOptionsMonitor<ReplicationSettings> _replicationSettings;

        public StatusController(VersionFile versionFile, IOptionsMonitor<ReplicationSettings> replicationSettings)
        {
            _versionFile = versionFile;
            _replicationSettings = replicationSettings;
        }

        /// <summary>
        /// Fetch information about Jupiter
        /// </summary>
        /// <remarks>
        /// General information about the Jupiter service, which version it is running and more.
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
            return Ok(new StatusResponse(_versionFile.VersionString ?? "Unknown", srcControlIdentifier, GetCapabilities(), _replicationSettings.CurrentValue.CurrentSite));
        }

        private string[] GetCapabilities()
        {
            return new string[]
            {
                "transactionlog",
                "ddc",
                "tree"
            };
        }
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
