// asm/vscode/extension.js - completion and go-to-definition for z80asm
// source. Colouring is the grammar's job (syntaxes/); both of these come
// from lib.js's scan of the file and everything it INCLUDEs.
//
// Where the cursor is decides what comes first: at the start of a
// statement, mnemonics, directives and macros; in operands, the file's
// labels and constants, registers and conditions. Everything is always
// offered, only the order changes, and the case of mnemonics follows the
// file's own.
'use strict';

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

function activate(context) {
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('z80asm', { provideCompletionItems: provide }),
        vscode.languages.registerDefinitionProvider('z80asm', { provideDefinition: define }));
}

module.exports = { activate, deactivate() {} };
