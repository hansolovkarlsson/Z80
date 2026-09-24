// asm/vscode/extension.js - completion, go-to-definition and error markers
// for z80asm source. Colouring is the grammar's job (syntaxes/); the first
// two come from lib.js's scan of the file and everything it INCLUDEs, and
// the markers from running the real assembler.
//
// Where the cursor is decides what comes first: at the start of a
// statement, mnemonics, directives and macros; in operands, the file's
// labels and constants, registers and conditions. Everything is always
// offered, only the order changes, and the case of mnemonics follows the
// file's own.
'use strict';

const childProcess = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const vscode = require('vscode');
const lib = require('./lib');

const K = vscode.CompletionItemKind;

function item(label, kind, detail, sortGroup, documentation) {
    const it = new vscode.CompletionItem(label, kind);
    if (detail) it.detail = detail;
    if (documentation) it.documentation = documentation;
    it.sortText = sortGroup + label.toLowerCase();
    return it;
}

// True while the cursor is still in a line's statement word: nothing but
// an optional label before it.
function inStatementWord(before) {
    return /^\s*([A-Za-z_.][A-Za-z0-9_.]*:\s*)?[A-Za-z_]*$/.test(before)
        || /^[A-Za-z_][A-Za-z0-9_]*\s+[A-Za-z_]*$/.test(before);   // colon-less label
}

function provide(document, position) {
    const text = document.getText();
    const before = document.lineAt(position.line).text.slice(0, position.character);
    if (lib.stripComment(before) !== before) return [];   // the cursor is in a comment

    const lower = lib.prefersLowercase(text);
    const cased = (w) => (lower ? w.toLowerCase() : w.toUpperCase());
    const statement = inStatementWord(before);
    const [first, second] = statement ? ['0', '1'] : ['1', '0'];

    const syms = lib.scanFile(document.uri.fsPath, text);
    const where = (d) => {
        const rel = vscode.workspace.asRelativePath(d.file);
        return `${rel}:${d.line + 1}`;
    };
    const items = [];
    for (const m of lib.keywords.mnemonics) items.push(item(cased(m), K.Keyword, 'instruction', first));
    for (const d of lib.keywords.directives) items.push(item(cased(d), K.Keyword, 'directive', first));
    for (const m of syms.macros)
        items.push(item(m.name, K.Function, `macro ${m.params.join(',')}`.trim(), first, where(m)));
    for (const l of syms.labels) items.push(item(l.name, K.Field, 'label', second, where(l)));
    for (const e of syms.equs) items.push(item(e.name, K.Constant, `equ ${e.value}`, second, where(e)));
    for (const r of lib.keywords.registers) items.push(item(cased(r), K.Variable, 'register', second));
    for (const c of lib.keywords.conditions) items.push(item(cased(c), K.EnumMember, 'condition', second));
    for (const o of lib.keywords.word_operators) items.push(item(cased(o), K.Operator, 'operator', second));
    return items;
}

// Go to definition (F12, Ctrl-click): the same scan, looked up by the
// exact name under the cursor. Nothing inside a comment, and nothing for a
// mnemonic, register or number, which have no definition in the file.
function define(document, position) {
    const line = document.lineAt(position.line).text;
    if (lib.stripComment(line).length < position.character) return [];
    const name = lib.wordAt(line, position.character);
    if (!name) return [];
    const syms = lib.scanFile(document.uri.fsPath, document.getText());
    return lib.definitionsOf(syms, name).map((d) =>
        new vscode.Location(vscode.Uri.file(d.file), new vscode.Position(d.line, d.column)));
}

// --- error markers: the assembler's own verdict, on open and on save -----
//
// Only files that are assembled on their own are checked: an .inc file is
// written to be INCLUDEd, and assembled alone it would report symbols its
// includer defines. The assembler writes into a temporary directory, never
// over the .com beside the source. Pass-1 errors stop the assembler before
// pass 2, so an undefined symbol (a pass-2 error) shows only once the
// pass-1 errors are fixed, exactly as on the command line.
const CHECKED = new Set(['.asm', '.z80', '.mac']);
let diagnostics = null;
const touched = new Map();   // root file -> files it put markers on last time
let warned = false;

function assemblerFor(file) {
    const configured = vscode.workspace.getConfiguration('z80asm').get('path');
    return configured || lib.findAssembler(path.dirname(file));
}

function clear(root) {
    for (const f of touched.get(root) || []) diagnostics.delete(vscode.Uri.file(f));
    touched.delete(root);
}

function check(document) {
    if (document.languageId !== 'z80asm' || document.uri.scheme !== 'file') return Promise.resolve([]);
    const root = document.uri.fsPath;
    if (!CHECKED.has(path.extname(root).toLowerCase())) return Promise.resolve([]);
    const asm = assemblerFor(root);
    if (!asm) {
        if (!warned) {
            warned = true;
            vscode.window.showWarningMessage(
                'z80asm: no bin/z80asm found above this file, so it is not checked. Set z80asm.path to the assembler.');
        }
        return Promise.resolve([]);
    }
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'z80asm-check-'));
    const cwd = path.dirname(root);
    return new Promise((resolve) => {
        childProcess.execFile(asm, [root, '-o', path.join(tmp, 'check.com')], { cwd, timeout: 15000 },
            (err, stdout, stderr) => {
                fs.rmSync(tmp, { recursive: true, force: true });
                if (err && err.code === 'ENOENT') {
                    vscode.window.showWarningMessage(`z80asm: cannot run ${asm}`);
                    return resolve([]);
                }
                const errors = lib.parseAssemblerErrors(`${stdout}\n${stderr}`, cwd);
                clear(root);
                const byFile = new Map();
                for (const e of errors) {
                    if (!byFile.has(e.file)) byFile.set(e.file, []);
                    byFile.get(e.file).push(e);
                }
                for (const [file, list] of byFile) {
                    let lines = [];
                    try { lines = fs.readFileSync(file, 'utf8').split(/\r?\n/); } catch (e) { /* marker still set */ }
                    diagnostics.set(vscode.Uri.file(file), list.map((e) => {
                        const text = lines[e.line] || '';
                        const start = Math.max(0, text.search(/\S/));
                        const d = new vscode.Diagnostic(new vscode.Range(e.line, start, e.line, text.length),
                            e.macro ? `${e.message} (in macro ${e.macro})` : e.message,
                            vscode.DiagnosticSeverity.Error);
                        d.source = 'z80asm';
                        return d;
                    }));
                }
                touched.set(root, [...byFile.keys()]);
                resolve(errors);
            });
    });
}

function activate(context) {
    diagnostics = vscode.languages.createDiagnosticCollection('z80asm');
    context.subscriptions.push(
        diagnostics,
        vscode.languages.registerCompletionItemProvider('z80asm', { provideCompletionItems: provide }),
        vscode.languages.registerDefinitionProvider('z80asm', { provideDefinition: define }),
        vscode.workspace.onDidSaveTextDocument(check),
        vscode.workspace.onDidOpenTextDocument(check),
        vscode.workspace.onDidCloseTextDocument((d) => clear(d.uri.fsPath)));
    for (const d of vscode.workspace.textDocuments) check(d);
}

module.exports = { activate, deactivate() {} };
