// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
using EpicGames.Core;
using Horde.Build.Commands;
using HordeServer;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Build.Tests
{
	[TestClass]
	public class ProgramTests
	{
		[TestMethod]
		public void TestReadGrpcCertificate()
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				// Test only works on Windows as .FriendlyName cannot be set on Unix.
				return;
			}
			
			string FriendlyName = "A testing cert";
			byte[] TempCertData = CertificateUtils.CreateSelfSignedCert("testing.epicgames.com", FriendlyName);
			string TempCertPath = Path.GetTempFileName();
			File.WriteAllBytes(TempCertPath, TempCertData);
			
			// No cert given
			Assert.IsNull(ServerCommand.ReadGrpcCertificate(new () { ServerPrivateCert = null }));

			// Cert as file path
			{
				X509Certificate2? Cert = ServerCommand.ReadGrpcCertificate(new() { ServerPrivateCert = TempCertPath });
				Assert.IsNotNull(Cert);
				Assert.AreEqual(FriendlyName, Cert!.FriendlyName);
			}

			// Cert as base64 data
			{
				string TempCertBase64 = Convert.ToBase64String(TempCertData);
				X509Certificate2? Cert = ServerCommand.ReadGrpcCertificate(new() { ServerPrivateCert = "base64:" + TempCertBase64 });
				Assert.IsNotNull(Cert);
				Assert.AreEqual(FriendlyName, Cert!.FriendlyName);	
			}
			
			File.Delete(TempCertPath);
		}
	}
}
