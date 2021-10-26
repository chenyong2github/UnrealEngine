// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;
using System.Reflection;
using System.Diagnostics;

namespace HordeServer.Api
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
        public string OSDescription { get; set; }

		/// <summary>
		/// Whether this is an installed Horde build
		/// </summary>
		public bool SingleInstance { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public GetServerInfoResponse( bool SingleInstance)
        {

            FileVersionInfo VersionInfo = FileVersionInfo.GetVersionInfo(Assembly.GetExecutingAssembly().Location);		
			ServerVersion = VersionInfo.ProductVersion ?? String.Empty;			
			OSDescription = RuntimeInformation.OSDescription;
			this.SingleInstance = SingleInstance;
		}
	}
}

