// Copyright Epic Games, Inc. All Rights Reserved.

//  Horde backend API types

// The session type
export enum SessionType {

	// Execute a command
	Job = 0,

	// Upgrade the agent software
	Upgrade = 1
}

export enum Priority {

	// Lowest priority
	Lowest = "Lowest",

	// Below normal priority
	BelowNormal = "BelowNormal",

	// Normal priority
	Normal = "Normal",

	// Above normal priority
	AboveNormal = "AboveNormal",

	// High priority
	High = "High",

	// Highest priority
	Highest = "Highest"
}

// The state of a jobstep
export enum JobStepState {

	// Waiting for dependencies of this step to complete (or paused)
	Waiting = "Waiting",

	// Ready to run, but has not been scheduled yet
	Ready = "Ready",

	// Dependencies of this step failed, so it cannot be executed
	Skipped = "Skipped",

	// There is an active instance of this step running
	Running = "Running",

	// This step has been run
	Completed = "Completed",

	// This step has been aborted
	Aborted = "Aborted"

}

// Outcome of a jobstep run
export enum JobStepOutcome {

	// Finished with errors
	Failure = "Failure",

	// Finished with warnings
	Warnings = "Warnings",

	// Finished Succesfully
	Success = "Success"
}

// The state of a particular run
export enum JobStepBatchState {

	// Waiting for dependencies of at least one jobstep to complete
	Waiting = "Waiting",

	// Ready to execute
	Ready = "Ready",

	// Currently running
	Running = "Running",

	// All steps have finished executing
	Complete = "Complete"
}

/**Error code for a batch not being executed */
export enum JobStepBatchError {

	/** No error */
	None = "None",

	/** The stream for this job is unknown */
	UnknownStream = "UnknownStream",

	/** The given agent type for this batch was not valid for this stream */
	UnknownAgentType = "UnknownAgentType",

	/** The pool id referenced by the agent type was not found */
	UnknownPool = "UnknownPool",

	/** There are no agents in the given pool currently online */
	NoAgentsInPool = "NoAgentsInPool",

	/** There are no agents in this pool that are onlinbe */
	NoAgentsOnline = "NoAgentsOnline",

	/** Unknown workspace referenced by the agent type */
	UnknownWorkspace = "UnknownWorkspace",

	/** Cancelled */
	Cancelled = "Cancelled",

	/** Lost connection with the agent machine */
	LostConnection = "LostConnection",

	/** Lease terminated prematurely */
	Incomplete = "Incomplete"

}

// The type of a template parameter
export enum TemplateParameterType {

	// An arbitrary string
	String = "String"
}

export enum EventSeverity {

	Unspecified = "Unspecified",
	Information = "Information",
	Warning = "Warning",
	Error = "Error"
}

export enum AclActions {

	// No actions allowed	
	None = "None",

	// Reading to the object	
	Read = "Read",

	// Modifying the object	
	Write = "Write",

	// Creating new objects	
	Create = "Create",

	// Deleting the object	
	Delete = "Delete",

	// Executing the object	
	Execute = "Execute",

	// Change permissions on the object	
	ChangePermissions = "ChangePermissions"

}

export enum SubscriptonNotificationType {
	// Slack notification
	Slack = "Slack",
}

// aliases and extensions
export type AgentData = GetAgentResponse
export type LeaseData = GetAgentLeaseResponse
export type SessionData = GetAgentSessionResponse
export type PoolData = GetPoolResponse
export type GroupData = GetGroupResponse
export type NodeData = GetNodeResponse
export type BatchData = GetBatchResponse
export type EventData = GetLogEventResponse
export type LogData = GetLogFileResponse
export type ArtifactData = GetArtifactResponse
export type AclData = GetAclResponse
export type AclEntryData = GetAclEntryResponse
export type SoftwareData = GetSoftwareResponse
export type ScheduleData = GetScheduleResponse
export type ChangeSummaryData = GetChangeSummaryResponse
export type JobsTabData = GetJobsTabResponse
export type JobsTabColumnData = GetJobsTabColumnResponse
export type TimingInfo = GetTimingInfoResponse
export type StepTimingInfo = GetStepTimingInfoResponse
export type SubscriptionData = GetSubscriptionResponse

export type IssueData = GetIssueResponse & {
	events?: GetLogEventResponse[];
}

export type LabelData = GetLabelResponse & {
	defaultLabel?: GetDefaultLabelStateResponse;
}

export type ProjectData = GetProjectResponse & {
	streams?: StreamData[];
}

export type StreamData = GetStreamResponse & {
	project?: ProjectData;
	// full path as returned by server
	fullname?: string;

}

export type TemplateData = GetTemplateResponse & {
	ref?: GetTemplateRefResponse;
}

export type JobData = GetJobResponse & {
	graphRef?: GetGraphResponse;
}

export type StepData = GetStepResponse & {

	timing?: GetStepTimingInfoResponse;
}

export type TestData = GetTestDataResponse & {
	data: object;
}

export enum LogType {
	JSON = "JSON",
	TEXT = "TEXT"
}

export enum LogLevel {
	Informational = "Informational",
	Warning = "Warning",
	Error = "Error"
}

export type LogLine = {
	time: string;
	level: LogLevel;
	message: string;
	id?: number;
	format?: string;
	properties?: Record<string, string | Record<string, string | number>>;
}

export type LogLineData = {
	format: LogType;
	index: number;
	count: number;
	maxLineIndex: number;
	lines?: LogLine[];
};

export type JobQuery = {
	filter?: string;
	streamId?: string;
	name?: string;
	template?: string[];
	state?: string[];
	outcome?: string[];
	minChange?: number;
	maxChange?: number;
	preflightChange?: number;
	includePreflight?: boolean,
	preflightStartedByUser?: string,
	minCreateTime?: string;
	maxCreateTime?: string;
	modifiedAfter?: string;
	index?: number;
	count?: number;
};

export type IssueQuery = {
	jobId?: string;
	batchId?: string;
	stepId?: string;
	label?: number;
	streamId?: string;
	change?: number;
	minChange?: number;
	maxChange?: number;
	index?: number;
	count?: number;
	resolved?: boolean;
}

export type ScheduleQuery = {
	streamId?: string;
	filter?: string;
}


/* The severity of an issue */
export enum IssueSeverity {
	/** Unspecified severity */
	Unspecified = "Unspecified",

	/** This error represents a warning */
	Warning = "Warning",

	/** This issue represents an error */
	Error = "Error"
}

/**Parameters to register a new agent */
export type CreateAgentRequest = {

	/**Friendly name for the agent */
	name: string;

	/**Whether the agent is currently enabled */
	enabled: boolean;

	/**Whether the agent is ephemeral (ie. should not be shown when inactive) */
	ephemeral: boolean;

	/**Per-agent override for the desired client version */
	forceVersion?: string;

	/**Pools for this agent */
	pools?: string[];
}

/**Response from creating an agent */
export type CreateAgentResponse = {

	/**Unique id for the new agent */
	id: string;

	/**Bearer token for this agent to authorize itself with the server */
	token: string;
}

/**Parameters to update an agent */
export type UpdateAgentRequest = {

	/**New name of the agent */
	name?: string;

	/**Whether the agent is currently enabled */
	enabled?: boolean;

	/**comment for the agent */
	comment?: string;

	/**Whether the agent is ephemeral */
	ephemeral?: boolean;

	/**Boolean to request a conform */
	requestConform?: boolean;

	requestRestart?: boolean;

	/**Per-agent override for the desired client version */
	forceVersion?: string;

	/**Pools for this agent */
	pools?: string[];

	/**New ACL for this agent */
	acl?: UpdateAclRequest;

}

/**Parameters to create a pool */
export type CreatePoolRequest = {

	/**New name of the pool */
	name: string;

	/**Arbitrary properties associated with this event */
	properties?: { [key: string]: string };
}


/**Parameters to update a pool */
export type UpdatePoolRequest = {

	/**New name of the pool */
	name?: string;

	/**Arbitrary properties associated with this event */
	properties?: { [key: string]: string };
}

/**Parameters to update a pool */
export type BatchUpdatePoolRequest = {

	/**ID of the pool to update  */
	id: string;

	/**New name of the pool */
	name?: string;

	/**Arbitrary properties associated with this event */
	properties?: { [key: string]: string };

	/**Soft-delete for this pool */
	deleted?: boolean;
}

/**Response for queries to find a particular lease within an agent */
export type GetAgentLeaseResponse = {

	/**Identifier for the lease */
	id: string;

	/**Name of the lease */
	name?: string;

	/**Time at which the lease started (UTC) */
	startTime: Date | string;

	/**Time at which the lease ended (UTC) */
	finishTime?: Date | string;

	/**Whether this lease has started executing on the agent yet */
	executing: boolean;

	/**Details for this lease */
	details?: { [key: string]: string };

	/**agentId of lease */
	agentId?: string;

	/**What type of lease this is (parsed from Details array) */
	type: string;

	/**BatchId of this lease (parsed from Details array) */
	batchId: string;

	/**JobId of this lease (parsed from Details array) */
	jobId: string;

	/**logId of this lease (parsed from Details array) */
	logId: string;
}

/**Information about an agent session */
export type GetAgentSessionResponse = {

	/**Unique id for this session */
	id: string;

	/**Start time for this session */
	startTime: Date | string;

	/**Finishing time for this session */
	finishTime?: Date | string;

	/**Capabilities of this agent */
	capabilities?: GetAgentCapabilitiesResponse;

	/**Version of the software running during this session */
	version?: string;

}

export type GetAgentWorkspaceResponse = {

	/**Server and port */
	serverAndPort?: string;

	/**username */
	userName?: string;

	/**password */
	password?: string;

	/**identifier */
	identifier: string;

	/**stream */
	stream: string;

	/**view */
	view?: string[];

	/**incremental */
	bIncremental: boolean;
}

/**Information about an agent */
export type GetAgentResponse = {

	/**The agent's unique ID */
	id: string;

	/**Friendly name of the agent */
	name: string;

	/**Session ID */
	sessionId: string;

	/**Session expiry time */
	sessionExpiresAt: Date | string;

	/**Status of the agent */
	online: boolean;

	/**Whether the agent is currently enabled */
	enabled: boolean;

	/**Whether the agent is ephemeral */
	ephemeral: boolean;

	/**comment for the agent */
	comment?: string;

	/**The current client version */
	version?: string;

	/**Per-agent override for the desired client version */
	forceVersion?: string;

	/**Pools for this agent */
	pools?: string[];

	/**Capabilities of this agent */
	capabilities?: GetAgentCapabilitiesResponse;

	/**Array of active leases. */
	leases?: GetAgentLeaseResponse[];

	/**Per-object permissions */
	acl?: GetAclResponse;

	/**Last update time of the agent */
	updateTime: Date | string;

	/**Whether this agent is deleted */
	deleted: boolean;

	/** Whether agent is pending conform */
	pendingConform: boolean;

	/** Conform attempt count */
	conformAttemptCount?: number;

	/** Last conform time */
	lastConformTime?: Date | string;

	/** Next conform attempt time */
	nextConformTime?: Date | string;

	/** agent workspaces */
	workspaces: GetAgentWorkspaceResponse[];

}

/**Information about an agent */
export type GetPoolResponse = {

	/**The agent's unique ID */
	id: string;

	/**Friendly name of the agent */
	name: string;

	/**Arbitrary properties associated with this event */
	properties?: { [key: string]: string };

}


/**Parameters for creating a new event */
export type CreateEventRequest = {

	/**Time at which the event ocurred */
	time: Date | string;

	/**Severity of this event */
	severity: EventSeverity;

	/**Diagnostic text */
	message: string;

	/**Unique id of the log containing this event */
	logId: string;

	/**Minimum offset within the log containing this event */
	minOffset: number;

	/**Maximum offset within the log containing this event */
	maxOffset: number;

	/**Unique id of the agent associated with this event */
	agentId?: string;

	/**Unique id of the job associated with this event */
	jobId?: string;

	/**Unique id of the jobstep associated with this event */
	jobStepId?: string;

	/**Unique id of the jobstep associated with this event */
	jobBatchId?: string;

	/*The structured message data */
	data?: { [key: string]: string };

}

/**Information about an uploaded event */
export type GetLogEventResponse = {

	/**Unique id of the log containing this event */
	logId: string;

	/**Severity of this event */
	severity: EventSeverity;

	/**Index of the first line for this event */
	lineIndex: number;

	/**Number of lines in the event */
	lineCount: number;

	/**The issue id associated with this event */
	issueId: number | null;

	/**The structured message data for this event */
	lines: LogLine[];

}



/** Stats for a search */
export type LogSearchStats = {

	/// Number of blocks that were scanned
	numScannedBlocks: number;

	/// Number of bytes that had to be scanned for results
	numScannedBytes: number;

	/// Number of blocks that were skipped
	numSkippedBlocks: number;

	/// Number of blocks that had to be decompressed
	numDecompressedBlocks: number;

	/// Number of blocks that were searched but did not contain the search term
	numFalsePositiveBlocks: number;
}

/** Response describing a log file */
export type SearchLogFileResponse = {
	/// List of line numbers containing the search text
	lines: number[];

	/// Stats for the search
	stats?: LogSearchStats;
}


/**  Information about a group of nodes */
export type CreateAggregateRequest = {

	/** Name of the aggregate */
	name: string;

	/** Nodes which must be part of the job for the aggregate to be valid */
	nodes: string[];

}


/** Response from creating a new aggregate */
export type CreateAggregateResponse = {

	/** Index of the first aggregate that was added	*/
	firstIndex: number;
}

/**Information about a group of nodes */
export type CreateLabelRequest = {

	/**Category for this label */
	category: string;

	/**Name of the label */
	name: string;

	/**Nodes which must be part of the job for the label to be valid */
	requiredNodes: string[];

	/**Nodes which must be part of the job for the label to be valid */
	includedNodes: string[];
}



/** Query selecting the base changelist to use */
export type ChangeQueryRequest = {

	/** The template id to query */
	templateId?: string;

	/** The target to query	*/
	target?: string;
}


/**Parameters required to create a job */
export type CreateJobRequest = {

	/** The stream that this job belongs to */
	streamId: string;

	/** The template for this job */
	templateId: string;

	/** Name of the job */
	name?: string;

	/** The changelist number to build. Can be null for latest. */
	change?: number;

	/** Parameters to use when selecting the change to execute at. */
	changeQuery?: ChangeQueryRequest;

	/** The preflight changelist number */
	preflightChange?: number;

	/** Priority for the job */
	priority?: Priority;

	/** Whether to automatically submit the preflighted change on completion */
	autoSubmit?: boolean;

	/** Nodes for the new job */
	groups?: CreateGroupRequest[];

	/** Aggregates for the new job */
	aggregates?: CreateAggregateRequest[];

	/** Labels for the new job*/
	labels?: CreateLabelRequest[];

	/** Arguments for the job */
	arguments?: string[];
}

/**Response from creating a new job */
export type CreateJobResponse = {

	/**Unique id for the new job */
	id: string;
}

/**Updates an existing job */
export type UpdateJobRequest = {

	/** New name for the job */
	name?: string;

	/** New priority for the job */
	priority?: Priority;

	/**  Set whether the job should be automatically submitted or not */
	autoSubmit?: boolean;

	/** Mark this job as aborted */
	aborted?: boolean;

	/** New list of arguments for the job. Only -Target= arguments can be modified after the job has started.  */
	arguments?: string[];

	/**  Custom permissions for this object */
	acl?: UpdateAclRequest;
}

export enum ReportPlacement {
	/** On a panel of its own */
	Panel = "Panel",
	/** In the summary panel */
	Summary = "Summary"
}

export enum ReportScope {
	Job = "Job",
	Step = "Step"
}

/**Information about a report associated with a job*/
export type GetReportResponse = {

	// Report scope
	scope: ReportScope;

	// Report placement
	placement: ReportPlacement;

	/** Name of the report */
	name: string;

	/** The artifact id */
	artifactId: string;
}


/**Information about a job */
export type GetJobResponse = {

	/**Unique Id for the job */
	id: string;

	/**Unique id of the stream containing this job */
	streamId: string;

	/**Name of the job */
	name: string;

	/** The changelist number to build */
	change?: number;

	/** The preflight changelist number */
	preflightChange?: number;

	/** The template type */
	templateId?: string;

	/** Hash of the actual template data */
	templateHash?: string;

	/** Hash of the graph for this job */
	graphHash?: string;

	/** The user that started this job */
	startedByUser?: string;

	/** The user that aborted this job */
	abortedByUser?: string;

	/** The roles to impersonate when executing this job */
	roles: string[];

	/** Priority of the job */
	priority: Priority;

	/** Whether the change will automatically be submitted or not */
	autoSubmit?: boolean;

	/** The submitted changelist number */
	autoSubmitChange?: number;

	/**  Message produced by trying to auto-submit the change */
	autoSubmitMessage?: string;

	/**Time that the job was created */
	createTime: Date | string;

	/** The global job state */
	state: JobState;

	/** Array of jobstep batches */
	batches?: GetBatchResponse[];

	/**  List of labels */
	labels?: GetLabelStateResponse[];

	/** The default label, containing the state of all steps that are otherwise not matched. */
	defaultLabel?: GetDefaultLabelStateResponse;

	/** List of reports */
	reports?: GetReportResponse[];

	/**  Parameters for the job */
	arguments: string[];

	/**The last update time for this job*/
	updateTime: Date | string;

	/**  Custom permissions for this object */
	acl?: GetAclResponse;

}

/**Request used to update a jobstep */
export type UpdateStepRequest = {

	/**The new jobstep state */
	state?: JobStepState;

	/**Outcome from the jobstep */
	outcome?: JobStepOutcome;

	/**If the step has been requested to abort */
	abortRequested?: boolean;

	/**Specifies the log file id for this step */
	logId?: string;

	/**Whether the step should be re-run */
	retry?: boolean;

	/**New priority for this step */
	priority?: Priority;

	/**Properties to set. Any entries with a null value will be removed. */
	properties?: { [key: string]: string | null };
}

/**Returns information about a jobstep */
export type GetStepResponse = {

	/**The unique id of the step */
	id: string;

	/**Index of the node which this jobstep is to execute */
	nodeIdx: number;

	/**Current state of the job step. This is updated automatically when runs complete. */
	state: JobStepState;

	/**Current outcome of the jobstep */
	outcome: JobStepOutcome;

	/** If the step has been requested to abort	*/
	abortRequested?: boolean;

	/** Name of the user that requested the abort of this step */
	abortByUser?: string;

	/** Name of the user that requested this step be run again */
	retryByUser?: string;

	/**The log id for this step */
	logId?: string;

	/** Time at which the batch was ready (UTC). */
	readyTime?: Date | string;

	/**Time at which the run started (UTC). */
	startTime?: Date | string;

	/**Time at which the run finished (UTC) */
	finishTime: Date | string;

	/**User-defined properties for this jobstep. */
	properties: { [key: string]: string };

}

//**Returns information about test data */
export type GetTestDataResponse = {

	/**The unique id of the test data */
	id: string;

	/**The key associated with this test data */
	key: string;

	/**The change id related to the job */
	change: number;

	/**The job id of this test data */
	jobId: string;

	/**The step id of this test data */
	stepId: string;

	/**The stream id of this test data */
	streamId: string;

	/**The template ref id of the related job */
	templateRefId: string;
}

export type UpdateStepResponse = {
	/** If a new step is created (due to specifying the retry flag), specifies the batch id */
	batchId?: string;

	/** If a step is retried, includes the new step id */
	stepId?: string
}

/**Request to update a jobstep batch */
export type UpdateBatchRequest = {

	/**The new log file id */
	logId?: string;

	/**The state of this batch */
	state?: JobStepBatchState;
}

/**Information about a jobstep batch */
export type GetBatchResponse = {

	/**Unique id for this batch */
	id: string;

	/**The unique log file id */
	logId?: string;

	/**Index of the group being executed */
	groupIdx: number;

	/**The state of this batch */
	state: JobStepBatchState;

	/**Error code for this batch */
	error: JobStepBatchError;

	/**Steps within this run */
	steps: GetStepResponse[];

	/**The agent assigned to execute this group */
	agentId?: string;

	/**The agent session holding this lease */
	sessionId?: string;

	/**The lease that's executing this group */
	leaseId?: string;

	/**The priority of this batch */
	weightedPriority: number;

	/**Time at which the group started (UTC). */
	startTime?: Date | string;

	/**Time at which the group finished (UTC) */
	finishTime?: Date | string;

}

/**Describes the history of a step */
export interface GetJobStepRefResponse {
	/**The job id */
	jobId: string;

	/**The batch containing the step */
	batchId: string;

	/**The step identifier */
	stepId: string;

	/**The change number being built */
	change: number;

	/**The step log id */
	logId?: string;

	/**The pool id */
	poolId?: string;

	/** The agent id */
	agentId?: string;

	/**Outcome of the step, once complete. */
	outcome?: JobStepOutcome;

	/**Time at which the step started. */
	startTime: Date | string;

	/**Time at which the step finished. */
	finishTime?: Date | string;

}

export interface GetJobStepTraceResponse {

	Name: string;

	Start: number

	Finish: number

	Service?: string;

	Resource?: string;

	Children: GetJobStepTraceResponse[];
}

/**Parameters required to create log file */
export type CreateLogFileRequest = {
	/**Job Id this log file belongs to */
	jobId: string;

}

/**Response from creating a log file */
export type CreateLogFileResponse = {
	/**Unique id for this log file */
	id: string;

}

/**Response describing a log file */
export type GetLogFileResponse = {

	/**Unique id of the log file */
	id: string;

	/** Unique id of the job for this log file */
	jobId: string;

	/** Type of events stored in this log */
	type: LogType;

	/** Length of the log, in bytes */
	length: number;

	/** Number of lines in the file	*/
	lineCount: number;

	/**Per-object permissions */
	acl?: GetAclResponse;

}

/**Response describing an artifact */
export type GetArtifactResponse = {

	/**Unique id of the artifact */
	id: string;

	/** Unique id of the job for this artifact */
	jobId: string;

	/** Unique id of the job for this artifact */
	stepId?: string;

	/** Download code for this artifact */
	code?: string;

	/** Name of the artifact */
	name: string;

	/** MimeType of the artifact	*/
	mimeType: string;

	/** Length of the artifact, in bytes */
	length: number;

	/**Per-object permissions */
	acl?: GetAclResponse;

}

/**Parameters request a zip file for artifacts */
export type GetArtifactZipRequest = {

	/**JobId to get all artifacts for */
	jobId?: string;

	/**StepId to get all artifacts for */
	stepId?: string;

	/**Individual file name if we've just got one file */
	fileName?: string;

	/** Whether or not we're a forced single file download (versus a link click for one file) */
	isForcedDownload?: boolean;

	/** Whether or not to open the resulting link in place or in a new tab */
	isOpenNewTab?: boolean;

	/**List of arbitrary artifact Ids to generate a zip for */
	artifactIds?: string[];
}


/**Parameters required to update a log file */
export type UpdateLogFileRequest = {

	/** New permissions */

	acl?: UpdateAclRequest;
}


export type CreateNodeRequest = {

	/**The name of this node  */
	name: string;

	/**Indices of nodes which must have succeeded for this node to run */
	inputDependencies?: string[];

	/**Indices of nodes which must have completed for this node to run */
	orderDependencies?: string[];

	/**The priority of this node */
	priority?: Priority;

	/**This node can be run multiple times */
	allowRetry?: boolean;

	/**This node can start running early, before dependencies of other nodes in the same group are complete */
	runEarly?: boolean;

	/**Credentials required for this node to run. This dictionary maps from environment variable names to a credential property in the format 'CredentialName.PropertyName' */
	credentials?: { [key: string]: string };

	/**Properties for this node */
	properties?: { [key: string]: string };
}

/**Information about a group of nodes */
export type CreateGroupRequest = {

	/**The executor to use for this group */
	executor: string;

	/**The type of agent to execute this group */
	agentType: string;

	/**Nodes in the group */
	nodes: CreateNodeRequest[];

}

/**Information required to create a node */
export type GetNodeResponse = {

	/**The name of this node  */
	name: string;

	/**Indices of nodes which must have succeeded for this node to run */
	inputDependencies: string[];

	/**Indices of nodes which must have completed for this node to run */
	orderDependencies: string[];

	/**The priority of this node */
	priority: Priority;

	/**Whether this node can be retried */
	allowRetry: boolean;

	/**This node can start running early, before dependencies of other nodes in the same group are complete */
	runEarly: boolean;

	/**Sets this node as a target to be built */
	target: boolean;

	/**Expected time to execute this node based on historical trends */
	averageDuration: number;

	/**Aggregates that this node belongs do */
	aggregates?: string[];

	/**Properties for this node */
	properties: { [key: string]: string };

}

/**Information about a group of nodes */
export type GetGroupResponse = {

	/**The executor to use for this group */
	executor: string;

	/**The type of agent to execute this group */
	agentType: string;

	/**Nodes in the group */
	nodes: GetNodeResponse[];
}

/**Request to update a node */
export type UpdateNodeRequest = {

	/**The priority of this node */
	priority?: Priority;

}

/**Parameters to create a new project */
export type CreateProjectRequest = {

	/**Name for the new project */
	name: string;

	/**Properties for the new project */
	properties?: { [key: string]: string };
}

/**Response from creating a new project */
export type CreateProjectResponse = {

	/**Unique id for the new project */
	id: string;
}

/**Parameters to update a project */
export type UpdateProjectRequest = {

	/**Optional new name for the project */
	name?: string;

	/**Properties to update for the project. Properties set to null will be removed. */
	properties?: { [key: string]: string };


	/**Custom permissions for this object */
	acl?: UpdateAclRequest;
}

/**Information about a stream within a project */
export interface GetProjectStreamResponse {
	/**The stream id */
	id: string;

	/**The stream name */
	name: string;
}


/**Information about a category to display for a stream */
export type GetProjectCategoryResponse = {
	/**Heading for this column */
	name: string;

	/**Index of the row to display this category on */
	row: number;

	/**Whether to show this category on the nav menu */
	showOnNavMenu: boolean;

	/**Patterns for stream names to include */
	includePatterns: string[];

	/**Patterns for stream names to exclude */
	excludePatterns: string[];

	/**Streams to include in this category */
	streams: string[];
}

/**Response describing a project */
export type GetProjectResponse = {
	/**Unique id of the project */
	id: string;

	/**Name of the project */
	name: string;

	/**Order to display this project on the dashboard */
	order: number;

	/**List of streams that are in this project */
	// streams?: ProjectStreamData[];

	/**List of stream categories to display */
	categories?: GetProjectCategoryResponse[];

	/**Properties for this project. */
	properties: { [key: string]: string };

	/**Custom permissions for this object */
	acl?: GetAclResponse;
}

/**Parameters to create a new schedule */
export type CreateSchedulePatternRequest = {

	/**Days of the week to run this schedule on. If null, the schedule will run every day. */
	daysOfWeek?: string[];

	/**Time during the day for the first schedule to trigger. Measured in minutes from midnight. */
	minTime: number;

	/**Time during the day for the last schedule to trigger. Measured in minutes from midnight. */
	maxTime?: number;

	/**Interval between each schedule triggering */
	interval?: number;
}

/**Parameters to create a new schedule */
export type CreateScheduleRequest = {

	/**Name for the new schedule */
	name: string;

	/**Whether the schedule should be enabled */
	enabled: boolean;

	/**Maximum number of builds that can be active at once */
	maxActive: number;

	/**The stream to run this schedule in */
	streamId: string;

	/**The template job to execute */
	templateId: string;

	/**Parameters for the template */
	templateParameters: { [key: string]: string };

	/**New patterns for the schedule */
	patterns: CreateSchedulePatternRequest[];

}

/**Response from creating a new schedule */
export type CreateScheduleResponse = {

	/**Unique id for the new schedule */
	id: string;
}

/**Parameters to update a schedule */
export type UpdateScheduleRequest = {

	/**Optional new name for the schedule */
	name?: string;

	/**Whether to enable the schedule */
	enabled?: boolean;

	/**Maximum number of builds that can be active at once */
	maxActive: number;

	/**The template job to execute */
	templateHash?: string;

	/**Parameters for the template */
	templateParameters?: { [key: string]: string };

	/**New patterns for the schedule */
	patterns?: CreateSchedulePatternRequest[];

	/** Custom permissions for this object */
	acl?: UpdateAclRequest;
}

/**Information about a schedule pattern */
export type GetSchedulePatternResponse = {

	/**Days of the week to run this schedule on. If null, the schedule will run every day. */
	daysOfWeek?: string[];

	/**Time during the day for the first schedule to trigger. Measured in minutes from midnight. */
	minTime: number;

	/**Time during the day for the last schedule to trigger. Measured in minutes from midnight. */
	maxTime?: number;

	/**Interval between each schedule triggering */
	interval?: number;

}

/**Response describing a schedule */
export type GetScheduleResponse = {

	/**Whether the schedule is currently enabled */
	enabled: boolean;

	/**Maximum number of scheduled jobs at once */
	maxActive: number;

	/**Maximum number of changes the schedule can fall behind head revision. If greater than zero, builds will be triggered for every submitted changelist until the backlog is this size. */
	maxChanges: number;

	/**The template job to execute */
	templateId: string;

	/**Parameters for the template */
	templateParameters: { [key: string]: string };

	/**New patterns for the schedule */
	patterns: GetSchedulePatternResponse[];

	/* Last changelist number that this was triggered for */
	lastTriggerChange: number;

	/** Last time that the schedule was triggered */
	lastTriggerTime: Date | string;

	/** List of active jobs */
	activeJobs: string[];

}

/**Response describing when a schedule is expected to trigger */
export type GetScheduleForecastResponse = {

	/**Next trigger times */
	times: Date | string[];

}


/**Parameters for creating a new software archive */
export type CreateSoftwareRequest = {

	/**Whether this software should be the default */
	default: boolean;
}

/**Information about a client version */
export type CreateSoftwareResponse = {

	/**The software id */
	id: string;

}

/**Parameters for updating a software archive */
export type UpdateSoftwareRequest = {

	/**Whether this software should be the default */
	default: boolean;
}

/**Information about an uploaded software archive */
export type GetSoftwareResponse = {

	/**Unique id for this enty */
	id: string;

	/**Name of the user that created this software */
	uploadedByUser?: string;

	/**Time at which the client was created */
	uploadedAtTime: Date | string;

	/**Name of the user that created this software */
	madeDefaultByUser?: string;

	/**Time at which the client was made default. */
	madeDefaultAtTime?: Date | string;

}


/**Parameters to create a new stream */
export type CreateStreamRequest = {

	/**Unique id for the project */
	projectId: string;

	/**Name for the new stream */
	name: string;

	/**Properties for the new stream */
	properties?: { [key: string]: string };



}

/**Response from creating a new stream */
export type CreateStreamResponse = {

	/**Unique id for the new stream */
	id: string;

}

/**Parameters to update a stream */
export type UpdateStreamRequest = {

	/**Optional new name for the stream */
	name?: string;

	/**Properties to update on the stream. Properties with a value of null will be removed. */
	properties?: { [key: string]: string | null };

	/** Custom permissions for this object */
	acl?: UpdateAclRequest;
}

/**Mapping from a BuildGraph agent type to a se t of machines on the farm */
export type GetAgentTypeResponse = {

	/**Pool of agents to use for this agent type */
	pool: string;

	/**Name of the workspace to sync */
	workspace?: string;

	/**Pool of agents to use to execute this work */
	requirements?: GetAgentRequirementsResponse;

	/**Path to the temporary storage dir */
	tempStorageDir?: string;

	/**Environment variables to be set when executing the job */
	environment?: { [key: string]: string };

}

/**Information about a workspace type */
export type GetWorkspaceTypeResponse = {

	/**The Perforce server and port (eg. perforce:1666) */
	serverAndPort?: string;

	/**User to log into Perforce with (defaults to buildmachine) */
	userName?: string;

	/**Identifier to distinguish this workspace from other workspaces. Defaults to the workspace type name. */
	identifier?: string;

	/**Override for the stream to sync */
	stream?: string;

	/**Custom view for the workspace */
	view?: string[];

	/**Whether to use an incrementally synced workspace */
	incremental: boolean;
}


/**Information about a template in this stream */
export type GetTemplateRefResponse = {
	/**Unique id of this template ref */
	id: string;

	/**Hash of the template definition */
	hash: string;

	/// The schedule for this ref
	schedule?: GetScheduleResponse;

	name: string;

}

/** Specifies defaults for running a preflight */
export type DefaultPreflightRequest = {

	/** The template id to query */
	templateId?: string;

	/**  The last successful job type to use for the base changelist */
	changeTemplateId?: string;
}

/**Response describing a stream */
export type GetStreamResponse = {

	/**Unique id of the stream */
	id: string;

	/**Unique id of the project containing this stream */
	projectId: string;

	/**Name of the stream */
	name: string;

	/**List of tabs to display for this stream*/
	tabs: GetStreamTabResponse[];

	/**  Default template to use for preflights (deprecated)*/
	defaultPreflightTemplate?: string;

	/** Default template for running preflights */
	defaultPreflight?: DefaultPreflightRequest;

	/**Map of agent name to type */
	agentTypes: { [key: string]: GetAgentTypeResponse };

	/**Map of workspace name to type */
	workspaceTypes?: { [key: string]: GetWorkspaceTypeResponse };

	/**Templates for jobs in this stream */
	templates: GetTemplateRefResponse[];

	/**Properties for this stream */
	properties: { [key: string]: string };

	/** Custom permissions for this object */
	acl?: GetAclResponse;

}

/**Information about a template parameter */
export type CreateTemplateParameterRequest = {

	/**Name of the parameter */
	name: string;

	/**Type of the template parameter */
	type: TemplateParameterType;

	/**Default value for this parameter */
	default: string;

	/**Whether this parameter is required */
	required: boolean;
}

/**Parameters to create a new template */
export type CreateTemplateRequest = {

	/**Name for the new template */
	name: string;

	/**Default priority for this job */
	priority: Priority;

	/**Whether to allow preflights of this template */
	allowPreflights: boolean;

	/**Array of nodes for this job */
	groups: CreateGroupRequest[];

	/**List of aggregates for this template */
	aggregates: CreateAggregateRequest[];

	/**List of labels  for this template */
	labels: CreateLabelRequest[];

	/**Parameter names for this template. These will be assigned to properties of the job at startup. */
	parameters: CreateTemplateParameterRequest[];

	/**Properties for the new template */
	properties: { [key: string]: string };
}

/**Describes how to render a group parameter */
export enum GroupParameterStyle {
	/**Separate tab on the form */
	Tab = "Tab",

	/**Section with heading */
	Section = "Section"
}

/**Style of list parameter */
export enum ListParameterStyle {
	/**Regular drop-down list. One item is always selected. */
	List = "List",

	/**Drop-down list with checkboxes */
	MultiList = "MultiList",

	/**Tag picker from list of options */
	TagPicker = "TagPicker"
}

export enum ParameterType {
	Bool = "Bool",
	List = "List",
	Text = "Text",
	Group = "Group"
}


/**Base class for template parameters */
export type ParameterData = {
	type: ParameterType;
}

/**Used to group a number of other parameters */
export type GroupParameterData = ParameterData & {

	/**Label to display next to this parameter */
	label: string;

	/**How to display this group */
	style: GroupParameterStyle;

	/**List of child parameters */
	children: ParameterData[];
}

/**Free-form text entry parameter */
export type TextParameterData = ParameterData & {

	/**Name of the parameter associated with this parameter. */
	label: string;

	/**Argument to pass to the executor */
	argument: string;

	/**Default value for this argument */
	default: string;

	/**Hint text for this parameter */
	hint?: string;

	/**Regex used to validate this parameter */
	validation?: string;

	/**Message displayed if validation fails, informing user of valid values. */
	validationError?: string;

	/**Tool-tip text to display */
	toolTip?: string;

}

/**Possible option for a list parameter */
export type ListParameterItemData = ParameterData & {

	/**Optional group heading to display this entry under, if the picker style supports it. */
	group?: string;

	/**Name of the parameter associated with this list. */
	text: string;

	/// <summary>
	/// Argument to pass with this parameter, if enabled
	/// </summary>
	argumentIfEnabled?: string;

	/// <summary>
	/// Argument to pass with this parameter, if disabled
	/// </summary>
	argumentIfDisabled?: string;

	/**Whether this item is selected by default */
	default: boolean;

}

/**Allows the user to select a value from a constrained list of choices */
export type ListParameterData = ParameterData & {
	/**Label to display next to this parameter. Defaults to the parameter name. */
	label: string;

	/**The type of list parameter */
	style: ListParameterStyle;

	/**List of values to display in the list */
	items: ListParameterItemData[];

	/**Tool-tip text to display */
	toolTip?: string;
}

/**Allows the user to toggle an option on or off */
export type BoolParameterData = ParameterData & {
	/**Name of the parameter associated with this parameter. */
	label: string;

	/**Value if enabled */
	argumentIfEnabled?: string;

	/**Value if disabled */
	argumentIfDisabled?: string;

	/**Whether this argument is enabled by default */
	default: boolean;

	/**Tool-tip text to display */
	toolTip?: string;

}


/**Response describing a template */
export type GetTemplateResponse = {

	/**Unique id of the template */
	id: string;

	/**Name of the template */
	name: string;

	/**Default priority for this job */
	priority?: Priority;

	/**Whether to allow preflights of this template */
	allowPreflights: boolean;

	/**List of node groups for this job */
	groups: CreateGroupRequest[];

	/**List of template aggregates */
	aggregates: CreateAggregateRequest[];

	/**Parameters for the job. */
	arguments: string[];

	/**List of parameters for this template */
	parameters: ParameterData[];

	/**Custom permissions for this object */
	acl?: GetAclResponse;
}

/** Information about a commit */
export type GetChangeSummaryResponse = {

	/**  The source changelist number */
	number: number;

	/**  Name of the user that authored this change */
	author: string;

	/**  The description text */
	description: string;

}


/**Information about a device attached to this agent */
export type GetDeviceCapabilitiesResponse = {

	/**Logical name of this device */
	name: string;

	/**Required properties for this device, in the form "KEY=VALUE" */
	properties?: string[];

	/**Required resources for this node. If null, the node will assume exclusive access to the device. */
	resources?: { [key: string]: number };

}

/**Information about the capabilities of this agent */
export type GetAgentCapabilitiesResponse = {

	/**Information about the devices required for this node to run */
	devices?: GetDeviceCapabilitiesResponse[];

	/**Global agent properties for this node */
	properties?: string[];

}

/**Information about the device requirements of a node */
export type CreateDeviceRequirementsRequest = {

	/**Logical name of this device */
	name: string;

	/**Required properties for this device, in the form "KEY=VALUE" */
	properties?: string[];

	/**Required resources for this node. If null, the node will assume exclusive access to the device. */
	resources?: { [key: string]: number };

}

/**Information about the agent requirements of node */
export type CreateAgentRequirementsRequest = {

	/**Information about the devices required for this node to run */
	devices?: CreateDeviceRequirementsRequest[];

	/**Global agent properties for this node */
	properties?: string[];

	/**Whether the agent can be shared with another job */
	shared: boolean;
}

/**Information about the device requirements of a node */
export type GetDeviceRequirementsResponse = {

	/**Logical name of this device */
	name: string;

	/**Required properties for this device, in the form "KEY=VALUE" */
	properties?: string[];

	/**Required resources for this node. If null, the node will assume exclusive access to the device. */
	resources?: { [key: string]: number };

}

/**Information about the agent requirements of node */
export type GetAgentRequirementsResponse = {

	/**Information about the devices required for this node to run */
	devices?: GetDeviceRequirementsResponse[];

	/**Global agent properties for this node */
	properties?: string[];

	/**Whether the agent can be shared with another job */
	shared: boolean;

}

/**Individual entry in an ACL */
export type UpdateAclEntryRequest = {
	/**Name of the user or group */
	roles: string[];

	/**Array of actions to allow */
	allow: string[];
}

/**Parameters to update an ACL */
export type UpdateAclRequest = {

	/**Entries to replace the existing ACL */
	entries: UpdateAclEntryRequest[] | null;

	/**Whether to inherit permissions from the parent ACL */
	inheritPermissions: boolean | null;


	/** List of exceptions to the inherited setting */
	exceptions?: string[];
}

/**Individual entry in an ACL */
export type GetAclEntryResponse = {
	/**Names of the user or group */
	roles: string[];

	/**Array of actions to allow */
	allow: string[];
}

/**Information about an ACL */
export type GetAclResponse = {
	/**Entries to replace the existing ACL */
	entries: GetAclEntryResponse[];

	/**Whether to inherit permissions from the parent entity */
	inheritPermissions: boolean;
}

// Labels

/**Type of a column in a jobs tab */
export enum JobsTabColumnType {
	/**Contains labels */
	Labels = "Labels",

	/**Contains parameters */
	Parameter = "Parameter"
}

/**Describes a column to display on the jobs page */
export type GetJobsTabColumnResponse = {

	type: JobsTabColumnType;

	/**Heading for this column */
	heading: string;

	/**Relative width of this column. */
	relativeWidth?: number;
}

export type GetJobsTabLabelColumnResponse = GetJobsTabColumnResponse & {

	/**Category of labels to display in this column. If null, includes any label not matched by another column. */
	category?: string;

}

export type GetJobsTabParameterColumnResponse = GetJobsTabColumnResponse & {
	/** Parameter to show in this column */
	parameter?: string;
}

export enum TabType {

	Jobs = "Jobs"

}

/**Information about a page to display in the dashboard for a stream */
export type GetStreamTabResponse = {

	/**Title of this page */
	title: string;

	type: TabType;

};

/**Describes a job page */
export type GetJobsTabResponse = GetStreamTabResponse & {

	/**  Whether to show names on the page */
	showNames: boolean;


	/** List of templates to show on the page */
	templates?: string[];

	/**Columns to display for different types of labels */
	columns?: GetJobsTabColumnResponse[];

};

/** State of an label */
export enum LabelState {

	/** label is not currently being built (no required nodes are present)*/
	Unspecified = "Unspecified",

	/** Steps are still running */
	Running = "Running",

	/** All steps are complete */
	Complete = "Complete"
}

/// Outcome of an aggregate
export enum LabelOutcome {

	/** Aggregate is not currently being built */
	Unspecified = "Unspecified",

	/** A step dependency failed */
	Failure = "Failure",

	/** A dependency finished with warnings */
	Warnings = "Warnings",

	/** Successful */
	Success = "Success"
}


/** State of the job */
export enum JobState {

	/** Waiting for resources*/
	Waiting = "Waiting",

	/** Currently running one or more steps*/
	Running = "Running",

	/** All steps have completed */
	Complete = "Complete"
}


/**Information about a label */
export type GetLabelResponse = {

	/**Category of the aggregate */
	category: string;

	/**Label for this aggregate */
	name: string;

	/**Label for this aggregate, currently mapped to name property on server */
	dashboardName?: string;

	/**Name to show for this label in UGS */
	ugsName?: string;

	/** Project to display this label for in UGS */
	ugsProject?: string;

	/**Nodes which must be part of the job for the aggregate to be shown */
	requiredNodes: string[];

	/**Nodes to include in the status of this aggregate, if present in the job */
	includedNodes: string[];
}


/**Information about an aggregate */
export type GetAggregateResponse = {

	/**Name of the aggregate */
	name: string;

	/**Nodes which must be part of the job for the aggregate to be shown */
	nodes: string[];

}

/**Information about a graph */
export type GetGraphResponse = {

	/**The hash of the graph */
	hash: string;

	/**Array of nodes for this job */
	groups?: GetGroupResponse[];

	/**List of aggregates */
	namedAggregates?: GetAggregateResponse[];

	/**List of labels for the graph */
	labels?: GetLabelResponse[];

}

/**The timing info for  */
export type GetJobTimingResponse = {

	/**Timing info for each step */
	steps: { [key: string]: GetStepTimingInfoResponse };

	/**Timing information for each label */
	labels: GetLabelTimingInfoResponse[];
}

/**Information about the timing info for a label */
export type GetLabelTimingInfoResponse = GetTimingInfoResponse &
{
	/**Category for the label */
	category: string;

	/**Name of the label */
	name: string;
}

/**State of an label within a job */
export type GetLabelStateResponse = {
	/**State of the label */
	state?: LabelState;

	/**Outcome of the label */
	outcome?: LabelOutcome;
}

/**Information about the default label (ie. with inlined list of nodes) */
export type GetDefaultLabelStateResponse = GetLabelStateResponse &
{
	/**List of nodes covered by default label */
	nodes: string[];
}


/**Information about the timing info for a particular target */
export type GetTimingInfoResponse = {

	/**Wait time on the critical path */
	totalWaitTime?: number;

	/**Sync time on the critical path */
	totalInitTime?: number;

	/**Duration to this point */
	totalTimeToComplete?: number;

	/**Average wait time by the time the job reaches this point */
	averageTotalWaitTime?: number;

	/**Average sync time to this point */
	averageTotalInitTime?: number;

	/**Average duration to this point */
	averageTotalTimeToComplete?: number;

}

/**Information about the timing info for a particular target */

export type GetStepTimingInfoResponse = GetTimingInfoResponse & {

	/** Name of this node */
	name?: string;

	/**Average wait time for this step */
	averageStepWaitTime?: number;

	/**Average init time for this step */
	averageStepInitTime?: number;

	/**Average duration for this step */
	averageStepDuration?: number;

}


/**Request used to update notifications */
export type UpdateNotificationsRequest = {

	/** Notify via email */
	email?: boolean;

	/** Notify via Slack */
	slack?: boolean;
}

/**Response describing notifications */
export type GetNotificationResponse = {

	/** Notify via email */
	email?: boolean;

	/** Notify via Slack */
	slack?: boolean;
}

export type JobCompleteEventRecord = {
	outcome: string;

	type: string;

	streamId: string;

	templateId: string;
}

export type LabelCompleteEventRecord = {

	type: string;

	streamId: string;

	templateId: string;

	labelName: string;

	categoryName: string;

	outcome: string;
}

export type StepCompleteEventRecord = {

	type: string;

	streamId: string;

	templateId: string;

	stepName: string;

	outcome: string;
}

/**Request used to create subscriptions */
export type CreateSubscriptionRequest = {

	event: JobCompleteEventRecord | LabelCompleteEventRecord | StepCompleteEventRecord;

	userId?: string;

	notificationType: SubscriptonNotificationType
}

/**Request used to create subscriptions */
export type CreateSubscriptionResponse = {

	id: string;
}

/**Request used to create subscriptions */
export type GetSubscriptionResponse = {

	id: string;

	event: JobCompleteEventRecord | LabelCompleteEventRecord | StepCompleteEventRecord;

	userId?: string;

	notificationType: SubscriptonNotificationType
}

/**Identifies a particular changelist and job */
export type GetIssueStepResponse = {

	/**The changelist number */
	change: number;

	/** Name of the job containing this step */
	jobName: string;

	/**The unique job id */
	jobId: string;

	/**The unique batch id */
	batchId: string;

	/**The unique step id */
	stepId: string;

	/**  Time at which the step ran */
	stepTime: Date | string;

	/// The unique log id
	logId?: string;

}

/** Information about a template affected by an issue */
export type GetIssueAffectedTemplateResponse = {

	/** The template id */
	templateId: string;

	/**  The template name */
	templateName: string;

	/**  Whether it has been resolved or not */
	resolved: boolean;

}

/**Trace of a set of node failures across multiple steps */
export type GetIssueNodeResponse = {

	/** The template containing this step */
	templateId: string;

	/**Name of the step */
	name: string;

	/**The previous build  */
	lastSuccess?: GetIssueStepResponse;

	/**The failing builds for a particular event */
	steps: GetIssueStepResponse[];

	/**The following successful build */
	nextSuccess?: GetIssueStepResponse;

}

/**Information about a particular step */
export type GetIssueStreamResponse = {
	/**Unique id of the stream */
	streamId: string;

	/**Minimum changelist affected by this issue (ie. last successful build) */
	minChange?: number;

	/**Maximum changelist affected by this issue (ie. next successful build) */
	maxChange?: number;

	/**Map of steps to (event signature id -> trace id) */
	nodes: GetIssueNodeResponse[];
}

/**Outcome of a particular build */
export enum IssueBuildOutcome {
	/**Unknown outcome */
	Unknown = "Unknown",

	/**Build succeeded */
	Success = "Success",

	/**Build failed */
	Error = "Error",

	/**Build finished with warnings */
	Warning = "Warning"
}

/**Information about a suspect changelist that may have caused an issue */
export type GetIssueSuspectResponse = {
	/**Number of the changelist that was submitted */
	change: number;

	/**Author of the changelist */
	author: string;

	/**The originating change */
	originatingChange?: number;

	/** Time at which the user declined this issue */
	declinedAt?: Date | string;
}

/**Information about a diagnostic */
export type GetIssueDiagnosticResponse = {
	/**The corresponding build id */
	buildId?: number;

	/**Message for the diagnostic */
	message: string;

	/**Link to the error */
	url: string;
}

/**Summary for the state of a stream in an issue */
export interface GetIssueAffectedStreamResponse {
	/**Id of the stream */
	streamId: string;

	/**Name of the stream */
	streamName: string;

	/**Whether the issue has been resolved in this stream */
	resolved: boolean;

	/** The affected templates */
	affectedTemplates: GetIssueAffectedTemplateResponse[];

	/**List of affected template ids */
	templateIds: string[];

	/** List of resolved template ids */
	resolvedTemplateIds: string[];

	/** List of resolved template ids */
	unresolvedTemplateIds: string[];
}


/**Stores information about a build health issue */
export type GetIssueResponse = {
	/**The unique object id */
	id: number;

	/**Time at which the issue was created */
	createdAt: Date | string;

	/**Time at which the issue was retrieved */
	retrievedAt: Date | string;

	/**The associated project for the issue */
	project?: string;

	/**The summary text for this issue */
	summary: string;

	/**Details text describing the issue */
	details?: string;

	/** Severity of this issue	*/
	severity: IssueSeverity;

	/**Owner of the issue */
	owner?: string;

	/**User that nominated the current owner */
	nominatedBy?: string;

	/**Time that the issue was acknowledged */
	acknowledgedAt?: Date | string;

	/**Changelist that fixed this issue */
	fixChange?: number;

	/**Time at which the issue was resolved */
	resolvedAt?: Date | string;

	/**  List of stream paths affected by this issue */
	streams: string[];

	/** List of affected stream ids */
	resolvedStreams: string[];

	/** List of unresolved streams */
	unresolvedStreams: string[];

	affectedStreams: GetIssueAffectedStreamResponse[];

	/** Most likely suspects for causing this issue */
	primarySuspects: string[];

	/** Whether to show alerts for this issue */
	showDesktopAlerts: boolean;

}

/**Request an issue to be updated */
export type UpdateIssueRequest = {

	/** Summary of the issue */
	summary?: string;

	/**New owner of the issue */
	owner?: string | null;

	/**User than nominates the new owner */
	nominatedBy?: string | null;

	/**Whether the issue has been acknowledged */
	acknowledged?: boolean;

	/** Whether the user has declined this issue */
	declined?: boolean;

	/**The change at which the issue is claimed fixed */
	fixChange?: number | null;

	/**Whether the issue should be marked as resolved */
	resolved?: boolean;
}

export type GetUtilizationTelemetryStream = {

	streamId: string;

	time: number;
}

export type GetUtilizationTelemetryPool = {

	poolId: string;

	numAgents: number;

	adminTime: number;

	otherTime: number;

	streams: GetUtilizationTelemetryStream[];
}

export type GetUtilizationTelemetryResponse = {

	startTime: Date;

	finishTime: Date;

	pools: GetUtilizationTelemetryPool[];

	adminTime: number;

	numAgents: number;
}

export type UserClaim = {

	type: string;
	value: string;
}

export enum DashboardPreference {
	DisplayUTC = "DisplayUTC",
	DisplayClock = "DisplayClock",
	ColorSuccess = "ColorSuccess",
	ColorWarning = "ColorWarning",
	ColorError = "ColorError",
	ColorRunning = "ColorRunning",
	LocalCache = "LocalCache",
}

export type DashboardSettings = {

	preferences: Map<DashboardPreference, string>;

}

export type UpdateDashboardSettings = {

	preferences?: Record<DashboardPreference, string>;

}


/**  Response describing the current user */
export type GetUserResponse = {

	/** Id of the user */
	id: string;

	/** Claims for the user */
	claims: UserClaim[];

	/** List of pinned job ids */
	pinnedJobIds: string[];

	/** Whether to enable slack notifications for this user */
	enableIssueNotifications: boolean;

	/** Whether to enable experimental features for this user */
	enableExperimentalFeatures: boolean;

	/**  Settings for the dashboard */
	dashboardSettings: DashboardSettings;

}

/// <summary>
/// Request to update settings for a user
/// </summary>
export type UpdateUserRequest = {

	/** New dashboard settings */
	dashboardSettings?: UpdateDashboardSettings;

	/** Job ids to add to the pinned list */
	addPinnedJobIds?: string[];

	/** Job ids to remove from the pinned list */
	removePinnedJobIds?: string[];

	/** Whether to enable slack notifications for this user */
	enableIssueNotifications?: boolean;

	/** Whether to enable experimental features for this user */
	enableExperimentalFeatures?: boolean;

}

export type GetPerforceServerStatusResponse = {
	serverAndPort: string;
	baseServerAndPort: string;
	cluster: string;
	numLeases: number;
	status: string;
	detail: string;
}
