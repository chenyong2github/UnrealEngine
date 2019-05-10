// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealGameSync
{
	partial class IssueBrowserWindow
	{
		/// <summary>
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.IContainer components = null;

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}
			base.Dispose(disposing);
		}

		#region Windows Form Designer generated code

		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
            this.IssueListView = new UnrealGameSync.CustomListViewControl();
            this.IconHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.IdHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.CreatedHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.ResolvedHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.DescriptionHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.OkBtn = new System.Windows.Forms.Button();
            this.DetailsBtn = new System.Windows.Forms.Button();
            this.tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
            this.flowLayoutPanel1 = new System.Windows.Forms.FlowLayoutPanel();
            this.StatusLabel = new System.Windows.Forms.Label();
            this.FetchMoreResultsLinkLabel = new System.Windows.Forms.LinkLabel();
            this.tableLayoutPanel2 = new System.Windows.Forms.TableLayoutPanel();
            this.OwnerHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.tableLayoutPanel1.SuspendLayout();
            this.flowLayoutPanel1.SuspendLayout();
            this.tableLayoutPanel2.SuspendLayout();
            this.SuspendLayout();
            // 
            // IssueListView
            // 
            this.IssueListView.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.IssueListView.Columns.AddRange(new System.Windows.Forms.ColumnHeader[] {
            this.IconHeader,
            this.IdHeader,
            this.CreatedHeader,
            this.ResolvedHeader,
            this.OwnerHeader,
            this.DescriptionHeader});
            this.IssueListView.FullRowSelect = true;
            this.IssueListView.HideSelection = false;
            this.IssueListView.Location = new System.Drawing.Point(3, 3);
            this.IssueListView.Margin = new System.Windows.Forms.Padding(3, 3, 3, 8);
            this.IssueListView.MultiSelect = false;
            this.IssueListView.Name = "IssueListView";
            this.IssueListView.OwnerDraw = true;
            this.IssueListView.Size = new System.Drawing.Size(1049, 436);
            this.IssueListView.TabIndex = 0;
            this.IssueListView.UseCompatibleStateImageBehavior = false;
            this.IssueListView.View = System.Windows.Forms.View.Details;
            this.IssueListView.DrawColumnHeader += new System.Windows.Forms.DrawListViewColumnHeaderEventHandler(this.IssueListView_DrawColumnHeader);
            this.IssueListView.DrawItem += new System.Windows.Forms.DrawListViewItemEventHandler(this.IssueListView_DrawItem);
            this.IssueListView.DrawSubItem += new System.Windows.Forms.DrawListViewSubItemEventHandler(this.IssueListView_DrawSubItem);
            this.IssueListView.SelectedIndexChanged += new System.EventHandler(this.IssueListView_SelectedIndexChanged);
            this.IssueListView.MouseDoubleClick += new System.Windows.Forms.MouseEventHandler(this.IssueListView_MouseDoubleClick);
            // 
            // IconHeader
            // 
            this.IconHeader.Text = "";
            this.IconHeader.Width = 28;
            // 
            // IdHeader
            // 
            this.IdHeader.Text = "Id";
            this.IdHeader.TextAlign = System.Windows.Forms.HorizontalAlignment.Center;
            // 
            // CreatedHeader
            // 
            this.CreatedHeader.Text = "Created";
            this.CreatedHeader.TextAlign = System.Windows.Forms.HorizontalAlignment.Center;
            this.CreatedHeader.Width = 117;
            // 
            // ResolvedHeader
            // 
            this.ResolvedHeader.Text = "Resolved";
            this.ResolvedHeader.TextAlign = System.Windows.Forms.HorizontalAlignment.Center;
            this.ResolvedHeader.Width = 125;
            // 
            // DescriptionHeader
            // 
            this.DescriptionHeader.Text = "Description";
            this.DescriptionHeader.Width = 567;
            // 
            // OkBtn
            // 
            this.OkBtn.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.OkBtn.Location = new System.Drawing.Point(956, 3);
            this.OkBtn.Name = "OkBtn";
            this.OkBtn.Size = new System.Drawing.Size(96, 29);
            this.OkBtn.TabIndex = 3;
            this.OkBtn.Text = "Ok";
            this.OkBtn.UseVisualStyleBackColor = true;
            this.OkBtn.Click += new System.EventHandler(this.OkBtn_Click);
            // 
            // DetailsBtn
            // 
            this.DetailsBtn.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.DetailsBtn.Location = new System.Drawing.Point(3, 3);
            this.DetailsBtn.Name = "DetailsBtn";
            this.DetailsBtn.Size = new System.Drawing.Size(114, 29);
            this.DetailsBtn.TabIndex = 4;
            this.DetailsBtn.Text = "Details";
            this.DetailsBtn.UseVisualStyleBackColor = true;
            this.DetailsBtn.Click += new System.EventHandler(this.DetailsBtn_Click);
            // 
            // tableLayoutPanel1
            // 
            this.tableLayoutPanel1.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.tableLayoutPanel1.AutoSize = true;
            this.tableLayoutPanel1.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
            this.tableLayoutPanel1.ColumnCount = 3;
            this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel1.Controls.Add(this.OkBtn, 2, 0);
            this.tableLayoutPanel1.Controls.Add(this.DetailsBtn, 0, 0);
            this.tableLayoutPanel1.Controls.Add(this.flowLayoutPanel1, 1, 0);
            this.tableLayoutPanel1.Location = new System.Drawing.Point(0, 447);
            this.tableLayoutPanel1.Margin = new System.Windows.Forms.Padding(0);
            this.tableLayoutPanel1.Name = "tableLayoutPanel1";
            this.tableLayoutPanel1.RowCount = 1;
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel1.Size = new System.Drawing.Size(1055, 35);
            this.tableLayoutPanel1.TabIndex = 5;
            // 
            // flowLayoutPanel1
            // 
            this.flowLayoutPanel1.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.flowLayoutPanel1.AutoSize = true;
            this.flowLayoutPanel1.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
            this.flowLayoutPanel1.Controls.Add(this.StatusLabel);
            this.flowLayoutPanel1.Controls.Add(this.FetchMoreResultsLinkLabel);
            this.flowLayoutPanel1.Location = new System.Drawing.Point(473, 10);
            this.flowLayoutPanel1.Name = "flowLayoutPanel1";
            this.flowLayoutPanel1.Size = new System.Drawing.Size(127, 15);
            this.flowLayoutPanel1.TabIndex = 0;
            this.flowLayoutPanel1.WrapContents = false;
            // 
            // StatusLabel
            // 
            this.StatusLabel.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.StatusLabel.AutoSize = true;
            this.StatusLabel.Location = new System.Drawing.Point(3, 0);
            this.StatusLabel.Name = "StatusLabel";
            this.StatusLabel.Size = new System.Drawing.Size(39, 15);
            this.StatusLabel.TabIndex = 3;
            this.StatusLabel.Text = "Status";
            // 
            // FetchMoreResultsLinkLabel
            // 
            this.FetchMoreResultsLinkLabel.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.FetchMoreResultsLinkLabel.AutoSize = true;
            this.FetchMoreResultsLinkLabel.Location = new System.Drawing.Point(48, 0);
            this.FetchMoreResultsLinkLabel.Name = "FetchMoreResultsLinkLabel";
            this.FetchMoreResultsLinkLabel.Size = new System.Drawing.Size(76, 15);
            this.FetchMoreResultsLinkLabel.TabIndex = 4;
            this.FetchMoreResultsLinkLabel.TabStop = true;
            this.FetchMoreResultsLinkLabel.Text = "Fetch more...";
            this.FetchMoreResultsLinkLabel.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.FetchMoreResultsLinkLabel_LinkClicked);
            // 
            // tableLayoutPanel2
            // 
            this.tableLayoutPanel2.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.tableLayoutPanel2.ColumnCount = 1;
            this.tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel2.Controls.Add(this.IssueListView, 0, 0);
            this.tableLayoutPanel2.Controls.Add(this.tableLayoutPanel1, 0, 1);
            this.tableLayoutPanel2.Location = new System.Drawing.Point(12, 12);
            this.tableLayoutPanel2.Margin = new System.Windows.Forms.Padding(0);
            this.tableLayoutPanel2.Name = "tableLayoutPanel2";
            this.tableLayoutPanel2.RowCount = 2;
            this.tableLayoutPanel2.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel2.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel2.Size = new System.Drawing.Size(1055, 482);
            this.tableLayoutPanel2.TabIndex = 6;
            // 
            // OwnerHeader
            // 
            this.OwnerHeader.Text = "Owner";
            this.OwnerHeader.TextAlign = System.Windows.Forms.HorizontalAlignment.Center;
            this.OwnerHeader.Width = 135;
            // 
            // IssueBrowserWindow
            // 
            this.AcceptButton = this.OkBtn;
            this.AutoScaleDimensions = new System.Drawing.SizeF(7F, 15F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(1079, 506);
            this.Controls.Add(this.tableLayoutPanel2);
            this.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(204)));
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.Name = "IssueBrowserWindow";
            this.ShowIcon = false;
            this.ShowInTaskbar = false;
            this.StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
            this.Text = "Issues";
            this.Load += new System.EventHandler(this.IssueBrowserWindow_Load);
            this.tableLayoutPanel1.ResumeLayout(false);
            this.tableLayoutPanel1.PerformLayout();
            this.flowLayoutPanel1.ResumeLayout(false);
            this.flowLayoutPanel1.PerformLayout();
            this.tableLayoutPanel2.ResumeLayout(false);
            this.tableLayoutPanel2.PerformLayout();
            this.ResumeLayout(false);

		}

		#endregion

		private CustomListViewControl IssueListView;
		private System.Windows.Forms.ColumnHeader IdHeader;
		private System.Windows.Forms.ColumnHeader CreatedHeader;
		private System.Windows.Forms.ColumnHeader ResolvedHeader;
		private System.Windows.Forms.ColumnHeader DescriptionHeader;
		private System.Windows.Forms.Button OkBtn;
		private System.Windows.Forms.Button DetailsBtn;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
		private System.Windows.Forms.FlowLayoutPanel flowLayoutPanel1;
		private System.Windows.Forms.Label StatusLabel;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel2;
		private System.Windows.Forms.LinkLabel FetchMoreResultsLinkLabel;
		private System.Windows.Forms.ColumnHeader IconHeader;
		private System.Windows.Forms.ColumnHeader OwnerHeader;
	}
}