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
function completionChecks() {
    const Module = require('module');
    let provider = null;
    class CompletionItem { constructor(label, kind) { this.label = label; this.kind = kind; } }
    const fake = {
        CompletionItem,
        CompletionItemKind: { Keyword: 'Keyword', Function: 'Function', Field: 'Field', Constant: 'Constant',
                              Variable: 'Variable', EnumMember: 'EnumMember', Operator: 'Operator' },
        workspace: { asRelativePath: (p) => path.relative(ROOT, p) },
        languages: { registerCompletionItemProvider: (lang, p) => { provider = p; return { dispose() {} }; } },
    };
    const realLoad = Module._load;
    Module._load = function (req, ...rest) { return req === 'vscode' ? fake : realLoad.call(this, req, ...rest); };
    try { require(path.join(HERE, 'extension.js')).activate({ subscriptions: [] }); }
    finally { Module._load = realLoad; }

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
}

(async () => {
    keywordChecks();
    scannerChecks();
    completionChecks();
    await grammarChecks();
    process.exit(failed ? 1 : 0);
})().catch((e) => { console.log(`FAIL: vscode/runner\n    ${e.stack}`); process.exit(1); });
