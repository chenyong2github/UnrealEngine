// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

importScripts('/js/graphviz-asm.js');
importScripts('/js/graphviz.js');

onmessage = function(event) {
	postMessage(render(event.data));
};
