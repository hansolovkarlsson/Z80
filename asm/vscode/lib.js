// asm/vscode/lib.js - the symbol scanner behind completion, with no VS Code
// dependency, so test/run.js can check it against the assembler itself.
//
// It follows asm/src's own line rules rather than a general idea of Z80
// syntax: a label is `name:` or a first word that is not a mnemonic,
// directive or macro; `name EQU expr` is a constant, `name MACRO params` a
// macro; a ' after a letter, digit or _ does not open a string (AF'); and
// INCLUDE paths are relative to the including file. What it cannot do is
// evaluate IF, so a label inside a false branch is still offered.
'use strict';

const fs = require('fs');
const path = require('path');
const keywords = require('./keywords.json');

const upper = (xs) => new Set(xs.map((x) => x.toUpperCase()));
const MNEMONICS = upper(keywords.mnemonics);
const DIRECTIVES = upper(keywords.directives);

// Everything before a ';' that is not inside a string, as strip_comment()
// in asm/src/assemble.c decides it.
function stripComment(line) {
    let sq = false, dq = false;
    for (let i = 0; i < line.length; i++) {
        const c = line[i];
        if (c === "'" && !dq) {
            if (sq) sq = false;
            else if (!/[A-Za-z0-9_]/.test(line[i - 1] || '')) sq = true;
        } else if (c === '"' && !sq) dq = !dq;
        else if (c === ';' && !sq && !dq) return line.slice(0, i);
    }
    return line;
}

const isKeyword = (w) => MNEMONICS.has(w.toUpperCase()) || DIRECTIVES.has(w.toUpperCase());

const MACRO_DEF = /^\s*([A-Za-z_.][A-Za-z0-9_.]*):?\s+macro\b/i;

function macroNamesIn(text) {
    const names = new Set();
    for (const raw of text.split(/\r?\n/).map(stripComment)) {
        const m = MACRO_DEF.exec(raw);
        if (m) names.add(m[1]);
    }
    return names;
}

// One file's definitions. Line numbers are 0-based, as VS Code counts.
// `knownMacros` are the macro names from every file in the include graph:
// an invocation is not a label, and the assembler expands macros across
// all of them before it reads a single label, so a macro defined in an
// INCLUDEd file must be known here too.
function scanText(text, knownMacros = new Set()) {
    const lines = text.split(/\r?\n/).map(stripComment);
    const out = { labels: [], equs: [], macros: [], includes: [] };
    const macroNames = new Set([...knownMacros, ...macroNamesIn(text)]);

    let depth = 0;   // inside a MACRO or REPT body; ENDM closes either
    lines.forEach((raw, line) => {
        const text = raw.trim();
        if (!text) return;
        const column = raw.search(/\S/);   // a definition is the line's first word

        const def = /^([A-Za-z_.][A-Za-z0-9_.]*):?\s+(macro|equ)\b\s*(.*)$/i.exec(text);
        if (def && def[2].toLowerCase() === 'macro') {
            if (depth === 0) {
                out.macros.push({ name: def[1], line, column,
                    params: def[3].split(',').map((p) => p.trim()).filter(Boolean) });
            }
            depth++;
            return;
        }
        const first = /^([A-Za-z0-9_]+)/.exec(text);
        const word = first ? first[1].toUpperCase() : '';
        if (word === 'REPT') { depth++; return; }
        if (word === 'ENDM') { if (depth > 0) depth--; return; }
        if (depth > 0) return;   // bodies are expanded per call; their names are not the file's

        if (def) { out.equs.push({ name: def[1], line, column, value: def[3].trim() }); return; }
        if (word === 'INCLUDE') {
            const p = text.slice(7).trim().replace(/^["']|["']$/g, '');
            if (p) out.includes.push(p);
            return;
        }
        const colon = /^([A-Za-z_.][A-Za-z0-9_.]*):/.exec(text);
        if (colon) { out.labels.push({ name: colon[1], line, column }); return; }
        if (first && /^[A-Za-z_]/.test(first[1]) && !isKeyword(first[1]) && !macroNames.has(first[1])) {
            out.labels.push({ name: first[1], line, column });
        }
    });
    return out;
}

const INCLUDE_LINE = /^\s*include\s+["']?([^"';\s]+)/i;

// A file and everything it INCLUDEs, each file once. The files are
// gathered first so that macro names from all of them are known before
// any line is classified. Every definition carries the file it came from.
function scanFile(file, text, read = (f) => fs.readFileSync(f, 'utf8')) {
    const files = [];
    const seen = new Set();
    const gather = (f, body) => {
        const key = path.resolve(f);
        if (seen.has(key) || seen.size > 64) return;
        seen.add(key);
        if (body === undefined) {
            try { body = read(key); } catch (e) { return; }
        }
        files.push({ file: key, text: body });
        for (const raw of body.split(/\r?\n/)) {
            const m = INCLUDE_LINE.exec(stripComment(raw));
            if (m) gather(path.resolve(path.dirname(key), m[1]));
        }
    };
    gather(file, text);

    const macros = new Set();
    for (const f of files) for (const n of macroNamesIn(f.text)) macros.add(n);
    const all = { labels: [], equs: [], macros: [] };
    for (const f of files) {
        const own = scanText(f.text, macros);
        for (const kind of ['labels', 'equs', 'macros'])
            for (const d of own[kind]) all[kind].push({ ...d, file: f.file });
    }
    return all;
}

// Whether a file writes its mnemonics in lower case, so completion can
// match it.
function prefersLowercase(text) {
    let lower = 0, up = 0;
    for (const m of text.matchAll(/^\s+([A-Za-z]+)\b/gm)) {
        if (!MNEMONICS.has(m[1].toUpperCase())) continue;
        if (m[1] === m[1].toLowerCase()) lower++;
        else if (m[1] === m[1].toUpperCase()) up++;
    }
    return lower >= up;
}

// The identifier under a cursor, or null. `&name` inside a macro body is a
// parameter or LOCAL, which has no single definition, so it gives null too.
function wordAt(lineText, character) {
    const re = /&?[A-Za-z_.][A-Za-z0-9_.]*/g;
    for (const m of lineText.matchAll(re)) {
        if (m.index <= character && character <= m.index + m[0].length) {
            return m[0].startsWith('&') ? null : m[0];
        }
    }
    return null;
}

// Where a name is defined: every label, constant or macro of that exact
// name, since labels are case-sensitive in the assembler.
function definitionsOf(syms, name) {
    return [...syms.labels, ...syms.equs, ...syms.macros].filter((d) => d.name === name);
}

module.exports = { keywords, stripComment, scanText, scanFile, prefersLowercase, wordAt, definitionsOf };
