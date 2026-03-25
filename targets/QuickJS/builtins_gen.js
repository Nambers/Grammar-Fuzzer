#!/usr/bin/env node
/*
 * QuickJS builtins generator (reflection + probing based).
 *
 * Output schema matches loader in targets/QuickJS/builtins.cpp:
 * {
 *   funcs: { [typeID: string]: PropInfo[] },
 *   modules: { [moduleID: string]: { [typeID: string]: PropInfo[] } },
 *   ops: TypeID[][][],
 *   types: string[],
 *   uops: TypeID[][]
 * }
 */

const fs = require('fs');

const outPath = process.argv[2] || 'builtins.json';

// Keep TypeID stable with C++ side assumptions.
const TYPES = [
	'object',    // 0
	'undefined', // 1
	'boolean',   // 2
	'number',    // 3
	'string',    // 4
	'array',     // 5
	'function',  // 6
	'Math',      // 7
	'JSON'       // 8
];

const T = Object.fromEntries(TYPES.map((name, i) => [name, i]));

const MODULE_NAME_TO_ID = {
	math: 1,
	json: 2
};

const GLOBAL_BLACKLIST = new Set([
	'global',
	'globalThis',
	'process',
	'console',
	'setTimeout',
	'setInterval',
	'setImmediate',
	'queueMicrotask',
	'clearTimeout',
	'clearInterval',
	'clearImmediate',
	'require',
	'module',
	'exports',
	'__dirname',
	'__filename',
	'eval',
	'Function'
]);

const PROP_BLACKLIST = new Set([
	'constructor',
	'__proto__',
	'prototype',
	'caller',
	'callee',
	'arguments'
]);

function makeSig(paramTypes, returnType, selfType = -1) {
	return { paramTypes, selfType, returnType };
}

function makeProp({
	type,
	name,
	isCallable,
	isConst = true,
	scope = 0,
	sig
}) {
	return {
		type,
		scope,
		name,
		isConst,
		isCallable,
		extra: sig
	};
}

function typeIdFromValue(v) {
	if (v === undefined) return T.undefined;
	if (typeof v === 'boolean') return T.boolean;
	if (typeof v === 'number') return T.number;
	if (typeof v === 'string') return T.string;
	if (typeof v === 'function') return T.function;
	if (Array.isArray(v)) return T.array;
	if (v && typeof v === 'object') return T.object;
	return T.object;
}

function enumerateOwnProps(obj) {
	if (!obj) return [];
	return Object.getOwnPropertyNames(obj).filter((k) => !PROP_BLACKLIST.has(k));
}

function mkArgs(tid, argc) {
	let val;
	switch (tid) {
		case T.undefined: val = undefined; break;
		case T.boolean: val = true; break;
		case T.number: val = 1; break;
		case T.string: val = 'x'; break;
		case T.array: val = [1, 2]; break;
		case T.function: val = function () { return 0; }; break;
		default: val = {};
	}
	return Array.from({ length: argc }, () => val);
}

function inferReturnTypeByProbe(fn, thisArg, argc, preferredTypes = []) {
	const order = [...preferredTypes, T.object, T.number, T.string, T.boolean, T.array, T.undefined]
		.filter((v, i, a) => a.indexOf(v) === i);
	for (const tid of order) {
		try {
			const ret = fn.apply(thisArg, mkArgs(tid, argc));
			if (ret && typeof ret.then === 'function' && typeof ret.catch === 'function') {
				ret.catch(() => { });
			}
			return typeIdFromValue(ret);
		} catch {
			// keep trying
		}
	}
	return T.object;
}

function inferCallable(name, fn, selfType = -1, preferredParamType = T.object, thisArg = undefined) {
	const argc = Math.max(0, Number(fn.length) || 0);
	const paramTypes = Array.from({ length: argc }, () => preferredParamType);
	const returnType = inferReturnTypeByProbe(fn, thisArg, argc, [preferredParamType]);
	return makeProp({
		type: returnType,
		name,
		isCallable: true,
		sig: makeSig(paramTypes, returnType, selfType)
	});
}

function inferNonCallable(name, value, selfType = -1) {
	const tid = typeIdFromValue(value);
	return makeProp({
		type: tid,
		name,
		isCallable: false,
		sig: makeSig([], tid, selfType)
	});
}

function safeGet(obj, key) {
	try {
		return obj[key];
	} catch {
		return undefined;
	}
}

function uniqueByName(arr) {
	const seen = new Set();
	const out = [];
	for (const it of arr) {
		if (seen.has(it.name)) continue;
		seen.add(it.name);
		out.push(it);
	}
	return out;
}

// ---------- funcs ----------
const funcs = { '-1': [] };

for (const k of Object.getOwnPropertyNames(globalThis)) {
	if (GLOBAL_BLACKLIST.has(k) || k.startsWith('_')) continue;
	const v = safeGet(globalThis, k);
	if (v === undefined && !(k in globalThis)) continue;
	if (typeof v === 'function') {
		let pref = T.object;
		if (k === 'parseInt' || k === 'parseFloat') pref = T.string;
		funcs['-1'].push(inferCallable(k, v, -1, pref, globalThis));
	} else {
		funcs['-1'].push(inferNonCallable(k, v));
	}
}
funcs['-1'] = uniqueByName(funcs['-1']);

function collectPrototypeBucket(typeId, protoObj, receiverFactory, preferredParamType) {
	const key = String(typeId);
	funcs[key] = [];
	for (const k of enumerateOwnProps(protoObj)) {
		const v = safeGet(protoObj, k);
		if (typeof v === 'function') {
			funcs[key].push(
				inferCallable(k, v, typeId, preferredParamType, receiverFactory())
			);
		} else {
			funcs[key].push(inferNonCallable(k, v, typeId));
		}
	}
	funcs[key] = uniqueByName(funcs[key]);
}

collectPrototypeBucket(T.number, Number.prototype, () => 1, T.number);
collectPrototypeBucket(T.string, String.prototype, () => 'x', T.string);
collectPrototypeBucket(T.array, Array.prototype, () => [1, 2, 3], T.object);
collectPrototypeBucket(T.boolean, Boolean.prototype, () => true, T.boolean);
collectPrototypeBucket(T.object, Object.prototype, () => ({ a: 1 }), T.object);
collectPrototypeBucket(T.function, Function.prototype, () => function () { return 0; }, T.object);

// ---------- modules ----------
// Module IDs must match mutation logic: distLib index + 1.
// TARGET_LIBS in targets/QuickJS/target.cpp is {"math", "json"}.
const modules = {
	'1': { '-1': [] }, // math
	'2': { '-1': [] }  // json
};

function collectModule(moduleName, modObj, preferredParamType) {
	const mid = MODULE_NAME_TO_ID[moduleName];
	if (!mid || !modObj) return;
	for (const k of enumerateOwnProps(modObj)) {
		const v = safeGet(modObj, k);
		const fq = `${moduleName}.${k}`;
		if (typeof v === 'function') {
			modules[String(mid)]['-1'].push(
				inferCallable(fq, v, -1, preferredParamType, modObj)
			);
		} else {
			modules[String(mid)]['-1'].push(inferNonCallable(fq, v));
		}
	}
}

collectModule('math', globalThis.Math, T.number);
collectModule('json', globalThis.JSON, T.object);
modules['1']['-1'] = uniqueByName(modules['1']['-1']);
modules['2']['-1'] = uniqueByName(modules['2']['-1']);

// ---------- ops/uops ----------
const probeVals = [
	{},
	undefined,
	true,
	1,
	'x',
	[1, 2],
	function () { return 0; },
	globalThis.Math,
	globalThis.JSON
];

const BOPS = [
	(a, b) => a + b,
	(a, b) => a - b,
	(a, b) => a * b,
	(a, b) => a / b,
	(a, b) => a % b,
	(a, b) => a ** b,
	(a, b) => a / b, // "//" mapped to "/" on JS side
	(a, b) => a == b,
	(a, b) => a != b,
	(a, b) => a < b,
	(a, b) => a > b,
	(a, b) => a <= b,
	(a, b) => a >= b,
	(a, b) => a & b,
	(a, b) => a | b,
	(a, b) => a ^ b,
	(a, b) => a << b,
	(a, b) => a >> b
];

function safeCall(fn, ...args) {
	try {
		fn(...args);
		return true;
	} catch {
		return false;
	}
}

const ops = [];
for (const op of BOPS) {
	const table = [[]];
	for (let i = 1; i < probeVals.length; i++) {
		const row = [];
		for (let j = 1; j < probeVals.length; j++) {
			if (safeCall(op, probeVals[i], probeVals[j])) row.push(j);
		}
		table.push(row);
	}
	ops.push(table);
}

const UOPS = [
	(a) => -a,
	(a) => !a, // maps to "not"
	(a) => ~a
];
const uops = [
	[]
];
for (const uop of UOPS) {
	const row = [];
	for (let i = 1; i < probeVals.length; i++) {
		if (safeCall(uop, probeVals[i])) row.push(i);
	}
	uops.push(row);
}

const result = {
	funcs,
	modules,
	ops,
	types: TYPES,
	uops
};

fs.writeFileSync(outPath, JSON.stringify(result, null, 2) + '\n', 'utf8');
console.log(`[builtins_gen] generated ${outPath}`);
