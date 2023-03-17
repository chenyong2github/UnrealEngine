// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Horde.Server.Server
{
	/// <summary>
	/// Server Info
	/// </summary>
	public class GetServerInfoResponse
	{
        /// <summary>
		/// Server version info
		/// </summary>
        public string ServerVersion { get; set; }

        /// <summary>
        /// The operating system server is hosted on
        /// </summary>
        public string OsDescription { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public GetServerInfoResponse()
        {
            FileVersionInfo versionInfo = FileVersionInfo.GetVersionInfo(Assembly.GetExecutingAssembly().Location);		
			ServerVersion = versionInfo.ProductVersion ?? String.Empty;			
			OsDescription = RuntimeInformation.OSDescription;			
		}
	}
}

