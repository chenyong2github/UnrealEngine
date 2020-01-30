// Copyright Epic Games, Inc. All Rights Reserved.

import * as request from '../common/request'

export interface SlackChannel {
	id: string
	botToken: string
}

export interface SlackArgs {
	id?: string
	botToken: string
}

export interface SlackMessageField {
	title: string
	value: string | number | boolean 
	short: boolean
}

// https://api.slack.com/docs/message-attachments#link_buttons
export interface SlackLinkButtonAction {
	type: string
	text: string
	url: string
	style?: "default" | "primary" | "danger" //black/white, (green), (red)
	value?: string
}
// https://api.slack.com/docs/interactive-message-field-guide#attachment_fields
export interface SlackAttachment {
	text?: string
	color?: string
	pretext?: string
	mrkdwn_in: string[] // ["pretext", "text", "fields"]
}
export interface SlackLinkButtonsAttachment extends SlackAttachment {
	text: string
	fallback: string
	actions: SlackLinkButtonAction[] // Up to 5 buttons can exist in one attachment before Slack gets mad
}


export interface SlackMessageOpts {
	username?: string // hack to show branch clearly?
	style?: string
	fields?: SlackMessageField[]
	title?: string
	title_link?: string
	icon_emoji?: string
	pretext?: string
	text?: string
	footer?: string

	// An array because the Slack API expects it to be
	attachments?: SlackAttachment[]
	// Direct Message support
	channel: string
	// Allows Markdown formatting in messages
	mrkdwn: boolean
}


const MAIN_MESSAGE_FIELDS = new Set(['username', 'icon_emoji', 'channel', 'text']);

export class Slack {
	constructor(private channel: SlackArgs) {
	}

// should make opts than can be a style string or an attachment object (make interface)
// next: add title and title_link

	async postMessage(text: string, messageOpts?: SlackMessageOpts) {
		return (await this.post('chat.postMessage', this.makeArgs(text, messageOpts))).ts
	}

	reply(text: string, thread_ts: string, messageOpts?: SlackMessageOpts) {
		const args = this.makeArgs(text, messageOpts)
		args.thread_ts = thread_ts
		return this.post('chat.postMessage', args)
	}

	update(text: string, ts: string, messageOpts?: SlackMessageOpts) {
		const args = this.makeArgs(text, messageOpts)
		args.ts = ts
		return this.post('chat.update', args)//, as_user:true})
	}

	listMessages(count?: number) {
		const args: any = count ? {count} : count
		// use channels.history if publlic?
		return this.get('groups.history', args)
	}

	async lookupUserIdByEmail(email: string) : Promise<string> {
		return (await this.get('users.lookupByEmail', {token: this.channel.botToken, email})).user.id
	}

	async openDMConversation(users: string | string[]) : Promise<string> {
		if (users instanceof Array) {
			users = users.join(',')
		}
		return (await this.post('conversations.open', {token: this.channel.botToken, users})).channel.id
	}

	/*private*/ async post(command: string, args: any) {
		// console.log(command)
		// console.log(JSON.stringify(args))
		// return {ts:'17', channel:{id:23}}

		const result = JSON.parse(await request.post({
			url: 'https://slack.com/api/' + command,
			body: JSON.stringify(args),
			headers: {Authorization: 'Bearer ' + this.channel.botToken},
			contentType: 'application/json; charset=utf-8'
		}))

		if (!result.ok) {
			throw new Error(`${command} generated:\n${JSON.stringify(result)}`)
		}
		return result
	}

	/*private*/ async get(command: string, args: any) {

		// erg: why am I always passing a channel?
		if (this.channel.id && !args.channel) {
			args.channel = this.channel.id
		}

		const qsBits: string[] = []
		for (const arg in args) {
			qsBits.push(`${encodeURIComponent(arg)}=${encodeURIComponent(args[arg])}`)
		}

		const url = `https://slack.com/api/${command}?${qsBits.join('&')}`
		const result = JSON.parse(await request.get({url,
			headers: {Authorization: 'Bearer ' + this.channel.botToken}
		}))

		if (!result.ok) {
			throw new Error(JSON.stringify(result))
		}
		return result
	}

	async* getPages(command: string, limit?: number, inArgs?: any) {
		const args = inArgs || {}
		args.limit = limit || 100
		for (let pageNum = 1;; ++pageNum) {
			// for now limit to 50 pages, just in case
			if (pageNum > 50) {throw new Error('busted safety valve!')}

			const result = await this.get(command, args)
			yield [result, pageNum]

			args.cursor = result.response_metadata && result.response_metadata.next_cursor
			if (!args.cursor) {
				break
			}
		}

	}

	private makeArgs(text: string, messageOpts?: SlackMessageOpts): any {
		let args: {[arg: string]: any};
		if (!messageOpts) {
			args = {text}
		}
		else {
			args = {}
			// markdown disabled to allow custom links
			// (seems can't have both a link and a user @ without looking up user ids)
			const attch: any = {color: messageOpts.style || 'good', text}
			args.attachments = [attch]

			// Add any explicit attachments
			if (messageOpts.attachments) {
				args.attachments = args.attachments.concat(messageOpts.attachments)
			}

			const opts = messageOpts as any
			for (const key in opts) {
				if (MAIN_MESSAGE_FIELDS.has(key)) {
					args[key] = opts[key]
				}
				else if (key !== 'style') {
					attch[key] = opts[key]
				}
			}
		}

		// parse=client doesn't allow custom links (can't seem to turn it specify it just for main message)
		// args.parse = 'client'
		// args.parse = 'full'
		args.link_names = 1
		return args
	}
}


// console.log(await request.get({
// 		url: 'https://slack.com/api/groups.list',
// 		headers: {
// 			Authorization: 'Bearer ' + APP_TOKEN
// 		},
// 		// contentType: 'application/json; charset=utf-8',
// 	}))

	// for (const group of JSON.parse(await request.get({
	// 	url: 'https://slack.com/api/groups.list',
	// 	headers: {
	// 		Authorization: 'Bearer ' + APP_TOKEN
	// 	},
	// 	// contentType: 'application/json; charset=utf-8',
	// })).groups) {
	// 	console.log(group)
	// 	const GROUP_NAME = 'robomerge-test'
	// 	if (group.name === GROUP_NAME) {
	// 		console.log(`'${GROUP_NAME}' id: ${group.id}`)
	// 		break
	// 	}
	// }

