// Builds the Windows release package: dist/llm-video-engine-<version>-windows-x64.zip with the engine,
// the VC++ runtime DLLs it links against, the bundled font, the Claude skill zip, an install script and
// a short readme. Run after a release build (cmake --build --preset release in engine/).
//
//   node tools/package_release.mjs
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { writeZip } from '../skill/scripts/lib/zip.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const exe = path.join(root, 'engine/build/release/llm-video-engine.exe');
if (!fs.existsSync(exe)) throw new Error('build the engine first: cmake --build --preset release (in engine/)');
const version = execFileSync(exe, ['--version']).toString().trim().split(' ').pop();
const name = `llm-video-engine-${version}-windows-x64`;

// The skill zip is rebuilt so both halves of the release come from the same tree.
execFileSync(process.execPath, [path.join(root, 'tools/package_skill.mjs')], { stdio: 'ignore' });

// VC++ runtime, app-local (allowed by Microsoft's redistribution terms): from the Build Tools redist folder,
// else the copies installed in System32.
const crt = ['msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'];
const redistRoots = ['C:/Program Files (x86)/Microsoft Visual Studio/2022', 'C:/Program Files/Microsoft Visual Studio/2022']
  .flatMap((vs) => (fs.existsSync(vs) ? fs.readdirSync(vs).map((ed) => path.join(vs, ed, 'VC/Redist/MSVC')) : []))
  .filter((p) => fs.existsSync(p))
  .flatMap((p) => fs.readdirSync(p).filter((v) => /^\d/.test(v)).sort().reverse().map((v) => path.join(p, v, 'x64/Microsoft.VC143.CRT')));
const crtDir = [...redistRoots, 'C:/Windows/System32'].find((d) => crt.every((f) => fs.existsSync(path.join(d, f))));
if (!crtDir) throw new Error('VC++ runtime DLLs not found');

const readme = `llm-video-engine ${version}
====================

Renders the video plan a Claude skill writes from an HTML deck (video-plan.zip) into an MP4,
on your machine. No browser, LLM or network involved.

Install (once)
  1. Unzip this folder somewhere permanent, e.g. C:\\Tools\\llm-video-engine.
  2. Right-click install.ps1 > Run with PowerShell (or: powershell -ExecutionPolicy Bypass -File install.ps1).
     It adds this folder to your user PATH and checks for ffmpeg. Add -InstallFfmpeg to install it with winget.
  3. Open a new terminal and run: llm-video-engine --version

The Claude skill
  Upload skill\\llm-video-engine-skill.zip in Claude (Settings > Capabilities > Skills). Ask Claude for a
  video summary of a deck; it returns video-plan.zip.

Use
  llm-video-engine validate video-plan.zip   problems in the plan, one line each (paste them back to Claude)
  llm-video-engine grid video-plan.zip       one frame per scene, to check before rendering
  llm-video-engine render video-plan.zip     the MP4, next to the zip
  Options: --encoder qsv (Intel Quick Sync, faster), --threads N, --out file.

Requirements
  Windows 10 or 11, x64. ffmpeg on PATH (winget install Gyan.FFmpeg).

Licences
  llm-video-engine: see LICENSE. Montserrat font: SIL Open Font License 1.1 (fonts\\OFL.txt).
`;

const install = `# Adds this folder to the user PATH and checks for ffmpeg.
#   powershell -ExecutionPolicy Bypass -File install.ps1 [-InstallFfmpeg]
param([switch]$InstallFfmpeg)
$dir = $PSScriptRoot
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (($userPath -split ';') -contains $dir) { Write-Host "Already on PATH: $dir" }
else {
  [Environment]::SetEnvironmentVariable('Path', ($userPath.TrimEnd(';') + ';' + $dir), 'User')
  Write-Host "Added to your user PATH: $dir"
}
if (Get-Command ffmpeg -ErrorAction SilentlyContinue) { Write-Host 'ffmpeg found.' }
elseif ($InstallFfmpeg) { winget install --id Gyan.FFmpeg -e }
else { Write-Host 'ffmpeg is missing. Install it with: winget install Gyan.FFmpeg (or run this script with -InstallFfmpeg)' }
Write-Host 'Open a new terminal, then run: llm-video-engine --version'
`;

const file = (p) => fs.readFileSync(path.join(root, p));
const entries = [
  { name: `${name}/llm-video-engine.exe`, data: fs.readFileSync(exe) },
  ...crt.map((f) => ({ name: `${name}/${f}`, data: fs.readFileSync(path.join(crtDir, f)) })),
  { name: `${name}/fonts/Montserrat-VF.ttf`, data: file('themes/default/Montserrat-VF.ttf') },
  { name: `${name}/fonts/OFL.txt`, data: file('themes/default/OFL.txt') },
  { name: `${name}/skill/llm-video-engine-skill.zip`, data: file('dist/llm-video-engine-skill.zip') },
  { name: `${name}/LICENSE`, data: file('LICENSE') },
  { name: `${name}/README.txt`, data: Buffer.from(readme.replace(/\n/g, '\r\n')) },
  { name: `${name}/install.ps1`, data: Buffer.from(install.replace(/\n/g, '\r\n')) },
];
const out = path.join(root, 'dist', `${name}.zip`);
writeZip(out, entries);
console.log(`${out}  ${(fs.statSync(out).size / 1048576).toFixed(1)} MB  (runtime from ${crtDir})`);
