// Copyright Epic Games, Inc. All Rights Reserved.

import * as fs from 'fs'
import * as util from 'util'

import {BotEvents, BotEventHandler} from './events'
import {Blockage, Branch, Conflict, ForcedCl} from './branch-interfaces'
import {PersistentConflict} from './conflict-interfaces'
import {Slack, SlackMessageOpts, SlackMessageField, SlackLinkButtonsAttachment, SlackLinkButtonAction, SlackAttachment} from './slack'
import {Conflicts} from './conflicts'
import {Context} from './settings'
import {Random} from '../common/helper'

import {Arg, readProcessArgs} from '../common/args'

// var to enable DMs on blockage
const DIRECT_MESSAGING_ENABLED = true

const SLACK_MESSAGES_PERSISTENCE_KEY = 'slackMessages'
const CHANGELIST_FIELD_TITLE = 'Change'
const SUNSET_PERSISTED_NOTIFICATIONS_DAYS = 30

// should probably be in slack.ts
const MESSAGE_STYLE_GOOD = 'good'
const MESSAGE_STYLE_WARNING = 'warning'
const MESSAGE_STYLE_DANGER = 'danger'

const COMMAND_LINE_ARGS: {[param: string]: (Arg<any>)} = {
	vault: {
		match: /^-vault_path=(.+)$/,
		env: 'ROBO_VAULT_PATH',
		dflt: '/vault'
	}
}

const args = readProcessArgs(COMMAND_LINE_ARGS)
if (!args) {
	process.exit(1)
}

const KNOWN_BOT_NAMES = ['buildmachine', 'robomerge'];

export let SLACK_TOKENS: {[name: string]: string} = {}

// init globals
;(() => {
	// deleted hook reading code - can retrieve from P4

	let vault
	try {
		const vaultString = fs.readFileSync(args.vault + '/vault.json', 'ascii')
		vault = JSON.parse(vaultString)
	}
	catch (err) {
		util.log(`Warning, failed to find Slack secrets in vault (ok in dev): ${err.toString()}`)
		return
	}

	const tokensObj = vault['slack-tokens']
	if (tokensObj) {
		SLACK_TOKENS = tokensObj
	}
})()


export function isUserAKnownBot(user: string) {
	return KNOWN_BOT_NAMES.indexOf(user) !== -1
}


//////////
// Utils

// make a Slack notifcation link for a user
function atifyUser(user: string) {
	// don't @ names of bots (currently making them tt style for Slack)
	return isUserAKnownBot(user) ? `\`${user}\`` : '@' + user
}

type BranchArg = string | Branch
function getBranchName(branchArg: BranchArg) {
	return (branchArg as Branch).name || branchArg as string
}

function generatePersistedSlackMessageKey(sourceCl: number, targetBranchArg: BranchArg, channel: string) {
	const branchName = getBranchName(targetBranchArg).toUpperCase()
	return `${sourceCl}:${branchName}:${channel}`
}

function formatDuration(durationSeconds: number) {
	const underSixHours = durationSeconds < 6 * 3600
	const durationBits: string[] = []
	if (durationSeconds > 3600) {
		const hoursFloat = durationSeconds / 3600
		const hours = underSixHours ? Math.floor(hoursFloat) : Math.round(hoursFloat)
		durationBits.push(`${hours} hour` + (hours === 1 ? '' : 's'))
	}
	// don't bother with minutes if over six hours
	if (underSixHours) {
		if (durationSeconds < 90) {
			if (durationSeconds > 10) {
				durationSeconds = Math.round(durationSeconds)
			}
			durationBits.push(`${durationSeconds} seconds`)
		}
		else {
			const minutes = Math.round((durationSeconds / 60) % 60)
			durationBits.push(`${minutes} minutes`)
		}
	}
	return durationBits.join(', ')
}

function formatResolution(info: PersistentConflict) {
	// potentially three people involved:
	//	a: original author
	//	b: owner of conflict (when branch resolver overridden)
	//	c: instigator of skip

	// Format message as "a's change was skipped [by c][ on behalf of b][ after N minutes]
	// combinations of sameness:
	// (treat null c as different value, but omit [by c])
	//		all same: 'a skipped own change'
	//		a:	skipped by owner, @a, write 'by owner (b)' instead of b, c
	//		b:	'a skipped own change' @b
	//		c:	resolver not overridden, @a, omit b
	//		all distinct: @a, @b
	// @ a and/or c if they're not the same as b

	const overriddenOwner = info.owner !== info.author

	// display info.resolvingAuthor where possible, because it has correct case
	const resolver = info.resolvingAuthor && info.resolvingAuthor.toLowerCase()
	const bits: string[] = []
	if (resolver === info.author) {
		bits.push(info.resolvingAuthor!, info.resolution, 'own change')
		if (overriddenOwner) {
			bits.push('on behalf of', info.owner)
		}
	}
	else {
		bits.push(info.author + "'s", 'change was', info.resolution)
		if (!resolver) {
			// don't know who skipped (shouldn't happen) - notify owner
			if (overriddenOwner) {
				bits.push(`(owner: ${info.owner})`)
			}
		}
		else if (info.owner === resolver) {
			bits.push(`by owner (${info.resolvingAuthor})`)
		}
		else {
			bits.push('by', info.resolvingAuthor!)
			if (overriddenOwner) {
				bits.push('on behalf of', atifyUser(info.owner))
			}
			else {
				// only case we @ author - change has been resolved by another known person, named in messaage
				bits[0] = atifyUser(info.author) + "'s"
			}
		}

		if (info.resolvingReason) {
			bits.push(`(${info.resolvingReason})`)
		}
	}

	if (info.timeTakenToResolveSeconds) {
		bits.push('after', formatDuration(info.timeTakenToResolveSeconds))
	}

	let message = bits.join(' ') + '.'

	if (info.timeTakenToResolveSeconds) {
		// add time emojis!
		if (info.timeTakenToResolveSeconds < 2*60) {
			const poke = Random.choose(['esp', 'sylv', 'flar', 'jolt', 'leaf', 'glac', 'umbr', 'vapor'])
			message += ` :${poke}eon_run:`
		}
		else if (info.timeTakenToResolveSeconds < 10*60) {
			message += ' :+1:'
		}
		// else if (info.timeTakenToResolveSeconds > 30*60) {
		// 	message += ' :sadpanda:'
		// }
	}
	return message
}

//////////

interface SlackConflictMessage {
	timestamp: string
	messageOpts: SlackMessageOpts
}

type PersistedSlackMessages = {[sourceClAndBranch: string]: SlackConflictMessage}

/** Wrapper around Slack, keeping track of messages keyed on target branch and source CL */

// how much implementation should be in here? If too much, not actually solving anything, just moving stuff around.

// Main thing is that only this talks to Slack, and it has no independent post.
// Will name things explicitly if we need posting/DMs for other reasons.

// So fine to have find/update public. But no post-only!

interface SlackMessageForUpdating {
	message: SlackConflictMessage
	_conflictKey: string
	_persistedMessages: PersistedSlackMessages
}

class SlackMessages {
	constructor(private slack: Slack, private persistence: Context) {
		const slackMessages: PersistedSlackMessages | null = this.persistence.get(SLACK_MESSAGES_PERSISTENCE_KEY)

		if (slackMessages) {
			this.doHouseKeeping(slackMessages)
		}
		else {
			this.persistence.set(SLACK_MESSAGES_PERSISTENCE_KEY, {})
		}
	}

	async postOrUpdate(sourceCl: number, branchArg: BranchArg, messageText: string, messageOpts: SlackMessageOpts, persistMessage = true) {
		const findResult = this.find(sourceCl, branchArg, messageOpts.channel)

		// If we find a message, simply update the contents
		if (findResult.message) {
			findResult.message.messageOpts = messageOpts
			await this.update(messageText, findResult)
		}
		// Otherwise, we will need to create a new one
		else {
			let timestamp
			try {
				timestamp = await this.slack.postMessage(messageText, messageOpts)
			}
			catch (err) {
				console.error('Error talking to Slack! ' + err.toString())
				return
			}

			// Used for messages we don't care to keep, currently the /api/test/directmessage endpoint
			if (persistMessage) {
				findResult._persistedMessages[findResult._conflictKey] = {timestamp, messageOpts}
				this.persistence.set(SLACK_MESSAGES_PERSISTENCE_KEY, findResult._persistedMessages)
			}
		}
	}

	async postDM(emailAddress: string, sourceCl: number, branchArg: BranchArg, 
		dmMessageOpts: SlackMessageOpts, persistMessage = true) {
		// The Slack API requires a user ID to open a direct message with users.
		// The most consistent way to do this is getting their email address out of P4.
		if (!emailAddress) {
			console.error("Failed to get email address during notifications for CL " + sourceCl)
			return
		}

		// With their email address, we can get their user ID via Slack API
		let userId : string
		try {
			userId = (await this.slack.lookupUserIdByEmail(emailAddress.toLowerCase()))
		} catch (err) {
			util.log(`Failed to get user ID for Slack DM, given email address "${emailAddress}" for CL ${sourceCl}:\n${err.toString()}`)
			return
		}

		// Open up a new conversation with the user now that we have their ID
		let channelId : string
		try {
			channelId = (await this.slack.openDMConversation(userId))
			dmMessageOpts.channel = channelId
		} catch (err) {
			util.log(`Failed to get Slack conversation ID for user ID "${userId}" given email address "${emailAddress}" for CL ${sourceCl}:\n${err.toString()}`)
			return
		}

		// Add the channel/conversation ID to the messageOpts and proceed normally.
		util.log(`Creating direct message for ${emailAddress} (key: ${generatePersistedSlackMessageKey(sourceCl, branchArg, channelId)})`)
		this.postOrUpdate(sourceCl, branchArg, "", dmMessageOpts, persistMessage)
	}

	// 
	findAll(sourceCl: number, branchArg: BranchArg) {
		// Get key to search our list of all messages
		const slackMessages: PersistedSlackMessages = this.persistence.get(SLACK_MESSAGES_PERSISTENCE_KEY)
		const conflictKey = generatePersistedSlackMessageKey(sourceCl, branchArg, "") // pass empty string as channel name

		// Prepare a return value
		let returnValue = {messages: new Array<SlackConflictMessage>(), _conflictKey: conflictKey, _persistedMessages: slackMessages}

		for (let key in slackMessages) {
			if (key.startsWith(conflictKey)) {
				returnValue.messages.push(slackMessages[key])
			}
		}
		
		return returnValue
	}

	find(sourceCl: number, branchArg: BranchArg, channel: string) {
		// Get key to search our list of all messages
		const slackMessages: PersistedSlackMessages = this.persistence.get(SLACK_MESSAGES_PERSISTENCE_KEY)
		const conflictKey = generatePersistedSlackMessageKey(sourceCl, branchArg, channel)
		return {message: slackMessages[conflictKey], _conflictKey: conflictKey, _persistedMessages: slackMessages}		
	}

	async update(text: string, msg: SlackMessageForUpdating) {
		try {
			await this.slack.update(text, msg.message.timestamp, msg.message.messageOpts)
		}
		catch (err) {
			console.error('Error updating message in Slack! ' + err.toString())
			return
		}

		this.persistence.set(SLACK_MESSAGES_PERSISTENCE_KEY, msg._persistedMessages)
	}

	/** Deliberately ugly function name to avoid accidentally posting duplicate messages about conflicts! */
	postNonConflictMessage(text: string, opts: SlackMessageOpts) {
		this.slack.postMessage(text, opts)
		.catch(err => console.error('Error posting non-conflict to Slack! ' + err.toString()))
	}

	private doHouseKeeping(messages: PersistedSlackMessages) {
		const nowTicks = Date.now() / 1000.

		const keys = Object.keys(messages)
		for (const key of keys) {
			const message = messages[key]
			const messageAgeDays = (nowTicks - parseFloat(message.timestamp))/24/60/60
			if (messageAgeDays > SUNSET_PERSISTED_NOTIFICATIONS_DAYS) {
				delete messages[key]
			}
		}

		// if we removed any messages, persist
		if (Object.keys(messages).length < keys.length) {
			this.persistence.set(SLACK_MESSAGES_PERSISTENCE_KEY, messages)
		}
	}
}

function makeClLink(cl: number, alias?: string) {
	return `<https://p4-swarm.companyname.net/changes/${cl}|${alias ? alias : cl}>`
}

export class BotNotifications implements BotEventHandler {
	private readonly externalRobomergeUrl : string;
	slackChannel: string;

	constructor(private botname: string, slackChannel: string, persistence: Context) {
		// Hacky way to dynamically change the URL for notifications
		this.externalRobomergeUrl = process.env["ROBO_EXTERNAL_URL"] || "https://robomerge";

		this.slackChannel = slackChannel

		const botToken = SLACK_TOKENS.bot
		if (botToken && slackChannel) {
			this.slackMessages = new SlackMessages(new Slack({id: slackChannel, botToken}), persistence)
		}
	}

	/** Conflict */
	// considered if no Slack set up, should continue to add people to notify, but too complicated:
	//		let's go all in on Slack. Fine for this to be async (but fire and forget)
	async onBlockage(blockage: Blockage) {
		const changeInfo = blockage.change

		if (changeInfo.isManual) {
			// maybe DM?
			return
		}

		const cl = changeInfo.cl

		if (!this.slackMessages) {
			// doing nothing at the moment - don't want to complicate things with fallbacks
			// probably worth having fallback channel, so don't necessarily have to always set up specific channel
			// (in that case would have to show bot as well as branch)
			return
		}

		// or integration failure (better wording? exclusive check-out?)
		const sourceBranch = changeInfo.branch
		let targetBranch
		if ((blockage as Conflict).action) {
			targetBranch = (blockage as Conflict).action.branch
		}
		const issue = blockage.kind.toLowerCase()

		const messageOpts = this.makeSlackChannelMessageOpts(
			`${sourceBranch.name} blocked! (${issue})`, 
			MESSAGE_STYLE_DANGER, 
			makeClLink(cl), 
			sourceBranch, 
			targetBranch, 
			changeInfo.author,
			(blockage as Conflict).shelfCl
		);

		if (issue === 'exclusive check-out') {
			messageOpts.footer = blockage.description
		}

		// If we get the user's email, we shouldn't ALSO ping them in the channel.
		const userEmail = await blockage.ownerEmail
		const channelPing = userEmail ? blockage.owner : `@${blockage.owner}`

		const isBotUser = isUserAKnownBot(blockage.owner)
		let text = isBotUser ? `Blockage caused by \`${blockage.owner}\` commit!` :  
			`${channelPing}, please resolve the following ${issue}:`

		if (blockage.id) {
			text += ` <http://localhost:8090/api/resolver/${blockage.id}|resolver>`
		}
		// separate message for syntax errors (other blockages have a target branch)
		const targetKey = targetBranch ? targetBranch.name : blockage.kind

		// Post message to channel
		this.slackMessages.postOrUpdate(changeInfo.source_cl, targetKey, text, messageOpts)

		// Post message to owner in DM
		if (DIRECT_MESSAGING_ENABLED && !isBotUser && targetBranch && userEmail) {
			let dmText = `Your change (${makeClLink(changeInfo.source_cl)}) ` +
				`hit a merge conflict error while merging from *${sourceBranch.name}* to *${targetBranch.name}*.\n\n` +
				"*_Resolving this blockage is time sensitive._ Please select one of the following:*"

			const dmMessageOpts = this.makeSlackDMOpts(dmText, changeInfo.source_cl, cl, (blockage as Conflict).shelfCl, sourceBranch.name, targetBranch.name)
			this.slackMessages.postDM(userEmail, changeInfo.source_cl, targetBranch, dmMessageOpts)
		}
	}


	////////////////////////
	// Conflict resolution
	//
	// For every non-conflicting merge merge operation, no matter whether we committed anything, look for Slack conflict
	// messages that can be set as resolved. This covers the following cases:
	//
	// Normal case (A): user commits CL with resolved unshelved changes.
	//	- RM reparses the change that conflicted and sees nothing to do
	//
	// Corner case (B): user could commit just those files that were conflicted.
	//	- RM will merge the rest of the files and we'll see a non-conflicted commit

	/** On change (case A above) - update message if we see that a conflict has been resolved */
	onBranchUnblocked(info: PersistentConflict) {
		if (this.slackMessages) {
			let newClDesc: string | undefined, messageStyle: string
			if (info.resolution === Conflicts.RESOLUTION_RESOLVED) {
				if (info.resolvingCl) {
					newClDesc = `${makeClLink(info.cl)} -> ${makeClLink(info.resolvingCl)}`
				}
				messageStyle = MESSAGE_STYLE_GOOD
			}
			else {
				messageStyle = MESSAGE_STYLE_WARNING
			}

			const message = formatResolution(info)
			const targetKey = info.targetBranchName || info.kind
			if (!this.tryUpdateMessages(info.sourceCl, targetKey, '', messageStyle, message, newClDesc, false)) {
				util.log(`Warning: conflict message not found to update (${info.blockedBranchName} -> ${targetKey} CL#${info.sourceCl})`)
				const messageOpts = this.makeSlackChannelMessageOpts('', messageStyle, makeClLink(info.cl), info.blockedBranchName, info.targetBranchName, info.author)
				this.slackMessages.postOrUpdate(info.sourceCl, targetKey, message, messageOpts)
			}
		}
	}

	onNonSkipLastClChange(details: ForcedCl) {
		const messageOpts = this.makeSlackChannelMessageOpts(
			`'${details.branch.name}' forced to ${details.forcedCl} (Reason: ${details.reason})`,
			MESSAGE_STYLE_WARNING,
			makeClLink(details.forcedCl),
			details.branch) 

		if (this.slackMessages) {
			this.slackMessages.postNonConflictMessage(
				`Last processed changelist for branch '${details.branch.name}' forced to ${details.forcedCl} by ${details.culprit} (was ${details.previousCl}) (Reason: ${details.reason})`,
				messageOpts
			)
		}
	}

	sendTestMessage(username : string) {
		/*const messageOpts = this.makeSlackDMOpts(
			`Main blocked! (TEST_MESSAGE)`, 
			MESSAGE_STYLE_DANGER, 
			"0", 
			"Main", 
			"NO_TARGET", 
			"jake.romigh",
			0
		)*/

		let text = `${username}, please resolve the following TEST_MESSAGE:\n(source CL: 0, conflict CL: 1, shelf CL: 2)`

		const messageOpts = this.makeSlackDMOpts(text, 0, 1, 2, "SOURCEBRANCH", "TARGETBRANCH")

		//this.slackMessages.postOrUpdateDM(blockage.ownerEmail, changeInfo.source_cl, targetKey, text, messageOpts, buttonsCollection)
		//this.slackMessages!.postOrUpdateDM(emailAddress, 0, "NO_TARGET", text, messageOpts, false)
		this.slackMessages!.postDM(`${username}@epicgames.com`, 0, "TARGETBRANCH", messageOpts, false)
	}

	private makeSlackChannelMessageOpts(title: string, style: string, clDesc: string, sourceBranch: BranchArg,
											targetBranch?: BranchArg, author?: string, shelf?: number, buttons?: SlackLinkButtonsAttachment[]) {
		const integrationText = [getBranchName(sourceBranch)]
		if (targetBranch) {
			integrationText.push(getBranchName(targetBranch))
		}
		const fields: SlackMessageField[] = [
			{title: 'Integration', short: true, value: integrationText.join(' -> ')},
			{title: CHANGELIST_FIELD_TITLE, short: true, value: clDesc},
		]

		if (author) {
			fields.push({title: 'Author', short: true, value: author})
		}

		if (shelf) {
			fields.push({title: 'Shelf', short: true, value: makeClLink(shelf)})
		}

		const opts: SlackMessageOpts = {title, style, fields,
			title_link: this.externalRobomergeUrl + '#' + this.botname,	// ought to configure this somewhere (also for email message)
			mrkdwn: true,
			channel: this.slackChannel // Default to the configured channel
		}

		if (buttons) {
			opts.attachments = buttons
		}

		return opts
	}

	// This is an extremely opinionated function to send a stylized direct message to the end user.
	//makeSlackChannelMessageOpts(title: string, style: string, clDesc: string, sourceBranch: BranchArg,targetBranch?: BranchArg, author?: string, shelf?: number, buttons?: SlackLinkButtonsAttachment[]) 
	private makeSlackDMOpts(messageText: string, sourceCl: number, conflictCl: number, shelfCl: number | undefined, 
		sourceBranch: string, targetBranch: string) : SlackMessageOpts {
		// Start collecting our attachments
		let attachCollection : SlackAttachment[] = []

		let changelistLink = shelfCl ? makeClLink(shelfCl, `shelf ${shelfCl}`) :
			makeClLink(conflictCl, `conflict CL #${conflictCl}`)
		
		// Acknowledge button
		attachCollection.push(<SlackLinkButtonsAttachment>{
			text: `"I will merge ${changelistLink} to *${targetBranch}* myself."`,
			fallback: `Please acknowledge blockages at ${this.externalRobomergeUrl}"`,
			mrkdwn_in: ["text"],
			actions: [this.generateAcknowledgeButton(sourceBranch, String(conflictCl), "Acknowledge Conflict")]
		})

		// First skip button
		attachCollection.push(<SlackLinkButtonsAttachment>{
			text: `"The work in ${makeClLink(sourceCl)} should not be merged to *${targetBranch}*."`,
			fallback: `You can skip work at ${this.externalRobomergeUrl}"`,
			mrkdwn_in: ["text"],
			actions: [this.generateSkipButton(sourceBranch, String(conflictCl), `Skip: Not Relevant to ${targetBranch}`, 
				undefined, 'notrelevant')]
		})

		// Second skip button
		attachCollection.push(<SlackLinkButtonsAttachment>{
			text: `"I will reproduce the work from ${makeClLink(sourceCl)} in *${targetBranch}*, rather than merge it."`,
			fallback: "",
			mrkdwn_in: ["text"],
			actions: [this.generateSkipButton(sourceBranch, String(conflictCl), `Skip: Will Redo in ${targetBranch}`, 
				undefined, 'willredo')]
		})

		// Append footer
		attachCollection.push({
			"pretext": "If you need help, please let us know in #robomerge-help!",
			mrkdwn_in: ["pretext", "text"]
		})

		// Return SlackMessageOpts for our direct message
		return {
			text: messageText,
			mrkdwn: true,
			channel: "",
			attachments: attachCollection
		}
	}

	private generateAcknowledgeButton(branch : string, cl : string, buttonText: string) : SlackLinkButtonAction {
		return { 
			type: "button",
			text: buttonText,
			url: `${this.externalRobomergeUrl}/branchop/acknowledge?` +
				`bot=${encodeURIComponent(this.botname)}`+ 
				`&branch=${encodeURIComponent(branch)}` +
				`&cl=${encodeURIComponent(cl)}`,
			style: "primary"
		}
	}

	private generateSkipButton(branch : string, cl : string, buttonText : string, style?: "default" | "primary" | "danger", 
		reason?: "notrelevant" | "willredo") : SlackLinkButtonAction {
		return { 
			type: "button",
			text: buttonText,
			url: `${this.externalRobomergeUrl}/branchop/skip?` +
				`bot=${encodeURIComponent(this.botname)}`+ 
				`&branch=${encodeURIComponent(branch)}` +
				`&cl=${encodeURIComponent(cl)}` +
				(reason ? `&reason=${encodeURIComponent(reason)}` : ""),
			style
		}
	}

	

	/** Pre-condition: this.slackMessages must be valid */
	private tryUpdateMessages(sourceCl: number, targetBranch: BranchArg, newTitle: string,
									newStyle: string, newText: string, newClDesc?: string, keepButtons?: boolean, keepAdditionalEntries?: boolean) {
		// Find all messages relating to CL and branch
		const findResult = this.slackMessages!.findAll(sourceCl, targetBranch)

		if (findResult.messages.length == 0) {
			return false
		}

		for (let message of findResult.messages) {
			const opts = message.messageOpts

			if (newTitle) {
				opts.title = newTitle
			}
			else {
				delete opts.title
			}

			// e.g. change colour from red to orange
			opts.style = newStyle

			// e.g. change source CL to 'source -> dest'
			if (newClDesc && opts.fields) {
				for (const field of opts.fields) {
					if (field.title === CHANGELIST_FIELD_TITLE) {
						field.value = newClDesc
						break
					}
				}
			}

			// If this message had text before, simply update the text here.
			let existingText : boolean = false
			if (opts.text) {
				existingText = true
				opts.text = newText
			}

			if (!keepButtons && message.messageOpts.attachments) {
				delete message.messageOpts.attachments
			}

			// optionally remove second row of entries
			if (!keepAdditionalEntries && opts.fields) {
				// remove shelf entry
				opts.fields = opts.fields.filter(field => field.title !== 'Shelf' && field.title !== 'Author')
				delete opts.footer
			}

			this.slackMessages!.update((existingText ? "" : newText), 
				{message, _conflictKey: findResult._conflictKey,
					 _persistedMessages: findResult._persistedMessages})
		}
		
		return true
	}

	private readonly slackMessages?: SlackMessages
}

export function bindBotNotifications(events: BotEvents, persistence: Context) {
	events.registerHandler(new BotNotifications(events.botname, events.botConfig.slackChannel, persistence))
}


export function runTests() {
	const conf: PersistentConflict = {
		blockedBranchName: 'from',
		targetBranchName: 'to',

		cl: 101,
		sourceCl: 1,
		author: 'x',
		owner: 'x',
		kind: 'blah',

		time: new Date,
		nagged: false,

		resolution: 'pickled'
	}

	let nextCl = 101

	const tests = [
		["x's change was pickled.",							'x'],
		["x's change was pickled (owner: y).",				'y'],
		["x pickled own change.",							'x', 'x'],
		["x pickled own change on behalf of y.",			'y', 'x'],
		["@x's change was pickled by y.",					'x', 'y'],
		["x's change was pickled by owner (y).",			'y', 'y'],
		["x's change was pickled by y on behalf of @z.",	'z', 'y'],
	]

	let passed = 0
	for (const test of tests) {
		conf.owner = test[1]
		conf.resolvingAuthor = test[2]
		conf.cl = nextCl++

		const formatted = formatResolution(conf)
		if (test[0] === formatted) {
			++passed
		}
		else {
			console.log('Mismatch!')
			console.log('\tExpected:   ' + test[0])
			console.log('\tResult:     ' + formatted)
			console.log()
		}
	}

	console.log(`Resolution format: ${passed} out of ${tests.length} correct`)
	return tests.length - passed
}
