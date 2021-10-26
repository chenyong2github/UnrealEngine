// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Net.Security;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using EpicGames.Core;

namespace HordeAgent.Utility
{
	/// <summary>
	/// Helper functions for dealing with certificates
	/// </summary>
	static class CertificateHelper
	{
		/// <summary>
		/// Provides additional diagnostic information for SSL certificate validation
		/// </summary>
		/// <param name="Logger">The logger instance</param>
		/// <param name="Sender"></param>
		/// <param name="Certificate"></param>
		/// <param name="Chain"></param>
		/// <param name="SslPolicyErrors"></param>
		/// <param name="ServerProfile">The server profile</param>
		/// <returns>True if the certificate is allowed, false otherwise</returns>
		public static bool CertificateValidationCallBack(ILogger Logger, object Sender, X509Certificate Certificate, X509Chain Chain, SslPolicyErrors SslPolicyErrors, ServerProfile ServerProfile)
		{
			// If the certificate is a valid, signed certificate, return true.
			if (SslPolicyErrors == SslPolicyErrors.None)
			{
				return true;
			}

			// Trust the remote certificate if it has the right thumbprint
			if (SslPolicyErrors == SslPolicyErrors.RemoteCertificateChainErrors)
			{
				if (Chain.ChainElements.Count == 1)
				{
					X509ChainElement Element = Chain.ChainElements[0];
					if (Element.ChainElementStatus.Length == 1 && (Element.ChainElementStatus[0].Status == X509ChainStatusFlags.UntrustedRoot || Element.ChainElementStatus[0].Status == X509ChainStatusFlags.PartialChain))
					{
						if (ServerProfile.IsTrustedCertificate(Element.Certificate.Thumbprint))
						{
							Logger.LogDebug("Trusting server certificate {Thumbprint}", Element.Certificate.Thumbprint);
							return true;
						}
					}

				}
			}

			// Generate diagnostic information
			StringBuilder Builder = new StringBuilder();
			if (Sender != null)
			{
				HttpRequestMessage? Message = Sender as HttpRequestMessage;
				if (Message != null)
				{
					Builder.Append($"\nSender: {Message.Method} {Message.RequestUri}");
				}
				else
				{
					string SenderInfo = StringUtils.Indent(Sender.ToString() ?? String.Empty, "    ");
					Builder.Append($"\nSender:\n{SenderInfo}");
				}
			}
			if (Certificate != null)
			{
				Builder.Append($"\nCertificate: {Certificate.Subject}");
			}
			if (Chain != null)
			{
				if (Chain.ChainStatus != null && Chain.ChainStatus.Length > 0)
				{
					Builder.Append("\nChain status:");
					foreach (X509ChainStatus Status in Chain.ChainStatus)
					{
						Builder.Append($"\n  {Status.StatusInformation}");
					}
				}
				if (Chain.ChainElements != null)
				{
					Builder.Append("\nChain elements:");
					for (int Idx = 0; Idx < Chain.ChainElements.Count; Idx++)
					{
						X509ChainElement Element = Chain.ChainElements[Idx];
						Builder.Append($"\n  {Idx,4} - Certificate: {Element.Certificate.Subject}");
						Builder.Append($"\n         Thumbprint: {Element.Certificate.Thumbprint}");
						if (Element.ChainElementStatus != null && Element.ChainElementStatus.Length > 0)
						{
							foreach (X509ChainStatus Status in Element.ChainElementStatus)
							{
								Builder.Append($"\n         Status: {Status.StatusInformation} ({Status.Status})");
							}
						}
						if (!String.IsNullOrEmpty(Element.Information))
						{
							Builder.Append($"\n         Info: {Element.Information}");
						}
					}
				}
			}

			// Print out additional diagnostic information
			Logger.LogError("TLS certificate validation failed ({Errors}).{AdditionalInfo}", SslPolicyErrors, StringUtils.Indent(Builder.ToString(), "    "));
			return false;
		}
	}
}
