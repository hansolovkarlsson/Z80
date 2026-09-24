// asm/vscode/test/run.js - checks the extension against the assembler.
// Run through run_tests.sh, which supplies VS Code's own Node (this machine
// needs no separate one) and the path to VS Code's bundled TextMate engine,
// so the grammar is tested by the exact code that will run it.
//
// Three kinds of check, each printing PASS/FAIL lines in the convention of
// cpm/tests/run_tests.sh:
//   - the keyword lists against asm/src, both ways, since they are a copy;
//   - the symbol scanner against `z80asm -s` on real sources;
//   - the grammar, by tokenising real lines and asserting on scopes.
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');

const HERE = path.resolve(__dirname, '..');
const ROOT = path.resolve(HERE, '../..');
const APP = process.argv[2];
const lib = require(path.join(HERE, 'lib.js'));

let failed = 0;
function report(name, problems) {
    if (problems.length === 0) { console.log(`PASS: vscode/${name}`); return; }
    failed++;
    console.log(`FAIL: vscode/${name}`);
    for (const p of problems.slice(0, 20)) console.log(`    ${p}`);
    if (problems.length > 20) console.log(`    ... ${problems.length - 20} more`);
}
const diff = (a, b) => [...a].filter((x) => !b.has(x)).sort();

// --- the keyword lists are a copy of asm/src's; compare both ways ---------
function keywordChecks() {
    const encode = fs.readFileSync(path.join(ROOT, 'asm/src/encode.c'), 'utf8');
    const table = /is_known_mnemonic[\s\S]*?names\[\]\s*=\s*\{([\s\S]*?)\};/.exec(encode);
    const asmMnem = new Set([...table[1].matchAll(/"([A-Z0-9]+)"/g)].map((m) => m[1]));
    const ours = new Set(lib.keywords.mnemonics.map((m) => m.toUpperCase()));
    report('mnemonics-match-assembler', [
        ...diff(asmMnem, ours).map((m) => `the assembler accepts ${m}; keywords.json does not list it`),
        ...diff(ours, asmMnem).map((m) => `keywords.json lists ${m}; the assembler does not accept it`)]);

    const asmDir = new Set();
    for (const f of ['assemble.c', 'preprocess.c', 'main.c']) {
        const src = fs.readFileSync(path.join(ROOT, 'asm/src', f), 'utf8');
        for (const m of src.matchAll(/str(?:n)?casecmp\([^,]+,\s*"([A-Z0-9]+)"/g)) asmDir.add(m[1]);
    }
    const ourDir = new Set(lib.keywords.directives.map((d) => d.toUpperCase()));
    report('directives-match-assembler', [
        ...diff(asmDir, ourDir).map((d) => `the assembler handles ${d}; keywords.json does not list it`),
        ...diff(ourDir, asmDir).map((d) => `keywords.json lists ${d}; the assembler does not handle it`)]);
}

// --- the scanner finds what the assembler defines -------------------------
function scannerChecks() {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'z80asm-vscode-'));
    const sources = fs.readdirSync(path.join(ROOT, 'asm/examples'))
        .filter((f) => f.endsWith('.asm')).map((f) => path.join(ROOT, 'asm/examples', f));
    sources.push(path.join(ROOT, 'cpm/emu/zexall/ZEXALL-main/zexall.mac'));
    const problems = [];
    for (const src of sources) {
        const sym = path.join(tmp, 'out.sym');
        try {
            execFileSync(path.join(ROOT, 'bin/z80asm'), [src, '-o', path.join(tmp, 'out.com'), '-s', sym],
                { stdio: 'ignore' });
        } catch (e) { problems.push(`${path.basename(src)}: z80asm failed`); continue; }
        const asm = { label: new Set(), equ: new Set() };
        for (const line of fs.readFileSync(sym, 'utf8').split('\n')) {
            const m = /^(\S+)\s+equ\s+\S+\s+;\s+(label|equ)$/.exec(line);
            if (m && !/^__.*_L\d+$/.test(m[1])) asm[m[2]].add(m[1]);   // LOCAL names are per expansion
        }
        const got = lib.scanFile(src);
        const ours = { label: new Set(got.labels.map((l) => l.name)), equ: new Set(got.equs.map((e) => e.name)) };
        const name = path.basename(src);
        for (const kind of ['label', 'equ']) {
            for (const n of diff(asm[kind], ours[kind])) problems.push(`${name}: the assembler defines ${kind} ${n}; the scanner missed it`);
            for (const n of diff(ours[kind], asm[kind])) problems.push(`${name}: the scanner found ${kind} ${n}; the assembler does not define it`);
        }
    }
    fs.rmSync(tmp, { recursive: true, force: true });
    report('scanner-matches-assembler', problems);
}

// --- the grammar, through VS Code's own TextMate engine -------------------
async function grammarChecks() {
    const tm = require(path.join(APP, 'node_modules.asar/vscode-textmate'));
    const onig = require(path.join(APP, 'node_modules.asar/vscode-oniguruma'));
    const wasm = fs.readFileSync(path.join(APP, 'node_modules.asar.unpacked/vscode-oniguruma/release/onig.wasm'));
    await onig.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
    const registry = new tm.Registry({
        onigLib: Promise.resolve({
            createOnigScanner: (p) => new onig.OnigScanner(p),
            createOnigString: (s) => new onig.OnigString(s) }),
        loadGrammar: async () => tm.parseRawGrammar(
            fs.readFileSync(path.join(HERE, 'syntaxes/z80asm.tmLanguage.json'), 'utf8'), 'z80asm.tmLanguage.json'),
    });
    const grammar = await registry.loadGrammar('source.z80asm');

    // The scope of the token that starts where `text` first appears on the line.
    const scopeOf = (line, text) => {
        // Leading spaces and commas only anchor the search: ', l' means that l.
        const at = line.indexOf(text) + /^[\s,]*/.exec(text)[0].length;
        const { tokens } = grammar.tokenizeLine(line, tm.INITIAL);
        const t = tokens.find((tok) => tok.startIndex <= at && at < tok.endIndex);
        return t ? t.scopes[t.scopes.length - 1] : '(none)';
    };
    const cases = [
        ['count_loop:', 'count_loop', 'entity.name.function.label'],
        ['        djnz count_loop         ; HL should end up holding 5', 'djnz', 'keyword.other.mnemonic'],
        ['        djnz count_loop         ; HL should end up holding 5', '; HL', 'comment.line.semicolon'],
        ['BDOS:   equ 5', 'BDOS', 'variable.other.constant'],
        ['BDOS:   equ 5', 'equ', 'storage.type.directive'],
        ['        org 100h', '100h', 'constant.numeric.hex'],
        ['        ld a, 0FFh', '0FFh', 'constant.numeric.hex'],
        ['        ld a, 0xFF', '0xFF', 'constant.numeric.hex'],
        ['        ld a, $FF', '$FF', 'constant.numeric.hex'],
        ['        ld a, 1100b', '1100b', 'constant.numeric.binary'],
        ['        ld hl, $-start', '$', 'variable.language.location-counter'],
        ['        ld a, l', ', l', 'variable.language.register'],
        ['        ex af, af\'', 'af\'', 'variable.language.register'],
        ['        ex af, af\'    ; swap', '; swap', 'comment.line.semicolon'],
        ['        jp nz, fail', 'nz', 'keyword.other.condition'],
        ['        ld a, (ix+5)', 'ix', 'variable.language.register'],
        ['msg:    db \'Hello; not a comment$\'', '\'Hello', 'string.quoted.single'],
        ['        ld e, \'0\'', '\'0\'', 'string.quoted.single'],
        ['        ld a, low msg', 'low', 'keyword.operator.word'],
        ['tstr    macro insn,memop', 'tstr', 'entity.name.function.macro'],
        ['        db &lab', '&lab', 'variable.parameter'],
        ['bdos    push af', 'bdos', 'entity.name.function.label'],
        ['        include "defs.inc"', 'include', 'storage.type.directive'],
        ['        rept target-$', 'rept', 'storage.type.directive'],
    ];
    const problems = [];
    for (const [line, text, want] of cases) {
        const got = scopeOf(line, text);
        if (!got.startsWith(want)) problems.push(`"${line.trim()}": ${text.trim()} is ${got}, expected ${want}`);
    }
    report('grammar-scopes', problems);
}

// --- completion, through extension.js with a stand-in for the vscode API ---
// Not a live editor: the stand-in records the provider extension.js
// registers and hands it documents, so what is checked is the extension's
// own logic (what is offered, in which order, in which case).
// One stand-in for the vscode API, shared by every check below. It records
// what extension.js registers (providers, event handlers, markers) so the
// checks can drive the extension's own code without a live editor.
let ext = null;
function loadExtension() {
    if (ext) return ext;
    const Module = require('module');
    ext = { provider: null, definer: null, hoverer: null, onSave: null, config: {}, warnings: [], markers: new Map() };
    class CompletionItem { constructor(label, kind) { this.label = label; this.kind = kind; } }
    class Position { constructor(line, character) { this.line = line; this.character = character; } }
    class Location { constructor(uri, pos) { this.file = uri.fsPath; this.line = pos.line; this.character = pos.character; } }
    class Range { constructor(l1, c1, l2, c2) { this.start = { line: l1, character: c1 }; this.end = { line: l2, character: c2 }; } }
    class Diagnostic { constructor(range, message, severity) { this.range = range; this.message = message; this.severity = severity; } }
    class MarkdownString { constructor(value) { this.value = value; } }
    class Hover { constructor(contents) { this.contents = contents; } }
    const event = (key) => (cb) => { if (key) ext[key] = cb; return { dispose() {} }; };
    const fake = {
        CompletionItem, Position, Location, Range, Diagnostic, MarkdownString, Hover,
        DiagnosticSeverity: { Error: 'Error' },
        Uri: { file: (f) => ({ fsPath: f, toString: () => f }) },
        CompletionItemKind: { Keyword: 'Keyword', Function: 'Function', Field: 'Field', Constant: 'Constant',
                              Variable: 'Variable', EnumMember: 'EnumMember', Operator: 'Operator' },
        window: { showWarningMessage: (m) => { ext.warnings.push(m); } },
        workspace: {
            asRelativePath: (p) => path.relative(ROOT, p),
            getConfiguration: () => ({ get: (k) => ext.config[k] }),
            onDidSaveTextDocument: event('onSave'), onDidOpenTextDocument: event(null),
            onDidCloseTextDocument: event(null), textDocuments: [],
        },
        languages: {
            registerCompletionItemProvider: (lang, p) => { ext.provider = p; return { dispose() {} }; },
            registerDefinitionProvider: (lang, p) => { ext.definer = p; return { dispose() {} }; },
            registerHoverProvider: (lang, p) => { ext.hoverer = p; return { dispose() {} }; },
            createDiagnosticCollection: () => ({
                set: (uri, list) => ext.markers.set(uri.fsPath, list),
                delete: (uri) => ext.markers.delete(uri.fsPath),
                dispose() {},
            }),
        },
    };
    const realLoad = Module._load;
    Module._load = function (req, ...rest) { return req === 'vscode' ? fake : realLoad.call(this, req, ...rest); };
    try { require(path.join(HERE, 'extension.js')).activate({ subscriptions: [] }); }
    finally { Module._load = realLoad; }
    return ext;
}

function completionChecks() {
    const { provider, definer } = loadExtension();

    const complete = (file, lineText) => {
        const text = fs.readFileSync(file, 'utf8');
        const doc = { getText: () => text, uri: { fsPath: file }, lineAt: () => ({ text: lineText }) };
        const items = provider.provideCompletionItems(doc, { line: 0, character: lineText.length });
        return items.slice().sort((a, b) => a.sortText.localeCompare(b.sortText));
    };
    const problems = [];
    const hello = path.join(ROOT, 'asm/examples/hello.asm');
    const inc = path.join(ROOT, 'asm/examples/include_test.asm');

    let items = complete(hello, '        dj');
    if (items[0].kind !== 'Keyword') problems.push(`at a statement, the first item is ${items[0].label}, not an instruction`);
    if (!items.some((i) => i.label === 'djnz')) problems.push('djnz not offered in lower case, as hello.asm writes it');

    items = complete(hello, '        djnz cou');
    const firstKeyword = items.findIndex((i) => i.kind === 'Keyword');
    const loop = items.findIndex((i) => i.label === 'count_loop');
    if (loop < 0) problems.push('count_loop not offered');
    else if (loop > firstKeyword) problems.push('in operands, count_loop sorts after the instructions');

    if (complete(hello, '        ld a, 5   ; cou').length) problems.push('suggestions offered inside a comment');

    items = complete(inc, '        ld c, ');
    const macro = items.find((i) => i.label === 'PRINTMSG');
    const bdos = items.find((i) => i.label === 'BDOS');
    if (!macro || macro.kind !== 'Function') problems.push('PRINTMSG, a macro from the INCLUDEd file, not offered as one');
    if (!bdos || !String(bdos.documentation).includes('include_defs.inc')) problems.push('BDOS not offered with the file that defines it');
    report('completion-items', problems);

    // Go to definition. The expected places come from a plain text search
    // of the file, not from the scanner, so the two cannot share a mistake.
    const defProblems = [];
    const lineOf = (file, re) => fs.readFileSync(file, 'utf8').split(/\r?\n/).findIndex((l) => re.test(l));
    const define = (file, lineText, at) => {
        const text = fs.readFileSync(file, 'utf8');
        const doc = { getText: () => text, uri: { fsPath: file }, lineAt: () => ({ text: lineText }) };
        return definer.provideDefinition(doc, { line: 0, character: lineText.indexOf(at) + 1 });
    };
    const defs = path.join(ROOT, 'asm/examples/include_defs.inc');
    const expectAt = (label, got, file, line) => {
        if (got.length !== 1 || got[0].file !== file || got[0].line !== line || got[0].character !== 0)
            defProblems.push(`${label}: got ${JSON.stringify(got)}, expected ${path.basename(file)}:${line + 1}`);
    };
    expectAt('count_loop', define(hello, '        djnz count_loop', 'count_loop'), hello, lineOf(hello, /^count_loop:/));
    expectAt('fail', define(hello, '        jp nz, fail', 'fail'), hello, lineOf(hello, /^fail:/));
    expectAt('PRINTMSG, a macro in an INCLUDEd file', define(inc, '        PRINTMSG msg', 'PRINTMSG'), defs, lineOf(defs, /^PRINTMSG\s+MACRO/i));
    expectAt('BDOS, an equ in an INCLUDEd file', define(inc, '        ld c, BDOS', 'BDOS'), defs, lineOf(defs, /^BDOS\b/));
    if (define(hello, '        djnz count_loop', 'djnz').length) defProblems.push('a mnemonic has a definition');
    if (define(hello, '        djnz COUNT_LOOP', 'COUNT_LOOP').length) defProblems.push('COUNT_LOOP matched count_loop; labels are case-sensitive');
    if (define(hello, '        nop   ; see count_loop', 'count_loop').length) defProblems.push('a name inside a comment has a definition');
    report('go-to-definition', defProblems);
}

// Error markers: the extension runs the real assembler on save. The test
// writes its own broken files, so the lines that must carry a marker are
// known from the input, not from anything the extension computed.
async function diagnosticsChecks() {
    const e = loadExtension();
    const problems = [];
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'z80asm-diag-'));
    const main = path.join(dir, 'bad.asm'), defs = path.join(dir, 'defs.inc');
    const write = (f, lines) => fs.writeFileSync(f, lines.join('\n') + '\n');
    write(main, ['        org 100h', '        include "defs.inc"', 'start:  ld a, 5',
                 '        ld q, 1', '        TWICE 3', '        nop']);
    write(defs, ['TWICE   MACRO n', '        ld b, &n', '        ld zz, &n', '        ENDM', '        ld q, 2']);
    const doc = (f) => ({ languageId: 'z80asm', uri: { fsPath: f, scheme: 'file' } });
    e.config.path = path.join(ROOT, 'bin/z80asm');

    await e.onSave(doc(main));
    const at = (f) => (e.markers.get(f) || []).map((d) => `${d.range.start.line}:${d.range.start.character}`).sort();
    if (at(main).join(' ') !== '3:8 4:8') problems.push(`bad.asm markers at ${at(main).join(' ') || 'none'}, expected lines 4 and 5 at the indent (3:8 4:8)`);
    if (at(defs).join(' ') !== '4:8') problems.push(`defs.inc markers at ${at(defs).join(' ') || 'none'}, expected its line 5 (4:8)`);
    const macroMsg = (e.markers.get(main) || []).find((d) => d.range.start.line === 4);
    if (!macroMsg || !macroMsg.message.includes('in macro TWICE')) problems.push('the macro call\'s marker does not name the macro');
    write(main, ['        org 100h', '        include "defs.inc"', 'start:  ld a, 5', '        nop']);
    write(defs, ['TWICE   MACRO n', '        ld b, &n', '        ENDM']);
    await e.onSave(doc(main));
    if (e.markers.has(main) || e.markers.has(defs)) problems.push('fixing the errors did not clear the markers');
    // Only a clean assembly writes output, so this is the moment a .com
    // beside the source would appear if the check wrote one there.
    const left = fs.readdirSync(dir).filter((f) => f !== 'bad.asm' && f !== 'defs.inc');
    if (left.length) problems.push(`the check left ${left.join(', ')} beside the source`);

    const incOnly = await e.onSave(doc(defs));
    if (incOnly.length || e.markers.has(defs)) problems.push('an .inc file was checked on its own');

    delete e.config.path;
    await e.onSave(doc(path.join(ROOT, 'asm/examples/hello.asm')));
    if (e.warnings.length) problems.push(`a file inside the repo did not find bin/z80asm: ${e.warnings[0]}`);
    await e.onSave(doc(main));
    if (!e.warnings.some((w) => w.includes('z80asm.path'))) problems.push('a file with no bin/z80asm above it gave no warning');
    fs.rmSync(dir, { recursive: true, force: true });
    report('error-markers', problems);
}

// Hover. Numbers are checked against the assembler itself: each literal is
// assembled with DW and the two bytes it produced must be the value shown.
// Names are checked against the defining line found by a text search.
function hoverChecks() {
    const e = loadExtension();
    const problems = [];
    const hoverAt = (file, lineText, at) => {
        const text = fs.readFileSync(file, 'utf8');
        const doc = { getText: () => text, uri: { fsPath: file }, lineAt: () => ({ text: lineText }) };
        const h = e.hoverer.provideHover(doc, { line: 0, character: lineText.indexOf(at) + 1 });
        return h ? h.contents.value : null;
    };
    const hello = path.join(ROOT, 'asm/examples/hello.asm');

    const literals = ['0FFh', '0xFF', '$FF', '1100b', '100', '41h', '0', '65535', '$1234', '0ABCDh', '101b', '7Fh'];
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'z80asm-hover-'));
    fs.writeFileSync(path.join(tmp, 'n.asm'), '        org 0\n' + literals.map((l) => `        dw ${l}`).join('\n') + '\n');
    execFileSync(path.join(ROOT, 'bin/z80asm'), [path.join(tmp, 'n.asm'), '-o', path.join(tmp, 'n.com')], { stdio: 'ignore' });
    const bytes = fs.readFileSync(path.join(tmp, 'n.com'));
    fs.rmSync(tmp, { recursive: true, force: true });
    literals.forEach((lit, k) => {
        const assembled = bytes[2 * k] | (bytes[2 * k + 1] << 8);
        const shown = hoverAt(hello, `        dw ${lit}`, lit);
        if (!shown || !shown.includes(`= ${assembled} =`)) problems.push(`${lit}: z80asm made ${assembled}; the hover says ${shown}`);
    });
    const a = hoverAt(hello, '        ld a, 41h', '41h');
    if (!a || !a.includes("'A'")) problems.push(`41h does not show as 'A': ${a}`);

    const helloLine = (re) => fs.readFileSync(hello, 'utf8').split(/\r?\n/).findIndex((l) => re.test(l));
    const loop = hoverAt(hello, '        djnz count_loop', 'count_loop');
    if (!loop || !loop.includes('label `count_loop`') || !loop.includes(`hello.asm:${helloLine(/^count_loop:/) + 1}`))
        problems.push(`count_loop hover: ${loop}`);
    const inc = path.join(ROOT, 'asm/examples/include_test.asm');
    const defsText = fs.readFileSync(path.join(ROOT, 'asm/examples/include_defs.inc'), 'utf8').split(/\r?\n/);
    const bdosLine = defsText.find((l) => /^BDOS\b/.test(l)).trim();
    const bdos = hoverAt(inc, '        call BDOS', 'BDOS');
    if (!bdos || !bdos.includes('constant `BDOS`') || !bdos.includes(bdosLine) || !bdos.includes('include_defs.inc'))
        problems.push(`BDOS hover: ${bdos}`);
    const mac = hoverAt(inc, '        PRINTMSG msg', 'PRINTMSG');
    if (!mac || !mac.includes('macro `PRINTMSG`')) problems.push(`PRINTMSG hover: ${mac}`);
    if (hoverAt(hello, '        djnz count_loop', 'djnz')) problems.push('a mnemonic has a hover');
    if (hoverAt(hello, '        nop   ; 0FFh count_loop', '0FFh')) problems.push('a number inside a comment has a hover');
    report('hover', problems);
}

(async () => {
    keywordChecks();
    scannerChecks();
    completionChecks();
    hoverChecks();
    await diagnosticsChecks();
    await grammarChecks();
    process.exit(failed ? 1 : 0);
})().catch((e) => { console.log(`FAIL: vscode/runner\n    ${e.stack}`); process.exit(1); });
