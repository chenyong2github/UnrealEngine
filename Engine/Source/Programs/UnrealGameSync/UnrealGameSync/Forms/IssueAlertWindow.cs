// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.Collections.Generic;

namespace UnrealGameSync
{
	partial class IssueAlertWindow : Form
	{
		static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);

		const uint SWP_NOMOVE = 0x0002;
		const uint SWP_NOSIZE = 0x0001;
		const uint SWP_NOACTIVATE = 0x0010;

		[DllImport("user32.dll", SetLastError=true)]
		static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int Width, int Height, uint Flags);

		public readonly IssueMonitor IssueMonitor;

		public bool bIsWarning;

		public bool bStrongAlert = false;

		public IssueAlertWindow(IssueMonitor IssueMonitor, IssueData Issue, IssueAlertReason Reason)
		{
			this.IssueMonitor = IssueMonitor;
			this.Issue = Issue;

			InitializeComponent();

			SetIssue(Issue, Reason);
		}

		public IssueData Issue
		{
			get;
			private set;
		}

		public IssueAlertReason Reason
		{
			get;
			private set;
		}

		protected override void OnPaintBackground(PaintEventArgs e)
		{
			base.OnPaintBackground(e);

			if(bStrongAlert)
			{
				Color StripeColor = bIsWarning? Color.FromArgb(216, 167, 64) : Color.FromArgb(214, 69, 64);
				using(Brush StripeBrush = new SolidBrush(StripeColor))
				{
					e.Graphics.FillRectangle(StripeBrush, 0, 0, Bounds.Width - 1, Bounds.Height - 1);
				}
			}
			else
			{
				Color StripeColor = bIsWarning? Color.FromArgb(216, 167, 64) : Color.FromArgb(214, 69, 64);

				Color BackgroundColor = Color.FromArgb(241, 236, 236);
				using(Brush SolidBrush = new SolidBrush(BackgroundColor))
				{
	//				e.Graphics.FillRectangle(SolidBrush, 0, 0, Bounds.Width - 1, Bounds.Height - 1);
				}

				Color BorderColor = StripeColor; 
				using(Pen Pen = new Pen(Color.FromArgb(128, 128, 128)))
				{
					e.Graphics.DrawRectangle(Pen, 0, 0, Bounds.Width - 1, Bounds.Height - 1);
				}

				using(Brush StripeBrush = new SolidBrush(StripeColor))
				{
					e.Graphics.FillRectangle(StripeBrush, 0, 0, /*6*/10 * e.Graphics.DpiX / 96.0f, Bounds.Height);
				}
			}
		}

		public void SetIssue(IssueData NewIssue, IssueAlertReason NewReason)
		{
			StringBuilder OwnerTextBuilder = new StringBuilder();
			if(NewIssue.Owner == null)
			{
				OwnerTextBuilder.Append("Currently unassigned.");
			}
			else
			{
				if(String.Compare(NewIssue.Owner, IssueMonitor.UserName, StringComparison.OrdinalIgnoreCase) == 0 && NewIssue.NominatedBy != null)
				{
					OwnerTextBuilder.AppendFormat("You have been nominated to fix this issue by {0}.", Utility.FormatUserName(NewIssue.NominatedBy));
				}
				else
				{
					OwnerTextBuilder.AppendFormat("Assigned to {0}", Utility.FormatUserName(NewIssue.Owner));
					if(NewIssue.NominatedBy != null)
					{
						OwnerTextBuilder.AppendFormat(" by {0}", Utility.FormatUserName(NewIssue.NominatedBy));
					}
					OwnerTextBuilder.Append(".");
				}
			}
			OwnerTextBuilder.AppendFormat(" Open for {0}.", Utility.FormatDurationMinutes((int)(NewIssue.RetrievedAt - NewIssue.CreatedAt).TotalMinutes));
			string OwnerText = OwnerTextBuilder.ToString();

			bool bNewIsWarning = Issue.Builds.Count > 0 && !Issue.Builds.Any(x => x.Outcome != IssueBuildOutcome.Warning);

			Issue = NewIssue;

			if(NewIssue.Summary != SummaryLabel.Text || OwnerText != OwnerLabel.Text || Reason != NewReason || bIsWarning != bNewIsWarning)
			{
				Rectangle PrevBounds = Bounds;
				SuspendLayout();

				SummaryLabel.Text = NewIssue.Summary;
				OwnerLabel.Text = OwnerText;

				if(bStrongAlert)
				{
					SummaryLabel.ForeColor = Color.FromArgb(255, 255, 255);
					SummaryLabel.LinkColor = Color.FromArgb(255, 255, 255);
					OwnerLabel.ForeColor = Color.FromArgb(255, 255, 255);
//					AcceptBtn.BackgroundColor1 = Color.FromArgb(214, 69, 64);//Color.FromArgb(255, 255, 255);
//					AcceptBtn.BackgroundColor2 = Color.FromArgb(214, 69, 64);//Color.FromArgb(255, 255, 255);
//					AcceptBtn.BorderColor = Color.FromArgb(255, 255, 255);
//					AcceptBtn.ForeColor = Color.FromArgb(255, 255, 255);//DetailsBtn.ForeColor;
					AcceptBtn.Margin = new Padding(0,0, 20, 0);//.Right = 20;
					LatestBuildLinkLabel.LinkColor = Color.FromArgb(255, 255, 255);
				}

				if(bIsWarning != bNewIsWarning)
				{
					bIsWarning = bNewIsWarning;
					IconPictureBox.Image = bIsWarning? Properties.Resources.AlertWarningIcon : Properties.Resources.AlertErrorIcon;
				}

				if(Reason != NewReason)
				{
					Reason = NewReason;

					List<Button> Buttons = new List<Button>();
					if(!bStrongAlert)
					{
						Buttons.Add(DetailsBtn);
					}
					if((NewReason & IssueAlertReason.Owner) != 0)
					{
						AcceptBtn.Text = "Acknowledge";
						Buttons.Add(AcceptBtn);
					}
					else if((NewReason & IssueAlertReason.Normal) != 0)
					{
						AcceptBtn.Text = "Will Fix";
						Buttons.Add(AcceptBtn);
						DeclineBtn.Text = "Not Me";
						Buttons.Add(DeclineBtn);
					}

					tableLayoutPanel3.ColumnCount = Buttons.Count;
					tableLayoutPanel3.Controls.Clear();
					for(int Idx = 0; Idx < Buttons.Count; Idx++)
					{
						tableLayoutPanel3.Controls.Add(Buttons[Idx], Idx, 0);
					}
				}

				ResumeLayout(true);
				Location = new Point(PrevBounds.Right - Bounds.Width, PrevBounds.Y);
				Invalidate();
			}
		}

		protected override void OnHandleCreated(EventArgs e)
		{
			base.OnHandleCreated(e);

			// Manually make the window topmost, so that we can pass SWP_NOACTIVATE
			SetWindowPos(Handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}

		protected override bool ShowWithoutActivation 
		{
			get { return true; }
		}

		private void SummaryLabel_LinkClicked(object sender, System.Windows.Forms.LinkLabelLinkClickedEventArgs e)
		{
			LaunchUrl();
		}

		private void IssueAlertWindow_Click(object sender, EventArgs e)
		{
			LaunchUrl();
		}

		public void LaunchUrl()
		{
			IssueBuildData LastBuild = Issue.Builds.OrderByDescending(x => x.Change).FirstOrDefault();
			if(LastBuild != null)
			{
				System.Diagnostics.Process.Start(LastBuild.Url);
			}
		}
	}
}
