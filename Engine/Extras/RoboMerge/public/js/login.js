// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
"use strict"

const COOKIE_REGEX = /\s*(.*?)=(.*)/;
function getCookie(name) {
	for (const cookieKV of decodeURIComponent(document.cookie).split(';')) {
		const match = cookieKV.match(COOKIE_REGEX);
		if (match && match[1].toLowerCase() === name.toLowerCase()) {
			return match[2];
		}
	}
	return null;
}

$(() => {

	const $form = $('.login-form form');

	const clearError = () => $('#login-error').hide();

	const $nameInput = $('input[name=user]', $form).on('input', clearError);
	const $passwordInput = $('input[name=password]', $form).on('input', clearError);

	$form.on('submit', () => {
		const $submitButton = $('button', $form).prop('disabled', true).text('Signing in ...');

		// clear auth on attempt to log in
		document.cookie = 'auth=;';

		const postData = {
			user: $nameInput.val(),
			password: $passwordInput.val()
		};

		$.post('/dologin', postData)
		.then((token) => {
			// win!
			clearError();

			const whereTo = getCookie('redirect_to') || '/';

			document.cookie = `auth=${token}; secure=true`;
			document.cookie = 'redirect_to=;';

			// @todo remove redirect_to cookie on server!
			window.location = whereTo + window.location.hash;

		}, (xhr, status, error) => {
			// bad luck
			$submitButton.prop('disabled', false).text('Sign in');
			$passwordInput.focus();

			$('#login-error')
			.text(error === 'Unauthorized' ? 'Invalid username or password!' : 'Failed to authenticate, please try later')
			.show();
		});

		return false;
	});
});
